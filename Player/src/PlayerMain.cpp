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

    // ---- keyboard for the in-game UI --------------------------------------
    // Accumulated by GLFW callbacks during glfwPollEvents and drained once per
    // frame by the UI draw callback.
    //
    // CALLBACKS, not polling, and for two reasons that both matter: glfwGetKey
    // reports a key being HELD, which cannot distinguish a fresh press from the
    // previous frame's and never reports auto-repeat; and there is no portable
    // way to turn a key code into a character — layouts, dead keys and IMEs all
    // live in glfwSetCharCallback.
    MyCoreEngine::ui::UIKeyboardState g_uiKeys;

    MyCoreEngine::ui::UIKey mapKey(int key) {
        using K = MyCoreEngine::ui::UIKey;
        switch (key) {
        case GLFW_KEY_TAB:       return K::Tab;
        case GLFW_KEY_ENTER:
        case GLFW_KEY_KP_ENTER:  return K::Enter;
        case GLFW_KEY_ESCAPE:    return K::Escape;
        case GLFW_KEY_BACKSPACE: return K::Backspace;
        case GLFW_KEY_DELETE:    return K::Delete;
        case GLFW_KEY_LEFT:      return K::Left;
        case GLFW_KEY_RIGHT:     return K::Right;
        case GLFW_KEY_UP:        return K::Up;
        case GLFW_KEY_DOWN:      return K::Down;
        case GLFW_KEY_HOME:      return K::Home;
        case GLFW_KEY_END:       return K::End;
        case GLFW_KEY_PAGE_UP:   return K::PageUp;
        case GLFW_KEY_PAGE_DOWN: return K::PageDown;
        case GLFW_KEY_A:         return K::A;
        case GLFW_KEY_C:         return K::C;
        case GLFW_KEY_V:         return K::V;
        case GLFW_KEY_X:         return K::X;
        case GLFW_KEY_Z:         return K::Z;
        case GLFW_KEY_Y:         return K::Y;
        default:                 return K::None;
        }
    }

    void onKey(GLFWwindow*, int key, int, int action, int mods) {
        if (action == GLFW_RELEASE) return;  // press and auto-repeat only
        const MyCoreEngine::ui::UIKey k = mapKey(key);
        if (k == MyCoreEngine::ui::UIKey::None) return;
        MyCoreEngine::ui::UIKeyEvent e;
        e.key = k;
        e.shift = (mods & GLFW_MOD_SHIFT) != 0;
        e.ctrl = (mods & GLFW_MOD_CONTROL) != 0;
        e.alt = (mods & GLFW_MOD_ALT) != 0;
        g_uiKeys.keys.push_back(e);
    }

    void onChar(GLFWwindow*, unsigned int codepoint) {
        MyCoreEngine::Font::AppendUTF8(g_uiKeys.text, codepoint);
    }
}

class PlayerApplication : public MyCoreEngine::Application {
    // Outlives RunLoop: the fixed-tick subscriber captures it by reference.
    MyCoreEngine::PhysicsWorld physics_;
    MyCoreEngine::ScriptWorld  scripts_; // same lifetime requirement
    MyCoreEngine::AudioWorld   audio_;   // same lifetime requirement
    // Same requirement again: the UI draw callback captures this by reference
    // and is invoked from the render pass for the app's whole life. Drives
    // every UIDocumentComponent in the scene — the game's UI is scene content,
    // so nothing here mentions a specific HUD.
    MyCoreEngine::UIWorld      uiWorld_;
    MyCoreEngine::Font         uiFont_;
    // Declared AFTER uiWorld_ is not required — the world only holds a pointer
    // — but it must outlive the GL context, which the host owns.
    MyCoreEngine::ui::UITextureCache uiTextures_;
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
        // commandLine() is captured portably by Main.h (argv), so `Player
        // <scene.json>` works on Windows and Linux alike.
        // Project settings ship in project.json: the startup scene and the
        // master volume the game boots at. Always load them (the master volume
        // is honoured even when a scene is passed on the command line).
        ProjectSettings settings;
        settings.Load(); // Exported/project.json, written by the editor
        std::string scenePath;
        if (commandLine().size() > 1) scenePath = commandLine()[1];
        if (scenePath.empty()) scenePath = settings.startupScene;

        Scene scene;
        // The loader is installed BEFORE the boot load, so the boot goes
        // through exactly the path a menu button later will — one code path,
        // and the Install* helpers below can subscribe their teardown to it.
        SceneLoader sceneLoader(scene, assets);
        setSceneLoader(&sceneLoader);

