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
add_dependencies(test_runtime_deps Engine)

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
