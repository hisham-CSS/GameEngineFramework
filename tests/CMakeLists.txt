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

# EventBus test
add_executable(test_event_bus test_event_bus.cpp)
target_link_libraries(test_event_bus PRIVATE
  Engine
  GTest::gtest_main
)
add_test(NAME EventBusTests COMMAND $<TARGET_FILE:test_event_bus>)

# InputSystem null-safe test
add_executable(test_input_system_nullsafe test_input_system_nullsafe.cpp)
target_link_libraries(test_input_system_nullsafe PRIVATE
  Engine
  GTest::gtest_main
)
add_test(NAME TestInputSystemNullsafe COMMAND $<TARGET_FILE:test_input_system_nullsafe>)

# InputSystem Integration test
add_executable(test_input_system_glfw test_input_system_glfw.cpp)
target_link_libraries(test_input_system_glfw PRIVATE
  Engine
  GTest::gtest_main
)
add_test(NAME TestInputSystemGlfw COMMAND $<TARGET_FILE:test_input_system_glfw>)

# Render Passes test
add_executable(test_render_passes test_render_passes.cpp)
target_compile_definitions(test_render_passes PRIVATE UNIT_TEST=1)
target_link_libraries(test_render_passes PRIVATE
  Engine
  GTest::gtest_main
)
 add_test(NAME test_render_passes COMMAND $<TARGET_FILE:test_render_passes>)

# Scene Details test
add_executable(test_scene_details test_scene_details.cpp)
target_link_libraries(test_scene_details PRIVATE Engine GTest::gtest_main)
add_test(NAME test_scene_details COMMAND $<TARGET_FILE:test_scene_details>)

add_custom_command(
  TARGET test_scene_details POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/Editor/src/Exported
    $<TARGET_FILE_DIR:test_scene_details>/Exported)
# Copy Runtime Dependencies for test_scene_details
add_custom_command(TARGET test_scene_details POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:Engine>
    $<TARGET_FILE_DIR:test_scene_details>)

# We need to find Assimp/Poly2Tri DLLs. 
# Since we use Vcpkg, they are usually in the same bin dir as Engine.dll *before* copy?
# Or we can ask CMake to find them if they are imported targets.
# But "assimp::assimp" might be static or shared.
# A robust way for Vcpkg is to look in the bin folder of the installed triplet?
# Or simpler: copy them from the folder where Engine.dll was built.
# "${CMAKE_BINARY_DIR}/bin" or similar.
# Let's try copying from the directory of Engine target.
add_custom_command(TARGET test_scene_details POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE_DIR:Engine>/assimp-vc143-mt$<$<CONFIG:Debug>:d>.dll"
    "$<TARGET_FILE_DIR:Engine>/poly2tri.dll"
    "$<TARGET_FILE_DIR:Engine>/zlib$<$<CONFIG:Debug>:d>1.dll"
    "$<TARGET_FILE_DIR:Engine>/minizip.dll"
    "$<TARGET_FILE_DIR:Engine>/pugixml.dll"
    $<TARGET_FILE_DIR:test_scene_details>
    # vcpkg's Debug DLLs carry a 'd' suffix (assimp-vc143-mtd.dll, zlibd1.dll);
    # the generator expressions keep this copy working in both configs.
)

# --- Scene serialization round-trip (CPU only, no GL) ---
add_executable(test_scene_serializer test_scene_serializer.cpp)
target_link_libraries(test_scene_serializer PRIVATE Engine GTest::gtest_main)
add_test(NAME test_scene_serializer COMMAND $<TARGET_FILE:test_scene_serializer>)
add_custom_command(TARGET test_scene_serializer POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:Engine> $<TARGET_FILE_DIR:test_scene_serializer>)

# --- Fixed-timestep accumulator (CPU only, no GL) ---
add_executable(test_fixed_timestep test_fixed_timestep.cpp)
target_link_libraries(test_fixed_timestep PRIVATE Engine GTest::gtest_main)
add_test(NAME test_fixed_timestep COMMAND $<TARGET_FILE:test_fixed_timestep>)
add_custom_command(TARGET test_fixed_timestep POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:Engine> $<TARGET_FILE_DIR:test_fixed_timestep>)

# --- CPU-only split tests (no GL) ---
add_executable(test_splits test_splits.cpp)
target_link_libraries(test_splits PRIVATE Engine GTest::gtest_main)

add_test(NAME test_splits COMMAND $<TARGET_FILE:test_splits>)

#Ensures the Engine library is copied to the test output directory
add_custom_command(TARGET test_event_bus POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:Engine> $<TARGET_FILE_DIR:test_event_bus>)

add_custom_command(
  TARGET test_render_passes POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/Editor/src/Exported
    $<TARGET_FILE_DIR:test_render_passes>/Exported)