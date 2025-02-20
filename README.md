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
│       └───core
│               Application.cpp
│               Application.h
│               Camera.h
│               Components.h
│               Core.h
│               Entity.h
│               Main.h
│               Mesh.h
│               Model.h
│               Renderer.h
│               Scene.h
│               Shader.h
│               Window.h
```

## Dependencies
- [GLFW](https://www.glfw.org/) - A multi-platform library for OpenGL, OpenGL ES, and Vulkan development
- [GLAD](https://glad.dav1d.de/) - A library that generates OpenGL loading code
- [GLM](https://glm.g-truc.net/0.9.9/index.html) - A header-only C++ mathematics library for graphics software based on the OpenGL Shading Language (GLSL) specifications
- [STB](https://github.com/nothings/stb) - A single-file public domain libraries for C/C++ - For image loading
- [ASSIMP](https://assimp.org/) - A library to import and export various 3D model formats
- [EnTT](https://github.com/skypjack/entt) - A header-only, fast and flexible C++ entity-component system
- [ImGui](https://github.com/ocornut/imgui/tree/docking) - A bloat-free graphical user interface library for C++ - For the editor - using the docking experimental feature
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
-	Basic rendering with frustum culling. [DONE]
-	Batch rendering [WIP]
-	Scene management and Entity-Component-System architecture using EnTT [WIP]
-   More features to be added...

### Editor

The editor application demonstrates the engine's capabilities and provides:

-   A window-based application using the Engine's GLFW wrapper
-   More features to be added...

## Development Status

- :white_check_mark: Basic project structure
- :white_check_mark: CMake build system integration
- :white_check_mark: vcpkg package management
- :white_check_mark: GLFW integration
- :white_check_mark: Basic rendering system
- :white_check_mark: 3D Projection and Camera
- :white_check_mark: EnTT implementation
- :white_check_mark: Scene management
- :black_square_button: Asset management
- :black_square_button: Input system
- :black_square_button: Nvida PhysX wrapper
- :black_square_button: ImGui integration
- :black_square_button: Editor UI
- :black_square_button: Editor Scene management
- :black_square_button: Editor Debugging tools
- :black_square_button: Editor Profiling tools
- :black_square_button: Editor Build system

## Contributing

Instructions for contributing will be added as the project develops.

## License

MIT License - you are free to use and modify this template.