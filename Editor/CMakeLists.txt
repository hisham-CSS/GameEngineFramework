# Add executable target for the editor
add_executable(Editor
    "src/EditorApplication.cpp"
    "src/EditorApplication.h" 
    "src/Shader.h"
    "src/Camera.h" 
    "src/Model.h" 
    "src/Mesh.h")


find_package(Stb REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)

target_include_directories(Editor PRIVATE ${Stb_INCLUDE_DIR})

# Link the Engine library to the Editor executable
target_link_libraries(Editor PRIVATE Engine)
target_link_libraries(Editor PRIVATE glm::glm)
target_link_libraries(Editor PRIVATE assimp::assimp)

# Set different output directories for Debug and Release
set_target_properties(Editor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/build/bin/Debug
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/build/bin/Release
)

# Copy shaders over into the build executable folder as that is what is needed to load the files
add_custom_command(
  TARGET Editor POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Exported
    $<TARGET_FILE_DIR:Editor>/Exported)

# Optionally add different compile options based on the build configuration
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(Engine PRIVATE -g)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(Engine PRIVATE /Zi)
endif()