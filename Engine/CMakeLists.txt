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
    src/core/PathSandbox.h
    src/core/PathSandbox.cpp
    src/core/Scene.cpp
    src/core/Material.h
    src/core/SceneSerializer.h
    src/core/SceneSerializer.cpp
    src/core/ProjectSettings.h
    src/core/ProjectSettings.cpp
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
    # --- environment lighting: bake + skybox ---
    src/render/IBLBaker.h
    src/render/IBLBaker.cpp
    src/render/passes/SkyboxPass.h
    src/render/passes/SkyboxPass.cpp
    src/render/passes/TransparentPass.h
    src/render/passes/TransparentPass.cpp
    src/render/passes/ForwardShading.h
    src/render/passes/FXAAPass.h
    src/render/passes/FXAAPass.cpp
    src/render/passes/VignettePass.h
    src/render/passes/VignettePass.cpp
    src/render/passes/OutlinePass.h
    src/render/passes/OutlinePass.cpp
    src/render/passes/ColorGradePass.h
    src/render/passes/ColorGradePass.cpp
    src/render/passes/BloomPass.h
    src/render/passes/BloomPass.cpp
    src/audio/AudioTypes.h
    src/audio/IAudioBackend.h
    src/audio/AudioBackendRegistry.h
    src/audio/AudioBackendRegistry.cpp
    src/audio/backends/NullAudioBackend.h
    src/audio/backends/MiniaudioBackend.h
    src/audio/backends/MiniaudioBackend.cpp
    src/audio/miniaudio_impl.cpp
    # --- physics: backend-agnostic core + pluggable backends ---
    src/physics/PhysicsTypes.h
    src/physics/PhysicsComponents.h
    src/physics/IPhysicsBackend.h
    src/physics/PhysicsBackendRegistry.h
    src/physics/PhysicsBackendRegistry.cpp
    src/physics/PhysicsWorld.h
    src/physics/PhysicsWorld.cpp
    src/physics/PhysicsInstall.h
    src/physics/backends/SimplePhysicsBackend.h
    src/physics/backends/SimplePhysicsBackend.cpp
    # --- scripting: backend-agnostic core + pluggable language backends ---
    src/script/ScriptTypes.h
    src/script/ScriptComponent.h
    src/script/IScriptHost.h
    src/script/IScriptBackend.h
    src/script/ScriptBackendRegistry.h
    src/script/ScriptBackendRegistry.cpp
    src/script/ScriptWorld.h
    src/script/ScriptWorld.cpp
    src/script/ScriptInstall.h
    src/script/backends/NullScriptBackend.h
    src/script/backends/NullScriptBackend.cpp)

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
# Used by BOTH the physics and the scripting seams -- the isolation problem is
# identical (Jolt exports _HAS_EXCEPTIONS=0, vcpkg's Lua exports
# LUA_BUILD_AS_DLL), so the fix is written once. Trailing arguments are the
# SDK targets to link, since a backend may need more than one (the Lua backend
# needs sol2 AND lua-cpp).
function(cse_add_isolated_backend tgt srcfile define)
    add_library(${tgt} STATIC ${srcfile})
    target_link_libraries(${tgt} PRIVATE ${ARGN} glm::glm EnTT::EnTT)
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
        cse_add_isolated_backend(EnginePhysicsJolt
            src/physics/backends/JoltPhysicsBackend.cpp CSE_WITH_JOLT Jolt::Jolt)
        message(STATUS "Physics: Jolt backend ENABLED")
    else()
        message(STATUS "Physics: Jolt not found - backend disabled")
    endif()
endif()

if (CSE_ENABLE_PHYSX)
    find_package(unofficial-omniverse-physx-sdk CONFIG QUIET)
    if (unofficial-omniverse-physx-sdk_FOUND)
        cse_add_isolated_backend(EnginePhysicsPhysX
            src/physics/backends/PhysXPhysicsBackend.cpp CSE_WITH_PHYSX
            unofficial::omniverse-physx-sdk::sdk)
        message(STATUS "Physics: PhysX backend ENABLED")
    else()
        message(STATUS "Physics: PhysX not found - backend disabled")
    endif()
endif()

# --- scripting backends (OPTIONAL, same shape as physics) ---------------
# A build with no language runtime still works: the dependency-free "Null"
# backend is always registered, so scenes with ScriptComponents load and play
# with those entities simply doing nothing.
option(CSE_ENABLE_LUA "Build the Lua scripting backend" ON)

if (CSE_ENABLE_LUA)
    find_package(sol2 CONFIG QUIET)
    # The [cpp] feature of vcpkg's lua builds it with C++ linkage, so a Lua
    # error unwinds as a C++ exception instead of longjmp'ing straight past
    # our destructors. sol2 relies on that for safe error propagation.
    find_package(unofficial-lua-cpp CONFIG QUIET)
    if (sol2_FOUND AND TARGET lua-cpp)
        cse_add_isolated_backend(EngineScriptLua
            src/script/backends/LuaScriptBackend.cpp CSE_WITH_LUA sol2 lua-cpp)
        # BOTH of these are required by the [cpp] Lua build, and the failure
        # mode of omitting SOL_USING_CXX_LUA is a wall of LNK2019s that looks
        # like a missing library rather than a linkage mismatch:
        #   SOL_USING_CXX_LUA  - lua-c++.lib exports C++-MANGLED names
        #     (?lua_close@@YAXPEAUlua_State@@@Z). sol2 otherwise wraps the Lua
        #     headers in extern "C" and asks the linker for `lua_close`.
        #   SOL_EXCEPTIONS_SAFE_PROPAGATION - C++ Lua raises errors as real
        #     exceptions; without this sol2 assumes longjmp and installs an
        #     error path that would skip destructors on the way out.
        target_compile_definitions(EngineScriptLua PRIVATE
            SOL_USING_CXX_LUA=1
            SOL_EXCEPTIONS_SAFE_PROPAGATION=1)

        # vcpkg's applocal deployment does NOT pick this one up -- the '+'
        # characters in "lua-c++.dll" defeat its filename matching, while
        # every other dependency (assimp, glfw, PhysX) is deployed normally.
        # Left alone, Engine.dll links cleanly and then fails to LOAD at
        # runtime with a missing-DLL error that points nowhere near Lua.
        add_custom_command(TARGET Engine POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:lua-cpp>" "$<TARGET_FILE_DIR:Engine>"
            COMMENT "Deploying lua-c++.dll (missed by vcpkg applocal)"
            VERBATIM)
        install(FILES "$<TARGET_FILE:lua-cpp>" DESTINATION .)
        # A plain flag for sibling directories (tests/, Editor/, Player/).
        # It cannot carry $<TARGET_FILE:lua-cpp>: find_package's imported
        # targets are DIRECTORY-SCOPED, and that genex is expanded in the
        # CONSUMING scope, where the target does not exist ("No target
        # lua-cpp" at generate time). Consumers copy the DLL out of Engine's
        # own output directory instead, which the POST_BUILD above populates.
        set(CSE_WITH_LUA_RUNTIME ON
            CACHE INTERNAL "Lua runtime DLL is staged beside Engine")
        message(STATUS "Scripting: Lua backend ENABLED")
    else()
        message(STATUS "Scripting: sol2/lua-cpp not found - backend disabled")
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