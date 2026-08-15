cmake_minimum_required(VERSION 3.20)

# Have the vcpkg toolchain deploy each installed target's DLL dependency
# closure (assimp, glfw, zlib, ...) during `cmake --install` as it already
# does at build time. Must be set before project().
set(X_VCPKG_APPLOCAL_DEPS_INSTALL ON)

# Turn OFF vcpkg's per-executable BUILD-TIME DLL deployment, project-wide.
#
# Note this is the BUILD-time knob and the line above is the INSTALL-time one.
# They are independent: `cmake --install` and `cpack` still get the full DLL
# closure deployed for them, which is what packaging needs. This only changes who
# copies DLLs next to binaries in the BUILD tree.
#
# WHY. vcpkg runs `z-applocal` as a POST_BUILD on EVERY executable. Every
# executable in this project lands in one of two shared directories —
# build/bin/<CONFIG>/ for Editor, Player, PlayerDebug and AssetCooker, and
# tests/ for the ~45 test binaries — so under Ninja a dozen links finish at once
# and a dozen applocal processes copy the SAME DLLs into the SAME directory
# simultaneously. On Windows that is a sharing violation waiting for a busy
# enough machine.
#
# It has now bitten twice in one day, which is what moved this from "tidy" to
# "fix it":
#   * `vcpkg z-applocal ... test_ui_binding.exe` -> "Access is denied."
#     (run 31662777312, hidden because gl-tests was still advisory)
#   * `vcpkg z-applocal ... Player.exe` -> "The process cannot access the file
#     because it is being used by another process." while PlayerDebug and Editor
#     were linking into the same directory (run 31670209142, the FIRST run after
#     those jobs were promoted to required)
#
# Replaced by one staging target per output directory — test_runtime_deps in
# tests/, app_runtime_deps below — both driven by cmake/stage_runtime_dlls.cmake,
# which copies a superset of what applocal would and does it once. That is the
# same lesson as the note at the top of tests/CMakeLists.txt about per-test
# POST_BUILD copies, and this repository has now learned it three times: the fix
# for concurrent copies into a shared directory is ONE copier, not faster ones.
set(VCPKG_APPLOCAL_DEPS OFF)

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

# The authoritative fighting-game simulation. FIRST, and deliberately not inside
# Engine/: it links nothing at all (see Kernel/CMakeLists.txt, which asserts
# that), and inside Engine it would inherit Engine's whole link interface and the
# guarantee would be worth nothing. docs/ARCHITECTURE.md D2, made structural by
# docs/ADR-002-open-decisions.md CHOICE D.
add_subdirectory(Kernel)

# Vendored third-party built with OUR flags: GekkoNet, the rollback session
# layer. Compiled here rather than via its own CMakeLists because that file
# forces /MT on a static build while this engine is /MD — see the long note in
# ThirdParty/CMakeLists.txt.
add_subdirectory(ThirdParty)

# The rollback session seam. Links GekkoNet PRIVATE so gekkonet.h reaches exactly
# one translation unit, which is what keeps ADR-002 CHOICE A reversible.
add_subdirectory(Net)

# Character data (schema v2 -> memory). Before Engine and tests because both may
# come to depend on it; it depends on neither.
add_subdirectory(Data)

# The headless game core: input sources, the fight session, replay capture and
# the live combo judge. Links CseKernel and CseData and nothing else — asserted
# at configure time, because Engine-free is what keeps its claims testable
# without a GL context.
add_subdirectory(Game)

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

# ONE staging step for the DLLs all four applications share, replacing the
# per-executable vcpkg applocal turned off at the top of this file.
#
# Editor, Player, PlayerDebug and AssetCooker all link into
# build/bin/<CONFIG>/ (see each of their RUNTIME_OUTPUT_DIRECTORY_* settings),
# and Engine.dll lands there too, so one copier serving that directory is the
# whole job. The engine's own directory IS the destination here, which is why
# SRC is left unset and only VCPKG_BIN is passed.
#
# Ordering: this depends on Engine so vcpkg has finished installing by the time
# it runs, and each application depends on IT, so no executable is considered
# built until its DLLs are beside it.
if (WIN32)
    add_custom_target(app_runtime_deps
        COMMAND ${CMAKE_COMMAND}
            -DVCPKG_BIN=${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<$<CONFIG:Debug>:debug/>bin
            -DDST=${CMAKE_BINARY_DIR}/build/bin/$<CONFIG>
            -P ${CMAKE_SOURCE_DIR}/cmake/stage_runtime_dlls.cmake
        COMMENT "Staging application runtime DLLs"
        VERBATIM)
    add_dependencies(app_runtime_deps Engine)
    foreach(app Editor PlayerDebug PlayerShipping AssetCooker)
        add_dependencies(${app} app_runtime_deps)
    endforeach()
endif()

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
