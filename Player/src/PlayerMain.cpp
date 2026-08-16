// Player/src/PlayerMain.cpp
// Standalone player: boots the engine, loads the startup scene, and runs it
// without any editor UI. Built twice: PlayerDebug.exe (console subsystem,
// keeps the terminal for logs) and Player.exe (shipping, no console).
// Usage: Player.exe [path/to/scene.json]   (overrides the project settings)
//
// A GENERAL HOST. Nothing in this file names a game, a fighter or a move, and
// nothing may: this executable is what every title built on this engine ships
// as, and the day a second one exists it must not begin by deleting the first
// one's code out of here.
//
// The saved scene is still what the player boots into and is still the default
// thing it does. What changed is that it is no longer the ONLY thing it can do:
// a title registers GAME MODES (Engine/src/core/GameMode.h) and the main menu
// offers them. A mode is something the host enters, ticks, draws and leaves, and
// it exists because A FIGHT IS NOT A SCENE -- a FightSession has its own fixed
// tick, its own render path and no ECS entities at all, so there is no .json
// that could express it. Which modes this build has is decided entirely by what
// is linked; see the composition block in the root CMakeLists.txt.
#include "Engine.h"

#include <fstream>   // "does project.json exist" — see the boot order in Run()
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
    // A MEMBER, not a local in Run(): the UI draw callback captures it and is
    // invoked for the app's whole life, which outlasts Run()'s stack frame.
    MyCoreEngine::MenuUIHooks  menuHooks_;
    MyCoreEngine::UINavSynth   navSynth_;   // holds the auto-repeat clock
    // The modes this build ships, and which one is live. A MEMBER for the same
    // reason as everything above it: the fixed-tick subscriber, the UI draw
    // callback and the capture provider all capture it by reference and are
    // invoked for the app's whole life. It also OWNS the modes, so a mode's
    // destructor must not outlive the GL context -- hence a member of the app
    // rather than a local in Run().
    MyCoreEngine::GameModeRegistry modes_;
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

        // The asset root: where Exported/ was staged next to this executable.
        // NAMED ONCE because two things have to agree about it and they are set
        // forty lines apart -- the scene this boots, and the contentRoot a game
        // mode resolves its own files against. The other "Exported/..." literals
        // in this file are single-use paths to engine assets and are deliberately
        // left alone; this is the pair that must not drift, not a refactor.
        const std::string contentRoot = "Exported";

        // Project settings ship in project.json: the startup scene and the
        // master volume the game boots at. Always load them -- the master volume
        // is honoured however the scene ends up being chosen.
        ProjectSettings settings;
        settings.Load(); // Exported/project.json, written by the editor

        // ---- the title's front end ------------------------------------------
        //
        // Empty when this build has no title, and everything below reads the
        // same either way: a player built with no title boots exactly what it
        // booted before this seam existed, which is the property the boundary
        // assertion in the root CMakeLists exists to protect.
        std::string titleFrontEnd;
#ifdef CSE_HOST_TITLE_FRONT_END
        {
            // THE HOST DOES THE JOIN, and the title returns a content-relative
            // path (TitleFrontEnd.h says why). It is one line here and it is the
            // only line that changes the day the asset search path lands, when
            // "the content root" stops being a single directory.
            const std::string rel = TitleFrontEndScene();
            if (!rel.empty()) titleFrontEnd = contentRoot + "/" + rel;
        }
