cmake_minimum_required(VERSION 3.20)

# Have the vcpkg toolchain deploy each installed target's DLL dependency
# closure (assimp, glfw, zlib, ...) during `cmake --install` as it already
# does at build time. Must be set before project().
set(X_VCPKG_APPLOCAL_DEPS_INSTALL ON)

# Set the project name
project(GameEngineFramework)

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED true)

# Add subdirectories for Engine, Editor, and the standalone Player
add_subdirectory(Engine)
add_subdirectory(Editor)
add_subdirectory(Player)

option(ENABLE_TESTS "Build unit tests" ON)

if(ENABLE_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# --- Packaging: `cpack -G ZIP` (or the `package` target) bundles the
# standalone game: Player.exe + Engine.dll + third-party DLLs + Exported/.
set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_NAME "CatSplatGame")
set(CPACK_PACKAGE_VERSION "0.1.0")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Standalone game build (player + assets)")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
include(CPack)