        SceneSerializer serializer(scene, assets);
        if (!serializer.Load(scenePath)) {
            fatal("failed to load scene '" + scenePath +
                  "' — save one from the editor (File > Save Scene, then File > "
                  "Set Current Scene as Player Startup), or pass a "
                  "path: Player.exe <scene.json>");
            return;
        }

        // Apply the saved quality tier so the shipped game boots at the same
        // tier as the editor (the CSM part of a tier isn't serialized).
        if (scene.GetQualityLevel() != Scene::QualityLevel::Custom)
            renderer().ApplyQualityTier(scene.GetQualityLevel(), scene);

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

        // Audio: the shipped game plays immediately, same as scripts. Boots at
        // the saved master volume so it matches what the editor previewed.
        InstallAudio(*this, scene, audio_, {}, AudioSettings{ settings.masterVolume });
        audio_.Start(scene.registry);
        // The per-frame tick is an AddUpdate SUBSCRIBER installed by
        // InstallScripting, not the primary SetUpdate slot -- that slot stays
        // free for a game's own hook, and both hosts now run scripts at the
        // same point in the loop.

        // In-game UI, drawn after all post-processing. Same HUD the editor's
        // Game view shows, from one definition, so the preview and the shipped
        // build cannot drift.
        // The UI comes from the SCENE: entities carrying a UIDocumentComponent.
        // Nothing here names a file — swapping the HUD is a scene edit.
        if (!uiFont_.LoadFromFile("Exported/Fonts/Roboto.ttf", 18.0f)) {
            std::cerr << "PLAYER: UI font missing - drawing without text" << std::endl;
        }
        uiWorld_.SetFont(&uiFont_);
        // The image cache lives with the host, one per GL context: the editor
        // runs a SECOND renderer for its Game view, and a process-wide cache
        // would hand one context's texture names to the other.
        uiWorld_.SetTextureCache(&uiTextures_);
        // The real system clipboard, so Ctrl+C/V in a field talks to the rest
        // of the machine rather than a private buffer.
        if (GLFWwindow* w = GetNativeWindow()) {
            uiWorld_.SetClipboardHandlers(
                [w](const std::string& t) { glfwSetClipboardString(w, t.c_str()); },
                [w] { const char* t = glfwGetClipboardString(w); return std::string(t ? t : ""); });
        }
        // The two things a file cannot carry: a named action and a converter.
        InstallDemoUIContent(uiWorld_);
        // Nothing else in the player installs these (there is no ImGui here),
        // so no chaining is needed. Installed once, for the app's life.
        if (GLFWwindow* win = GetNativeWindow()) {
            glfwSetKeyCallback(win, &onKey);
            glfwSetCharCallback(win, &onChar);
        }

        // `scene` is a local that outlives RunLoop below, so capturing it by
        // reference is safe for the callback's whole life.
        renderer().SetUIDraw([this, &scene](MyCoreEngine::Renderer2D& r2d,
                                            int w, int h, float dt) {
            // The UI covers the whole window here, so window coords ARE UI
            // coords — no mapping needed (the editor is the case that needs it).
            // Read straight from GLFW: the engine's InputMap is action/axis
            // based and has no notion of a cursor position.
            GLFWwindow* win = GetNativeWindow();
            MyCoreEngine::ui::UIPointerState p;
            if (win) {
                double mx = 0.0, my = 0.0;
                glfwGetCursorPos(win, &mx, &my);
                p.position = { float(mx), float(my) };
                p.inside = (mx >= 0.0 && my >= 0.0 && mx < double(w) && my < double(h));
                p.buttonDown =
                    glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
                // Sampled here rather than in the scroll callback: GLFW does not
                // report modifiers with a scroll event at all, and reading the
                // key state in the same frame the notches are consumed is as
                // close as the API allows.
                p.shift = glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                          glfwGetKey(win, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
            }
            // Drained from the Application's GLFW scroll callback. It has to be
            // a callback rather than a poll — GLFW has no "wheel position" to
            // read — and the Engine owns that single callback slot, so a second
            // install here would silently kill the fly camera's zoom.
            p.wheel = ConsumeScrollDelta();
            uiWorld_.SetPointer(p);
            // Drained, not copied: a keystroke must be delivered exactly once,
            // and the callbacks keep filling this between frames.
            uiWorld_.SetKeyboard(g_uiKeys);
            g_uiKeys.clear();

            uiWorld_.Update(scene.registry, w, h, dt);
            uiWorld_.Draw(r2d);
        });

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
