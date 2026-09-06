#pragma once
// A MODE the host can enter, tick, draw and leave.
//
// ---------------------------------------------------------------------------
// WHY THE PLAYER NEEDED A CONCEPT IT DID NOT HAVE
// ---------------------------------------------------------------------------
// The Player has exactly one thing it can do: load a saved scene and run it.
// That is a perfectly good default and it is not general enough, because A
// FIGHT IS NOT A SCENE. It is a FightSession with its own fixed tick, its own
// render path and no ECS entities at all -- and a training mode, a replay
// viewer and a lockstep netplay session are all the same shape. None of them
// can be expressed as "a .json with a camera in it", so the host needs
// something above the scene: a thing it can be IN.
//
// So "play the saved scene" stops being what the Player is and becomes one mode
// among several. This file is what "a mode" means.
//
// ---------------------------------------------------------------------------
// THE ENGINE OWNS THE SEAM, THE TITLE OWNS THE MODES
// ---------------------------------------------------------------------------
// Nothing here knows what a fighting game is, and nothing here ever may:
// Games/UntitledFighter/CMakeLists.txt states the rule -- a title may depend on
// the engine, the engine may never depend on a title. IGameMode names no
// fighter for the same reason CseNet's ISession does not, and it lives in the
// Engine rather than in Player/ for the same reason InstallPhysics,
// InstallScripting and MenuUIContent do: the editor is going to want to enter a
// mode too, and the one thing this codebase protects hardest is that the editor
// previews exactly what ships. Two copies of a host loop drift.
//
// ---------------------------------------------------------------------------
// IT IS FITTED TO THE LOOP THAT EXISTS
// ---------------------------------------------------------------------------
// Every call below lands in a hook Application already had, and no new stage was
// added to RunLoop for it:
//
//   FixedTick   Application::SetFixedUpdate -- "the single primary fixed-update
//               slot (gameplay)". The comment on that slot already says it is
//               kept free for a game's own hook. This is that game.
//   Update      Application::AddUpdate, a subscriber, because SetUpdate is the
//               matching primary slot and scripting already coexists on the
//               subscriber list rather than stealing it.
//   Draw        Renderer::SetUIDraw, i.e. the UIPass -- screen-space, after the
//               entire LDR post chain, with a Renderer2D already in pixel
//               coordinates. UIPass.h says it in as many words: "a 2D game can
//               use the same hook to draw its sprites".
//
// A mode that needed a fourth kind of callback would be telling you the loop is
// wrong, and that is a conversation about Application, not about this file.
#include "Core.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace MyCoreEngine {

    class Application;
    class AssetManager;
    class Font;
    class Renderer2D;
    class Scene;

    // Everything a mode is handed on entry.
    //
    // ALL HOST-LIFETIME, AND NOTHING SCENE-DERIVED -- the same rule MenuUIHooks
    // states and for the same reason: a mode may outlive several scenes, and an
    // entt::entity or a component pointer captured here would be dangling after
    // the first swap. `Scene*` is the one exception and it is safe, because
    // SceneLoader loads INTO the same Scene object; only its contents change.
    //
    // WHAT IS DELIBERATELY ABSENT: the InputMap and the Renderer. Both are
    // accessors ON Application (app->input(), app->renderer()), so listing them
    // here would be two more things to keep in step with one pointer that
    // already answers. The Font is here because it is NOT that -- it is a member
    // of the host executable, baked by the host at the size the whole UI shares,
    // and a mode that loaded its own would be a second atlas and a second answer
    // to "how big is body text".
    struct GameModeContext {
        Application* app   = nullptr;   // fixed timestep, input, quit, scene loads
        Scene*       scene = nullptr;   // the host's one Scene object; may be empty
        Font*        font  = nullptr;   // the host's UI font; may be null if it failed to bake

        // The host's asset root -- where Exported/ was staged next to the
        // executable. A mode that opens authored content resolves against this
        // rather than hardcoding a guess, and authored paths are untrusted, so
        // it is expected to go through a sandbox check first
        // (docs/MAINTENANCE.md).
        std::string contentRoot = "Exported";

        // The host's asset cache (ROADMAP M3.4b, ADR-019 D9), so a mode can load
        // a presentation model through the same cache and reload door every
        // other model uses instead of owning a second loader. May be null -- a
        // host with no renderer has none -- and a mode must then draw nothing
        // 3D rather than crash.
        AssetManager* assets = nullptr;
    };

    // One thing the host can be in.
    //
    // Header-only and abstract. A mode is implemented in a title's own library
    // and linked into the host executable, so the interface is deliberately free
    // of anything that would have to cross a library boundary as an engine type.
    class ENGINE_API IGameMode {
    public:
        virtual ~IGameMode() = default;

        // What the menu calls this, shown VERBATIM. The host does not
        // capitalise, truncate or decorate it -- a title names its own modes.
        virtual const char* DisplayName() const = 0;

        // Take up residence. The only place a mode is allowed to load, allocate
        // or build anything, so that the cost of entering is paid at a moment
        // the player understands (they just pressed a button) rather than
        // scattered through the first few frames.
        //
        // Return false with `error` filled in for a failure the PLAYER must be
        // told about and that leaves nothing to look at; the host reports it and
        // stays where it was. A mode that CAN still show something honest --
        // its own name and why the content is missing -- should return true and
        // draw that, because a mode that refuses to enter proves nothing about
        // the wiring and tells the player less than a screen saying what broke.
        virtual bool Enter(const GameModeContext& ctx, std::string& error) = 0;

        // Give it back. Called on the way out, including when the host is
        // shutting down, and it must be safe to call after a FAILED Enter --
        // the host does not track how far Enter got.
        virtual void Exit() {}

        // The simulation step, at the host's fixed rate.
        //
        // READ THE RATE, DO NOT ASSUME IT: Application::fixedTimestepHz() is a
        // host setting, and Application's FixedTimestep caps at 8 steps per
        // frame and then ZEROES the accumulator, dropping the backlog. For a
        // local single-player mode that is a hitch. For anything lockstep,
        // replay-verified or rollback-networked it is a LOST TICK, which is a
        // lost input and an unrecoverable desync -- so such a mode must own its
        // own accumulator or drive from its session's own event stream instead
        // of trusting this call to arrive the right number of times.
        virtual void FixedTick(float dt) { (void)dt; }

        // Per-frame, variable rate: presentation, interpolation, input edges
        // that are not part of the simulation, and the decision to leave.
        virtual void Update(float dt) { (void)dt; }

        // Screen space, pixel units, origin top-left, +y down. Called from the
        // UI pass, after all post-processing, with Begin/End already handled --
        // draw only.
        virtual void Draw(Renderer2D& r2d, int widthPx, int heightPx, float dt) {
            (void)r2d; (void)widthPx; (void)heightPx; (void)dt;
        }

        // Whether this mode has the screen and the keyboard to itself.
        //
        // TRUE for anything that is not a scene, which is the case this seam
        // exists for, and it means two things the host must honour together:
        // the scene's own UI documents do not run (so the main menu is not
        // sitting live underneath, taking clicks nobody can see), and the host
        // reports keyboard capture to Application (so RunLoop's built-in
        // Escape-quits and its fly camera stand down and the mode's own Escape
        // means "leave the mode"). Both or neither -- suppressing the menu while
        // leaving Escape wired to the host is how pressing Back in a fight quits
        // the game.
        //
        // FALSE is for a mode layered OVER the running scene, which is what a
        // "play the saved scene" mode is: it wants the scene's UI, its camera
        // and the host's ordinary key handling exactly as they were.
        virtual bool OwnsScreen() const { return true; }

        // The mode asking to be let out -- Escape, a Quit-to-menu button, a
        // match ending. The host drains it at a frame boundary and calls Exit;
        // it is a REQUEST rather than a direct call so that a mode can raise it
        // from inside a fixed tick or a draw without unloading itself
        // mid-callback.
        bool wantsExit() const { return exitRequested_; }

        // Withdraw that request. The registry calls this when it ENTERS a mode,
        // so one that was left and re-entered does not immediately ask to leave
        // again; a mode may also call it on itself to change its mind before the
        // host drains it, which is the only reason it is harmless in public.
        //
        // Public rather than a friendship with the registry, and the reason is
        // dull rather than principled: `friend class GameModeRegistry;` would be
        // a second declaration of a dllexport'd class, which is the sort of
        // thing MSVC has opinions about, and there is nothing behind this flag
        // worth protecting that hard.
        void clearExitRequest() { exitRequested_ = false; }

    protected:
        IGameMode() = default;
        void requestExit() { exitRequested_ = true; }

    private:
        bool exitRequested_ = false;
    };

    // The modes this build ships, and which one is active.
    //
    // Owns them: a mode is constructed once at registration and lives for the
    // process, entered and exited as many times as the player likes. That is
    // what lets a mode keep a warmed cache across visits, and it means Enter is
    // the only cost of coming back rather than a construction as well.
    class ENGINE_API GameModeRegistry {
    public:
        GameModeRegistry() = default;
        ~GameModeRegistry();

        GameModeRegistry(const GameModeRegistry&) = delete;
        GameModeRegistry& operator=(const GameModeRegistry&) = delete;

        // Takes ownership. REGISTRATION ORDER IS MENU ORDER -- the host's menu
        // lists them as they were added, so a title decides what its first
        // option is by registering it first. Null is ignored rather than stored.
        void Add(std::unique_ptr<IGameMode> mode);

        int  Count() const { return static_cast<int>(modes_.size()); }
        // Empty string for an out-of-range index rather than a crash: this is
        // read by menu code driven by data, and an index that outran the list is
        // a slot that should render as absent.
        const char* DisplayNameAt(int index) const;

        // Enter by index. Leaves the current mode first, so there is never more
        // than one live -- two modes each believing they own the screen is the
        // failure this single-slot design exists to make impossible.
        //
        // False with `error` filled in when the index is out of range or the
        // mode's own Enter refused; in the refusal case Exit() has already been
        // called and NOTHING is active, so the host is back where it started
        // rather than half in.
        bool Enter(int index, const GameModeContext& ctx, std::string& error);

        // Exit whatever is active. Safe to call with nothing active.
        void Leave();

        IGameMode* active() const {
            return activeIndex_ < 0
                 ? nullptr
                 : modes_[static_cast<std::size_t>(activeIndex_)].get();
        }
        int        activeIndex() const { return activeIndex_; }

        // True when a mode is active AND claims the screen. The one question
        // hosts actually ask -- of the input capture provider, of the UI draw --
        // written once here so two call sites cannot answer it differently.
        bool activeOwnsScreen() const;

        // Drain the active mode's exit request, if it made one. Returns true if
        // a mode was left.
        //
        // CALL IT SOMEWHERE THAT RUNS EVERY FRAME REGARDLESS OF PAUSE. The
        // obvious home is a variable-rate update subscriber, and it is the wrong
        // one: those are skipped entirely while paused, at timeScale 0, and
        // during the frame of a scene swap -- so a mode that asked to leave
        // while paused would stay up forever, with its own Escape apparently
        // dead.
        bool DrainExitRequest();

    private:
        std::vector<std::unique_ptr<IGameMode>> modes_;
        int activeIndex_ = -1;
    };

