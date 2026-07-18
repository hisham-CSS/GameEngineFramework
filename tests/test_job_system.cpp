// JobSystem (P4-3 phase 1): headless tests — pure threading, no GL.
// Timing-sensitive assertions use generous margins so a loaded machine
// can't flake them.
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include "Engine.h"

using MyCoreEngine::JobSystem;
using namespace std::chrono_literals;

TEST(JobSystem, ExecutesEverySubmittedJob) {
    JobSystem jobs(2);
    std::atomic<int> ran{ 0 };
    for (int i = 0; i < 100; ++i) {
        jobs.submit([&] { ran.fetch_add(1); });
    }
    jobs.waitIdle();
    EXPECT_EQ(ran.load(), 100);
    EXPECT_EQ(jobs.pendingJobs(), 0u);
}

TEST(JobSystem, CompletionsRunOnMainThreadOnlyWhenPumped) {
    JobSystem jobs(2);
    const auto mainId = std::this_thread::get_id();
    ASSERT_TRUE(jobs.isMainThread());

    std::atomic<bool> workerWasMain{ true };
    std::thread::id completionThread{};
    std::atomic<int> completions{ 0 };

    jobs.submit(
        [&] { workerWasMain = jobs.isMainThread(); },
        [&] { completionThread = std::this_thread::get_id(); completions.fetch_add(1); });
    jobs.waitIdle();

    // finished executing, but the completion must WAIT for the pump
    EXPECT_FALSE(workerWasMain.load());
    EXPECT_EQ(completions.load(), 0);
    EXPECT_EQ(jobs.pendingCompletions(), 1u);

    EXPECT_EQ(jobs.pumpCompletions(10.f), 1);
    EXPECT_EQ(completions.load(), 1);
    EXPECT_EQ(completionThread, mainId);
    EXPECT_EQ(jobs.pendingCompletions(), 0u);
}

TEST(JobSystem, PumpBudgetStopsEarlyButAlwaysMakesProgress) {
    JobSystem jobs(2);
    std::atomic<int> ran{ 0 };
    for (int i = 0; i < 20; ++i) {
        jobs.submit([] {}, [&] {
            std::this_thread::sleep_for(5ms);
            ran.fetch_add(1);
        });
    }
    jobs.waitIdle();
    ASSERT_EQ(jobs.pendingCompletions(), 20u);

    // 1ms budget vs 5ms completions: at least one runs, the burst doesn't
    const int first = jobs.pumpCompletions(1.f);
    EXPECT_GE(first, 1);
    EXPECT_LT(first, 20);
    EXPECT_GT(jobs.pendingCompletions(), 0u);

    // repeated pumps drain the rest
    int total = first;
    while (jobs.pendingCompletions() > 0) total += jobs.pumpCompletions(50.f);
    EXPECT_EQ(total, 20);
    EXPECT_EQ(ran.load(), 20);
}

TEST(JobSystem, WaitIdleBlocksUntilInFlightWorkFinishes) {
    JobSystem jobs(1);
    std::atomic<bool> finished{ false };
    jobs.submit([&] {
        std::this_thread::sleep_for(50ms);
        finished = true;
    });
    jobs.waitIdle();
    EXPECT_TRUE(finished.load()) << "waitIdle returned while a job was still executing";
}

TEST(JobSystem, WorkersActuallyRunInParallel) {
    JobSystem jobs(4);
    ASSERT_EQ(jobs.workerCount(), 4u);

    // all four jobs rendezvous: each asserts it SAW all four running at
    // once (wait_for's boolean, not the counter afterwards — the counter
    // alone reaches 4 even fully serialized, one timeout at a time)
    std::mutex m;
    std::condition_variable cv;
    int running = 0;
    std::atomic<int> sawAllFour{ 0 };
    for (int i = 0; i < 4; ++i) {
        jobs.submit([&] {
            std::unique_lock<std::mutex> lk(m);
            ++running;
            cv.notify_all();
            if (cv.wait_for(lk, 5s, [&] { return running >= 4; })) {
                sawAllFour.fetch_add(1);
            }
        });
    }
    jobs.waitIdle();
    EXPECT_EQ(sawAllFour.load(), 4)
        << "fewer than four workers overlapped within the 5s rendezvous window";
}

