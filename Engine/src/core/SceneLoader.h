#pragma once
// Runtime scene replacement: what a game calls when a menu button says
// "New Game", and what the editor's File > Open goes through.
//
// TWO THINGS MAKE THIS MORE THAN A CALL TO SceneSerializer::Load.
//
// It is DEFERRED, always, including the "synchronous" API. A UI button's
// handler runs inside UIWorld::Update, which runs inside the UI render pass,
// between BeginScreen and End — nothing there may clear the registry or touch
// GL. So a request is recorded and runs at a frame boundary, once, in
// Application::RunLoop. There is no immediate variant, because there is nowhere
// safe to put one.
//
// And it carries THE SWAP CONTRACT. Replacing a scene is not just replacing
// entities: physics holds entity->body pairs, scripting holds entity->instance
// pairs, audio holds live voices, and the renderer holds shadow cascades baked
// against the departed geometry. That knowledge used to live in one editor
// private method, which meant a game literally could not switch scenes
// correctly even by calling Load itself. It lives here now, as an observer
// list, so a subsystem that forgets to subscribe is a missing subscription
// rather than a silent leak.
#include "Core.h"
#include "AssetManager.h"      // ModelRequestHandle, for the prewarm list
#include "SceneSerializer.h"   // SceneLoadReport

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace MyCoreEngine {

    class Scene;
    class AssetManager;
    class JobSystem;

    // Who asked. Host = the editor's File > Open, the player's boot. Game = a
    // UI action or a script — content asking, which a host may refuse. The
    // editor refuses Game swaps in edit mode: its Game panel dispatches the
    // game's UI clicks even while stopped, so a menu button could otherwise
    // replace the scene somebody was editing.
    enum class SceneSwapOrigin { Host, Game };

    struct ENGINE_API SceneSwapContext {
        std::string     path;
        SceneSwapOrigin origin = SceneSwapOrigin::Game;
    };

    enum class SceneSwapStatus {
        Ok,
        Refused,      // an observer vetoed it
        Invalid,      // the file would not load; nothing was touched
        LoadFailed,   // it validated, then failed anyway — see the note below
        Superseded,   // a later request replaced this one before it ran
    };

    struct ENGINE_API SceneSwapResult {
        SceneSwapStatus status = SceneSwapStatus::Ok;
        std::string     path;
        std::string     message;     // host-facing, already logged
        SceneLoadReport report;
        bool ok() const { return status == SceneSwapStatus::Ok; }
    };

    // Everything a scene replacement must reset that the engine cannot reach on
    // its own. Registration order IS notification order, in both directions.
    //
    // WHICH HOOK matters more than the order within one:
    //
    //   OnSceneWillUnload runs while the OUTGOING entities are still alive.
    //     Anything that must SEE them belongs here — a script world's Clear
    //     fires OnDestroy, and against an already-cleared registry every
    //     accessor in those handlers silently no-ops.
    //
    //   OnSceneDidLoad runs after the new entities exist AND after
    //     Scene::UpdateTransforms(), so world matrices are current. That
    //     ordering is engine knowledge and it lives here rather than in each
    //     host: building physics bodies before UpdateTransforms puts a scaled
    //     ground plane at the origin at unit size.
    class ENGINE_API ISceneSwapObserver {
    public:
        virtual ~ISceneSwapObserver() = default;
        // Refuse before anything is touched. `reason` is logged verbatim.
        virtual bool AllowSceneSwap(const SceneSwapContext&, std::string& /*reason*/) {
            return true;
        }
        virtual void OnSceneWillUnload(Scene&, const SceneSwapContext&) {}
        virtual void OnSceneDidLoad(Scene&, const SceneSwapContext&) {}
    };

    class ENGINE_API SceneLoader {
    public:
        SceneLoader(Scene& scene, AssetManager& assets)
            : scene_(scene), assets_(assets) {}
        SceneLoader(const SceneLoader&) = delete;
        SceneLoader& operator=(const SceneLoader&) = delete;

        using ObserverHandle = std::uint32_t;
        // NON-OWNING. An observer must outlive the loader, which is trivially
        // true for the subsystems a host holds by value.
        ObserverHandle AddObserver(ISceneSwapObserver* obs);

        // Convenience for a subsystem whose whole contract is "drop everything
        // you derived from the old registry" — which is all three of physics,
        // scripting and audio. OWNED by the loader, unlike the pointer
        // overload, so an Install helper can subscribe in one line and never
        // think about the observer's lifetime.
        //
        // `didLoad` is usually empty: those subsystems rebuild lazily from
        // components on their next tick, which is why the editor's old private
        // swap only ever called Clear().
        ObserverHandle AddObserver(std::function<void(Scene&)> willUnload,
                                   std::function<void(Scene&)> didLoad = {});

        void           RemoveObserver(ObserverHandle h);

        // Record a swap; NEVER loads inline. The caller may be inside a render
        // pass, a fixed tick or another observer, and none of those may see the
        // registry change underneath them.
        //
        // The file is VALIDATED here, at request time, and a bad one is refused
        // immediately with nothing touched. That matters because the swap
        // itself tears the outgoing scene's subsystems down BEFORE it loads —
        // so "the file was bad" has to be caught before anyone commits.
        //
        // A second request before the first runs supersedes it.
        bool RequestSwap(std::string path, SceneSwapOrigin origin = SceneSwapOrigin::Game);

        // The SAME swap, with the models warmed on worker threads first.
        //
        // The destructive part cannot move off the main thread and never will:
        // a load creates entities, and the registry is single-threaded by
        // design. What CAN move is the expensive part -- importing the meshes
        // and textures the scene names, which is where the seconds actually go.
        //
        // So this collects those paths at request time, hands them to
        // AssetManager::RequestModel (decode on a worker, GL finalize on the
        // main thread), and holds the swap until every one has settled. The
        // OUTGOING scene stays alive and rendering at full rate throughout,
        // which is the whole point: a loading screen you can look at.
        //
        // By the time the swap runs, every GetModel inside it is a cache hit,
        // so the window in which the scene is torn down shrinks from "however
        // long the models take" to "however long entity creation takes".
        //
        // DEGRADES HONESTLY. With no JobSystem attached, or with a scene that
        // names no models, this is exactly RequestSwap -- the swap still
        // happens, it simply is not warmed first.
        bool RequestSwapAsync(std::string path, SceneSwapOrigin origin = SceneSwapOrigin::Game);

        // Where the worker pool comes from. Non-owning; the Application's pool
        // outlives the loader in both hosts. Without one, RequestSwapAsync is
        // RequestSwap.
        void SetJobSystem(JobSystem* jobs) { jobs_ = jobs; }

        bool swapPending() const { return pending_.has_value(); }

        // A pending swap is waiting on its models. A host draws its loading
        // screen while this is true, and DrainPendingSwap does nothing.
        bool swapPrewarming() const;
        // Settled / total models for that wait, so a progress bar has numbers.
        // Both zero when nothing is prewarming.
        std::size_t prewarmDone() const;
        std::size_t prewarmTotal() const { return prewarm_.size(); }

        // How many observers are subscribed. Diagnostic, and worth having:
        // the tempting way to implement "rebuild the subsystems after a swap"
        // is to re-run the Install* helpers, each of which subscribes another
        // observer AND another tick subscriber. Three of each per swap is
        // thirty by the tenth, and the only symptom is that the game gets
        // slower the more scenes you visit.
        std::size_t observerCount() const { return observers_.size(); }
        const std::string& pendingPath() const;
        void CancelPendingSwap();

        // Fired at the end of DrainPendingSwap, success or not.
        using OnSwapFn = std::function<void(const SceneSwapResult&)>;
        void SetOnSwapComplete(OnSwapFn fn) { onComplete_ = std::move(fn); }
        const SceneSwapResult& lastResult() const { return last_; }

        // MAIN THREAD, FRAME BOUNDARY ONLY. Returns true when a swap actually
        // ran, so the caller can skip that frame's gameplay — a fresh scene
        // should be rendered once before it is simulated.
        bool DrainPendingSwap();

    private:
        void finish_(SceneSwapResult r);

        Scene&        scene_;
        AssetManager& assets_;

        // A function-based observer the loader owns, adapted to the interface
        // so the notification loop has exactly one shape.
        class FnObserver;
        struct Entry {
            ObserverHandle handle;
            ISceneSwapObserver* obs;
            std::shared_ptr<ISceneSwapObserver> owned;   // null for the pointer overload
        };
        std::vector<Entry> observers_;
        ObserverHandle     nextHandle_ = 1;

        std::optional<SceneSwapContext> pending_;
        JobSystem* jobs_ = nullptr;
        // Held only while a prewarmed swap is in flight, and cleared the moment
        // it runs, is superseded or is cancelled.
        //
        // Holding them at all is the point: a handle keeps its model alive, so
        // the cache entry the swap is about to read cannot be collected between
        // the decode finishing and the load asking for it. Holding them any
        // LONGER would pin every model of every scene ever prewarmed.
        std::vector<AssetManager::ModelRequestHandle> prewarm_;
        // Guards re-entry: an observer that requests a swap from inside a hook
        // gets the NEXT frame, not a nested swap.
        bool            swapping_ = false;
        SceneSwapResult last_;
        OnSwapFn        onComplete_;
    };

} // namespace MyCoreEngine