#ifdef CSE_HOST_TITLE_MODES
    // DEFINED BY THE TITLE, not by the engine, and not by any host.
    //
    // The same idiom Application.h already uses two screens down for
    // CreateApplication ("defined in other projects"): the engine declares the
    // function, a game defines it, and the LINK is where they meet. NOT
    // ENGINE_API -- it lives in whatever archive the executable links, not in
    // the engine DLL, so marking it dllimport would send the linker looking in
    // the wrong place.
    //
    // The guard is not set by any host's CMakeLists. It rides in as an INTERFACE
    // compile definition on the title's own player library, so LINKING THE
    // LIBRARY IS TURNING THE HOOK ON and the two cannot disagree: a host that
    // links nothing compiles the call out and still links clean, and a host that
    // links a title but somehow lost the definition fails at LINK time rather
    // than booting with a menu that quietly has no game in it.
    //
    // The road not taken is a self-registering static in the title's library.
    // It needs no hook and it does not work -- a static-library member with no
    // referenced symbol is dropped by both linkers, so the constructor never
    // runs and the mode is simply absent with no diagnostic anywhere.
    //
    // Declared here rather than in Player/ deliberately: it is the EDITOR that
    // wants this next (entering a mode in the Game view is how the editor
    // previews what ships), and a hook declared in one host is a hook the second
    // host has to redeclare.
    void RegisterTitleGameModes(GameModeRegistry& registry);
#endif

} // namespace MyCoreEngine
