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

# Gives an executable the Cat Splat icon (Explorer/taskbar/alt-tab, and the
# GLFW window class — see resources/app.rc). Windows-only; other platforms set
# the icon at runtime from Exported/Icon/icon.png.
#
# The OBJECT_DEPENDS is load-bearing. CMake compiles .rc through cmcldeps, which
# discovers dependencies by scraping `cl /showIncludes` — that reports #included
# HEADERS only, never a resource data file like `GLFW_ICON ICON "CatSplat.ico"`.
# So regenerating the .ico rebuilt nothing and every exe silently kept the old
# icon, exactly contradicting app.rc's own "no build changes needed" note.
# Declaring it explicitly makes the .ico a real input.
function(cse_add_app_icon tgt)
    if (WIN32)
        target_sources(${tgt} PRIVATE ${CMAKE_SOURCE_DIR}/resources/app.rc)
        set_property(SOURCE ${CMAKE_SOURCE_DIR}/resources/app.rc
                     APPEND PROPERTY OBJECT_DEPENDS
                     ${CMAKE_SOURCE_DIR}/resources/CatSplat.ico)
    endif()
endfunction()

# Add subdirectories for Engine, Editor, the standalone Player, and the
# headless AssetCooker (batch asset work, spawned by the editor)
add_subdirectory(Engine)
add_subdirectory(Editor)
add_subdirectory(Player)
add_subdirectory(Cooker)
# API reference (optional): defines a 'docs' target when Doxygen is installed,
# and quietly skips it otherwise. Never a build dependency.
add_subdirectory(docs)

# The editor invokes AssetCooker.exe at runtime (Validate button): building
# the Editor must produce/refresh the cooker too, or a dev-loop build ships
# a missing/stale cooker next to a new editor.
add_dependencies(Editor AssetCooker)

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
