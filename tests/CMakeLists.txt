# tests/CMakeLists.txt
include(FetchContent)

# GoogleTest
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE) # keep gtest out of the game package
FetchContent_MakeAvailable(googletest)

# One shared staging step for everything the test executables need at runtime:
# Engine.dll, its third-party DLLs (from the engine's bin dir, where vcpkg
# deploys them), and the Exported/ shaders+assets. A single target avoids the
# parallel-copy races that per-test POST_BUILD copies caused under Ninja
# (several tests copying the same file into tests/ at the same time).
#
# The DLLs are copied by GLOBBING the engine's output directory at build time
# (stage_runtime_dlls.cmake) rather than by naming them here. This was a list of
# seven names, and it was missing four; the one that mattered was
# PhysXCommon_64.dll, which no dependency analysis can find because Engine.dll
# does not import it -- PhysX_64 loads it itself at runtime. On a clean tree the
# physics tests died with an SEH 0xc06d007e and eighteen "resource deadlock would
# occur" cascades, while a developer machine with an older build lying around
# passed. See stage_runtime_dlls.cmake for the full story. Globbing also drops
# the $<CONFIG:Debug> 'd'-suffix genexes, which existed only to spell the names.
#
# WINDOWS-ONLY, still: `copy` fails on a missing source, those names do not exist
# next to libEngine.so, and this target once aborted the whole Linux build --
# taking the Editor and Player with it, because every test depends on it. Linux
# needs no staging at all: the loader resolves libEngine.so through the build
# RPATH. Only the Exported/ assets are shared by both platforms.
#
# The assets go through the SAME cmake/stage_runtime_assets.cmake the Editor and
# the players use. This was `cmake -E copy_directory`, which is a pure add-and-
# overwrite: a file deleted from the source tree stayed staged here forever. Two
# staging rules for one directory layout is the divergence that has already bitten
# this build twice, so there is now one script and the tests exercise it too.
if (WIN32)
  add_custom_target(test_runtime_deps
    COMMAND ${CMAKE_COMMAND}
      -DSRC=$<TARGET_FILE_DIR:Engine>
      -DVCPKG_BIN=${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<$<CONFIG:Debug>:debug/>bin
      -DDST=${CMAKE_CURRENT_BINARY_DIR}
      -P ${CMAKE_SOURCE_DIR}/cmake/stage_runtime_dlls.cmake
    COMMAND ${CMAKE_COMMAND}
      -DSRC=${CMAKE_SOURCE_DIR}/Editor/src/Exported
      -DDST=${CMAKE_CURRENT_BINARY_DIR}/Exported
      -P ${CMAKE_SOURCE_DIR}/cmake/stage_runtime_assets.cmake
    COMMENT "Staging test runtime dependencies"
    VERBATIM)
else()
  add_custom_target(test_runtime_deps
    COMMAND ${CMAKE_COMMAND}
      -DSRC=${CMAKE_SOURCE_DIR}/Editor/src/Exported
      -DDST=${CMAKE_CURRENT_BINARY_DIR}/Exported
      -P ${CMAKE_SOURCE_DIR}/cmake/stage_runtime_assets.cmake
    COMMENT "Staging test runtime assets"
    VERBATIM)
endif()
add_dependencies(test_runtime_deps Engine)

# lua-c++.dll needed its own conditional block here, because naming it in a fixed
# copy list broke every test in a -DCSE_ENABLE_LUA=OFF build where the file does
# not exist. The glob subsumes that: Engine/CMakeLists.txt already deploys
# lua-c++.dll into the engine's output directory (vcpkg's applocal misses it, for
# the same import-table reason as PhysXCommon_64.dll), so it is copied when it is
# there and simply absent when it is not. No condition required.

macro(engine_test name)
  add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE Engine GTest::gtest_main)
  add_test(NAME ${name} COMMAND $<TARGET_FILE:${name}>)
  add_dependencies(${name} test_runtime_deps)
endmacro()

# The kernel test links CseKernel and NOT Engine, which is the point: if it ever
# needs the Engine to build, the kernel has stopped being independent and
# ADR-002 CHOICE D has quietly failed. It also needs none of the staged runtime
# DLLs, because the kernel has no dependencies to stage.
add_executable(test_kernel test_kernel.cpp)
target_link_libraries(test_kernel PRIVATE CseKernel GTest::gtest_main)
add_test(NAME test_kernel COMMAND $<TARGET_FILE:test_kernel>)

# The kernel driven through the rollback session seam. Links CseKernel and
# CseNet and NOT Engine — and note it gets no access to gekkonet.h, because
# CseNet links GekkoNet PRIVATE. If this ever fails to compile for want of a
# GekkoNet header, the seam has leaked.
add_executable(test_session test_session.cpp)
target_link_libraries(test_session PRIVATE CseKernel CseNet GTest::gtest_main)
add_test(NAME test_session COMMAND $<TARGET_FILE:test_session>)

