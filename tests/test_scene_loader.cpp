// Runtime scene replacement: the deferral, the swap contract, and the failure
// semantics. Pure CPU — no GL context, because none of this paints.
//
// The two properties everything here defends:
//
//  - a swap NEVER runs inline. A menu button's handler runs inside
//    UIWorld::Update, inside the UI render pass, between BeginScreen and End;
//    clearing the registry there would leave the UI iterating a tree whose
//    entities just vanished. So RequestSwap records, and the drain runs at a
//    frame boundary.
//  - teardown happens while the outgoing entities are still ALIVE, and rebuild
//    happens after the new ones exist AND after UpdateTransforms. A script's
//    OnDestroy against a cleared registry silently no-ops; a physics body built
//    before UpdateTransforms lands at the origin at unit size.
#include <gtest/gtest.h>

#include "Engine.h"
#include "../Engine/src/core/SceneLoader.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace MyCoreEngine;

namespace {

// Records the order and the timing of every hook, and can see the registry at
// each — which is the whole point of the two-hook split.
struct Spy : ISceneSwapObserver {
    std::string name;
    std::vector<std::string>* log = nullptr;
    bool allow = true;
    std::string refuseReason;
    // What the registry held when each hook ran.
    int entitiesAtUnload = -1;
    int entitiesAtDidLoad = -1;
    SceneLoader* requestFromHook = nullptr;   // re-entrancy probe

    static int count(Scene& s) {
        int n = 0;
        for (auto e : s.registry.view<entt::entity>()) { (void)e; ++n; }
        return n;
    }
    bool AllowSceneSwap(const SceneSwapContext&, std::string& reason) override {
        if (log) log->push_back(name + ":allow");
        if (!allow) reason = refuseReason;
        return allow;
    }
    void OnSceneWillUnload(Scene& s, const SceneSwapContext&) override {
        if (log) log->push_back(name + ":unload");
        entitiesAtUnload = count(s);
    }
    void OnSceneDidLoad(Scene& s, const SceneSwapContext&) override {
        if (log) log->push_back(name + ":load");
        entitiesAtDidLoad = count(s);
        if (requestFromHook) requestFromHook->RequestSwap("test_loader_b.json",
                                                          SceneSwapOrigin::Game);
    }
};

// Writes a minimal but real scene file with `n` named entities.
std::string writeScene(const char* path, int n) {
    std::string s = R"({"version":1,"entities":[)";
    for (int i = 0; i < n; ++i) {
        if (i) s += ",";
        s += R"({"name":"E)" + std::to_string(i) + R"("})";
    }
    s += "]}";
    std::ofstream(path) << s;
    return path;
}

int namedCount(Scene& s) {
    int n = 0;
    for (auto e : s.registry.view<Name>()) { (void)e; ++n; }
    return n;
}

} // namespace

// ------------------------------------------------------------ the deferral

// The single most important property: a request from inside a frame does not
// touch the registry until somebody drains it.
TEST(SceneLoader, ARequestDoesNotLoadAnythingUntilItIsDrained) {
    writeScene("test_loader_a.json", 3);
    AssetManager assets;
    Scene scene;
    scene.createEntity().addComponent<Name>(Name{ "Original" });

    SceneLoader loader(scene, assets);
    ASSERT_TRUE(loader.RequestSwap("test_loader_a.json"));
    EXPECT_TRUE(loader.swapPending());
    EXPECT_EQ(namedCount(scene), 1) << "RequestSwap loaded inline - a UI handler "
                                       "would have had the registry pulled out from under it";

    EXPECT_TRUE(loader.DrainPendingSwap());
    EXPECT_FALSE(loader.swapPending());
    EXPECT_EQ(namedCount(scene), 3);
    std::remove("test_loader_a.json");
}

TEST(SceneLoader, DrainingWithNothingPendingDoesNothingAndSaysSo) {
    AssetManager assets;
    Scene scene;
    SceneLoader loader(scene, assets);
    EXPECT_FALSE(loader.DrainPendingSwap());
}

