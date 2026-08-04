#pragma once
// The C++ half of the main-menu demo, the same way DemoUIContent.h is the C++
// half of the sample HUD: what a FILE cannot carry is a named function.
//
// It takes a context struct rather than a bare UIWorld because a menu's verbs
// are HOST verbs — load a scene, close the window, set the master volume, apply
// a quality tier — and `InstallDemoUIContent(UIWorld&)` can reach none of them.
// Widening that signature would make every existing caller carry arguments it
// has no use for.
//
// It lives in the Engine rather than being written twice for the same reason
// InstallPhysics / InstallScripting / InstallAudio do: two copies drift, and
// "the editor previews exactly what ships" is the property this codebase
// protects hardest.
#include "../core/Core.h"

#include <functional>
#include <string>

namespace MyCoreEngine {

    class Application;
    class AudioWorld;
    class Renderer;
    class Scene;
    class UIWorld;
    struct SceneSwapResult;

    // Everything the menu's verbs need that a .cxml cannot express.
    //
    // NOTHING SCENE-DERIVED IS IN HERE, deliberately: no entt::entity, no
    // component pointer, no element pointer. Those are precisely what a swap
    // invalidates, and these hooks outlive every scene the game loads.
    //
    // `Scene*` is the exception and it is safe: SceneLoader loads INTO the same
    // Scene object, so only its contents change. `Application*`, `Renderer*` and
    // `AudioWorld*` are host members that live for the whole process.
    struct ENGINE_API MenuUIHooks {
        Application* app      = nullptr;  // LoadScene, setVSync, GetNativeWindow
        Scene*       scene    = nullptr;  // the quality tier's target
        Renderer*    renderer = nullptr;  // ApplyQualityTier
        AudioWorld*  audio    = nullptr;  // SetMasterVolume — there is NO getter,
                                          // which is why initialVolume exists

        std::string playScenePath = "Exported/scene.json";
        std::string menuScenePath = "Exported/menu.json";

        float initialVolume = 1.0f;   // AudioWorld cannot be asked, so it is told
        float volumeStep    = 0.1f;

        // Empty => allowed. The editor supplies [this]{ return playing_; },
        // because its Game panel dispatches the running document's clicks even
        // while STOPPED — and the edit-mode gate guards SWAPS, not the volume.
        std::function<bool()> allowHostMutation;

        // Empty => close the host window. The editor overrides it: its only
        // window is the editor itself, and a game's Quit button must not take
        // the editor down with it.
        std::function<void()> onQuit;

        // Empty => this file writes Exported/project.json itself. The editor
        // supplies one because it keeps its own masterVolume_ mirror (see
        // above: AudioWorld has no getter) and two writers would desync.
        std::function<void(float)> onMasterVolume;

        // Editor: forceAllCSMUpdate_ — the CSM half of a tier is not part of
        // the scene, so changing tiers has to rebuild cascades. Player: empty.
        std::function<void()> onQualityChanged;
    };

    // Call ONCE per host, AFTER setSceneLoader (the verbs need it) and after
    // InstallDemoUIContent (the menu's name field binds to `playerName`, which
    // is seeded there).
    ENGINE_API void InstallMenuUIContent(UIWorld& world, const MenuUIHooks& hooks);

    // Call from the host's SceneLoader::SetOnSwapComplete handler. Appends to
    // the menu's swap log and updates its status line.
    //
    // RE-ENTRANCY, and it is not theoretical: SceneLoader::finish_ also fires
    // SYNCHRONOUSLY from inside RequestSwap for the Invalid and Superseded
    // cases — which means from inside a UI action, inside UIWorld::Update's
    // loop over its documents, between BeginScreen and End. So this function
    // may touch UIWorld::shared() and nothing else. No element, no document, no
    // registry.
    ENGINE_API void MenuUIReportSwap(UIWorld& world, const SceneSwapResult& r);

    // Call once per frame from the host's UI-draw lambda, BEFORE UIWorld::Update.
    //
    // Also reconciles the master volume, which a <Slider> now writes DIRECTLY
    // into the shared source through its two-way binding rather than through a
    // named action. Audio follows every change immediately; the SETTING is
    // written to disk only once the value stops moving.
    //
    // Settle detection rather than a release event, deliberately: it is the
    // same code for a mouse drag, an arrow key and a gamepad stick, and none of
    // them has to tell this file which device it was. Without it a continuous
    // drag would do a ProjectSettings load-modify-save once per input frame --
    // the shipped Player takes exactly that branch, because it installs no
    // onMasterVolume hook.
    //
    // Push, not Observe. UIDataSource::hasPolled() is a WHOLE-SOURCE flag, so a
    // single polled property on the shared source would defeat the binder's
    // version fast path for every binding in every document in the process, for
    // the rest of the run. Every setter here is equality-gated, so a frame in
    // which nothing moved writes nothing and wakes no binding.
    ENGINE_API void MenuUIPublishCounters(UIWorld& world, const MenuUIHooks& hooks);

} // namespace MyCoreEngine
