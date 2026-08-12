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
cmake_minimum_required(VERSION 3.21)

if (NOT DEFINED SRC OR NOT DEFINED DST)
  message(FATAL_ERROR "stage_runtime_dlls.cmake: SRC and DST are required")
endif()

file(GLOB dlls "${SRC}/*.dll")

if (NOT dlls)
  # Not fatal -- a Linux build has no DLLs and never invokes this. But say so,
  # because a silent no-op here means the tests fail later with a loader error
  # that points nowhere near this file.
  message(WARNING "stage_runtime_dlls: no DLLs found in ${SRC}; tests may not load")
  return()
endif()

# file(COPY) already skips files whose timestamp and size match, so this is cheap
# on a rebuild and safe to run from a target that is always considered dirty.
file(COPY ${dlls} DESTINATION "${DST}")