# The cross-toolchain golden hash. This is the test that turns "bit-identity is a
# property of integer arithmetic, not of a build flag" (ARCHITECTURE.md D2/D3)
# from an argument into a fact: the same simulation, compiled by MSVC and by gcc,
# must agree with one recorded constant. CI running it on both legs IS the
# cross-toolchain comparison — no test binary can compare itself to another
# platform, so the golden constant is the meeting point.
add_executable(test_determinism_crossplat test_determinism_crossplat.cpp)
target_link_libraries(test_determinism_crossplat PRIVATE CseKernel GTest::gtest_main)
add_test(NAME test_determinism_crossplat COMMAND $<TARGET_FILE:test_determinism_crossplat>)

# Character data loading. nlohmann is found HERE rather than inherited: it is an
# IMPORTED target created by find_package in Engine/'s scope, and tests/ is a
# sibling scope that never sees it. The other test targets get away with
# including <nlohmann/json.hpp> only because engine_test links Engine, whose
# PUBLIC vcpkg dependencies drag the include root along. This target links no
# such thing, on purpose.
find_package(nlohmann_json CONFIG REQUIRED)
add_executable(test_character_data test_character_data.cpp)
target_link_libraries(test_character_data
    PRIVATE CseData nlohmann_json::nlohmann_json GTest::gtest_main)
add_test(NAME test_character_data COMMAND $<TARGET_FILE:test_character_data>)

# Hitboxes and hit resolution. Links CseKernel only — combat is simulation, and
# the simulation links nothing.
add_executable(test_combat test_combat.cpp)
target_link_libraries(test_combat PRIVATE CseKernel GTest::gtest_main)
add_test(NAME test_combat COMMAND $<TARGET_FILE:test_combat>)

# The projection to the combo prover, run over the three real transcribed
# characters. This is the test behind research contribution #9: the artifact
# analysed is the artifact that ships.
#
# It links ComboProver directly as well as CseData, because the test asserts
# against comboprover's own Status enum — the SEAM is that ProverAdapter.h names
# no comboprover type, not that nothing may ever include the header.
add_executable(test_prover_adapter test_prover_adapter.cpp)
target_link_libraries(test_prover_adapter
    PRIVATE CseData ComboProver nlohmann_json::nlohmann_json GTest::gtest_main)
add_test(NAME test_prover_adapter COMMAND $<TARGET_FILE:test_prover_adapter>)

# A real transcribed character driving the real simulation. The first test to
# link BOTH CseData and CseKernel, which is the shape the note at the top of
# Combat.h predicts: the data layer builds a POD, the kernel consumes it, and
# the two never link to each other — only a caller links both.
add_executable(test_match_bridge test_match_bridge.cpp)
target_link_libraries(test_match_bridge PRIVATE CseData CseKernel GTest::gtest_main)
add_test(NAME test_match_bridge COMMAND $<TARGET_FILE:test_match_bridge>)

# Cancels: the rule that lets a connected move be interrupted into another, which
# is what makes the combos the prover reasons about performable at all.
add_executable(test_cancels test_cancels.cpp)
target_link_libraries(test_cancels PRIVATE CseData CseKernel GTest::gtest_main)
add_test(NAME test_cancels COMMAND $<TARGET_FILE:test_cancels>)

# GROUND-TRUTH VALIDATION — ARCHITECTURE.md 5.5 item 4, the evidence no offline
# analysis can produce: the loop the prover PRINTS, executed by the kernel as a
# scripted input trace.
#
# Unlike every other character test, this one reads the STAGED SHIPPING tree
# (Exported/Characters) rather than tests/fixtures/characters, because it is
# about the project's own characters rather than the MUGEN corpus. So it takes an
# explicit dependency on the staging target instead of relying on whatever a
# previous build happened to leave behind — the test walks up from the working
# directory to find them and would otherwise fall through to the source tree,
# silently testing unstaged content.
add_executable(test_ground_truth test_ground_truth.cpp)
target_link_libraries(test_ground_truth PRIVATE CseData CseKernel GTest::gtest_main)
add_dependencies(test_ground_truth test_runtime_deps)
add_test(NAME test_ground_truth COMMAND $<TARGET_FILE:test_ground_truth>)

