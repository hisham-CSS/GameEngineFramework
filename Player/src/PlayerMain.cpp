// Player/src/PlayerMain.cpp
// Standalone player: boots the engine, loads the startup scene, and runs it
// without any editor UI. Built twice: PlayerDebug.exe (console subsystem,
// keeps the terminal for logs) and Player.exe (shipping, no console).
// Usage: Player.exe [path/to/scene.json]   (overrides the project settings)
#include "Engine.h"

#include <iostream>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// Run on the discrete GPU on hybrid-GPU laptops (see EditorMain.cpp).
extern "C" {
    __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

namespace {
    // The shipping player has no console, so a startup failure would be an
    // instant silent exit — surface it in a message box instead.
    void fatal(const std::string& msg) {
        std::cerr << "PLAYER: " << msg << std::endl;
#if defined(_WIN32) && defined(MYCE_SHIPPING)
        MessageBoxA(nullptr, msg.c_str(), "Cat Splat Player", MB_OK | MB_ICONERROR);
#endif
    }
}

class PlayerApplication : public MyCoreEngine::Application {
    // Outlives RunLoop: the fixed-tick subscriber captures it by reference.
    MyCoreEngine::PhysicsWorld physics_;
public:
    PlayerApplication() : Application(1280, 720, "Cat Splat Player") {}

    void Run() override {
        using namespace MyCoreEngine;

        InitGL();

        AssetManager assets;
        Shader shader("Exported/Shaders/vertex.glsl", "Exported/Shaders/frag.glsl");
        if (!shader.isValid()) {
            fatal("shader failed to build — cannot render.");
            return;
        }

        // Startup scene: command line beats project settings beats default.
        std::string scenePath;
#ifdef _WIN32
        if (__argc > 1 && __argv && __argv[1]) scenePath = __argv[1];
#endif
        if (scenePath.empty()) {
            ProjectSettings settings;
            settings.Load(); // Exported/project.json, written by the editor
            scenePath = settings.startupScene;
        }

        Scene scene;
        SceneSerializer serializer(scene, assets);
        if (!serializer.Load(scenePath)) {
            fatal("failed to load scene '" + scenePath +
                  "' — save one from the editor (and set it as the startup "
                  "scene under Settings > Scene > Build Settings), or pass a "
                  "path: Player.exe <scene.json>");
            return;
        }

        // The player is always "playing": ticks run from frame one
        // (Application::gameplayEnabled_ defaults on; only the editor gates
        // it). There is no Play button here, so physics bodies are built
        // right after the scene loads rather than on a play transition.
        InstallPhysics(*this, scene, physics_);
        physics_.Build(scene.registry);

        // Render through the scene's primary camera entity (Unity-style):
        // the editor's Game view shows the same thing. Scenes without a
        // camera fall back to the free-fly camera automatically.
        setRenderFromSceneCamera(true);

        RunLoop(scene, shader); // ESC or window close exits
    }
};

MyCoreEngine::Application* MyCoreEngine::CreateApplication() {
    return new PlayerApplication();
}

#define MYCE_DEFINE_ENTRY
#include "../src/core/Main.h"
