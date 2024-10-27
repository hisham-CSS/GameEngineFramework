# GameEngineFramework

## Project Overview

This project is structured as a modular game engine framework, with **CMake** used for builds and **vcpkg** for external package management. The framework includes an `Engine` project, which is compiled as a DLL with wrappers around external libraries. The `Editor` project uses the `Engine` API and its wrappers to implement features and leverage game engine libraries efficiently.

## Project Structure

The project is organized as follows:
```cmd
│   .gitignore
│   CMakeLists.txt
│   LICENSE.txt
│   README.md
│   vcpkg-configuration.json
│   vcpkg.json
├───Editor
│   │   CMakeLists.txt
│   └───src
│           EditorApplication.cpp
├───Engine
│   │   CMakeLists.txt
│   ├───include
│   │       Engine.h
│   └───src
│       ├───core
│       │       Application.cpp
│       │       Application.h
│       │       Core.h
│       │       Main.h
│       └───renderer
│               window.cpp
│               window.h
```

## Dependencies
- [GLFW](https://www.glfw.org/) - A multi-platform library for OpenGL, OpenGL ES, and Vulkan development
- More dependencies will be added as the project grows

## Building the Project

### Prerequisites
1. [CMake](https://cmake.org/) (version 3.20 or higher)
2. [vcpkg](https://vcpkg.io/)
3. A C++ compiler supporting C++17 or later

### Build Steps
TBD