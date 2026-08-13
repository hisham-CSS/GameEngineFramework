# Third-party code we vendor rather than consume from vcpkg.
#
# GekkoNet is the rollback session layer (docs/ADR-002 CHOICE A, proved in
# docs/ADR-003). It is not in vcpkg in any form, so it is a git submodule pinned
# to a commit, and we are its maintainer at that pin from day one -- ADR-003 says
# so explicitly and it is not a reason against, only a reason to pin deliberately.
#
# WHY WE COMPILE ITS SOURCES OURSELVES INSTEAD OF add_subdirectory()ing IT.
#
# Their GekkoLib/CMakeLists.txt:10-15 does
#     if(MSVC)
#         if(BUILD_SHARED_LIBS) set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
#         else()               set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
# which derives the C RUNTIME from whether the library is static. Those are
# independent choices, and the consequence is real: a static GekkoNet gets /MT
# while this engine is /MD, and mixing CRTs in one image is the classic
# LNK2038-or-worse. Measured, not assumed -- passing
# -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL from outside does NOT fix it,
# because their set() runs after ours and wins.
#
# The obvious response is to patch that line in the submodule. A patch against a
# submodule is a thing you re-apply on every update and forget once. Compiling
# the sources here instead costs about twenty lines, needs no patch, and gives us
# the flags outright -- which for a project whose whole thesis is bit-identical
# arithmetic is worth having anyway. It also sidesteps their
# CMAKE_ARCHIVE_OUTPUT_DIRECTORY, which writes build artifacts back into the
# source tree.
#
# The submodule stays pristine. `git diff` in ThirdParty/GekkoNet is always empty,
# so "have we modified upstream?" has an obvious answer.

set(GEKKO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/GekkoNet/GekkoLib)

if (NOT EXISTS ${GEKKO_DIR}/src/gekkonet.cpp)
    message(FATAL_ERROR
        "ThirdParty/GekkoNet is empty. It is a git submodule:\n"
        "    git submodule update --init --recursive")
endif()

# CONFIGURE_DEPENDS so an upstream commit that ADDS a source file is picked up by
# a plain rebuild rather than silently omitted until someone reconfigures.
file(GLOB GEKKO_SRC CONFIGURE_DEPENDS ${GEKKO_DIR}/src/*.cpp)

add_library(GekkoNet STATIC ${GEKKO_SRC})

target_include_directories(GekkoNet
    PUBLIC  ${GEKKO_DIR}/include
    PRIVATE ${GEKKO_DIR}/include/private
    PRIVATE ${GEKKO_DIR}/thirdparty)          # zpp_bits, used by backend/replay

# C++20 for THIS target only. Their implementation needs it (zpp_bits uses
# concepts and std::bit_cast); our C++17 never sees it, because the public header
# is C and contains no C++20 construct. Two standards in one build, partitioned
# by a C boundary -- and unlike Jolt.cmake, nothing here is include()d into our
# scope, so nothing leaks.
target_compile_features(GekkoNet PRIVATE cxx_std_20)
set_target_properties(GekkoNet PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)

# PUBLIC because consumers need it too: gekkonet.h falls through to
# __declspec(dllimport) without it and the link fails with __imp_ symbols that
# name nothing useful (gekkonet.h:35-38). Found by hitting it.
target_compile_definitions(GekkoNet PUBLIC GEKKONET_STATIC)

# No asio, and therefore no bundled UDP transport. Only gekkonet.cpp references
# it, behind #ifndef GEKKONET_NO_ASIO, and excluding it drops a 6.8 MB header
# tree plus its Boost licence from the build. We supply transport through
# ISession when there is a network to talk to; nothing in the rollback core,
# the prediction, or the state ring needs it.
target_compile_definitions(GekkoNet PUBLIC GEKKONET_NO_ASIO)

# NOTE: the C runtime is deliberately NOT set here. Leaving it unset means
# GekkoNet inherits this project's default -- /MD, the same as Engine -- which is
# the entire reason this file exists.
