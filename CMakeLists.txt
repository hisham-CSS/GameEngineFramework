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

# Vendored third-party built with OUR flags: GekkoNet, the rollback session
# layer. Compiled here rather than via its own CMakeLists because that file
# forces /MT on a static build while this engine is /MD — see the long note in
# ThirdParty/CMakeLists.txt.
add_subdirectory(ThirdParty)

# The rollback session seam. Links GekkoNet PRIVATE so gekkonet.h reaches exactly
# one translation unit, which is what keeps ADR-002 CHOICE A reversible.
#
# It is deliberately NOT under Games/ with the fighting game that motivated it:
# ISession takes an opaque state blob and a byte of input per player and names
# no fighter, so it is infrastructure any deterministic game wants. See the
# header of Games/UntitledFighter/CMakeLists.txt.
add_subdirectory(Net)

# ---------------------------------------------------------------------------
# THE TITLES
# ---------------------------------------------------------------------------
# Every other directory in this file is general-purpose. The one line below is
# the entire fighting game, and it is the only place in the root that knows
# such a thing exists. That split is the point: this engine is meant to host
# more than one title and the fighting game is only the first, so a title may
# depend on the engine and the engine may never depend on a title. The rule,
# and why Net/ stayed behind, are written out in
# Games/UntitledFighter/CMakeLists.txt.
#
# ONE LINE PER TITLE, NOT PER LIBRARY. The fighting game is three libraries
# (Kernel, Data, Game) and that is its own file's business. The day a second
# game exists this list gains one line, not three.
#
# After ThirdParty/ because the title's data layer links ComboProver; before
# Engine/ and tests/ because either may come to depend on the title while the
# title depends on neither.
add_subdirectory(Games/UntitledFighter)

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

# ---------------------------------------------------------------------------
# COMPOSING THE GENERAL HOSTS WITH THE TITLE
# ---------------------------------------------------------------------------
# Editor/, Player/ and Engine/ are general-purpose and none of them names a game
# anywhere in its sources or its build file. This block is where a general host
# and a specific title are put together, and it is here rather than in
# Editor/CMakeLists.txt or Player/CMakeLists.txt for the reason the block above
# gives: THIS FILE IS ALREADY THE ONE PLACE ALLOWED TO KNOW A TITLE EXISTS.
#
# Each host declares a hook it never defines and the title's library defines it:
#   editor::RegisterTitlePanels             Editor/src/EditorPanel.h
#   MyCoreEngine::RegisterTitleGameModes    Engine/src/core/GameMode.h
# Linking the library is also what TURNS THE HOOK ON -- each library carries the
# guard macro as an INTERFACE compile definition, so there is no second line here
# that anybody could forget and no way for the link and the macro to disagree.
# See Games/UntitledFighter/CMakeLists.txt for the pattern written out.
#
# REMOVE THE add_subdirectory ABOVE AND YOU HAVE A GENERAL ENGINE with a general
# editor and a general player, all of which still build and run -- the editor
# simply has no Combo Prover in its Window menu and the player's main menu has no
# fighting game on it. That is the property this arrangement is for.
#
# IT IS GUARDED RATHER THAN ADVERTISED, and the difference is the whole point.
# These three lines named their targets WITHOUT a `::`, and CMake treats an
# unknown non-namespaced name as a raw library for the linker rather than as a
# missing target -- so dropping the title configured CLEAN and then died with
# `LNK1104: cannot open file 'UntitledFighterPlayer.lib'` (gcc:
# `cannot find -lUntitledFighterPlayer`), naming a file that never existed. That
# is the opposite of how every other rule in this repository fails, and it made
# the paragraph above a wish rather than a boundary. Verified by experiment, not
# by reading.
#
# `if (TARGET ...)` turns it into an executed property: with the title present
# these link exactly as before; with it absent the engine builds and the hooks
# stay undefined, which is precisely what the guard macros already expect.
#
# A SECOND TITLE ADDS LINES HERE, and edits nothing under Engine/, Editor/ or
# Player/. If it ever needs to edit one of those, the seam was drawn wrong.
if (TARGET UntitledFighterEditor)
    target_link_libraries(Editor PRIVATE UntitledFighterEditor)
