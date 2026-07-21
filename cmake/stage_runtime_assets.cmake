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
    endif()
endforeach()
