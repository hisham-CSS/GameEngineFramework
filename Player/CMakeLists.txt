# Standalone player: Engine only, no ImGui/editor dependencies.
add_executable(Player
    src/PlayerMain.cpp
)

target_link_libraries(Player PRIVATE Engine)
target_compile_definitions(Player PRIVATE GLFW_INCLUDE_NONE)

# Same output dir as the Editor so they share Exported/ (a scene saved in the
# editor is immediately runnable in the player).
set_target_properties(Player PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/build/bin/Debug
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/build/bin/Release
)

# Shaders + models + default scene. Exported/ currently lives under Editor/;
# moving it to a shared assets/ dir is roadmap work (asset pipeline).
add_custom_command(TARGET Player POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/Editor/src/Exported
    $<TARGET_FILE_DIR:Player>/Exported)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(Player PRIVATE -g)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(Player PRIVATE /Zi)
endif()
