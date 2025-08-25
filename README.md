# GameEngineFramework

## Project Overview

This project is a modular, high-performance game engine framework built with modern C++17. It features a clean architecture with a separate Engine (DLL) and Editor, a sophisticated rendering pipeline with advanced features, and a professional development environment using CMake and vcpkg.

## Key Features

- **Modern C++17 Architecture**: Clean, modular design with a separate Engine and Editor.
- **Advanced Rendering Pipeline**: Sophisticated rendering system with modern graphics features.
- **Entity-Component-System (ECS)**: High-performance scene management with EnTT.
- **Functional Editor**: ImGui-based editor with core panels for scene management and inspection.
- **Professional Build System**: CMake and vcpkg for easy building and dependency management.
- **Comprehensive Testing**: Unit tests for core systems to ensure stability.

## Project Structure

```
GameEngineFramework/
├── Editor/         # Editor application (ImGui)
├── Engine/         # Core engine (DLL)
├── tests/          # Unit tests
├── .gitignore
├── CMakeLists.txt
├── LICENSE.txt
├── README.md
├── vcpkg.json
└── vcpkg-configuration.json
```

## Core Engine Systems

- **Application Framework**: Main application loop and window management.
- **Asset Management**: System for loading and managing assets.
- **Camera System**: 3D camera with projection and view controls.
- **ECS (EnTT)**: High-performance entity-component-system.
- **Event System**: Event-driven architecture with a central event bus.
- **Input System**: GLFW-based input handling.
- **Material System**: Material management for rendering.
- **Model & Mesh**: 3D model and mesh loading/handling.
- **Scene Management**: Scene graph and object hierarchy.
- **Shader System**: Shader loading and management.

## Advanced Rendering System

- **Modular Render Pass Architecture**: Clean, extensible design with an `IRenderPass` interface.
- **Forward Rendering Pipeline**: Modern forward rendering implementation.
- **Cascading Shadow Maps (CSM)**: Advanced shadow mapping for large, dynamic scenes.
- **HDR Rendering & Tone Mapping**: High-dynamic-range rendering with a tone mapping pass.
- **Modern OpenGL**: Proper use of modern OpenGL features (framebuffers, vertex arrays, etc.).

## Editor

- **ImGui Integration**: Modern, immediate-mode GUI.
- **Scene Hierarchy Panel**: View and manage scene objects.
- **Inspector Panel**: View and edit component properties.
- **Docking Support**: Flexible window layout.

## Dependencies

- **GLFW**: Window and input management.
- **GLAD**: OpenGL loading.
- **GLM**: Mathematics library.
- **STB**: Image loading.
- **ASSIMP**: 3D model loading.
- **EnTT**: Entity-component-system.
- **ImGui**: Editor GUI.

## Building the Project

### Prerequisites

1.  **CMake** (version 3.20 or higher)
2.  **vcpkg**
3.  **C++17 Compiler**

### Build Steps

1.  **Clone the repository:**
    ```bash
    git clone https://github.com/hisham-CSS/GameEngineFramework
    cd GameEngineFramework
    ```

2.  **Configure with CMake:**
    ```bash
    cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=[path-to-vcpkg]/scripts/buildsystems/vcpkg.cmake
    ```

3.  **Build the project:**
    ```bash
    cmake --build build
    ```

## Development Status

- ✅ **Core Engine Architecture**: Complete
- ✅ **Rendering Pipeline**: Complete
- ✅ **Editor Framework**: Complete
- ✅ **Build System**: Complete
- ✅ **Testing Infrastructure**: In Progress
- 🔲 **Physics Integration**: Planned
- 🔲 **Animation System**: Planned
- 🔲 **Audio System**: Planned
- 🔲 **Scripting System**: Planned

## Contributing

Contributions are welcome! Please open an issue or pull request to discuss any changes.

## License

This project is licensed under the MIT License. See the [LICENSE.txt](LICENSE.txt) file for details.