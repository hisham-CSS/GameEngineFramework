cmake_minimum_required(VERSION 3.20)

# DO NOT have the vcpkg toolchain deploy DLLs during `cmake --install`.
#
# This was ON, and it SHIPPED A GAME THAT COULD NOT START: a bundle built from
# the editor contained Engine.dll and lua-c++.dll -- the only two with explicit
# install() rules -- and none of the twelve others. Double-clicking it produced
# "The code execution cannot proceed because glfw3.dll was not found."
#
# THE CAUSE, MEASURED. vcpkg's applocal.ps1 reads a binary's import table with
# an external tool, and its very first choice is `dumpbin`:
#
#     if (Get-Command "dumpbin") { ... }
#     elseif (Get-Command "llvm-objdump") { ... }
#     elseif (Get-Command "objdump") { ... }
#     else { Write-Error "Neither dumpbin, llvm-objdump nor objdump ..." }
#
# `dumpbin.exe` ships with MSVC and is on PATH only inside a Visual Studio
# developer environment. The editor spawns `cmake --install` as a child of
# ITSELF, and the editor is a GUI process with an ordinary environment -- so
# there is no dumpbin, applocal takes the else branch, and `Write-Error` does
# NOT fail the install. cmake reports success, the Build panel reports success,
# and the bundle is missing every dependency. Running the same command from a
# developer prompt deploys all twelve, which is why this survived: it works
# everywhere it was tested and fails everywhere it is used.
#
# AND WALKING THE IMPORT TABLE WOULD NOT BE ENOUGH EVEN WITH dumpbin PRESENT.
# cmake/stage_runtime_dlls.cmake already records why: PhysXCommon_64.dll appears
# in no import table because PhysX_64 loads it at runtime, so a dependency-graph
# walk cannot see it. That file's conclusion -- "the tests need whatever the
# engine needs to run", stated once, as a directory copy -- is the rule, and the
# install path is now the third consumer of it after tests/ and build/bin/.
#
# So: ONE COPIER, the same conclusion the line below reaches for the build tree,
# and Player/CMakeLists.txt owns the install-side copy. Two mechanisms that can
# disagree about a bundle's contents is exactly what this repository keeps
# getting caught by.
set(X_VCPKG_APPLOCAL_DEPS_INSTALL OFF)

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

# ---------------------------------------------------------------------------
# THE IDE'S LISTS, AND WHICH HALF OF THEM THIS FILE CAN REACH
# ---------------------------------------------------------------------------
# Visual Studio in Open Folder mode builds two lists out of this project, and
# they are not the same list:
#
#   THE STARTUP-ITEM DROPDOWN comes from the CMake file API -- every EXECUTABLE
#   target in <binaryDir>/.cmake/api/v1/reply/, which VS requests as client
#   "MicrosoftVS" (its query.json is sitting in every build tree here). NOTHING
#   IN THIS REPOSITORY CAN FILTER THAT LIST. A target's entry carries name,
#   type, artifacts and folder, and no "not launchable" bit of any kind: an
#   EXCLUDE_FROM_ALL executable and an ordinary one produce byte-identical
#   target JSON. Measured against codemodel-v2 2.10 by generating both and
#   diffing the reply, not inferred from the documentation. The only way to
#   take a name out of that dropdown is to not define the target at all in the
#   configuration being used.
#
#   THE CMAKE TARGETS VIEW groups by the FOLDER property, and codemodel-v2 DOES
#   carry `folder` (same experiment: the target that set it got a `folder` key,
#   the ones that did not have none). That is the one lever there is, so the
#   `set(CMAKE_FOLDER ...)` lines below pull it. Every library, artifact and
#   test in this build goes into a named group and THE ONE TARGET LEFT LOOSE IS
#   THE EDITOR -- the only executable here a person is meant to start
#   (ADR-008). A target that lands beside the Editor at the top level is
#   telling you it was never grouped, not that it is launchable.
#
# THE THIRD LIST IS NOT OURS AND NOT CMAKE'S. ThirdParty/GekkoNet is a pinned
# submodule that ships GekkoNet.sln, GekkoLib/GekkoLib.vcxproj and six
# Examples/*.vcxproj; Open Folder discovers those by scanning the tree, and no
# CMake file reaches them because they are not CMake targets. The only thing
# that does is Visual Studio's own workspace exclusion list --
# .vs/VSWorkspaceSettings.json, which this repository now commits. Its header
# comment carries the rest of the story, including what to do by hand if your
# Visual Studio ignores it.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Vendored third-party built with OUR flags: GekkoNet, the rollback session
# layer. Compiled here rather than via its own CMakeLists because that file
# forces /MT on a static build while this engine is /MD — see the long note in
# ThirdParty/CMakeLists.txt.
set(CMAKE_FOLDER "Third-party")
add_subdirectory(ThirdParty)

