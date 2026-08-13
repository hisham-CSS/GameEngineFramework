# Stages runtime assets next to the executables. Invoked as:
#   cmake -DSRC=<source Exported dir> -DDST=<runtime Exported dir> -P stage_runtime_assets.cmake
#
# Two classes of content, handled differently:
#
# - STATIC assets live in SUBDIRECTORIES (Model/, Shaders/, Scripts/, Env/...).
#   They are owned by the source tree and re-copied every build so edits show
#   up. Every subdirectory is copied, deliberately: this list used to be
#   hardcoded, and adding Scripts/ and then Env/ each silently shipped a
#   feature whose assets never reached the runtime directory. The symptom is
#   always the same and always misleading -- the file is visibly right there
#   in the source tree, and the engine reports it missing.
#
# - AUTHORED files are the *.json at the TOP LEVEL (scene.json, project.json --
#   anything the EDITOR writes back into this same directory). Seeded only when
#   missing. A blind copy here silently reverted editor-saved scenes to the
#   checked-in copy on every build, which also meant the packaged game shipped
#   a stale scene.
# SRC and DST ARE MADE ABSOLUTE BEFORE ANYTHING LOOKS AT THEM. `if(EXISTS)` and
# `if(IS_DIRECTORY)` are documented as well-defined only for full paths, and with
# a relative one they simply answer false -- so the mirror below quietly did
# nothing at all when this script was invoked by hand with repo-relative
# arguments. It removed no stale file and printed no message, which is the worst
# available failure for a step whose whole job is removing things. The build
# always passes absolute paths and was never affected; this makes the script
# behave the same way regardless of who calls it.
if(NOT DEFINED SRC OR NOT DEFINED DST)
    message(FATAL_ERROR "stage_runtime_assets.cmake needs -DSRC=<source Exported> -DDST=<runtime Exported>")
endif()
get_filename_component(SRC "${SRC}" ABSOLUTE)
get_filename_component(DST "${DST}" ABSOLUTE)
if(NOT IS_DIRECTORY "${SRC}")
    message(FATAL_ERROR "stage_runtime_assets.cmake: SRC is not a directory: ${SRC}")
endif()

file(GLOB children RELATIVE "${SRC}" "${SRC}/*")
set(sourceDirs "")
foreach(child ${children})
    if(IS_DIRECTORY "${SRC}/${child}")
        file(COPY "${SRC}/${child}" DESTINATION "${DST}")
        list(APPEND sourceDirs "${child}")
    endif()
endforeach()

# COPYING IS ONLY HALF OF "OWNED BY THE SOURCE TREE". A copy adds and overwrites;
# it never REMOVES, so a file deleted from the source tree lived on in every
# build directory that had ever seen it -- invisible, because the source tree
# looks correct and nobody greps a build output.
#
# That is not a tidiness problem. `Player/CMakeLists.txt:63-81` packages the game by
# walking this staged directory and copying whatever it finds, so a stale file
# here is a file that SHIPS. The case that found this: three characters
# transcribed from third-party MUGEN sources were moved out of the asset root
# precisely because they may not be distributed, and every existing build
# directory went on holding them, ready to be bundled by the next `install`.
#
# So the static half now MIRRORS rather than merely copies: a staged file whose
# source counterpart is gone is deleted, and so is a whole staged subdirectory
# whose source counterpart is gone. Every removal is announced, because a build
# that silently deletes files is its own kind of trap.
#
# ONE EXCEPTION, and it is the same exception the .json rule below is made of:
# `*.import` sidecars are written by the EDITOR into this tree (ImportSettings.h
# -- "foo.png" -> "foo.png.import"), so they legitimately exist here with no
# source counterpart. Deleting them would throw away a designer's import
# settings on every build. They are also excluded from packaging by both install
# rules, so keeping them costs the bundle nothing.
if(EXISTS "${DST}")
    file(GLOB stagedChildren RELATIVE "${DST}" "${DST}/*")
    foreach(child ${stagedChildren})
        if(IS_DIRECTORY "${DST}/${child}")
            list(FIND sourceDirs "${child}" foundAt)
            if(foundAt EQUAL -1)
                message(STATUS
                    "[stage] REMOVING staged ${child}/: the source tree no longer has it.")
                file(REMOVE_RECURSE "${DST}/${child}")
            else()
                file(GLOB_RECURSE stagedFiles RELATIVE "${DST}/${child}" "${DST}/${child}/*")
                foreach(rel ${stagedFiles})
                    if(NOT rel MATCHES "\\.import$" AND NOT EXISTS "${SRC}/${child}/${rel}")
                        message(STATUS
                            "[stage] REMOVING staged ${child}/${rel}: the source tree no "
                            "longer has it.")
                        file(REMOVE "${DST}/${child}/${rel}")
                    endif()
                endforeach()
            endif()
        endif()
    endforeach()
endif()

file(GLOB seedFiles "${SRC}/*.json")
foreach(f ${seedFiles})
    get_filename_component(name "${f}" NAME)
    if(NOT EXISTS "${DST}/${name}")
        file(COPY "${f}" DESTINATION "${DST}")
    else()
        # SEEDED-ONLY IS SAFE BUT SILENT, and the silence is its own bug: an
        # authored change to one of these in the source tree never reaches a
        # tree that has already been built once, so the file is visibly right
        # in the repo and the running game disagrees with it. That cost a whole
        # debugging session on a menu.json scale setting that "did nothing".
        #
        # Still not copied -- overwriting is what reverts editor-saved scenes,
        # which is worse. Just say so, and only when they actually differ.
        file(SHA256 "${f}" srcHash)
        file(SHA256 "${DST}/${name}" dstHash)
        if(NOT srcHash STREQUAL dstHash)
            message(STATUS
                "[stage] KEEPING the staged ${name}: it differs from the source "
                "copy, and these are editor-writable so a build must not "
                "overwrite them. If the SOURCE one is the one you want, delete "
                "${DST}/${name} and build again.")
        endif()
    endif()
endforeach()