TEST(JobSystem, ConcurrentSubmitAndPumpUnderContention) {
    // the production interleaving: multiple threads submitting while
    // workers execute and the main thread pumps — no waitIdle first
    JobSystem jobs(4);
    constexpr int kThreads = 4, kPerThread = 50, kTotal = kThreads * kPerThread;
    std::atomic<int> completions{ 0 };
    std::atomic<int> completionsOffMain{ 0 };

    std::vector<std::thread> submitters;
    for (int t = 0; t < kThreads; ++t) {
        submitters.emplace_back([&] {
            for (int i = 0; i < kPerThread; ++i) {
                jobs.submit(
                    [] { std::this_thread::sleep_for(std::chrono::microseconds(200)); },
                    [&] {
                        if (!jobs.isMainThread()) completionsOffMain.fetch_add(1);
                        completions.fetch_add(1);
                    });
            }
        });
    }

    // pump WHILE submitters and workers run; bail out on wall clock so a
    // regression fails visibly instead of hanging
    const auto deadline = std::chrono::steady_clock::now() + 30s;
    while (completions.load() < kTotal &&
           std::chrono::steady_clock::now() < deadline) {
        jobs.pumpCompletions(1.f);
        std::this_thread::yield();
    }
    for (auto& t : submitters) t.join();
    jobs.waitIdle();
    jobs.pumpCompletions(1e6f);

    EXPECT_EQ(completions.load(), kTotal) << "lost completions under contention";
    EXPECT_EQ(completionsOffMain.load(), 0) << "a completion ran off the main thread";
}

TEST(JobSystem, ExceptionInJobIsContainedAndCompletionStillRuns) {
    JobSystem jobs(2);
    std::atomic<bool> completionRan{ false };
    jobs.submit([] { throw std::runtime_error("decode failed"); },
                [&] { completionRan = true; });
    jobs.waitIdle();
    jobs.pumpCompletions(10.f);
    EXPECT_TRUE(completionRan.load())
        << "a throwing job must still surface through its completion";

    // the pool survives and keeps working
    std::atomic<bool> ranAfter{ false };
    jobs.submit([&] { ranAfter = true; });
    jobs.waitIdle();
    EXPECT_TRUE(ranAfter.load());
}

TEST(JobSystem, CompletionCanSubmitFollowUpWork) {
    JobSystem jobs(2);
    std::atomic<int> stage{ 0 };
    jobs.submit([] {}, [&] {
        stage = 1;
        jobs.submit([&] { stage = 2; }, [&] { stage = 3; });
    });
    jobs.waitIdle();
    jobs.pumpCompletions(10.f); // runs stage-1 completion, submits follow-up
    jobs.waitIdle();            // follow-up job executes
    jobs.pumpCompletions(10.f); // follow-up completion
    EXPECT_EQ(stage.load(), 3);
}

TEST(JobSystem, DestructorJoinsCleanlyAndNeverRunsUnpumpedCompletions) {
    std::atomic<int> ran{ 0 };
    std::atomic<int> completionsRan{ 0 };
    {
        JobSystem jobs(2);
        for (int i = 0; i < 50; ++i) {
            jobs.submit(
                [&] {
                    std::this_thread::sleep_for(1ms);
                    ran.fetch_add(1);
                },
                [&] { completionsRan.fetch_add(1); });
        }
        // destroy immediately: in-flight jobs finish, queued ones may drop
    }
    // the shutdown CONTRACT phase 2's GL-upload completions depend on:
    // completions that were never pumped must never run — not on a worker,
    // not inline in the destructor
    EXPECT_EQ(completionsRan.load(), 0)
        << "an unpumped completion executed during shutdown";
    EXPECT_LE(ran.load(), 50); // whatever started, finished; queued may drop
}

TEST(JobSystem, AutoWorkerCountIsBoundedAndNonZero) {
    JobSystem jobs; // auto
    EXPECT_GE(jobs.workerCount(), 1u);
    EXPECT_LE(jobs.workerCount(), 4u);
}