# The rollback session seam. Links GekkoNet PRIVATE so gekkonet.h reaches exactly
# one translation unit, which is what keeps ADR-002 CHOICE A reversible.
#
# It is deliberately NOT under Games/ with the fighting game that motivated it:
# ISession takes an opaque state blob and a byte of input per player and names
# no fighter, so it is infrastructure any deterministic game wants. See the
# header of Games/UntitledFighter/CMakeLists.txt.
#
# It groups under "Engine" for exactly that reason: the folder a target sits in
# is the same claim its directory makes.
set(CMAKE_FOLDER "Engine")
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
set(CMAKE_FOLDER "Games/UntitledFighter")
add_subdirectory(Games/UntitledFighter)

# Add subdirectories for Engine, Editor, the standalone Player, and the
# headless AssetCooker (batch asset work, spawned by the editor).
#
# THE CMAKE_FOLDER LINES ARE THE WHOLE GROUPING SCHEME, and they read top to
# bottom as the answer to "what is this thing": the engine, then the one
# application, then everything that application produces or spawns -- the two
# players, the cooker, and the API reference. The `unset` before Editor
# is not a reset for tidiness; it is what leaves the Editor alone at the top
# level, which is the rule stated above.
#
# `unset`, NOT `set(CMAKE_FOLDER "")`, and the difference is visible in the file
# API rather than a style preference. An empty value is still a value: CMake
# emits `"folder": {"name": ""}` for such a target, and an IDE reading that has
# been handed a folder whose name happens to be blank rather than told there is
# no folder. Unsetting it omits the key entirely, which is what an ungrouped
# target looks like. Checked in a generated reply both ways.
set(CMAKE_FOLDER "Engine")
add_subdirectory(Engine)
unset(CMAKE_FOLDER)
add_subdirectory(Editor)
set(CMAKE_FOLDER "Artifacts")
add_subdirectory(Player)
add_subdirectory(Cooker)
# API reference (optional): defines a 'docs' target when Doxygen is installed,
# and quietly skips it otherwise. Never a build dependency.
# NO add_subdirectory(docs). There was one, for an optional Doxygen target that
# CI never ran, covering only Engine/, behind a README row advertising a
# "generated reference ... produced from the headers themselves". The reference
# was 57 hand-written lines and nothing generated it (docs/adr/ADR-010 section 9.1).
# The public surface is one facade header and its comments; that is the reference.
unset(CMAKE_FOLDER)

# The editor invokes AssetCooker.exe at runtime (Validate button): building
# the Editor must produce/refresh the cooker too, or a dev-loop build ships
# a missing/stale cooker next to a new editor.
add_dependencies(Editor AssetCooker)

