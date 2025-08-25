# tests/CMakeLists.txt
include(FetchContent)

# GoogleTest
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
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

# InputSystem Integration test
add_executable(test_render_passes test_render_passes.cpp)
target_compile_definitions(test_render_passes PRIVATE UNIT_TEST=1)
target_link_libraries(test_render_passes PRIVATE
  Engine
  GTest::gtest_main
)
 add_test(NAME test_render_passes COMMAND $<TARGET_FILE:test_render_passes>)

#Ensures the Engine library is copied to the test output directory
add_custom_command(TARGET test_event_bus POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:Engine> $<TARGET_FILE_DIR:test_event_bus>)

add_custom_command(
  TARGET test_render_passes POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/Editor/src/Exported
    $<TARGET_FILE_DIR:test_render_passes>/Exported)