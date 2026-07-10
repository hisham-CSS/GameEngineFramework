// Player/src/PlayerMain.cpp
// Standalone player: boots the engine, loads a serialized scene, and runs it
// without any editor UI. Usage: Player.exe [path/to/scene.json]
#include "Engine.h"

#include <iostream>
#include <string>

#ifdef _WIN32
// Run on the discrete GPU on hybrid-GPU laptops (see EditorMain.cpp).
extern "C" {
    __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

class PlayerApplication : public MyCoreEngine::Application {
public:
    PlayerApplication() : renderer_(1280, 720, "Cat Splat Player") {}

    void Run() override {
        using namespace MyCoreEngine;

        renderer_.InitGL();

        AssetManager assets;
        Shader shader("Exported/Shaders/vertex.glsl", "Exported/Shaders/frag.glsl");
        if (!shader.isValid()) {
            std::cerr << "PLAYER: shader failed to build — cannot render." << std::endl;
            return;
        }

        std::string scenePath = "Exported/scene.json";
#ifdef _WIN32
        if (__argc > 1 && __argv && __argv[1]) scenePath = __argv[1];
#endif

        Scene scene;
        SceneSerializer serializer(scene, assets);
        if (!serializer.Load(scenePath)) {
            std::cerr << "PLAYER: failed to load scene '" << scenePath
                      << "' — save one from the editor first, or pass a path: Player.exe <scene.json>"
                      << std::endl;
            return;
        }

        renderer_.run(scene, shader); // ESC or window close exits
    }

private:
    MyCoreEngine::Renderer renderer_;
};

MyCoreEngine::Application* MyCoreEngine::CreateApplication() {
    return new PlayerApplication();
}

#define MYCE_DEFINE_ENTRY
#include "../src/core/Main.h"