// A menu that fires twice — a double click, or a click and a keypress — must
// load the last one, not both.
TEST(SceneLoader, ASecondRequestSupersedesTheFirstRatherThanQueueing) {
    writeScene("test_loader_a.json", 2);
    writeScene("test_loader_b.json", 5);
    AssetManager assets;
    Scene scene;
    SceneLoader loader(scene, assets);

    std::vector<SceneSwapStatus> seen;
    loader.SetOnSwapComplete([&](const SceneSwapResult& r) { seen.push_back(r.status); });

    loader.RequestSwap("test_loader_a.json");
    loader.RequestSwap("test_loader_b.json");
    EXPECT_EQ(loader.pendingPath(), "test_loader_b.json");
    ASSERT_EQ(seen.size(), 1u) << "the superseded request never reported";
    EXPECT_EQ(seen[0], SceneSwapStatus::Superseded);

    EXPECT_TRUE(loader.DrainPendingSwap());
    EXPECT_EQ(namedCount(scene), 5);
    EXPECT_FALSE(loader.DrainPendingSwap()) << "the superseded request also ran";
    std::remove("test_loader_a.json");
    std::remove("test_loader_b.json");
}

// ------------------------------------------------------- failure semantics

// The ordinary bad-path case — a typo, a deleted file — must be completely
// inert. Catching it at REQUEST time is what makes that true: by drain time the
// outgoing scene's subsystems have already been torn down.
TEST(SceneLoader, AnInvalidFileIsRefusedAtRequestTimeWithNothingTouched) {
    std::ofstream("test_loader_bad.json") << R"({"version":1,"entities":[{"name":42}]})";
    AssetManager assets;
    Scene scene;
    scene.createEntity().addComponent<Name>(Name{ "Precious" });

    SceneLoader loader(scene, assets);
    Spy spy; spy.name = "spy";
    loader.AddObserver(&spy);

    EXPECT_FALSE(loader.RequestSwap("test_loader_bad.json"));
    EXPECT_FALSE(loader.swapPending());
    EXPECT_EQ(loader.lastResult().status, SceneSwapStatus::Invalid);
    EXPECT_EQ(namedCount(scene), 1) << "a bad file destroyed the live scene";
    EXPECT_EQ(spy.entitiesAtUnload, -1) << "teardown ran for a swap that never happened";
    std::remove("test_loader_bad.json");
}

TEST(SceneLoader, AMissingFileIsRefusedAtRequestTime) {
    AssetManager assets;
    Scene scene;
    SceneLoader loader(scene, assets);
    EXPECT_FALSE(loader.RequestSwap("test_loader_no_such_file_98765.json"));
    EXPECT_EQ(loader.lastResult().status, SceneSwapStatus::Invalid);
}

// ------------------------------------------------------- the swap contract

// The two-hook split is the whole contract, and this is what makes it real:
// teardown must SEE the outgoing entities, rebuild must see the incoming ones.
TEST(SceneLoader, TeardownSeesTheOldEntitiesAndRebuildSeesTheNewOnes) {
    writeScene("test_loader_a.json", 4);
    AssetManager assets;
    Scene scene;
    for (int i = 0; i < 2; ++i) scene.createEntity().addComponent<Name>(Name{ "old" });

    SceneLoader loader(scene, assets);
    Spy spy; spy.name = "spy";
    loader.AddObserver(&spy);

    ASSERT_TRUE(loader.RequestSwap("test_loader_a.json"));
    ASSERT_TRUE(loader.DrainPendingSwap());

    EXPECT_EQ(spy.entitiesAtUnload, 2)
        << "teardown ran against an already-cleared registry - every OnDestroy "
           "in a script would have silently no-opped";
    EXPECT_EQ(spy.entitiesAtDidLoad, 4) << "rebuild ran before the new entities existed";
    std::remove("test_loader_a.json");
}