# HOW BIG THE GAP IS. test_ground_truth measured ONE of fighter_a's cycles
# executing forever in a kernel that has no juggle to stop it. This one
# enumerates the cancel graph's simple cycles from the loaded character —
# derived, not taken from the authoring report — and asks the same question of
# every one of them.
add_executable(test_gap_extent test_gap_extent.cpp)
target_link_libraries(test_gap_extent PRIVATE CseData CseKernel GTest::gtest_main)
add_dependencies(test_gap_extent test_runtime_deps)
add_test(NAME test_gap_extent COMMAND $<TARGET_FILE:test_gap_extent>)

# ONE INTEGER, AND TWO BOUNDARIES THAT ARE NOT THE SAME BOUNDARY.
#
# fighter_a's `crouch_lp` self-cancel is authored at delay 10 and resolves to the
# kernel window [13, 9] — empty. The upper bound is the move's own authored
# `engine.cancel_window_ticks` close of 9 (MatchBuilder.cpp:427-435 clamps
# `duration - 1` by it); HITSTUN IS NOT IN THAT ARITHMETIC AT ALL, which is the
# thing to know before reasoning about this edge by hand.
#
# So the two boundaries sit three frames apart, and the test sweeps the delay to
# show it: the PROVER flips TERMINATING -> INFINITE at 10 -> 9 (it compares
# `startup <= hitstun - delay`, i.e. 3 <= 12 - delay, and prints a shortfall of
# exactly 1), while the KERNEL cannot perform the loop until delay 6 opens the
# window. The analysis is three frames ahead of the game — the conservative
# direction, which is the safe way round.
#
# The delay is mutated IN MEMORY — the shape test_character_data.cpp uses to make
# each load assertion fail on purpose — rather than by shipping a variant file.
# That holds everything else identical by construction, so the claim really is
# that one integer does it, and it keeps a deliberately-broken character out of
# the game's asset directory.
add_executable(test_one_frame test_one_frame.cpp)
target_link_libraries(test_one_frame
    PRIVATE CseData CseKernel nlohmann_json::nlohmann_json GTest::gtest_main)
add_dependencies(test_one_frame test_runtime_deps)
add_test(NAME test_one_frame COMMAND $<TARGET_FILE:test_one_frame>)

engine_test(test_event_bus)        # EventBus publish/subscribe
engine_test(test_input_map)        # actions/axes/deadzone via scripted-input fake
engine_test(test_render_passes)    # GL-headless CSM/forward/tonemap wiring
engine_test(test_scene_details)    # cascade Z-split culling
engine_test(test_scene_serializer) # JSON scene round-trip
engine_test(test_fixed_timestep)   # fixed-step accumulator
engine_test(test_splits)           # CSM split math (pure CPU)
engine_test(test_undo_history)     # editor undo/redo snapshots (pure CPU)
engine_test(test_hierarchy)        # transform parenting (pure CPU)
engine_test(test_camera_director)  # Cinemachine-style selection/blending (pure CPU)
engine_test(test_asset_index)      # asset filesystem domain: scan/cache/find (pure CPU)
engine_test(test_job_system)       # P4-3 thread pool + main-thread completion pump (pure CPU)
engine_test(test_model_decode)     # P4-3 model decode stage (pure CPU — no GL by design)
engine_test(test_asset_manager_async) # P4-3 async RequestModel: states/dedupe/cap (pure CPU)
engine_test(test_lights)           # punctual light selection + culling (pure CPU)
engine_test(test_physics)          # backend-conformance suite + PhysicsWorld/ECS (pure CPU)
engine_test(test_scripting)        # script seam + Lua isolation/error policy (pure CPU)
engine_test(test_audio)            # audio backend seam + registry (headless, no device needed)
engine_test(test_mesh_material)  # mesh->material-slot mapping (GL)
engine_test(test_ibl)              # IBL bake: needs a real GL context
engine_test(test_fxaa)             # FXAA edge/flat behaviour: needs a real GL context
engine_test(test_post_chain)       # LDR ping-pong routing + per-pass GL-state hygiene
engine_test(test_renderer2d)       # 2D batcher: batching, layers, clip, projections
engine_test(test_font_text)        # stb_truetype atlas bake, UTF-8, text layout
engine_test(test_ui_layout)        # element tree + flexbox against known CSS results
engine_test(test_ui_pass)          # UI overlay: draws on the final image, no chain slot
engine_test(test_ui_input)         # hit-testing, hover/press, bubbling events
engine_test(test_ui_stylesheet)    # .cstyle subset: selectors, cascade, diagnostics
engine_test(test_ui_markup)        # CXML subset: tree building, inline style, sandbox
engine_test(test_ui_hotreload)     # asset reload: change detection, re-binding, failure safety
engine_test(test_ui_value)         # the bound-value transport: coercion refusals, non-finite gate
engine_test(test_ui_binding)       # data sources, {hole} templates, the binder, hot-reload survival
engine_test(test_ui_nav)         # directional nav: the repeat clock, focus moves, paging
engine_test(test_ui_slider)      # <Slider>: the drag, the capture, the two-way value
engine_test(test_ui_menu)        # main-menu verbs + what a scene swap does to the UI
engine_test(test_ui_pseudo)        # :hover/:active — specificity, re-cascade, what it must not destroy
engine_test(test_ui_focus)         # keyboard focus, tab order, :focus/:disabled, UTF-8 input
engine_test(test_ui_selectors)     # descendant/child combinators, summed specificity
engine_test(test_ui_textfield)     # text entry: UTF-8 caret/editing, selection, mask, limits
engine_test(test_ui_twoway)        # element -> source: push-* state and a field's bind-value
engine_test(test_ui_component)     # UI as scene content: component, serialization, undo, UIWorld
engine_test(test_ui_classbind)     # classes= toggles: re-cascade, other bindings surviving, no recursion
engine_test(test_scene_loader)     # runtime scene swap: deferral, the contract, the report
engine_test(test_ui_scale)         # UI scaling: the authored-vs-real unit rule, and the origin
engine_test(test_ui_tabs)          # TabView: load-time expansion, panel visibility, focus
engine_test(test_ui_repeat)        # repeat= collection binding: fixed pool, sliding window, teardown
engine_test(test_ui_scroll)        # overflow: scroll — extent, clamp order, wheel routing, thumb capture
engine_test(test_import_settings)  # P4-3 phase 4: .import sidecar round-trip (pure CPU)
engine_test(test_asset_validator)  # P4-3 phase 4: cooker validate logic (pure CPU)

