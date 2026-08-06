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
file(GLOB children RELATIVE "${SRC}" "${SRC}/*")
foreach(child ${children})
    if(IS_DIRECTORY "${SRC}/${child}")
        file(COPY "${SRC}/${child}" DESTINATION "${DST}")
    endif()
endforeach()

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
