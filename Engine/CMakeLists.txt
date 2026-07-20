# Add library target for the engine (this will compile into a DLL)
add_library(Engine SHARED
    include/Engine.h
    src/core/Application.cpp
    src/core/Application.h
    src/core/Main.h
    src/core/Core.h 
    src/core/Camera.h
    src/core/Model.h
    src/core/Shader.h 
    src/core/Entity.h
    src/core/Renderer.h 
    src/core/Renderer.cpp
    src/core/Window.h  
    src/core/Components.h 
    src/core/Scene.h 
    src/core/Event.h 
    src/core/EventBus.h  
    src/thirdparty/stb_impl.cpp 
    src/core/ImageIO.h 
    src/core/ImageIO.cpp 
    src/core/Model.cpp 
    src/core/Shader.cpp 
    src/core/Camera.cpp
    src/core/CameraDirector.h
    src/core/CameraDirector.cpp
    src/core/AssetManager.h
    src/core/AssetManager.cpp
    src/core/Scene.cpp
    src/core/Material.h
    src/core/SceneSerializer.h
    src/core/SceneSerializer.cpp
    src/core/ProjectSettings.h
    src/core/ProjectSettings.cpp
    src/core/DemoGameplay.h
    src/core/FixedTimestep.h
    src/core/InputMap.h
    src/core/InputMap.cpp
    src/core/JobSystem.h
    src/core/JobSystem.cpp
    src/core/RenderTarget.h
    src/core/RenderTarget.cpp
    src/assets/AssetIndex.h
    src/assets/AssetIndex.cpp
    src/assets/AssetValidator.h
    src/assets/AssetValidator.cpp
    src/assets/ImportSettings.h
    src/assets/ImportSettings.cpp
    src/render/IRenderPass.h 
    src/render/passes/ForwardOpaquePass.h 
    src/render/passes/ShadowCSMPass.h 
    src/render/passes/TonemapPass.h 
    src/render/RenderPipeline.h 
    src/render/passes/ShadowCSMPass.cpp 
    src/render/passes/ForwardOpaquePass.cpp 
    src/render/passes/TonemapPass.cpp "src/core/GLInit.h" "src/core/GLInit.cpp" "src/render/CSMSplits.h"
    # --- physics: backend-agnostic core + pluggable backends ---
    src/physics/PhysicsTypes.h
    src/physics/PhysicsComponents.h
    src/physics/IPhysicsBackend.h
    src/physics/PhysicsBackendRegistry.h
    src/physics/PhysicsBackendRegistry.cpp
    src/physics/PhysicsWorld.h
    src/physics/PhysicsWorld.cpp
    src/physics/backends/SimplePhysicsBackend.h
    src/physics/backends/SimplePhysicsBackend.cpp)

target_compile_definitions(Engine PUBLIC ENGINE_DLL_EXPORTS)

# CMake is not a package manager so this command doesn't work as a fetch from remote.
# You have to have the dependancy added before you can find it
# If VCPKG is installed and used as a global store for your packages in C++ projects - then you can use this to install the dependancy
# into the project - here we are requiring glfw3.
find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(meshoptimizer CONFIG REQUIRED)

# --- physics backends (OPTIONAL by design) ------------------------------
# Each SDK is compiled in only when found, and every backend sits behind
# IPhysicsBackend. A build with neither still works: the dependency-free
# "Simple" backend is always registered. This is what keeps "support any
# physics library" honest -- adding one is a new subclass + a find_package
# block here, and nothing else in the engine changes.
option(CSE_ENABLE_JOLT  "Build the Jolt physics backend"  ON)
option(CSE_ENABLE_PHYSX "Build the PhysX physics backend" ON)

