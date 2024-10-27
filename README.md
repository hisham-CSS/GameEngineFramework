# GameEngineFramework

## Project Overview

This project is structured as a modular game engine framework, with **CMake** used for builds and **vcpkg** for external package management. The framework includes an `Engine` project, which is compiled as a DLL with wrappers around external libraries. The `Editor` project uses the `Engine` API and its wrappers to implement features and leverage game engine libraries efficiently.

## Project Structure

The project is organized as follows:
```cmd
GameEngineFramework
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
1. Clone the repository
```
git clone https://github.com/hisham-CSS/GameEngineFramework
cd GameEngineFramework
```
2. Configure the project with CMake (make sure that your path-to-vcpkg is configured locally)
 ```
 cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=[path-to-vcpkg]/scripts/buildsystems/vcpkg.cmake
 ```
3. Build the project
```
cmake --build .
```
## Project Components

### Engine

The core engine library is built as a DLL and provides:

-   Wrapped GLFW functionality for window management and input handling [WIP]
-   More features to be added...

### Editor

The editor application demonstrates the engine's capabilities and provides:

-   A window-based application using the Engine's GLFW wrapper
-   More features to be added...

## Development Status

- [X]  Basic project structure
- [X] CMake build system integration
- [X] vcpkg package management
- [X] GLFW integration
- [ ]  Basic rendering system
- [ ]  Input system
- [ ]  Scene management
- [ ]  Asset management

## Contributing

Instructions for contributing will be added as the project develops.

## License

MIT License - you are free to use and modify this template.