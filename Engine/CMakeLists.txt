# Add library target for the engine (this will compile into a DLL)
add_library(Engine SHARED
    include/Engine.h
    src/core/Application.cpp
    src/core/Application.h
    src/core/Main.h
    src/core/Core.h
    src/renderer/window.h 
    src/renderer/window.cpp)

target_compile_definitions(Engine PRIVATE ENGINE_DLL_EXPORTS)

# CMake is not a package manager so this command doesn't work as a fetch from remote.
# You have to have the dependancy added before you can find it
# If VCPKG is installed and used as a global store for your packages in C++ projects - then you can use this to install the dependancy
# into the project - here we are requiring glfw3.
find_package(glfw3 CONFIG REQUIRED)

# Publicly include the Engine's `include` folder for other projects (like Editor, Launcher)
target_include_directories(Engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(Engine PUBLIC glfw)

# Set different output directories for Debug and Release
set_target_properties(Engine PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/build/lib/Debug
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/build/lib/Release
    LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/build/lib/Debug
    LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/build/lib/Release
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/build/bin/Debug
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/build/bin/Release
)

# Optionally define different compile options based on the build configuration
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(Engine PRIVATE -g)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(Engine PRIVATE /Zi)
endif()