endif()
if (TARGET UntitledFighterPlayer)
    target_link_libraries(PlayerDebug    PRIVATE UntitledFighterPlayer)
    target_link_libraries(PlayerShipping PRIVATE UntitledFighterPlayer)
endif()

# ---------------------------------------------------------------------------
# COMPOSING THE CONTENT, WHICH IS THE OTHER HALF OF COMPOSING A TITLE
# ---------------------------------------------------------------------------
# The block above composes CODE. This one composes ASSETS, and until now there
# was nothing to compose: `Editor/src/Exported` WAS the asset root, it was the
# only one, and a title's content therefore had to live inside it. Fighting-game
# character data sat in the general-purpose editor's asset directory, and the
# game itself was a verb on the ENGINE DEMO's main menu whose NEW GAME loads a
# backpack. The code boundary was a configure-time FATAL_ERROR and the content
# boundary was held by nothing at all -- not because anyone decided that, but
# because there was no way for a title to own content.
#
# ONE ORDERED LIST, LATER WINNING. The engine's asset root is first and each
# linked title's goes on top; cmake/stage_runtime_assets.cmake flattens them into
# the single runtime Exported/ beside the executables. Every relative path that
# resolves today goes on resolving, because the RUNTIME layout is unchanged --
# what changes is who is allowed to put things into it.
#
# INTERIM, AND MEANT TO BE. The real answer is an asset SEARCH PATH that the
# LOADER walks, so nothing is ever copied over anything and a shipped title
# carries its own root wherever it likes. That is a change to every path
# resolution in the engine; this is a change to one build step, and it buys the
# SOURCE separation now.
#
# DECLARED HERE FOR THE SAME REASON THE LINKS ARE: this file is the one place
# allowed to know a title exists. The staging target below reads this list and so
# does tests/, and neither of them ends up naming a game.
set(CSE_ASSET_ROOTS ${CMAKE_SOURCE_DIR}/Editor/src/Exported)

# A title's asset root is `Games/<title>/Assets` BY CONVENTION rather than by
# declaration, so composing a title's content costs the same one line as
# composing its code, and a second title copies the line rather than inventing a
# protocol.
#
# THE TARGET IS THE GATE, not the directory. Delete the add_subdirectory above
# and the Assets/ folder is still sitting on disk, so a directory test alone
# would go on staging a game into a build that does not contain one. Both of the
# title's host libraries come from that single add_subdirectory, so either
# answers "is this title in the build"; this one is named to match the block
# directly above.
#
# THE DIRECTORY TEST IS THE SECOND HALF, and it is not a transitional
# convenience: a title need not ship assets at all. The fighting game was exactly
# such a title until its front end moved out of the engine's asset root, and a
# title whose modes draw everything procedurally never acquires one. The staging
# script FATAL_ERRORs on a root that is not a directory -- the right answer for a
# path somebody mistyped, the wrong one for a title that simply has no files.
if (TARGET UntitledFighterPlayer AND
    IS_DIRECTORY ${CMAKE_SOURCE_DIR}/Games/UntitledFighter/Assets)
    list(APPEND CSE_ASSET_ROOTS ${CMAKE_SOURCE_DIR}/Games/UntitledFighter/Assets)
endif()

