# Add executable target for the editor
add_executable(Editor
    src/EditorApplication.cpp
    src/EditorApplication.h
    src/EditorImGuiLayer.cpp
    src/EditorImGuiLayer.h
    src/EditorTitleBar.cpp
    src/EditorTitleBar.h
    src/Subprocess.cpp
    src/Subprocess.h
    src/ImGuiInputMap.h
    src/UndoHistory.cpp
    src/UndoHistory.h
    src/panels/AssetBrowserPanel.h
    src/panels/AssetBrowserPanel.cpp
    src/panels/SceneHierarchyPanel.h "src/panels/SceneHierarchyPanel.cpp" "src/panels/InspectorPanel.h" "src/panels/InspectorPanel.cpp" "src/EditorMain.cpp")


# App icon (Explorer/taskbar/alt-tab + GLFW window class — see resources/app.rc).
# Windows resource script; other platforms get the icon at runtime via
# TrySetWindowIconFromFile (Exported/Icon/icon.png).
if(WIN32)
    target_sources(Editor PRIVATE ${CMAKE_SOURCE_DIR}/resources/app.rc)
endif()

find_package(imgui CONFIG REQUIRED)
find_package(imguizmo CONFIG REQUIRED)

# Link the Engine library to the Editor executable
target_link_libraries(Editor PRIVATE Engine)
target_link_libraries(Editor PRIVATE imgui::imgui)
target_link_libraries(Editor PRIVATE imguizmo::imguizmo)


# Set different output directories for Debug and Release
set_target_properties(Editor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/build/bin/Debug
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/build/bin/Release
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/build/bin/RelWithDebInfo
)

# Stage runtime assets (shaders/models + seed scene) next to the exes.
# ONE staging target shared by Editor and both Players: concurrent copies
# into the same directory race under Ninja (intermittent sharing violations
# — same lesson as tests' test_runtime_deps), and a blind copy_directory
# clobbered editor-saved scenes with the checked-in copy every build. The
# script overwrites static assets but only seeds authored .json files.
add_custom_target(runtime_assets
  COMMAND ${CMAKE_COMMAND}
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/src/Exported
    -DDST=$<TARGET_FILE_DIR:Editor>/Exported
    -P ${CMAKE_SOURCE_DIR}/cmake/stage_runtime_assets.cmake
  COMMENT "Staging runtime assets")
add_dependencies(Editor runtime_assets)

# Optionally add different compile options based on the build configuration
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(Engine PRIVATE -g)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(Engine PRIVATE /Zi)
endif()

#target_compile_definitions(Editor PRIVATE MYCE_ENABLE_ENTRY=1)
target_compile_definitions(Editor PRIVATE GLFW_INCLUDE_NONE)