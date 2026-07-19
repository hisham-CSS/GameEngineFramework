// AssetManager::RequestModel (P4-3 phase 3): headless tests. Valid models
// need GL to finalize, so the Live path lives in test_scene_details; here
// we pin everything reachable with FAILED loads (missing files): state
// transitions, dedupe, the decode-concurrency cap, and queue accounting.
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "Engine.h"

using namespace MyCoreEngine;
using LoadState = AssetManager::LoadState;

namespace {

void pumpUntilDone(JobSystem& jobs, AssetManager& assets) {
    // decode jobs may launch follow-up jobs from completions: loop until
    // the manager reports quiet (bounded by the request count, no clocks)
    for (int i = 0; i < 1000 && assets.pendingRequests() > 0; ++i) {
        jobs.waitIdle();
        jobs.pumpCompletions(1e6f);
    }
}

} // namespace

TEST(AssetManagerAsync, MissingFileEndsFailedWithEmptyModel) {
    JobSystem jobs(2);
    AssetManager assets;

    auto req = assets.RequestModel(jobs, "no_such_model_async.obj");
    ASSERT_TRUE(req != nullptr);
    EXPECT_TRUE(req->state == LoadState::Queued || req->state == LoadState::Decoding);
    EXPECT_EQ(assets.pendingRequests(), 1u);

    pumpUntilDone(jobs, assets);
    EXPECT_EQ(req->state, LoadState::Failed);
    ASSERT_TRUE(req->model != nullptr); // parity with sync: empty model, not null
    EXPECT_TRUE(req->model->Meshes().empty());
    EXPECT_EQ(req->model->SourcePath(), "no_such_model_async.obj");
    EXPECT_EQ(assets.pendingRequests(), 0u);
}

TEST(AssetManagerAsync, SamePathSharesOneRequest) {
    JobSystem jobs(2);
    AssetManager assets;

    auto a = assets.RequestModel(jobs, "shared_missing.obj");
    auto b = assets.RequestModel(jobs, "shared_missing.obj");
    auto c = assets.RequestModel(jobs, "Shared_Missing.OBJ"); // normalized dedupe
    EXPECT_EQ(a.get(), b.get());
    EXPECT_EQ(a.get(), c.get());
    EXPECT_EQ(assets.pendingRequests(), 1u);

    pumpUntilDone(jobs, assets);
    EXPECT_EQ(a->state, LoadState::Failed);
}

TEST(AssetManagerAsync, DecodeCapQueuesTheRest) {
    JobSystem jobs(4); // more workers than the cap: the cap must gate, not the pool
    AssetManager assets;

    std::vector<AssetManager::ModelRequestHandle> reqs;
    for (int i = 0; i < 5; ++i) {
        reqs.push_back(assets.RequestModel(jobs, "cap_missing_" + std::to_string(i) + ".obj"));
    }
    // launch bookkeeping is main-thread-only and synchronous: right after
    // the requests, at most kMaxConcurrentDecodes are past Queued
    EXPECT_EQ(assets.pendingRequests(), 5u);
    EXPECT_LE(assets.inFlightDecodes(), AssetManager::kMaxConcurrentDecodes);
    int decoding = 0, queuedCount = 0;
    for (const auto& r : reqs) {
        if (r->state == LoadState::Decoding) ++decoding;
        if (r->state == LoadState::Queued) ++queuedCount;
    }
    EXPECT_LE(decoding, (int)AssetManager::kMaxConcurrentDecodes);
    EXPECT_GE(queuedCount, 3); // 5 requests, cap 2: at least 3 must wait

    pumpUntilDone(jobs, assets);
    for (const auto& r : reqs) {
        EXPECT_EQ(r->state, LoadState::Failed);
        ASSERT_TRUE(r->model != nullptr);
    }
    EXPECT_EQ(assets.pendingRequests(), 0u);
    EXPECT_EQ(assets.inFlightDecodes(), 0u);
}

TEST(AssetManagerAsync, RunLoopStyleDrainConvergesThroughChainedLaunches) {
    // completions chain-submit (a finished decode launches the next queued
    // one) — the RunLoop exit drain must LOOP until a pump runs zero
    // completions, or it returns with workers still decoding. This pins
    // the exact drain form Application::RunLoop uses.
    JobSystem jobs(2);
    AssetManager assets;
    std::vector<AssetManager::ModelRequestHandle> reqs;
    for (int i = 0; i < 5; ++i) { // > kMaxConcurrentDecodes: chaining required
        reqs.push_back(assets.RequestModel(jobs, "drain_missing_" + std::to_string(i) + ".obj"));
    }

    do {
        jobs.waitIdle();
    } while (jobs.pumpCompletions(1e6f) > 0);

    for (const auto& r : reqs) {
        EXPECT_EQ(r->state, LoadState::Failed) << "a chained load never completed";
    }
    EXPECT_EQ(assets.pendingRequests(), 0u);
    EXPECT_EQ(assets.inFlightDecodes(), 0u);
    EXPECT_EQ(jobs.pendingJobs(), 0u);
    EXPECT_EQ(jobs.pendingCompletions(), 0u);
}

TEST(AssetManagerAsync, FailedResultIsCachedForLaterRequests) {
    JobSystem jobs(2);
    AssetManager assets;

    auto first = assets.RequestModel(jobs, "cached_missing.obj");
    pumpUntilDone(jobs, assets);
    ASSERT_EQ(first->state, LoadState::Failed);

    // the (empty) model is cached: a re-request resolves instantly with the
    // same instance and no new job
    auto second = assets.RequestModel(jobs, "cached_missing.obj");
    EXPECT_EQ(second->state, LoadState::Failed);
    EXPECT_EQ(second->model.get(), first->model.get());
    EXPECT_EQ(assets.pendingRequests(), 0u);
}
