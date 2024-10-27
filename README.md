# GameEngineFramework

## Project Overview

This project is structured as a modular game engine framework, with **CMake** used for builds and **vcpkg** for external package management. The framework includes an `Engine` project, which is compiled as a DLL with wrappers around external libraries. The `Editor` project uses the `Engine` API and its wrappers to implement features and leverage game engine libraries efficiently.

## Project Structure

The project is organized as follows:

``plaintext
/GameEngineFramework
    /Editor                     # Editor code utilizing the engine API
        /src                    # Source files for editor implementation
        /include                # Header files specific to editor functionality
        CMakeLists.txt          # Editor CMake configuration file
    /Engine                     # Core engine code
        /src                    # Source files for engine implementation
        /include                # Header files for engine API and wrapper interfaces
        CMakeLists.txt          # Engine CMake configuration file
    README.md                   # Project documentation (this file)
    vcpkg-configuration.json    # VCPKG configuration file
    vcpkg.json                  # VCPKG manifest file - this lists all the dependencies of the project
    CMakeLists.txt              # Root CMake configuration file``