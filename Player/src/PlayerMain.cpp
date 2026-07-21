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
    MyCoreEngine::ScriptWorld  scripts_; // same lifetime requirement
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
        // UpdateTransforms FIRST: a freshly loaded scene has dirty Transforms
        // whose cached world matrices are still identity, and physics bodies
        // are built from world poses. Building first put the ground (authored
        // at y=-3, scaled 300x) at the origin as a 1x1 box, so everything
        // fell straight past it. PhysicsWorld::Build is now robust to this on
        // its own, but the ordering is still the honest way to express it.
        scene.UpdateTransforms();
        InstallPhysics(*this, scene, physics_);
        physics_.Build(scene.registry);

        // Scripting, installed AFTER physics for the same tick-ordering
        // reason as the editor. The shipped game starts scripts immediately —
        // there is no Play button here, the saved scene IS the game.
        {
            ScriptSettings ss;
            ss.scriptDirectory = "Exported/Scripts";
            InstallScripting(*this, scene, scripts_, &physics_, &input(), {}, ss);
        }
        scripts_.Build(scene.registry);
        scripts_.Start(scene.registry);
        // Takes the PRIMARY variable-update slot. Defensible only because in
        // the shipped player the scene's scripts ARE the game -- there is no
        // other gameplay hook to displace. If the player ever grows one, this
        // needs an AddUpdate subscriber list like AddFixedUpdate has, or it
        // will silently delete that hook.
        SetUpdate([this, &scene](float dt) { scripts_.Update(scene.registry, dt); });

        // Render through the scene's camera entity, exactly like the editor's
        // Game view: same CameraDirector selection, same blending.
        setRenderFromSceneCamera(true);

        // A shipped game must not hand the player a debug fly-camera. When
        // the scene actually has a camera, the director owns the view and
        // the engine's built-in WASD/mouse-look is turned OFF so gameplay is
        // the only thing that can move it.
        //
        // When the scene has NO camera the director can't drive anything and
        // the engine silently falls back to the fly cam — which looks exactly
        // like "the game ignored my camera". That was a real, confusing
        // failure, so it is now reported instead of guessed at, and free-fly
        // stays enabled purely as a diagnostic so the level is still
        // inspectable.
        scene.UpdateTransforms(); // world matrices before the camera search
        const entt::entity active = FindActiveCamera(scene.registry);
        if (active != entt::null) {
            setInternalCameraInput(false);
            std::cout << "PLAYER: rendering from scene camera." << std::endl;
        }
        else {
            fatal("scene '" + scenePath + "' contains no enabled CameraComponent, "
                  "so there is nothing to render from.\n\n"
                  "Add a Camera component to an entity in the editor and SAVE "
                  "the scene (File > Save Scene). Falling back to a free-fly "
                  "debug camera for now.");
        }

        RunLoop(scene, shader); // ESC or window close exits
    }
};

MyCoreEngine::Application* MyCoreEngine::CreateApplication() {
    return new PlayerApplication();
}

#define MYCE_DEFINE_ENTRY
#include "../src/core/Main.h"
