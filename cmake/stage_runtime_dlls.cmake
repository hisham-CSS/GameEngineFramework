# Stage every DLL the engine needs beside the test executables.
#
# Run in script mode (cmake -P) from a build-time custom command, so the GLOB
# happens when the tests are BUILT rather than when CMake configures. That is the
# whole point: at configure time the engine's output directory is empty, and
# vcpkg's applocal deployment has not run yet.
#
# WHY A GLOB AND NOT A LIST. This used to be seven hand-written DLL names. It was
# missing four, and the failure that exposed it was expensive to read: on a clean
# tree the physics tests died with
#
#     SEH exception with code 0xc06d007e
#
# (ERROR_MOD_NOT_FOUND from the loader) followed by eighteen instances of
# "resource deadlock would occur", which is what MSVC's std::call_once raises
# after its callable has thrown once. Nothing in any of that names a DLL.
#
# The missing one was PhysXCommon_64.dll, and the reason a hand-written list was
# always going to miss it is structural: vcpkg's applocal deployment walks IMPORT
# TABLES, and Engine.dll does not import PhysXCommon_64 -- PhysX_64 loads it
# itself at runtime. So it never appears in any dependency analysis, only in the
# engine's output directory, where applocal does put it.
#
# Copying that directory wholesale is therefore not laziness but the correct
# rule: "the tests need whatever the engine needs to run", stated once. It cannot
# drift when a dependency is added, and it removes the standing invitation to
# debug a loader error six months from now.
# TWO SOURCES, because the engine's output directory turned out not to be enough.
#
# SRC is where the engine links and where vcpkg's applocal deployment puts the
# DLLs it can find. Copying it fixed 18 of the 19 failing tests but not the last
# one: PhysXCommon_64.dll reached that directory on a developer machine and did
# not on a clean CI runner, with the same vcpkg baseline and the same
# physx@5.3.0. Rather than keep bisecting a deployment heuristic across two
# machines, VCPKG_BIN names vcpkg's own bin directory, which contains every DLL
# of every package the manifest installs, unconditionally and by construction.
#
# The cost is a handful of DLLs the tests never load. That is the right trade for
# a test directory: the alternative failure mode is a loader error that names no
# file (see the 0xc06d007e note above) and costs a CI round trip to read.
cmake_minimum_required(VERSION 3.21)

if (NOT DEFINED DST)
  message(FATAL_ERROR "stage_runtime_dlls.cmake: DST is required")
endif()

# SRC is optional. The tests directory needs it (their DLLs live next to the
# engine, one directory up); the app bin directory does not, because the engine
# links straight into it and SRC would be the same directory as DST.
if (NOT DEFINED SRC OR SRC STREQUAL "")
  set(dlls "")
else()
  file(GLOB dlls "${SRC}/*.dll")
endif()

if (NOT dlls AND NOT DEFINED VCPKG_BIN)
  # Not fatal -- a Linux build has no DLLs and never invokes this. But say so,
  # because a silent no-op here means a loader error later that points nowhere
  # near this file.
  message(WARNING "stage_runtime_dlls: nothing to copy (SRC='${SRC}', no VCPKG_BIN)")
  return()
endif()

# NOT file(COPY), and the reason is a CI failure rather than taste.
#
# file(COPY) preserves timestamps, which means that after writing the bytes it
# SETS THE MODIFICATION TIME on the destination — a second open of a file it has
# just created. On a busy machine something else can hold that handle for the
# instant in between (a virus scanner on a freshly written 40 MB DLL is the usual
# suspect), and the copy fails with
#
#   file COPY cannot set modification time on ".../PhysXGpu_64.dll":
#   The process cannot access the file because it is being used by another process
#
# which is what took the Windows job down on run 31677229284. configure_file with
# COPYONLY writes content and stops. It also skips a destination whose content is
# already identical, so it stays cheap on a rebuild — the same property file(COPY)
# was chosen for, obtained without the extra open.
#
# This is the third time this repository has met the same shape of bug: parallel
# applocal in tests/, parallel applocal in build/bin/, and now a redundant file
# handle inside the fix for both. The pattern is worth naming — on Windows, every
# additional touch of a file you just wrote is a chance for someone else to be
# holding it.
function(stage_dlls files)
  foreach (f IN LISTS files)
    get_filename_component(name "${f}" NAME)
    configure_file("${f}" "${DST}/${name}" COPYONLY)
  endforeach()
endfunction()

if (dlls)
  stage_dlls("${dlls}")
endif()

# Second, anything vcpkg installed that applocal did not carry across. Copied
# FIRST-LOSES order-wise -- the engine's own directory was copied above and these
# are the same files by content, so overwrite order does not matter.
if (DEFINED VCPKG_BIN AND IS_DIRECTORY "${VCPKG_BIN}")
  file(GLOB vcpkg_dlls "${VCPKG_BIN}/*.dll")
  if (vcpkg_dlls)
    stage_dlls("${vcpkg_dlls}")
  endif()
endif()