# AssetCooker CLI smoke: no args must print the usage text (matching on
# output, not exit code — a crash also exits nonzero and would fool
# WILL_FAIL, but it never prints "usage:")
add_test(NAME cooker_usage COMMAND AssetCooker)
set_tests_properties(cooker_usage PROPERTIES PASS_REGULAR_EXPRESSION "usage:")

# End-to-end validate over the staged test asset tree: the success path —
# warnings allowed, errors none, DONE protocol line intact
add_test(NAME cooker_validate COMMAND AssetCooker validate Exported
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(cooker_validate PROPERTIES
    PASS_REGULAR_EXPRESSION "DONE models=[0-9]+ textures=[0-9]+ warnings=[0-9]+ errors=0")

# ...and the fail-closed contract: a missing root must NOT validate clean
add_test(NAME cooker_missing_root COMMAND AssetCooker validate no_such_root_xyz
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(cooker_missing_root PROPERTIES
    PASS_REGULAR_EXPRESSION "asset root not found")

# UndoHistory lives in the Editor (it snapshots editor-managed components)
# but is ImGui-free, so it can be compiled straight into a headless test.
target_sources(test_undo_history PRIVATE ${CMAKE_SOURCE_DIR}/Editor/src/UndoHistory.cpp)
target_include_directories(test_undo_history PRIVATE ${CMAKE_SOURCE_DIR}/Editor/src)

# Same arrangement: the UI component test asserts the undo snapshot covers it,
# which is the check that catches EntitySnapshot's closed list being missed.
target_sources(test_ui_component PRIVATE ${CMAKE_SOURCE_DIR}/Editor/src/UndoHistory.cpp)
target_include_directories(test_ui_component PRIVATE ${CMAKE_SOURCE_DIR}/Editor/src)

engine_test(test_perf_render)      # P4-8 perf budgets (real GPU work)
# Timing test: never share the machine with other tests, and give the
# model load + 4 scenarios room to breathe.
set_tests_properties(test_perf_render PROPERTIES
  LABELS "perf;gl"
  RUN_SERIAL TRUE
  TIMEOUT 300)

target_compile_definitions(test_render_passes PRIVATE UNIT_TEST=1)

# Which tests need a real GL context. They create a hidden GLFW window and load
# GLAD, so they cannot run on a machine with no OpenGL driver at all -- which is
# every default CI runner. Labelling them lets CI choose: run everything under a
# software rasteriser (what .github/workflows/ci.yml does, via Mesa llvmpipe),
# or fall back to `ctest -LE "perf|gl"` and lose that coverage knowingly rather
# than by accident. Keep this list in step with the greps in the workflow.
set_tests_properties(
    test_font_text
    test_fxaa
    test_ibl
    test_mesh_material
    test_post_chain
    test_render_passes
    test_renderer2d
    test_scene_details
    test_ui_layout
    test_ui_pass
  PROPERTIES LABELS "gl")

# The instruction-limit test proves a runaway script gets aborted. If that
# guard ever regresses the test HANGS instead of failing, so cap it: a red
# test is actionable, a stuck CI job is not.
set_tests_properties(test_scripting PROPERTIES TIMEOUT 120)
