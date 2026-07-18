#include "JobSystem.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace MyCoreEngine {

    JobSystem::JobSystem(unsigned workerCount)
        : mainThread_(std::this_thread::get_id())
    {
        if (workerCount == 0) {
            const unsigned hc = std::thread::hardware_concurrency(); // may be 0
            workerCount = std::clamp(hc / 3u, 1u, 4u);
        }
        workers_.reserve(workerCount);
        for (unsigned i = 0; i < workerCount; ++i) {
            workers_.emplace_back([this] { workerLoop_(); });
        }
    }

    JobSystem::~JobSystem()
    {
        {
            std::lock_guard<std::mutex> lk(qMutex_);
            stopping_ = true;
        }
        qCv_.notify_all();
        for (auto& t : workers_) {
            if (t.joinable()) t.join();
        }
    }

    void JobSystem::submit(Job work, Completion onComplete)
    {
        {
            std::lock_guard<std::mutex> lk(qMutex_);
            if (stopping_) return; // shutting down: dropped (documented)
            jobs_.emplace_back(std::move(work), std::move(onComplete));
        }
        qCv_.notify_one();
    }

    void JobSystem::workerLoop_()
    {
        for (;;) {
            std::pair<Job, Completion> item;
            {
                std::unique_lock<std::mutex> lk(qMutex_);
                qCv_.wait(lk, [this] { return stopping_ || !jobs_.empty(); });
                if (stopping_) return; // pending jobs are dropped on shutdown
                item = std::move(jobs_.front());
                jobs_.pop_front();
                ++inFlight_;
            }

            try {
                if (item.first) item.first();
            }
            catch (const std::exception& e) {
                std::fprintf(stderr, "[JobSystem] job threw: %s\n", e.what());
            }
            catch (...) {
                std::fprintf(stderr, "[JobSystem] job threw (non-std exception)\n");
            }

            // completion becomes visible BEFORE the in-flight count drops:
            // after waitIdle() returns, every finished job's completion is
            // already queued for the pump
            if (item.second) {
                std::lock_guard<std::mutex> lk(cMutex_);
                completions_.push_back(std::move(item.second));
            }
            {
                std::lock_guard<std::mutex> lk(qMutex_);
                --inFlight_;
            }
            idleCv_.notify_all();
        }
    }

    int JobSystem::pumpCompletions(float budgetMs)
    {
        using clock = std::chrono::steady_clock;
        const auto deadline = clock::now() +
            std::chrono::duration_cast<clock::duration>(
                std::chrono::duration<float, std::milli>(budgetMs > 0.f ? budgetMs : 0.f));

        int ran = 0;
        for (;;) {
            Completion c;
            {
                std::lock_guard<std::mutex> lk(cMutex_);
                if (completions_.empty()) break;
                c = std::move(completions_.front());
                completions_.pop_front();
            }
            // executed OUTSIDE the lock: a completion may submit follow-up
            // jobs or queue further completions without deadlocking
            try {
                c();
            }
            catch (const std::exception& e) {
                std::fprintf(stderr, "[JobSystem] completion threw: %s\n", e.what());
            }
            catch (...) {
                std::fprintf(stderr, "[JobSystem] completion threw (non-std exception)\n");
            }
            ++ran;
            if (clock::now() >= deadline) break; // ≥1 ran even on a 0 budget
        }
        return ran;
    }

    void JobSystem::waitIdle()
    {
        std::unique_lock<std::mutex> lk(qMutex_);
        idleCv_.wait(lk, [this] { return jobs_.empty() && inFlight_ == 0; });
    }

    std::size_t JobSystem::pendingJobs() const
    {
        std::lock_guard<std::mutex> lk(qMutex_);
        return jobs_.size();
    }

    std::size_t JobSystem::pendingCompletions() const
    {
        std::lock_guard<std::mutex> lk(cMutex_);
        return completions_.size();
    }

} // namespace MyCoreEngine
