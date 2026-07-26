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
# vcpkg's Debug DLLs carry a 'd' suffix (assimp-vc143-mtd.dll, zlibd1.dll);
# the generator expressions keep this working in both configs.
#
# The third-party DLL list is WINDOWS-ONLY. Those names (assimp-vc143-mt.dll,
# zlib1.dll, ...) do not exist next to libEngine.so, and `cmake -E
# copy_if_different` fails on a missing source — so on Linux this target aborted
# the build with "Error copying file" naming an MSVC-suffixed DLL, taking the
# Editor and Player down with it because every test target depends on it. Linux
# needs no such staging: the loader resolves libEngine.so through the build
# RPATH. Only the Exported/ assets are shared by both platforms.
if (WIN32)
  add_custom_target(test_runtime_deps
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:Engine>
      "$<TARGET_FILE_DIR:Engine>/assimp-vc143-mt$<$<CONFIG:Debug>:d>.dll"
      "$<TARGET_FILE_DIR:Engine>/poly2tri.dll"
      "$<TARGET_FILE_DIR:Engine>/zlib$<$<CONFIG:Debug>:d>1.dll"
      "$<TARGET_FILE_DIR:Engine>/minizip.dll"
      "$<TARGET_FILE_DIR:Engine>/pugixml.dll"
      "$<TARGET_FILE_DIR:Engine>/glfw3.dll"
      ${CMAKE_CURRENT_BINARY_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      ${CMAKE_SOURCE_DIR}/Editor/src/Exported
      ${CMAKE_CURRENT_BINARY_DIR}/Exported
    COMMENT "Staging test runtime dependencies"
    VERBATIM)
else()
  add_custom_target(test_runtime_deps
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      ${CMAKE_SOURCE_DIR}/Editor/src/Exported
      ${CMAKE_CURRENT_BINARY_DIR}/Exported
    COMMENT "Staging test runtime assets"
    VERBATIM)
endif()
add_dependencies(test_runtime_deps Engine)

# Staged separately because the Lua backend is optional: listing lua-c++.dll
# in the copy above would break every test in a -DCSE_ENABLE_LUA=OFF build.
# Uses the path Engine published rather than `TARGET lua-cpp`, which is not
# visible here -- imported targets do not cross directory scopes.
if (CSE_WITH_LUA_RUNTIME AND WIN32)
  add_custom_command(TARGET test_runtime_deps POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE_DIR:Engine>/lua-c++.dll" ${CMAKE_CURRENT_BINARY_DIR}
    VERBATIM)
endif()

macro(engine_test name)
  add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE Engine GTest::gtest_main)
  add_test(NAME ${name} COMMAND $<TARGET_FILE:${name}>)
  add_dependencies(${name} test_runtime_deps)
endmacro()

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
engine_test(test_ui_stylesheet)    # USS subset: selectors, cascade, diagnostics
engine_test(test_ui_markup)        # UXML subset: tree building, inline style, sandbox
engine_test(test_ui_hotreload)     # asset reload: change detection, re-binding, failure safety
engine_test(test_ui_value)         # the bound-value transport: coercion refusals, non-finite gate
engine_test(test_ui_binding)       # data sources, {hole} templates, the binder, hot-reload survival
engine_test(test_ui_pseudo)        # :hover/:active — specificity, re-cascade, what it must not destroy
engine_test(test_ui_focus)         # keyboard focus, tab order, :focus/:disabled, UTF-8 input
engine_test(test_ui_selectors)     # descendant/child combinators, summed specificity
engine_test(test_ui_textfield)     # text entry: UTF-8 caret/editing, selection, mask, limits
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

engine_test(test_perf_render)      # P4-8 perf budgets (real GPU work)
# Timing test: never share the machine with other tests, and give the
# model load + 4 scenarios room to breathe.
set_tests_properties(test_perf_render PROPERTIES
  LABELS "perf"
  RUN_SERIAL TRUE
  TIMEOUT 300)

target_compile_definitions(test_render_passes PRIVATE UNIT_TEST=1)

# The instruction-limit test proves a runaway script gets aborted. If that
# guard ever regresses the test HANGS instead of failing, so cap it: a red
# test is actionable, a stuck CI job is not.
set_tests_properties(test_scripting PROPERTIES TIMEOUT 120)
