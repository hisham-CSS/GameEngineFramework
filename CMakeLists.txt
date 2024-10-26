cmake_minimum_required(VERSION 3.20)

# Set the project name
project(GameEngineFramework)

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED true)

# This will make sure that when the visual studio project is generated - the ZERO_CHECK project is not included in the sln
# This may behave differently on different operating systems and compilers
set(CMAKE_SUPPRESS_REGENERATION true)

# Add subdirectories for Engine and Editor
add_subdirectory(Engine)
add_subdirectory(Editor)