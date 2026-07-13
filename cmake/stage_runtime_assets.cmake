# Stages runtime assets next to the executables. Invoked as:
#   cmake -DSRC=<source Exported dir> -DDST=<runtime Exported dir> -P stage_runtime_assets.cmake
#
# Two classes of content, handled differently:
# - STATIC assets (Model/, Shaders/): owned by the source tree, re-copied
#   every build so shader/model edits show up.
# - AUTHORED files (scene.json — anything the EDITOR writes at runtime into
#   the same directory): seeded only when missing. A blind copy_directory
#   here silently reverted editor-saved scenes to the checked-in copy on
#   every build, which also meant the packaged game shipped a stale scene.
file(COPY "${SRC}/Model" DESTINATION "${DST}")
file(COPY "${SRC}/Shaders" DESTINATION "${DST}")

file(GLOB seedFiles "${SRC}/*.json")
foreach(f ${seedFiles})
    get_filename_component(name "${f}" NAME)
    if(NOT EXISTS "${DST}/${name}")
        file(COPY "${f}" DESTINATION "${DST}")
    endif()
endforeach()