# ---------------------------------------------------------------------------
# FOUR EXECUTABLES, AND ONE OF THEM IS A THING SOMEBODY STARTS
# ---------------------------------------------------------------------------
#   Editor          the application. This is the startup item.
#   AssetCooker     a CHILD PROCESS. The editor spawns it (Validate); the
#                   dependency above is what keeps it from being stale. Running
#                   it by hand is a debugging act, not a use of the engine.
#   PlayerDebug     ARTIFACTS. File > Build Settings > Build picks a profile,
#   PlayerShipping  builds one of them and installs it into a bundle, and that
#                   bundle is the product. Starting one from the IDE runs a
#                   player against the BUILD TREE's staged Exported/, which is
#                   not the thing anybody ships.
#
# ALL FOUR STAY IN THE DEFAULT BUILD, and that is a decision about coverage
# rather than a leftover. EXCLUDE_FROM_ALL on the two players is the obvious
# way to make them "an artifact produced on demand" in the build system as well
# as in the ADR, and the editor would not even notice: BuildPipeline.cpp runs
# `cmake --build <binaryDir> --target PlayerShipping`, BY NAME, so membership
# of `all` is not what makes the Build button work.
#
# It is still not worth what it costs, for two reasons that were measured
# rather than argued:
#   * IT WOULD NOT SHORTEN THE DROPDOWN BY ONE ENTRY. See the note above
#     add_subdirectory(ThirdParty): EXCLUDE_FROM_ALL is invisible to the CMake
#     file API, so Visual Studio would go on offering PlayerDebug and
#     PlayerShipping exactly as it does today. The purity would be real and the
#     visible benefit exactly zero.
#   * IT WOULD GUT A CI CHECK WITHOUT FAILING ONE. `cmake --build --preset
#     x64-release` builds the default target set, and the "Build the shipping
#     configurations" step in .github/workflows/ci.yml exists for one stated
#     reason: "a missing symbol shows up when the Player links, not when the
#     Engine does". Take the players out of `all` and that step stops linking a
#     player while still passing -- a green job checking less than its own
#     comment claims. Every developer's Build All loses the same check.
#
# So the two dropdown entries stay and the FOLDER above is what says what they
# are. IF THEY EVER DO LEAVE `all`, the same commit has to add `--target
# PlayerDebug PlayerShipping` to that CI step, or the coverage goes quietly.

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
#   MyCoreEngine::TitleFrontEndScene        Engine/src/core/TitleFrontEnd.h
# Linking the library is also what TURNS THE HOOK ON -- each library carries the
# guard macro as an INTERFACE compile definition, so there is no second line here
# that anybody could forget and no way for the link and the macro to disagree.
# See Games/UntitledFighter/CMakeLists.txt for the pattern written out.
#
# ONE LIBRARY PER SEAM, AND A HOST TAKES THE SEAMS IT WANTS. That is why the
# three hooks above are three targets below rather than one per host: the modes
# and the front end used to share a library called `UntitledFighterPlayer`, and
# the moment the EDITOR wanted the modes -- entering a mode in the Game view is
# how the editor previews what ships -- the only way to give it them was to make
# a general-purpose editor link a library called "Player". That works and it is a
# lie. The split is what makes each link line below readable on its own: the
# editor links the panels and the modes, a player links the modes and the front
# end, and nobody links a name that predicts who they are.
#
# REMOVE THE add_subdirectory ABOVE AND YOU HAVE A GENERAL ENGINE with a general
# editor and a general player, all of which still build and run -- the editor
# simply has no Combo Prover in its Window menu and nothing to enter in its Game
# view, and the player's main menu has no fighting game on it and boots the
# engine demo. That is the property this arrangement is for.
#
# A TITLE-LESS EDITOR STILL HAS THE MACHINERY, and that is what makes the above
# cost nothing to keep true. GameModeRegistry is an ENGINE type, so the editor's
# registry is still constructed, still empty, and still the thing the previewed
# menu derives its slots from -- `Count() == 0`, four `if="menuModeN"` slots
# false, a menu identical to the one before modes existed (MenuUIHooks::modes
# says so). Only the CALL to MyCoreEngine::RegisterTitleGameModes is compiled
# out, because only the call needs a definition that a title supplies. Guarding
# the registry itself would be guarding the wrong half: it would mean two shapes
# of editor to reason about instead of one editor with an empty list.
#
# IT IS GUARDED RATHER THAN ADVERTISED, and the difference is the whole point.
# These lines name their targets WITHOUT a `::`, and CMake treats an unknown
# non-namespaced name as a raw library for the linker rather than as a missing
# target -- so dropping the title configured CLEAN and then died with
# `LNK1104: cannot open file 'UntitledFighterPlayer.lib'` (gcc:
# `cannot find -lUntitledFighterPlayer`), naming a file that never existed. (That
# was the library's name at the time; it is `UntitledFighterModes` now, and the
# trap is identical for every name below.) That is the opposite of how every
# other rule in this repository fails, and it made the paragraph above a wish
# rather than a boundary. Verified by experiment, not by reading.
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

