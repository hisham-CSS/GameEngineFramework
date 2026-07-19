#pragma once
#include "Core.h"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace MyCoreEngine {

    // The engine's thread pool (P4-3 phase 1). Purpose-built for the
    // decode-on-worker / finalize-on-main asset pipeline:
    //
    //   jobs.submit(
    //       [state] { state->cpuData = DecodeModel(state->path); },  // worker
    //       [state] { state->model = FinalizeGL(state->cpuData); }); // main
    //
    // HARD CONTRACT — the reason this class exists at all:
    // - `work` runs on a WORKER thread and must never touch GL, the entt
    //   registry, or ImGui. GL function-pointer tables are per-module and
    //   the context is current on the main thread only; the registry and
    //   ImGui are single-threaded by design.
    // - `onComplete` runs on the MAIN thread, inside pumpCompletions(),
    //   with the GL context current — this is where uploads belong.
    // - Closures OWN their transient state (shared_ptr, like the example
    //   above). Referencing longer-lived objects (the AssetManager that
    //   submitted, an app-owned index) is safe ONLY because of the drain
    //   guarantee: RunLoop finishes all jobs and runs all completions
    //   before returning, while Run() locals and app members are still
    //   alive — and closures destroyed unexecuted at teardown never
    //   dereference anything. Work submitted OUTSIDE RunLoop's lifetime,
    //   or under any future stop()-style teardown, loses that guarantee:
    //   such closures must be fully self-owning.
    //
    // Threading model: fixed worker pool (default ~hardware/3, capped at 4
    // — leave headroom for the driver and OS on the 6c/12t target), one
    // mutex+condvar job queue (contention is per-asset, not per-item; no
    // lock-free heroics needed at this scale), and a completion queue the
    // main loop drains under a per-frame time budget so a burst of
    // finished decodes cannot hitch a frame.
    //
    // Failure containment: an exception escaping `work` is logged and
    // swallowed and the completion STILL runs — closures carry their own
    // success state (e.g. an empty cpuData), so the pipeline keeps moving
    // instead of leaking a permanently-pending request.
    //
    // Shutdown: the destructor stops accepting work, lets in-flight jobs
    // finish, drops jobs that never started, and joins. Completions not
    // yet pumped never run.
    //
    // Planned (NOT implemented — this is the seam): job priorities,
    // dependencies/continuations, parallel_for, cancellation tokens.
    class ENGINE_API JobSystem {
    public:
        using Job = std::function<void()>;
        using Completion = std::function<void()>;

        // Construct on the main thread (it captures the thread id that
        // pumpCompletions/isMainThread treat as "main").
        // workerCount 0 = auto (hardware_concurrency/3, clamped to [1,4]).
        explicit JobSystem(unsigned workerCount = 0);
        ~JobSystem();

        JobSystem(const JobSystem&) = delete;
        JobSystem& operator=(const JobSystem&) = delete;

        // Queue `work` for a worker thread; `onComplete` (optional) runs
        // later on the main thread via pumpCompletions. Safe to call from
        // any thread, including from inside a completion. Dropped silently
        // if the system is shutting down.
        void submit(Job work, Completion onComplete = {});

        // MAIN THREAD ONLY. Run queued completions until `budgetMs`
        // elapses (always at least one when any are pending, so a 0
        // budget still makes progress). Returns how many ran.
        int pumpCompletions(float budgetMs = 2.0f);

        // Block until every submitted job has finished EXECUTING. Their
        // completions may still be queued — pump afterwards. Test/teardown
        // aid; never call it from a worker or a completion.
        void waitIdle();

        unsigned workerCount() const { return (unsigned)workers_.size(); }
        std::size_t pendingJobs() const;        // not yet started
        std::size_t pendingCompletions() const; // finished, awaiting pump
        bool isMainThread() const { return std::this_thread::get_id() == mainThread_; }

    private:
        void workerLoop_();

        std::thread::id mainThread_;
        std::vector<std::thread> workers_;

        mutable std::mutex qMutex_; // guards jobs_, inFlight_, stopping_
        std::condition_variable qCv_;    // workers wait for jobs
        std::condition_variable idleCv_; // waitIdle waits for drain
        std::deque<std::pair<Job, Completion>> jobs_;
        std::size_t inFlight_ = 0;
        bool stopping_ = false;

        mutable std::mutex cMutex_; // guards completions_
        std::deque<Completion> completions_;
    };

} // namespace MyCoreEngine
