# API reference generation (the "Scripting API" half of the docs).
#
# Deliberately OPTIONAL and never REQUIRED: Doxygen is a documentation tool,
# not a build dependency, and a contributor without it must still be able to
# build and run the engine. When it is missing we say so once, at configure
# time, and skip the target rather than failing the configure.

find_package(Doxygen QUIET)

if (NOT DOXYGEN_FOUND)
    message(STATUS
        "Docs: Doxygen not found - the 'docs' target is unavailable. "
        "The hand-written manual in docs/manual/ needs no tooling. "
        "Install Doxygen (https://www.doxygen.nl) to generate the API reference.")
    return()
endif()

set(CSE_VERSION "0.1.0")
set(DOXYGEN_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}")
set(DOXYGEN_MAINPAGE  "${CMAKE_CURRENT_SOURCE_DIR}/api-index.md")

# Engine/include is the façade header; Engine/src holds the headers it pulls
# in. Note this is the WHOLE engine header tree, not a curated public surface:
# RECURSIVE=YES also documents the concrete backend headers under */backends/
# (Doxyfile.in excludes only their .cpp files) and headers the façade
# deliberately leaves out — Engine/src/core/Mesh.h is dead code whose include
# is commented out at Engine/include/Engine.h:11 and still reaches the output.
# Editor/Player/Cooker are applications that CONSUME the engine, so they are
# not part of the API reference.
set(DOXYGEN_INPUT_DIRS
    "\"${CMAKE_SOURCE_DIR}/Engine/include\" \
\"${CMAKE_SOURCE_DIR}/Engine/src\" \
\"${DOXYGEN_MAINPAGE}\"")

# Class diagrams are a nice-to-have that needs Graphviz.
if (DOXYGEN_DOT_FOUND)
    set(DOXYGEN_HAVE_DOT "YES")
else()
    set(DOXYGEN_HAVE_DOT "NO")
endif()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in
               ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY)

# NOT in ALL: regenerating docs on every build is pure latency for something
# nobody looks at per-compile. Build it explicitly when you want it.
add_custom_target(docs
    COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Generating API reference -> ${DOXYGEN_OUTPUT_DIR}/api/html/index.html"
    VERBATIM)

message(STATUS "Docs: Doxygen found - build the 'docs' target for the API reference")