# THE MODES GO TO BOTH HOSTS, and this is the line the whole split was for.
#
# The editor links them because ENTERING A MODE IN THE GAME VIEW IS HOW THE
# EDITOR PREVIEWS WHAT SHIPS -- the one property this codebase protects hardest,
# and the one place it was not being kept: the shipped player's main menu offered
# the game's modes and the same menu previewed in the editor's Game view showed
# four authored verbs and none of them, because the editor had no registry to
# publish. It is the same archive in both executables, so there is no second
# implementation of a mode to drift.
#
# THE GUARD MACRO ARRIVES WITH THE LINK, here as everywhere: this line is also
# what defines CSE_HOST_TITLE_MODES for every source file in the target, which is
# what compiles each host's call to MyCoreEngine::RegisterTitleGameModes in. A
# host that gains this line and never calls the hook is not an error -- it is a
# host with no concept of entering a mode, which is what the editor was.
if (TARGET UntitledFighterModes)
    target_link_libraries(Editor         PRIVATE UntitledFighterModes)
    target_link_libraries(PlayerDebug    PRIVATE UntitledFighterModes)
    target_link_libraries(PlayerShipping PRIVATE UntitledFighterModes)
endif()

# THE FRONT END GOES TO THE PLAYERS ONLY, and the absence of `Editor` here is a
# decision rather than an oversight.
#
# MyCoreEngine::TitleFrontEndScene answers "what does this host START AT", and an
# editor never asks it: a designer opens the scene they want, from the asset
# browser or the command line, and an editor that booted a game's title screen
# instead would be overriding them. So the editor gets no
# CSE_HOST_TITLE_FRONT_END, which also means an editor call site added later
# would fail to compile rather than quietly change what the editor opens -- the
# loud failure, which is how the rest of this seam behaves.
#
# The editor still PREVIEWS that front end perfectly well: it is an ordinary
# scene in the title's staged assets (UntitledFighter/menu.json), opened the
# ordinary way. Booting it and opening it are different verbs, and only one of
# them belongs to a tool.
if (TARGET UntitledFighterFrontEnd)
    target_link_libraries(PlayerDebug    PRIVATE UntitledFighterFrontEnd)
    target_link_libraries(PlayerShipping PRIVATE UntitledFighterFrontEnd)
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
# would go on staging a game into a build that does not contain one. All THREE
# of the title's host libraries come from that single add_subdirectory, so any of
# them answers "is this title in the build" identically -- the question is
# whether the title was added, not which seams a host took. This names the modes
# because they are the half both hosts link.
#
# THE DIRECTORY TEST IS THE SECOND HALF, and it is not a transitional
# convenience: a title need not ship assets at all. The fighting game was exactly
# such a title until its front end moved out of the engine's asset root, and a
# title whose modes draw everything procedurally never acquires one. The staging
# script FATAL_ERRORs on a root that is not a directory -- the right answer for a
# path somebody mistyped, the wrong one for a title that simply has no files.
if (TARGET UntitledFighterModes AND
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
# Grouped explicitly rather than by CMAKE_FOLDER: this target is created in the
# ROOT scope, where CMAKE_FOLDER is deliberately unset so that the Editor is the
# only thing at the top level. A build step sitting up there beside it would say
# the wrong thing about both.
set_target_properties(runtime_assets PROPERTIES FOLDER "Build steps")

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
#
# WHAT IS CHECKED IS THE DIRECT LINK LINE, NOT THE CLOSURE, and that is the
# correct thing to check rather than a limitation to apologise for. Linking
# UntitledFighterModes now drags CseGame, CseKernel and CseData into the Editor's
# transitive closure -- more than the Combo Prover's CseData ever did -- and none
# of it is reachable from Editor/src: the title's libraries export no include
# directories at all (both of their CMakeLists say so and why), so the compiler
# cannot find a `cse/...` header from inside the editor's sources even though the
# linker holds the archives. The rule this block enforces is about what a
# general-purpose target may NAME, and the property that keeps the rule honest is
# that naming one is the only way to reach one.
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
    # Same reason as runtime_assets above: created in the root scope, grouped by
    # hand so the top level stays the Editor's alone.
    set_target_properties(app_runtime_deps PROPERTIES FOLDER "Build steps")
endif()

option(ENABLE_TESTS "Build unit tests" ON)

# THE ~45 TEST BINARIES ARE THE BIGGEST SINGLE THING IN THE STARTUP DROPDOWN,
# and the reason they are usually not in it is this option: the three
# application presets in CMakePresets.json set ENABLE_TESTS=OFF, so the tests
# exist as targets only in x64-relwithdebinfo-tests, which is where somebody
# wanting to run a test already is. That is not a tidiness setting; it is the
# one lever on that list this repository actually owns (nothing can filter the
# dropdown -- see the note above add_subdirectory(ThirdParty)), so turning it
# back ON in an application preset puts all of them back.
#
# They still group: one folder for the lot rather than 45 loose targets in the
# view. tests/CMakeLists.txt names no folder itself, so this is the whole of it.
if(ENABLE_TESTS)
  enable_testing()
  set(CMAKE_FOLDER "Tests")
  add_subdirectory(tests)
  unset(CMAKE_FOLDER)
endif()

# ---------------------------------------------------------------------------
# ONE PLACE PRODUCES A PLAYER BUNDLE, AND IT IS NOT THIS FILE
# ---------------------------------------------------------------------------
# This block used to be five CPACK_ variables and `include(CPack)`, which gave
# the build a `package` target and `cpack -G ZIP`: a second way to turn this
# tree into a shipped game, beside File > Build Settings > Build in the editor.
# ADR-008 says the editor produces the player, and two entry points are two
# things that have to keep agreeing.
#
# THEY AGREED ABOUT THE FILE LIST AND DISAGREED ABOUT EVERYTHING BEFORE IT.
# Contents were never the problem: both routes go through the SAME install
# rules in Player/CMakeLists.txt, which were deliberately written so `cpack`
# and the Build action assemble the same directory. The difference is the
# questions the editor asks first. The Build action refuses a build whose
# configuration is not the one this tree was generated for, whose scene list is
# empty or names a scene that fails SceneSerializer::Validate or sits outside
# the asset root, whose output directory is inside the build tree, whose first
# scene is not the one it says the game boots into. `cpack` asks none of that:
# run it on a tree the editor has never saved in and it prints a WARNING, ships
# the source-tree defaults instead of the author's scenes, and exits 0. A game
# with the wrong level in it and a green exit code is exactly the failure this
# whole pipeline exists to make impossible.
#
# THE MECHANISM STAYS AND THE SECOND DOOR CLOSES. The install rules are
# untouched -- they are still the only thing in this tree that assembles a
# bundle, and the editor calls them. A headless release job is still one line,
# `cmake --install <tree> --config Release --prefix <dir>`, because that is
# literally the command BuildPipeline.cpp runs. What is gone is the target that
# ran it without asking any of the questions.
#
# AND THERE IS DELIBERATELY NO REPLACEMENT `package` TARGET, which was tried and
# reverted here rather than shipped.
#
# The idea was a signpost: an `add_custom_target(package)` that prints where
# bundles come from and then fails, catching anybody reaching for the old target
# by name. It configures cleanly on a FRESH tree, which is the only way it was
# tested, and it hard-fails on every EXISTING one:
#
#     CMake Error at CMakeLists.txt (add_custom_target):
#       The target name "package" is reserved when CPack packaging is enabled.
#
# `package` is reserved while CPack is enabled, and CMake still considers it
# enabled from the FOUR STALE CPACK_* ENTRIES a previously-configured cache
# carries -- deleting `include(CPack)` from this file does not remove them. So
# the signpost would have configured green on CI, which always starts from
# nothing, and broken the configure for every developer with a build directory
# older than this commit. A change whose failure mode is invisible to CI and
# universal locally is the exact shape this repository keeps getting caught by,
# and a nicer error message is not worth it.
#
# What a bare `cpack` does now is already self-explanatory: without
# include(CPack) there is no CPackConfig.cmake in the build tree and it says so.
# `--target package` says "unknown target", which is also true. Neither needs a
# target to explain it, and ADR-008 is where the reasoning lives.
