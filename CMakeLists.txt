cmake_minimum_required(VERSION 3.20)

# Set the project name
project(GameEngineFramework)

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED true)

# Add subdirectories for Engine and Editor
add_subdirectory(Engine)
add_subdirectory(Editor)

option(ENABLE_TESTS "Build unit tests" ON)

if(ENABLE_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