#endif

        // ---- what this boots, in order --------------------------------------
        //
        //   1. a scene named on the COMMAND LINE. It beats everything, including
        //      a linked title, because somebody debugging a scene must not have
        //      to uninstall the game to open it. commandLine() is captured
        //      portably by Main.h (argv), so it works on Windows and Linux alike.
        //   2. the TITLE's front end, if one is linked.
        //   3. project.json's startupScene, i.e. exactly what this did before.
        //
        // THE TITLE BEATS project.json, and that is the interesting one because
        // project.json is EDITOR-WRITTEN: a designer really can set a startup
        // scene in the editor and be overruled here. It is still the right
        // order. project.json is a HOST setting that predates titles and its
        // default (`Exported/scene.json`) is the engine's demo, so letting it win
        // would mean a shipped game boots the backpack room whenever anyone ever
        // pressed "Set Current Scene as Player Startup" -- a game that fails to
        // start itself because of a preference nobody remembers setting. The
        // command line is the deliberate act, so the command line is what wins.
        //
        // AND THE LOSING CASE SAYS SO. A setting that is read, ignored and never
        // mentioned is the same failure as the staging step that silently kept a
        // stale scene: the file is visibly right and the running game disagrees
        // with it, with nothing anywhere to explain the gap.
        std::string scenePath;
        if (commandLine().size() > 1) scenePath = commandLine()[1];
        const bool fromCommandLine = !scenePath.empty();
        if (scenePath.empty()) scenePath = titleFrontEnd;
        if (scenePath.empty()) scenePath = settings.startupScene;

        if (!titleFrontEnd.empty()) {
            if (fromCommandLine) {
                std::cout << "PLAYER: booting '" << scenePath
                          << "' from the command line; this build's title front end ('"
                          << titleFrontEnd << "') is not being used." << std::endl;
            }
            // Only when the file is actually THERE. project.json is written by
            // the editor and is not in the source tree, so a fresh install does
            // not have one and settings.startupScene is just the struct's
            // default -- announcing a conflict with a file nobody wrote, naming
            // a path that does not exist, would be its own small mystery.
            else if (settings.startupScene != titleFrontEnd &&
                     std::ifstream(ProjectSettings::DefaultPath()).good()) {
                std::cout << "PLAYER: " << ProjectSettings::DefaultPath()
                          << " asks for '" << settings.startupScene
                          << "', but this build links a title and the title's front end "
                             "wins: booting '" << titleFrontEnd
                          << "'. Pass a path (Player <scene.json>) to open the other one. "
                             "The rest of that file still applies — the master volume is "
                             "read from it." << std::endl;
            }
        }

        Scene scene;
        // The loader is installed BEFORE the boot load, so the boot goes
        // through exactly the path a menu button later will — one code path,
        // and the Install* helpers below can subscribe their teardown to it.
        SceneLoader sceneLoader(scene, assets);
        // The Application's pool, so RequestSwapAsync can warm a scene's models
        // on workers instead of importing them inside the swap.
        sceneLoader.SetJobSystem(&jobs());
        setSceneLoader(&sceneLoader);

        // The RENDERER's per-scene state, subscribed where the loader is
        // created for the same reason InstallPhysics/Scripting/Audio subscribe
        // theirs: a host that has to remember is a host that drifts from the
        // editor. The CSM half of a quality tier (cascade count, resolution)
        // lives on the Renderer and is NOT serialized, so a swap from a Low
        // menu into a High level kept running Low's cascades while
        // scene.GetQualityLevel() reported High.
        sceneLoader.AddObserver(nullptr, [this](Scene& s) {
            if (s.GetQualityLevel() != Scene::QualityLevel::Custom)
                renderer().ApplyQualityTier(s.GetQualityLevel(), s);
        });

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
        // The atlas size is the unit `font-scale` multiplies, so it is a shared
        // constant rather than a literal here -- see Font.h.
        if (!uiFont_.LoadFromFile("Exported/Fonts/Roboto.ttf",
                                  MyCoreEngine::kUIFontAtlasPixels)) {
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
        // Until this existed, the shipped game had NO capture provider at all
        // -- SetUICaptureProvider was called in exactly one place in the whole
        // repo, and it was the editor. So Application::RunLoop's `capK` was
        // permanently false and the quit check ran unconditionally: "Quit" is
        // bound to Escape AND to gamepad BACK, so typing your name into the
        // menu and pressing Escape exited the game.
        //
        // A one-frame lag is unavoidable and harmless: this runs before
        // input_->update, and a whole frame before uiWorld_.Update inside the
        // render pass, so it answers for the PREVIOUS frame's focus. Focus does
        // not move without input, so the only way to notice would be to press
        // Escape on the exact frame you first clicked into a field.
        SetUICaptureProvider([this] {
            UICapture caps;
            // A MODE THAT OWNS THE SCREEN TAKES THE KEYBOARD WITH IT, and the
            // two are one decision rather than two (IGameMode::OwnsScreen says
            // so). `keyboard` is what gates RunLoop's built-in behaviour: the
            // free-fly camera, and the "Quit" check that closes the window.
            // "Quit" is bound to Escape AND to gamepad BACK -- which is exactly
            // what a mode means by "leave" -- so a mode owning the screen while
            // this stayed false would find its Back button quitting the game
            // instead of returning to the menu. That is the same failure the
            // paragraph above records for text fields, one layer up.
            const bool modeOwnsScreen = modes_.activeOwnsScreen();
            caps.keyboard = modeOwnsScreen || uiWorld_.wantsKeyboard();
            // The scene's documents are not being updated while a mode owns the
            // screen (see the UI draw below), so whatever a menu text field
            // believed about focus is stale and must not keep suppressing
            // gameplay input for the whole time the mode is up.
            caps.textInput = !modeOwnsScreen && uiWorld_.wantsTextInput();
            // The pointer is NOT captured. A game's UI and its camera share the
            // mouse -- the UI takes clicks by hit-testing, which is a decision
            // per press rather than a mode.
            caps.mouse = false;
            return caps;
        });

        // ---- game modes -----------------------------------------------------
        //
        // What a mode is handed on entry, built in ONE place so the menu's verb
        // and any future entry point cannot hand out different contexts.
        //
        // A FACTORY rather than a stored struct, because it is evaluated at
        // ENTRY: the font may not have finished baking when this lambda is
        // written, and a stored context would carry that first answer forever.
        // Everything it names is host-lifetime, so the pointers are safe for as
        // long as a mode can hold them.
        const auto modeContext = [this, &scene, contentRoot] {
            GameModeContext ctx;
            ctx.app = this;
            ctx.scene = &scene;
            // The host's UI font, baked once at the size the whole UI shares. A
            // mode that loaded its own would be a second atlas and a second
            // answer to "how big is body text". May be null if the bake failed,
            // which is why GameModeContext documents it as optional.
            ctx.font = uiFont_.IsValid() ? &uiFont_ : nullptr;
            // Where Exported/ was staged next to this executable, i.e. the same
            // root every other path in this file is relative to -- and the same
            // string the title's front-end path was joined onto at boot, which
            // is why it is a named local rather than a second literal.
            ctx.contentRoot = contentRoot;
            return ctx;
        };

#ifdef CSE_HOST_TITLE_MODES
        // The modes this build ships. The macro is not set in
        // Player/CMakeLists.txt -- it arrives as an INTERFACE compile definition
        // on the title's own library, so this call exists exactly when something
        // is linked that defines it, and a player built with no title has no
        // mode verbs and boots exactly as it did before modes existed.
        //
        // BEFORE InstallMenuUIContent below, which publishes the modes' names
        // into the menu once. Registration order is menu order.
        RegisterTitleGameModes(modes_);
#endif

        // The active mode's simulation step. SetFixedUpdate is the PRIMARY fixed
        // slot and its own comment in Application.h reserves it for "a game's
        // own hook" -- this is that game, whichever one it turns out to be.
        // Physics and anything else that needs the tick are AddFixedUpdate
        // subscribers and are unaffected by taking it.
        SetFixedUpdate([this](float dt) {
            if (IGameMode* m = modes_.active()) m->FixedTick(dt);
        });
        // Per-frame. A SUBSCRIBER rather than SetUpdate, because scripting
        // already occupies the subscriber list and the primary variable slot is
        // the matching reservation to the one above; a mode taking it would
        // silently delete a scene's own per-frame hook the day one exists.
        AddUpdate([this](float dt) {
            if (IGameMode* m = modes_.active()) m->Update(dt);
        });

        InstallDemoUIContent(uiWorld_);

        // The menu's verbs. AFTER the demo content, because the menu's name
        // field binds to `playerName`, which is seeded there.
        //
        // The shipped player has no reason to refuse any of them: there is no
        // edit mode, gameplay is always live, and Quit means quit.
        menuHooks_.app = this;
        menuHooks_.scene = &scene;
        menuHooks_.renderer = &renderer();
        menuHooks_.audio = &audio_;
        menuHooks_.initialVolume = settings.masterVolume;
        // "THE MENU" IS WHATEVER FRONT END THIS BUILD ACTUALLY HAS. MenuUIHooks
        // defaults menuScenePath to the engine demo's own Exported/menu.json,
        // which is right for a title-less build and wrong the moment a title
        // supplies its own: every verb that means "go back to the menu" would
        // swap the player OUT of the game's front end and INTO the engine's
        // sample, which is the failure this whole seam is about, arriving one
        // button later. `playScenePath` is left alone deliberately -- it is the
        // demo menu's NEW GAME target, so a title that wants that verb is
        // declaring it in its own markup rather than inheriting the backpack.
        if (!titleFrontEnd.empty()) menuHooks_.menuScenePath = titleFrontEnd;
        // The modes, for their NAMES, and the verb that enters one. Two fields
        // because they are two different things -- data and an action -- the
        // same split as `audio` and `onMasterVolume` above.
        //
        // THE HOST ENTERS, NOT THE MENU: GameModeContext carries this
        // executable's Font and its asset root, which MenuUIContent has no
        // business acquiring. The returned string is empty on success or a
        // message for the menu's own status line, so a mode that refuses to
        // start is reported the same way a scene that will not load is, in the
        // shipped player which has no console to print to.
        //
        // THIS LINE WAS MISSING AND THE FEATURE WAS INVISIBLE. The comment above
        // said "two fields" and only the action half was ever assigned, so
        // `MenuUIContent.cpp`'s `(h.modes && h.onEnterMode) ? h.modes->Count() : 0`
        // read a null registry, published `available = 0`, and every one of the
        // four `if="menuModeN"` slots evaluated false. The result was a main menu
        // that looked exactly like the one from before modes existed -- no error,
        // no empty button, nothing to notice -- on a build whose binary contained
        // the mode, whose registration ran first, and whose markup was correct.
        //
        // Everything about the seam was right except that nobody handed it the
        // data. It is worth naming because no test in this repository could have
        // caught it: the menu needs a GL context, the registry is populated in a
        // host this file is the only instance of, and both halves independently
        // reviewed as correct.
        menuHooks_.modes = &modes_;
        menuHooks_.onEnterMode = [this, modeContext](int index) -> std::string {
            std::string error;
            if (!modes_.Enter(index, modeContext(), error)) {
                std::cerr << "PLAYER: mode " << index << " refused: " << error
                          << std::endl;
                return error;
            }
            std::cout << "PLAYER: entered mode '"
                      << modes_.DisplayNameAt(index) << "'." << std::endl;
            return {};
        };
        InstallMenuUIContent(uiWorld_, menuHooks_);
        sceneLoader.SetOnSwapComplete([this](const SceneSwapResult& r) {
            MenuUIReportSwap(uiWorld_, r);
        });
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
            // ---- the active game mode, first ----------------------------
            //
            // THE EXIT DRAIN LIVES HERE, and the obvious home for it -- an
            // AddUpdate subscriber beside the one that calls Update -- is the
            // wrong one. Variable-rate subscribers are skipped entirely while
            // paused, at timeScale 0, and on the frame of a scene swap, so a
            // mode that asked to leave in any of those states would stay up
            // forever with its own Escape apparently dead. This callback runs
            // every frame the renderer runs, unconditionally, and it runs
            // BEFORE the mode's Draw below -- so a mode that asked to leave
            // never draws another frame.
            modes_.DrainExitRequest();

            if (IGameMode* mode = modes_.active()) {
                if (mode->OwnsScreen()) {
                    // The scene's own UI documents do NOT run. Not merely "are
                    // not drawn": uiWorld_.Update is what dispatches clicks and
                    // moves focus, so leaving it running would put a live main
                    // menu underneath the mode, taking presses nobody can see.
                    // The other half of this pairing is in the capture provider
                    // above -- both or neither.
                    //
                    // THE 3D PASS STILL RAN. RunLoop renders the scene every
                    // frame and this callback is a pass at the end of it, so a
                    // mode that owns the screen is painting over a frame that
                    // was already drawn -- correct, and wasteful. Skipping it
                    // needs a switch on Application ("render no world this
                    // frame"), which is a change to the loop rather than to this
                    // seam, and it is not worth making until a mode is doing
                    // enough work for the wasted pass to matter.
                    //
                    // DRAIN THE UI'S INPUT ACCUMULATORS ANYWAY. The GLFW key and
                    // char callbacks keep filling g_uiKeys between frames
                    // whatever is on screen, and the engine's scroll callback
                    // keeps summing notches -- they are installed once, for the
                    // app's life, and know nothing about modes. Skipping the
                    // drain below without doing it here means the vector grows
                    // for as long as the mode is up and then delivers every
                    // keystroke of a whole fight to the menu the instant you
                    // return to it, along with one enormous scroll.
                    // Cleared, not forwarded: a mode reads the engine's InputMap
                    // (actions and axes), which is polled by RunLoop and needs
                    // nothing from here. uiWorld_ is not updated at all while a
                    // mode is up, so its own copy of this state is overwritten
                    // by the first ordinary frame after the mode leaves.
                    g_uiKeys.clear();
                    (void)ConsumeScrollDelta();
                    mode->Draw(r2d, w, h, dt);
                    return;
                }
                // A mode layered OVER the running scene draws after the
                // documents do, at the bottom of this callback.
            }

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

            // Before Update, so the frame that draws the counters draws the
            // current ones. Pushed rather than polled -- see MenuUIContent.h.
            MenuUIPublishCounters(uiWorld_, menuHooks_);
            // The controller. Unconditional: the shipped game has no edit mode
            // and no competing panels, so a pad always drives the UI.
            // WASD navigates the menu, but the SAME four keys type a pilot
            // name. The flag silences the key half of the nav actions and
            // leaves the pad half alone -- a pad types nothing, so it has no
            // reason to go quiet.
            uiWorld_.SetNav(navSynth_.Poll(input(), dt,
                                           !uiWorld_.wantsTextInput()));
            uiWorld_.Update(scene.registry, w, h, dt);
            uiWorld_.Draw(r2d);

            // A non-OwnsScreen mode: an overlay on the running scene, drawn
            // last so it sits on top of the scene's own HUD.
            if (IGameMode* mode = modes_.active()) mode->Draw(r2d, w, h, dt);
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

        // Leave whatever mode was live, HERE rather than leaving it to
        // ~GameModeRegistry. The registry is a member and would do it, but by
        // then `scene`, `sceneLoader` and `shader` -- all locals of this
        // function, and all reachable from a GameModeContext -- have already
        // been destroyed. A mode's Exit is allowed to touch what it was handed;
        // doing it before this function's frame unwinds is the only place that
        // is true.
        modes_.Leave();
    }
};

MyCoreEngine::Application* MyCoreEngine::CreateApplication() {
    return new PlayerApplication();
}

#define MYCE_DEFINE_ENTRY
#include "../src/core/Main.h"