TEST(SceneLoader, ObserversAreNotifiedInRegistrationOrderInBothDirections) {
    writeScene("test_loader_a.json", 1);
    AssetManager assets;
    Scene scene;
    SceneLoader loader(scene, assets);

    std::vector<std::string> log;
    Spy a, b, c;
    a.name = "a"; b.name = "b"; c.name = "c";
    a.log = b.log = c.log = &log;
    loader.AddObserver(&a);
    loader.AddObserver(&b);
    loader.AddObserver(&c);

    ASSERT_TRUE(loader.RequestSwap("test_loader_a.json"));
    ASSERT_TRUE(loader.DrainPendingSwap());

    const std::vector<std::string> want = {
        "a:allow", "b:allow", "c:allow",
        "a:unload", "b:unload", "c:unload",
        "a:load",   "b:load",   "c:load",
    };
    EXPECT_EQ(log, want);
    std::remove("test_loader_a.json");
}

// The editor refuses game-originated swaps in edit mode: its Game panel
// dispatches the game's UI clicks even while stopped, so a menu button could
// otherwise replace the scene somebody is editing.
TEST(SceneLoader, AVetoStopsTheSwapBeforeAnythingIsTornDown) {
    writeScene("test_loader_a.json", 4);
    AssetManager assets;
    Scene scene;
    scene.createEntity().addComponent<Name>(Name{ "Editing" });

    SceneLoader loader(scene, assets);
    Spy gate; gate.name = "gate";
    gate.allow = false;
    gate.refuseReason = "not while editing";
    loader.AddObserver(&gate);

    ASSERT_TRUE(loader.RequestSwap("test_loader_a.json", SceneSwapOrigin::Game));
    EXPECT_FALSE(loader.DrainPendingSwap());
    EXPECT_EQ(loader.lastResult().status, SceneSwapStatus::Refused);
    EXPECT_EQ(loader.lastResult().message, "not while editing");
    EXPECT_EQ(namedCount(scene), 1) << "a refused swap still replaced the scene";
    EXPECT_EQ(gate.entitiesAtUnload, -1) << "a refused swap still tore the scene down";
    std::remove("test_loader_a.json");
}

TEST(SceneLoader, TheOriginReachesTheObserverSoAHostCanRefuseOnlyGameSwaps) {
    writeScene("test_loader_a.json", 1);
    AssetManager assets;
    Scene scene;
    SceneLoader loader(scene, assets);

    struct OriginSpy : ISceneSwapObserver {
        SceneSwapOrigin seen{};
        bool AllowSceneSwap(const SceneSwapContext& c, std::string&) override {
            seen = c.origin;
            return true;
        }
    } spy;
    loader.AddObserver(&spy);

    loader.RequestSwap("test_loader_a.json", SceneSwapOrigin::Host);
    loader.DrainPendingSwap();
    EXPECT_EQ(spy.seen, SceneSwapOrigin::Host);
    std::remove("test_loader_a.json");
}

// An observer that requests a swap from inside a hook must get the NEXT frame,
// not a nested swap that unwinds the loop it is standing in.
TEST(SceneLoader, ASwapRequestedFromInsideAHookRunsOnTheNextFrameNotNested) {
    writeScene("test_loader_a.json", 2);
    writeScene("test_loader_b.json", 6);
    AssetManager assets;
    Scene scene;
    SceneLoader loader(scene, assets);

    Spy spy; spy.name = "spy";
    spy.requestFromHook = &loader;
    loader.AddObserver(&spy);

    ASSERT_TRUE(loader.RequestSwap("test_loader_a.json"));
    ASSERT_TRUE(loader.DrainPendingSwap());
    EXPECT_EQ(namedCount(scene), 2) << "the nested request ran inside the first swap";
    EXPECT_TRUE(loader.swapPending()) << "the nested request was dropped";

    spy.requestFromHook = nullptr;   // one re-entry is enough
    ASSERT_TRUE(loader.DrainPendingSwap());
    EXPECT_EQ(namedCount(scene), 6);
    std::remove("test_loader_a.json");
    std::remove("test_loader_b.json");
}

// ------------------------------------------------------------- the report