# CRITICAL: each SDK is linked into its OWN object library, never directly
# into Engine. These imported targets propagate INTERFACE compile definitions
# to every consumer source file, and Jolt's include `_HAS_EXCEPTIONS=0`.
# Linking Jolt straight into Engine rebuilt the ENTIRE engine without
# exception support, which turned the std::filesystem throw that AssetIndex
# relies on (non-codepage filenames) into a 0xC0000409 fast-fail and would
# have silently broken every other try/catch in the engine.
# Object-library isolation keeps those definitions on the one translation
# unit that actually includes the SDK -- which is the whole point of the
# pimpl in each backend header.
# STATIC (not OBJECT) is load-bearing: for a static library CMake records
# PRIVATE deps as $<LINK_ONLY:...> in the link interface, so Engine inherits
# the SDK's .lib for linking but NOT its INTERFACE compile definitions or
# include dirs. An OBJECT library would hand over object files without the
# link deps, and linking the SDK into Engine directly is what caused the
# _HAS_EXCEPTIONS=0 contamination above.
function(cse_add_physics_backend tgt srcfile sdk_target define)
    add_library(${tgt} STATIC ${srcfile})
    target_link_libraries(${tgt} PRIVATE ${sdk_target} glm::glm EnTT::EnTT)
    target_compile_definitions(${tgt} PRIVATE ENGINE_DLL_EXPORTS ${define})
    set_target_properties(${tgt} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_link_libraries(Engine PRIVATE ${tgt})
    # Engine itself only needs the DEFINE (so the registry compiles its entry);
    # the backend header is SDK-free by design, so no SDK types leak in here.
    target_compile_definitions(Engine PRIVATE ${define})
endfunction()

if (CSE_ENABLE_JOLT)
    find_package(Jolt CONFIG QUIET)
    if (Jolt_FOUND)
        cse_add_physics_backend(EnginePhysicsJolt
            src/physics/backends/JoltPhysicsBackend.cpp Jolt::Jolt CSE_WITH_JOLT)
        message(STATUS "Physics: Jolt backend ENABLED")
    else()
        message(STATUS "Physics: Jolt not found - backend disabled")
    endif()
endif()

if (CSE_ENABLE_PHYSX)
    find_package(unofficial-omniverse-physx-sdk CONFIG QUIET)
    if (unofficial-omniverse-physx-sdk_FOUND)
        cse_add_physics_backend(EnginePhysicsPhysX
            src/physics/backends/PhysXPhysicsBackend.cpp
            unofficial::omniverse-physx-sdk::sdk CSE_WITH_PHYSX)
        message(STATUS "Physics: PhysX backend ENABLED")
    else()
        message(STATUS "Physics: PhysX not found - backend disabled")
    endif()
endif()

# Publicly include the Engine's `include` folder for other projects (like Editor, Launcher)
target_include_directories(Engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(Engine PRIVATE ${Stb_INCLUDE_DIR})

target_link_libraries(Engine PUBLIC glfw)
target_link_libraries(Engine PUBLIC glad::glad)
target_link_libraries(Engine PUBLIC assimp::assimp)
target_link_libraries(Engine PRIVATE glm::glm)
target_link_libraries(Engine PUBLIC EnTT::EnTT)
target_link_libraries(Engine PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(Engine PRIVATE meshoptimizer::meshoptimizer)

# Set different output directories for Debug and Release
set_target_properties(Engine PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/build/lib/Debug
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/build/lib/Release
    LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/build/lib/Debug
    LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/build/lib/Release
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/build/bin/Debug
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/build/bin/Release
    # RelWithDebInfo = optimized + symbols (debug real scenes at Release
    # speed). Without an explicit dir this config falls back to each
    # target's own binary dir, which scatters Engine.dll away from the exes.
    ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/build/lib/RelWithDebInfo
    LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/build/lib/RelWithDebInfo
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/build/bin/RelWithDebInfo
)

# Optionally define different compile options based on the build configuration
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(Engine PRIVATE -g)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(Engine PRIVATE /Zi)
endif()


# Engine CMakeLists.txt
target_compile_definitions(Engine PRIVATE GLFW_INCLUDE_NONE)

# Ship the engine DLL at the package root, next to Player.exe. With
# X_VCPKG_APPLOCAL_DEPS_INSTALL, vcpkg also deploys Engine.dll's own DLL
# dependency closure (assimp, glfw3, zlib, ...) into the same directory.
install(TARGETS Engine RUNTIME DESTINATION .)