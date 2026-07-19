// AssetIndex (engine asset-filesystem domain): headless tests — pure
// std::filesystem walking + caching, no GL needed.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "Engine.h"

using MyCoreEngine::AssetIndex;
namespace fs = std::filesystem;

namespace {

const char* kRoot = "asset_index_test_root";

void touch(const fs::path& p) {
    std::ofstream f(p);
    f << "x";
}

class AssetIndexTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::error_code ec;
        fs::remove_all(kRoot, ec);
        fs::create_directories(fs::path(kRoot) / "Model");
        fs::create_directories(fs::path(kRoot) / "Scenes" / "Sub");
        touch(fs::path(kRoot) / "Model" / "a.obj");
        touch(fs::path(kRoot) / "Scenes" / "scene1.json");
        touch(fs::path(kRoot) / "Scenes" / "Sub" / "deep.json");
        touch(fs::path(kRoot) / "project.json"); // excluded from SceneJson
        touch(fs::path(kRoot) / "tex.png");
        touch(fs::path(kRoot) / "note.txt");
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(kRoot, ec);
    }
};

} // namespace

TEST_F(AssetIndexTest, ScanBuildsSortedClassifiedTree) {
    AssetIndex idx(kRoot);
    idx.tick(10.f); // first tick always scans

    const auto& root = idx.root();
    ASSERT_EQ(root.children.size(), 5u);
    // directories first, then files, each A-Z
    EXPECT_EQ(root.children[0].name, "Model");
    EXPECT_EQ(root.children[0].kind, AssetIndex::Kind::Directory);
    EXPECT_EQ(root.children[1].name, "Scenes");
    EXPECT_EQ(root.children[2].name, "note.txt");
    EXPECT_EQ(root.children[2].kind, AssetIndex::Kind::Other);
    EXPECT_EQ(root.children[3].name, "project.json");
    EXPECT_EQ(root.children[3].kind, AssetIndex::Kind::Other) // never a scene
        << "project.json must not be offered as a loadable scene";
    EXPECT_EQ(root.children[4].name, "tex.png");
    EXPECT_EQ(root.children[4].kind, AssetIndex::Kind::Texture);

    // nested content scanned recursively
    const auto& scenes = root.children[1];
    ASSERT_EQ(scenes.children.size(), 2u);
    EXPECT_EQ(scenes.children[0].name, "Sub");
    EXPECT_EQ(scenes.children[1].kind, AssetIndex::Kind::SceneJson);
}

TEST_F(AssetIndexTest, FindResolvesNestedPaths) {
    AssetIndex idx(kRoot);
    idx.tick(10.f);

    ASSERT_NE(idx.find(std::string(kRoot)), nullptr);
    EXPECT_EQ(idx.find(std::string(kRoot)), &idx.root());

    const auto* model = idx.find(std::string(kRoot) + "/Model/a.obj");
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(model->kind, AssetIndex::Kind::Model);

    const auto* deep = idx.find(std::string(kRoot) + "/Scenes/Sub/deep.json");
    ASSERT_NE(deep, nullptr);
    EXPECT_EQ(deep->kind, AssetIndex::Kind::SceneJson);

    EXPECT_EQ(idx.find(std::string(kRoot) + "/Model/missing.obj"), nullptr);
    EXPECT_EQ(idx.find("unrelated/path"), nullptr);
}

TEST_F(AssetIndexTest, VersionBumpsOnlyWhenTheTreeChanges) {
    AssetIndex idx(kRoot);
    idx.tick(10.f);
    const auto v1 = idx.version();
    EXPECT_GT(v1, 0u); // empty -> scanned counts as a change

    idx.forceRescan();
    idx.tick(0.f);
    EXPECT_EQ(idx.version(), v1) << "no disk change: version must hold";

    touch(fs::path(kRoot) / "Model" / "b.obj");
    idx.forceRescan();
    idx.tick(0.f);
    EXPECT_GT(idx.version(), v1);
    EXPECT_NE(idx.find(std::string(kRoot) + "/Model/b.obj"), nullptr);
}

TEST_F(AssetIndexTest, TickThrottlesRescans) {
    AssetIndex idx(kRoot);
    idx.setRescanInterval(1.f);
    idx.tick(10.f); // initial scan

    touch(fs::path(kRoot) / "late.obj");
    idx.tick(0.3f);
    idx.tick(0.3f);
    EXPECT_EQ(idx.find(std::string(kRoot) + "/late.obj"), nullptr)
        << "0.6s elapsed of a 1s interval: no rescan yet";
    idx.tick(0.5f); // 1.1s total
    EXPECT_NE(idx.find(std::string(kRoot) + "/late.obj"), nullptr);
}

