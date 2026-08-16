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
    class GameModeRegistry;
    class Renderer;
    class Scene;
    class UIWorld;
    struct SceneSwapResult;

    // How many game-mode verbs the menu markup has room for.
    //
    // A FIXED NUMBER OF SLOTS, and not because a list would be nicer. A UI
    // action is a `void()` registered by NAME on a data source, so a `repeat=`
    // block over the registry would render one button per mode and every one of
    // them would invoke the SAME action with no way to say which row was
    // clicked. Slots are the shape the markup language actually has: each is a
    // `<Button if="menuMode0" text="{menuMode0Name}" on-click="menuEnterMode0">`,
    // absent when the slot is empty, so a build with one mode shows one verb and
    // a build with none shows a menu identical to the one before modes existed.
    //
    // Four because the shipped menu has four verbs of its own and a column of
    // nine is a different design; a registry with more modes than this is a
    // title that needs a mode SELECT screen rather than a taller main menu, and
    // the extras are reported rather than silently dropped.
    inline constexpr int kMenuModeSlots = 4;

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

        // The modes this build ships, for their NAMES and their count. Null =>
        // no mode verbs at all, which is every host that has not registered any
        // and is exactly the menu as it was before modes existed.
        //
        // READ AT INSTALL *AND* ON EVERY MenuUIPublishCounters CALL, so the
        // `menuModeN` / `menuModeNName` properties are a FUNCTION of this
        // registry rather than a photograph of it. A registry that grows a mode
        // after the menu is up gets its verb on the next frame, and a document
        // that loads later binds against current values whatever order the host
        // did things in.
        //
        // IT USED TO BE READ ONCE, AT INSTALL, and that is worth keeping because
        // it is the bug this contract was rewritten around. These were the only
        // properties in the whole menu published exactly once during startup:
        // everything else is either re-derived per frame or moved by a user
        // action, so everything else re-evaluated and these did not. Nothing was
        // wrong at any single step -- the registry held the mode, the hooks
        // pointed at it, the install wrote `menuMode0 = true` -- and the menu
        // still said the build had no playable modes, because the value was read
        // at an instant when it was not yet, or no longer, what it should be.
        // A once-published flag with no owner watching it is not cheaper than a
        // derived one; it is the same cost paid in a debugging session instead.
        //
        // A HOST STILL HAS TO CALL MenuUIPublishCounters for the tracking half.
        // One that does not (a test, a tool) gets the install-time answer, which
        // is correct as long as it registered its modes first -- the original
        // contract, still honoured, now the floor rather than the whole story.
        GameModeRegistry* modes = nullptr;

        // The verb behind a mode button. Empty => the buttons are not drawn at
        // all (see the seeding in the .cpp), because a menu that offers a mode
        // it cannot enter is worse than one that offers nothing.
        //
        // The HOST enters, not this file, and the reason is GameModeContext: it
        // carries the host's Font and its asset root, which live in the
        // executable and which MenuUIHooks has no business acquiring. Returns
        // an EMPTY string on success or a message to show the player, so the
        // failure lands on the same status line as a failed scene load rather
        // than in a console the shipped player does not have.
        std::function<std::string(int /*modeIndex*/)> onEnterMode;

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

    // Call ONCE per host, AFTER setSceneLoader (the verbs need it), after
    // InstallDemoUIContent (the menu's name field binds to `playerName`, which
    // is seeded there), and after every game mode has been registered (see
    // MenuUIHooks::modes).
    //
    // ...AND BEFORE ANY DOCUMENT THAT READS THESE PROPERTIES EXISTS. That is the
    // other half of the contract and it went unwritten for a long time, which is
    // exactly why it is now CHECKED rather than merely stated: this function
    // seeds ~20 properties into UIWorld::shared(), and a document binds at LOAD.
    // A host that has already built one gets a menu wired to whatever the
    // properties happened to be, plus one load-time diagnostic per unresolved
    // hole — into a console the shipped player does not have.
    //
    // So the install ASKS: UIWorld::liveCount() > 0 means a document has already
    // bound, and that is reported to std::cerr AND to the menu's own status line,
    // which is the only one of the two a shipped player can show you. It is a
    // report rather than a refusal because the situation is recoverable — the
    // mode flags are re-derived every frame, and `if=` is a style write rather
    // than tree surgery, so a hidden element keeps its bindings and reappears the
    // moment a value moves. What is not recoverable is silence.
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
    // BEFORE Update is not a style note, it is the whole reason this is safe to
    // rely on: Update is where UIWorld builds a document it has not seen yet, so
    // a menu appearing this frame binds against values written moments earlier.
    // That is what lets `menuModeN` be published here rather than only at install
    // — see MenuUIHooks::modes for the bug that made the difference matter.
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