// A scene whose models are all missing loads SUCCESSFULLY and renders nothing.
// The bool cannot express that; the report has to.
TEST(SceneLoader, AMissingModelIsReportedEvenThoughTheLoadSucceeds) {
    std::ofstream("test_loader_model.json")
        << R"({"version":1,"entities":[{"name":"A","model":"Exported/no_such_model.obj"}]})";
    AssetManager assets;
    Scene scene;
    SceneLoader loader(scene, assets);

    ASSERT_TRUE(loader.RequestSwap("test_loader_model.json"));
    ASSERT_TRUE(loader.DrainPendingSwap());

    const SceneSwapResult& r = loader.lastResult();
    EXPECT_TRUE(r.ok()) << "a missing model should not fail the load";
    EXPECT_FALSE(r.report.complete())
        << "the scene reported itself complete while its only model is missing";
    ASSERT_EQ(r.report.failedModels.size(), 1u);
    EXPECT_EQ(r.report.failedModels[0], "Exported/no_such_model.obj");
    EXPECT_EQ(r.report.entitiesCreated, 1);
    std::remove("test_loader_model.json");
}

// An authored path outside the project is refused by the sandbox, and that is a
// different failure from "the file was not there" — an author needs to know
// which.
TEST(SceneLoader, ASandboxedModelPathIsReportedSeparatelyFromAMissingOne) {
    std::ofstream("test_loader_model.json")
        << R"({"version":1,"entities":[{"name":"A","model":"../../evil.obj"}]})";
    AssetManager assets;
    Scene scene;
    SceneLoader loader(scene, assets);

    ASSERT_TRUE(loader.RequestSwap("test_loader_model.json"));
    ASSERT_TRUE(loader.DrainPendingSwap());

    const SceneSwapResult& r = loader.lastResult();
    EXPECT_TRUE(r.ok());
    ASSERT_EQ(r.report.rejectedModels.size(), 1u);
    EXPECT_TRUE(r.report.failedModels.empty()) << "a rejection was double-counted as a failure";
    std::remove("test_loader_model.json");
}

TEST(SceneLoader, ACleanSceneReportsItselfComplete) {
    writeScene("test_loader_a.json", 3);
    AssetManager assets;
    Scene scene;
    SceneLoader loader(scene, assets);
    ASSERT_TRUE(loader.RequestSwap("test_loader_a.json"));
    ASSERT_TRUE(loader.DrainPendingSwap());
    EXPECT_TRUE(loader.lastResult().report.complete());
    EXPECT_EQ(loader.lastResult().report.entitiesCreated, 3);
    std::remove("test_loader_a.json");
}

// ------------------------------------------------------------- observers

TEST(SceneLoader, ARemovedObserverIsNotNotified) {
    writeScene("test_loader_a.json", 1);
    AssetManager assets;
    Scene scene;
    SceneLoader loader(scene, assets);

    std::vector<std::string> log;
    Spy a, b;
    a.name = "a"; b.name = "b";
    a.log = b.log = &log;
    const auto ha = loader.AddObserver(&a);
    loader.AddObserver(&b);
    loader.RemoveObserver(ha);

    loader.RequestSwap("test_loader_a.json");
    loader.DrainPendingSwap();
    for (const std::string& e : log) {
        EXPECT_EQ(e.rfind("a:", 0), std::string::npos) << "a removed observer was notified";
    }
    EXPECT_FALSE(log.empty());
    std::remove("test_loader_a.json");
}

TEST(SceneLoader, CancellingAPendingSwapLeavesTheSceneAlone) {
    writeScene("test_loader_a.json", 9);
    AssetManager assets;
    Scene scene;
    scene.createEntity().addComponent<Name>(Name{ "Stay" });
    SceneLoader loader(scene, assets);

    ASSERT_TRUE(loader.RequestSwap("test_loader_a.json"));
    loader.CancelPendingSwap();
    EXPECT_FALSE(loader.swapPending());
    EXPECT_FALSE(loader.DrainPendingSwap());
    EXPECT_EQ(namedCount(scene), 1);
    std::remove("test_loader_a.json");
}
