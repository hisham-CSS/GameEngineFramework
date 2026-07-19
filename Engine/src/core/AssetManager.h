#pragma once
#include "Core.h"
#include <cstddef>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <mutex>

namespace MyCoreEngine {

    class Model;
    class JobSystem;

    // Model asset manager: dedupe by normalized path.
    //
    // Two ways in:
    // - GetModel: synchronous — decode + GL upload inline on the calling
    //   thread (must be the main thread). Blocks for the whole load; fine
    //   for boot-time and cache hits.
    // - RequestModel: asynchronous (P4-3 phase 3) — returns a handle
    //   immediately, decodes on a JobSystem worker, finalizes GL on the
    //   main thread inside pumpCompletions. In-flight decodes are capped
    //   (kMaxConcurrentDecodes) so a burst of requests can't hold every
    //   model's decoded pixels in memory at once; excess requests queue
    //   and launch as decodes finish.
    //
    // Threading: RequestModel and the async bookkeeping are MAIN THREAD
    // ONLY (states change inside the main-thread completion pump — poll
    // handles from the main loop, never from workers). GetModel keeps the
    // coarse mutex it always had for the shared cache map.
    //
    // Mixing GetModel and RequestModel on the SAME path can duplicate a
    // load (sync can't wait on an in-flight decode) — never wrong (the
    // texture cache still dedupes GPU ids), just wasted CPU. Prefer one
    // style per path.
    class ENGINE_API AssetManager {
    public:
        // Get a shared handle to a GPU-ready Model for this path.
        // If already loaded and still alive, returns the existing instance.
        std::shared_ptr<Model> GetModel(const std::string& path, bool gamma = false);

        // --- async loading -------------------------------------------------
        enum class LoadState {
            Queued,   // waiting for a decode slot
            Decoding, // on a worker (or awaiting the main-thread finalize)
            Live,     // model is GPU-ready (model != nullptr, meshes present)
            Failed,   // import failed: model exists but has no meshes
        };
        // Shared status block for one request. Handles from the same path
        // are the SAME block while the load is in flight (dedupe).
        struct ModelRequest {
            LoadState state = LoadState::Queued;
            std::shared_ptr<Model> model; // set once Live/Failed
            std::string path;             // as requested (engine style)
        };
        using ModelRequestHandle = std::shared_ptr<ModelRequest>;

        // MAIN THREAD ONLY. Returns immediately; the handle's state flips
        // to Live/Failed inside a later pumpCompletions. Cache hits return
        // an already-Live handle. `jobs` must outlive this AssetManager's
        // pending requests (both apps: the Application's pool).
        ModelRequestHandle RequestModel(JobSystem& jobs, const std::string& path,
                                        bool gamma = false);

        // queued + decoding request count (UI "loading…" indicators)
        std::size_t pendingRequests() const { return pending_.size(); }
        // decodes currently on workers (tests; bounded by the cap)
        std::size_t inFlightDecodes() const { return inFlight_; }

        // Bound on concurrent decodes: each in-flight decode holds a whole
        // model's pixels in RAM until its finalize runs (review finding,
        // phase 2) — two keeps the pipeline busy without memory spikes.
        static constexpr std::size_t kMaxConcurrentDecodes = 2;

        // Force reload of a model from disk and replace the cache entry.
        // Existing holders keep their old shared_ptr (you can retarget them manually).
        std::shared_ptr<Model> ReloadModel(const std::string& path, bool gamma = false);

        // Remove expired entries from the cache.
        void GarbageCollect();

        // Clear cache map (dangerous if callers still hold shared_ptrs, but safe: they keep their instances).
        void Clear();

    private:
        static std::string NormalizePath(const std::string& path);
        void launchQueued_(JobSystem& jobs); // start decodes while slots free

        // path -> weak ref (dedupe by path)
        std::unordered_map<std::string, std::weak_ptr<Model>> models_;
        mutable std::mutex mtx_;

        // async state — MAIN THREAD ONLY (mutated in RequestModel and the
        // completion pump; no locks needed, and none taken)
        struct QueuedLoad {
            ModelRequestHandle req;
            std::string rawPath; // pre-normalization, as Decode wants it
            bool gamma = false;
        };
        std::unordered_map<std::string, ModelRequestHandle> pending_; // key -> in-flight/queued
        std::deque<QueuedLoad> queued_;
        std::size_t inFlight_ = 0;
    };

} // namespace MyCoreEngine