# ONE staging step for the runtime assets the applications share.
#
# IT IS HERE RATHER THAN IN Editor/CMakeLists.txt, where it lived for as long as
# there was one root, because staging is now COMPOSITION: the runtime Exported/
# is the engine plus every linked title, and the general-purpose editor's build
# file may no more name a game than its link line may. Editor/, Player/ and
# tests/ keep their `add_dependencies(... runtime_assets)` untouched -- a forward
# reference to a target defined later in the top-level scope, resolved at
# generate time. Verified rather than assumed: CMake accepts it, orders the two
# correctly, and errors loudly if the target never appears at all.
#
# ONE WRITER, still, which is why it was a single shared target to begin with:
# Editor, PlayerDebug and PlayerShipping all link into build/bin/<CONFIG>/, and
# concurrent copies into one directory race under Ninja. Same lesson as
# app_runtime_deps below and test_runtime_deps in tests/.
#
# QUOTE THE ROOTS ARGUMENT, AND PASS VERBATIM. Both are load-bearing and neither
# was needed while there was one root, which is exactly why they are easy to drop:
#   * unquoted, CMake splits `-DROOTS=a;b` into two arguments BEFORE the command
#     line is built, so the script receives one root, mirrors the staged tree
#     against it, and deletes everything the title staged on every build -- while
#     configuring and building perfectly happily.
#   * VERBATIM is what escapes that `;` for /bin/sh on Linux, where it is a
#     command separator rather than part of an argument.
add_custom_target(runtime_assets
  COMMAND ${CMAKE_COMMAND}
    "-DROOTS=${CSE_ASSET_ROOTS}"
    "-DDST=$<TARGET_FILE_DIR:Editor>/Exported"
    -P ${CMAKE_SOURCE_DIR}/cmake/stage_runtime_assets.cmake
  COMMENT "Staging runtime assets"
  VERBATIM)

# --------------------------------------------------------------------------
# THE BOUNDARY, AS A CONFIGURE-TIME FAILURE RATHER THAN AN INTENTION
# --------------------------------------------------------------------------
# The rule: a TITLE may depend on the engine; the ENGINE may never depend on a
# title. Every other load-bearing rule in this tree is a FATAL_ERROR --
# CseKernel links nothing, CseData never links CseKernel, CseGame links exactly
# those two, CseNet leaks no GekkoNet -- and this one was held by inspection.
#
# It is the wrong rule to leave unchecked, because it is the only one on that
# list that HAS ACTUALLY BEEN BROKEN. `target_link_libraries(Editor PRIVATE
# CseData)` sat in Editor/CMakeLists.txt for months so that a fighting-game panel
# could compile inside the general-purpose editor, and no configure ever said a
# word. Re-adding that line today would trip nothing without this block.
#
# The engine's own targets are checked, not the composition above: Editor and the
# players are ALLOWED to link the title's thin host libraries, because that is
# what composing a title into a host means and this file is the one place
# documented to know a title exists. What they may not do is reach past those
# libraries into the title's simulation, its data layer or its game systems.
set(_cse_engine_targets Engine Editor PlayerDebug PlayerShipping AssetCooker)
set(_cse_title_internals CseKernel CseData CseGame)
foreach(_t IN LISTS _cse_engine_targets)
    if (NOT TARGET ${_t})
        continue()
    endif()
    get_target_property(_libs ${_t} LINK_LIBRARIES)
    if (NOT _libs)
        continue()
    endif()
    foreach(_forbidden IN LISTS _cse_title_internals)
        # $<LINK_ONLY:X> is what a PRIVATE dependency of a static library looks
        # like from the outside, so match the name rather than the whole entry --
        # the lesson from an earlier guard in this tree that asserted an empty
        # INTERFACE_LINK_LIBRARIES and was wrong about what correct looks like.
        if (_libs MATCHES "(^|;|:|<)${_forbidden}(;|>|$)")
            message(FATAL_ERROR
                "BOUNDARY VIOLATION: the general-purpose target `${_t}` links "
                "`${_forbidden}`, which belongs to a title under Games/.\n"
                "  The engine may not depend on a game. If ${_t} needs something "
                "from the title, the title should push it in through a seam "
                "(editor::RegisterTitlePanels, MyCoreEngine::RegisterTitleGameModes) "
                "rather than the engine reaching in.\n"
                "  Link line was: ${_libs}")
        endif()
    endforeach()
endforeach()

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