TEST_F(AssetIndexTest, AsyncTickScansOnAWorkerAndAdoptsOnPump) {
    MyCoreEngine::JobSystem jobs(2);
    AssetIndex idx(kRoot);

    idx.tick(10.f, &jobs); // walk submitted to a worker
    // the tree only lands via the main-thread completion pump
    jobs.waitIdle();
    EXPECT_TRUE(idx.root().children.empty()) << "tree adopted before the pump ran";
    jobs.pumpCompletions(1e6f);
    ASSERT_EQ(idx.root().children.size(), 5u);
    EXPECT_NE(idx.find(std::string(kRoot) + "/Model/a.obj"), nullptr);
    const auto v1 = idx.version();
    EXPECT_GT(v1, 0u);

    // async rescan with no disk change: version holds
    idx.forceRescan();
    idx.tick(0.f, &jobs);
    jobs.waitIdle();
    jobs.pumpCompletions(1e6f);
    EXPECT_EQ(idx.version(), v1);

    // disk change picked up asynchronously
    touch(fs::path(kRoot) / "async_late.obj");
    idx.forceRescan();
    idx.tick(0.f, &jobs);
    jobs.waitIdle();
    jobs.pumpCompletions(1e6f);
    EXPECT_GT(idx.version(), v1);
    EXPECT_NE(idx.find(std::string(kRoot) + "/async_late.obj"), nullptr);
}

TEST_F(AssetIndexTest, ForceRescanDuringInFlightScanIsNotLost) {
    // ordering contract in rescanAsync_: the in-flight early-return must
    // happen BEFORE the pending flag is consumed, or a Refresh clicked
    // mid-walk silently dies and the pre-Refresh tree is the final state
    MyCoreEngine::JobSystem jobs(2);
    AssetIndex idx(kRoot);

    idx.tick(10.f, &jobs);     // walk #1 launched
    jobs.waitIdle();           // walk done; adoption queued; scan still "in flight"

    idx.forceRescan();
    idx.tick(0.f, &jobs);      // must early-return, NOT consume the pending flag
    EXPECT_EQ(jobs.pendingCompletions(), 1u) << "a second walk launched mid-flight";

    touch(fs::path(kRoot) / "midflight.obj");
    jobs.pumpCompletions(1e6f); // adopts walk #1's PRE-touch tree
    EXPECT_EQ(idx.find(std::string(kRoot) + "/midflight.obj"), nullptr);

    idx.tick(0.f, &jobs);      // the surviving pending flag launches walk #2
    jobs.waitIdle();
    jobs.pumpCompletions(1e6f);
    EXPECT_NE(idx.find(std::string(kRoot) + "/midflight.obj"), nullptr)
        << "the Refresh requested mid-flight was lost";
}

TEST_F(AssetIndexTest, MissingRootYieldsEmptyTreeNotAnError) {
    AssetIndex idx("no_such_dir_asset_index");
    idx.tick(10.f);
    EXPECT_TRUE(idx.root().children.empty());
    EXPECT_EQ(idx.root().kind, AssetIndex::Kind::Directory);
}

TEST_F(AssetIndexTest, ImportSidecarsAreHiddenFromTheTree) {
    touch(fs::path(kRoot) / "tex.png.import"); // per-asset metadata
    AssetIndex idx(kRoot);
    idx.tick(10.f);
    EXPECT_EQ(idx.find(std::string(kRoot) + "/tex.png.import"), nullptr)
        << ".import sidecars are metadata, not browsable assets";
    EXPECT_NE(idx.find(std::string(kRoot) + "/tex.png"), nullptr)
        << "the asset itself must still be listed";
}

TEST_F(AssetIndexTest, HostileFilenamesNeverCrashTheScan) {
    // a name outside the active code page makes path::string() THROW on
    // MSVC (no '?' substitution) — the recurring editor-boot crash class.
    // The scan must survive; whether the entry appears depends on the ACP.
    // Universal-character-name escapes, NOT a Cyrillic source literal: this
    // file has no BOM and the build has no /utf-8, so MSVC decodes raw
    // UTF-8 bytes as cp1252 — the mojibake result IS ACP-representable and
    // the throw path silently never fires (review-caught vacuous test).
    touch(fs::path(kRoot) / L"\u0442\u0435\u0441\u0442.obj"); // Cyrillic "test"
    AssetIndex idx(kRoot);
    ASSERT_NO_THROW(idx.tick(10.f));
    // the well-formed neighbors are still fully indexed
    EXPECT_NE(idx.find(std::string(kRoot) + "/Model/a.obj"), nullptr);
    EXPECT_NE(idx.find(std::string(kRoot) + "/tex.png"), nullptr);
}

TEST_F(AssetIndexTest, FindDistinguishesSiblingPrefixNames) {
    // "Model" must not swallow lookups aimed at its prefix-sibling "Model2"
    fs::create_directories(fs::path(kRoot) / "Model2");
    touch(fs::path(kRoot) / "Model2" / "c.obj");
    AssetIndex idx(kRoot);
    idx.tick(10.f);
    const auto* c = idx.find(std::string(kRoot) + "/Model2/c.obj");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->kind, AssetIndex::Kind::Model);
    EXPECT_NE(idx.find(std::string(kRoot) + "/Model/a.obj"), nullptr);
}
