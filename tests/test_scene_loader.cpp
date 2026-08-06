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
#include "../Engine/src/core/JobSystem.h"

#include <cstdio>
#include <chrono>
#include <fstream>
#include <thread>
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

// ------------------------------------------- the function-observer overload

// What every Install* helper uses. The loader OWNS the adapter: the caller
// keeps nothing, so a subsystem that installs and forgets is still correctly
// torn down.
TEST(SceneLoader, AFunctionObserverFiresInBothPhasesAndIsOwnedByTheLoader) {
    writeScene("test_loader_a.json", 4);
    AssetManager assets;
    Scene scene;
    scene.createEntity().addComponent<Name>(Name{ "Original" });
    SceneLoader loader(scene, assets);

    int atUnload = -1, atLoad = -1;
    loader.AddObserver([&](Scene& s) { atUnload = namedCount(s); },
                       [&](Scene& s) { atLoad   = namedCount(s); });

    loader.RequestSwap("test_loader_a.json");
    ASSERT_TRUE(loader.DrainPendingSwap());
    EXPECT_EQ(atUnload, 1) << "teardown ran after the old entities were gone";
    EXPECT_EQ(atLoad, 4);
    std::remove("test_loader_a.json");
}

// The shape the physics/script/audio installs actually use: teardown only.
// The did-load argument is defaulted, and an empty std::function must be
// skipped rather than called.
TEST(SceneLoader, AFunctionObserverWithNoDidLoadHookIsSafeToDrain) {
    writeScene("test_loader_a.json", 2);
    AssetManager assets;
    Scene scene;
    SceneLoader loader(scene, assets);

    int cleared = 0;
    loader.AddObserver([&](Scene&) { ++cleared; });

    loader.RequestSwap("test_loader_a.json");
    ASSERT_TRUE(loader.DrainPendingSwap());
    EXPECT_EQ(cleared, 1);
    std::remove("test_loader_a.json");
}

// A function observer never vetoes. Otherwise installing physics would give
// every subsystem an accidental say over whether the game may change scene.
TEST(SceneLoader, AFunctionObserverNeverVetoesTheSwap) {
    writeScene("test_loader_a.json", 2);
    AssetManager assets;
    Scene scene;
    SceneLoader loader(scene, assets);
    loader.AddObserver([](Scene&) {}, [](Scene&) {});

    loader.RequestSwap("test_loader_a.json", SceneSwapOrigin::Game);
    ASSERT_TRUE(loader.DrainPendingSwap());
    EXPECT_EQ(loader.lastResult().status, SceneSwapStatus::Ok);
    std::remove("test_loader_a.json");
}

// The editor registers a pointer observer (its edit-mode gate) and a lambda
// (its own resets) into the same list. Order has to hold across both kinds.
TEST(SceneLoader, FunctionAndPointerObserversShareOneRegistrationOrder) {
    writeScene("test_loader_a.json", 1);
    AssetManager assets;
    Scene scene;
    SceneLoader loader(scene, assets);

    std::vector<std::string> log;
    Spy first; first.name = "ptr"; first.log = &log;
    loader.AddObserver(&first);
    loader.AddObserver([&](Scene&) { log.push_back("fn:unload"); },
                       [&](Scene&) { log.push_back("fn:load"); });
    Spy last; last.name = "ptr2"; last.log = &log;
    loader.AddObserver(&last);

    loader.RequestSwap("test_loader_a.json");
    ASSERT_TRUE(loader.DrainPendingSwap());
    const std::vector<std::string> want{
        "ptr:allow", "ptr2:allow",
        "ptr:unload", "fn:unload", "ptr2:unload",
        "ptr:load",   "fn:load",   "ptr2:load",
    };
    EXPECT_EQ(log, want);
    std::remove("test_loader_a.json");
}

// The owned adapter has to come out of the list with its handle like any
// other observer, and stop firing.
TEST(SceneLoader, ARemovedFunctionObserverStopsFiring) {
    writeScene("test_loader_a.json", 1);
    AssetManager assets;
    Scene scene;
    SceneLoader loader(scene, assets);

    int fired = 0;
    const auto h = loader.AddObserver([&](Scene&) { ++fired; });
    loader.RequestSwap("test_loader_a.json");
    ASSERT_TRUE(loader.DrainPendingSwap());
    ASSERT_EQ(fired, 1);

    loader.RemoveObserver(h);
    loader.RequestSwap("test_loader_a.json");
    ASSERT_TRUE(loader.DrainPendingSwap());
    EXPECT_EQ(fired, 1) << "a removed function observer still ran";
    std::remove("test_loader_a.json");
}

// The editor's own gate, in miniature: Host swaps always pass, Game swaps are
// refused while stopped. Proves the origin is enough to build the policy on
// without the loader knowing anything about play modes.
TEST(SceneLoader, AnEditModeGateCanRefuseGameSwapsAndStillAllowHostOnes) {
    writeScene("test_loader_a.json", 5);
    AssetManager assets;
    Scene scene;
    SceneLoader loader(scene, assets);

    bool playing = false;
    struct Gate : ISceneSwapObserver {
        const bool* playing = nullptr;
        bool AllowSceneSwap(const SceneSwapContext& c, std::string& why) override {
            if (c.origin == SceneSwapOrigin::Host || (playing && *playing)) return true;
            why = "stopped";
            return false;
        }
    } gate;
    gate.playing = &playing;
    loader.AddObserver(&gate);

    loader.RequestSwap("test_loader_a.json", SceneSwapOrigin::Game);
    EXPECT_FALSE(loader.DrainPendingSwap());
    EXPECT_EQ(loader.lastResult().status, SceneSwapStatus::Refused);
    EXPECT_EQ(namedCount(scene), 0) << "a refused swap loaded anyway";

    loader.RequestSwap("test_loader_a.json", SceneSwapOrigin::Host);
    EXPECT_TRUE(loader.DrainPendingSwap());
    EXPECT_EQ(namedCount(scene), 5);

    playing = true;
    loader.RequestSwap("test_loader_a.json", SceneSwapOrigin::Game);
    EXPECT_TRUE(loader.DrainPendingSwap()) << "the gate refused during play";
    std::remove("test_loader_a.json");
}


// ------------------------------------------- the rebuild half of the swap
//
// Every subsystem subscribes Clear() to will-unload. Clear on its own is only
// half a scene swap: a cleared world stays cleared, so a game that changed
// scene arrived with no bodies, no script instances and no voices, and the only
// symptom was that gameplay looked switched off.
//
// These tests are the CONTRACT the Install* helpers implement, exercised
// against a real SceneLoader. The gate is app.gameplayEnabled(), which already
// means "gameplay is live" -- always true in the shipped player, true in the
// editor only between Play and Stop -- so a bool stands in for it here.

namespace {
// Writes a scene whose entities all carry a static box collider, so a
// PhysicsWorld built against it has a body per entity.
std::string writePhysicsScene(const char* path, int n) {
    std::string s = R"({"version":1,"entities":[)";
    for (int i = 0; i < n; ++i) {
        if (i) s += ",";
        // "type" is the BodyType ENUM as an int (0 = Static), not a name --
        // hand-writing "Static" here silently fails validation and the swap is
        // refused before it starts.
        // A Transform is required as well as the body: PhysicsWorld::Build
        // views <RigidBody, Transform>, so a body with no pose is silently
        // not a body at all.
        s += R"({"name":"B)" + std::to_string(i) + R"(")"
             R"(,"transform":{"position":[0,)" + std::to_string(i * 3) + R"(,0])"
             R"(,"rotation":[0,0,0],"scale":[1,1,1]})"
             R"(,"rigidBody":{"type":0})"
             R"(,"boxCollider":{"halfExtents":[1,1,1]}})";
    }
    s += "]}";
    std::ofstream(path) << s;
    return path;
}
} // namespace

TEST(SceneLoader, ASubsystemRebuiltOnDidLoadTracksTheNewScene) {
    writePhysicsScene("test_loader_p1.json", 2);
    writePhysicsScene("test_loader_p2.json", 5);
    AssetManager assets;
    Scene scene;
    RegisterBuiltinPhysicsBackends();
    PhysicsWorld world;
    ASSERT_TRUE(world.SetBackend(DefaultPhysicsBackendName()));

    SceneLoader loader(scene, assets);
    bool gameplayLive = true;
    // Exactly the pair PhysicsInstall subscribes.
    loader.AddObserver([&world](Scene&) { world.Clear(); },
                       [&](Scene& s) { if (gameplayLive) world.Rebuild(s.registry); });

    loader.RequestSwap("test_loader_p1.json");
    ASSERT_TRUE(loader.DrainPendingSwap());
    EXPECT_EQ(world.BodyCount(), 2u) << "the first scene's bodies were never built";

    loader.RequestSwap("test_loader_p2.json");
    ASSERT_TRUE(loader.DrainPendingSwap());
    EXPECT_EQ(world.BodyCount(), 5u)
        << "a cleared world stayed cleared - the swap left the game with no physics";

    // ...and the tenth swap costs what the first did. A Rebuild that leaked
    // would show up here as a count that keeps climbing.
    for (int i = 0; i < 8; ++i) {
        loader.RequestSwap(i % 2 ? "test_loader_p2.json" : "test_loader_p1.json");
        ASSERT_TRUE(loader.DrainPendingSwap());
    }
    EXPECT_EQ(world.BodyCount(), 5u);
    std::remove("test_loader_p1.json");
    std::remove("test_loader_p2.json");
}

// Edit mode has no bodies on purpose -- the editor builds them at Play and
// destroys them at Stop -- so a File > Open while stopped must not give it any.
TEST(SceneLoader, TheRebuildIsSkippedWhenGameplayIsNotLive) {
    writePhysicsScene("test_loader_p1.json", 3);
    AssetManager assets;
    Scene scene;
    RegisterBuiltinPhysicsBackends();
    PhysicsWorld world;
    ASSERT_TRUE(world.SetBackend(DefaultPhysicsBackendName()));

    SceneLoader loader(scene, assets);
    bool gameplayLive = false;
    loader.AddObserver([&world](Scene&) { world.Clear(); },
                       [&](Scene& s) { if (gameplayLive) world.Rebuild(s.registry); });

    loader.RequestSwap("test_loader_p1.json", SceneSwapOrigin::Host);
    ASSERT_TRUE(loader.DrainPendingSwap());
    EXPECT_EQ(world.BodyCount(), 0u) << "edit mode was handed live bodies";

    // Press Play: the host builds, and from then on swaps track.
    gameplayLive = true;
    world.Rebuild(scene.registry);
    EXPECT_EQ(world.BodyCount(), 3u);
    std::remove("test_loader_p1.json");
}

// Observer count is the cheap proof that "rebuild after a swap" was not
// implemented by re-running the Install helpers, each of which subscribes
// another observer AND another tick subscriber. Three per swap is thirty by
// swap ten, and the symptom is quadratic slowdown with no other clue.
TEST(SceneLoader, SwappingRepeatedlyNeverAccumulatesObservers) {
    writeScene("test_loader_a.json", 2);
    AssetManager assets;
    Scene scene;
    SceneLoader loader(scene, assets);
    loader.AddObserver([](Scene&) {}, [](Scene&) {});
    loader.AddObserver([](Scene&) {}, [](Scene&) {});
    const std::size_t before = loader.observerCount();
    ASSERT_EQ(before, 2u);

    for (int i = 0; i < 10; ++i) {
        loader.RequestSwap("test_loader_a.json");
        ASSERT_TRUE(loader.DrainPendingSwap());
    }
    EXPECT_EQ(loader.observerCount(), before);
    std::remove("test_loader_a.json");
}


// ================================================ asynchronous swaps (U24) ==
//
// The destructive half of a swap cannot leave the main thread and never will:
// a load creates entities, and the registry is single-threaded. What CAN leave
// is the expensive half -- importing the meshes a scene names -- so a swap
// warms those on workers first and holds the teardown until they have settled.
//
// The property that matters is what happens MEANWHILE: nothing has been torn
// down, so the outgoing scene is still there.

namespace {

// A scene naming `n` models, at paths that do not exist. Absent is the right
// shape here: the request still goes through the sandbox, still runs on a
// worker, and still SETTLES -- as Failed. A model that will never arrive must
// not hold a swap open forever.
std::string writeModelScene(const char* path, int n) {
    std::string s = R"({"version":1,"entities":[)";
    for (int i = 0; i < n; ++i) {
        if (i) s += ",";
        s += R"({"name":"M)" + std::to_string(i) +
             R"(","model":"Exported/Model/async_probe_)" + std::to_string(i) + R"(.obj"})";
    }
    s += "]}";
    std::ofstream(path) << s;
    return path;
}

// Pumps until the prewarm settles, so a hang fails the test rather than it.
//
// The sleep is load-bearing, not politeness: pumpCompletions returns at once
// when nothing has finished, so a tight loop laps the workers thousands of
// times and gives up before the first decode lands. It also has to outlast the
// CAP -- kMaxConcurrentDecodes is 2, so with three models the third only
// launches from another completion, one round later.
bool settle(SceneLoader& loader, JobSystem& jobs) {
    for (int i = 0; i < 3000 && loader.swapPrewarming(); ++i) {
        jobs.pumpCompletions(1.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return !loader.swapPrewarming();
}

} // namespace

TEST(SceneLoaderAsync, CollectsExactlyThePathsALoadWouldImport) {
    // Two entities share a path, one names a path the sandbox refuses, and one
    // carries an empty model component.
    std::ofstream("test_async_paths.json") << R"({"version":1,"entities":[)"
        R"({"name":"A","model":"Exported/Model/one.obj"},)"
        R"({"name":"B","model":"Exported/Model/one.obj"},)"
        R"({"name":"C","model":"../../evil.obj"},)"
        R"({"name":"D","model":""}]})";

    AssetManager assets;
    std::vector<std::string> paths;
    ASSERT_TRUE(SceneSerializer::CollectModelPaths("test_async_paths.json", assets, paths));

    ASSERT_EQ(paths.size(), 1u)
        << "the warm list is not the set a load would actually import";
    EXPECT_EQ(paths[0], "Exported/Model/one.obj");
    std::remove("test_async_paths.json");
}

TEST(SceneLoaderAsync, AFileThatWouldNotLoadCollectsNothing) {
    std::ofstream("test_async_bad.json") << R"({"version":1,"entities":[{"name":5}]})";
    AssetManager assets;
    std::vector<std::string> paths;
    EXPECT_FALSE(SceneSerializer::CollectModelPaths("test_async_bad.json", assets, paths));
    EXPECT_TRUE(paths.empty()) << "a rejected file left paths behind to warm";
    std::remove("test_async_bad.json");
}

// THE POINT OF THE FEATURE.
TEST(SceneLoaderAsync, TheOutgoingSceneSurvivesUntilEveryModelHasSettled) {
    writeScene("test_async_from.json", 4);
    writeModelScene("test_async_to.json", 3);

    AssetManager assets;
    JobSystem jobs;
    Scene scene;
    SceneLoader loader(scene, assets);
    loader.SetJobSystem(&jobs);

    ASSERT_TRUE(loader.RequestSwap("test_async_from.json", SceneSwapOrigin::Host));
    ASSERT_TRUE(loader.DrainPendingSwap());
    ASSERT_EQ(namedCount(scene), 4);

    ASSERT_TRUE(loader.RequestSwapAsync("test_async_to.json", SceneSwapOrigin::Host));
    EXPECT_EQ(loader.prewarmTotal(), 3u) << "the models were never requested";

    // Draining WHILE it warms must do nothing at all -- not tear down, not load.
    int spins = 0;
    while (loader.swapPrewarming() && spins++ < 3000) {
        EXPECT_FALSE(loader.DrainPendingSwap())
            << "the swap ran with models still in flight";
        EXPECT_EQ(namedCount(scene), 4)
            << "the outgoing scene was torn down while the new one was loading";
        jobs.pumpCompletions(1.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_FALSE(loader.swapPrewarming()) << "the prewarm never settled";
    EXPECT_EQ(loader.prewarmDone(), 3u);

    // ...and now it runs.
    EXPECT_TRUE(loader.DrainPendingSwap());
    EXPECT_EQ(namedCount(scene), 3);
    EXPECT_EQ(loader.prewarmTotal(), 0u) << "the warmed handles were never released";

    std::remove("test_async_from.json");
    std::remove("test_async_to.json");
}

// A model that will never arrive settles as Failed rather than hanging the
// swap. The scene is still loadable; the miss is reported where a missing
// asset has always been reported.
TEST(SceneLoaderAsync, AModelThatCannotLoadStillSettlesAndTheSwapProceeds) {
    writeModelScene("test_async_missing.json", 2);

    AssetManager assets;
    JobSystem jobs;
    Scene scene;
    SceneLoader loader(scene, assets);
    loader.SetJobSystem(&jobs);

    ASSERT_TRUE(loader.RequestSwapAsync("test_async_missing.json", SceneSwapOrigin::Host));
    ASSERT_TRUE(settle(loader, jobs)) << "a model that cannot load hung the swap";

    EXPECT_TRUE(loader.DrainPendingSwap());
    EXPECT_EQ(namedCount(scene), 2) << "the entities still exist";
    EXPECT_FALSE(loader.lastResult().report.complete())
        << "a scene whose models all failed called itself complete";
    std::remove("test_async_missing.json");
}

// DEGRADES HONESTLY: no pool means no prewarm and the same swap, rather than a
// swap that silently never runs.
TEST(SceneLoaderAsync, WithNoJobSystemItIsExactlyTheSynchronousSwap) {
    writeScene("test_async_nopool.json", 2);
    AssetManager assets;
    Scene scene;
    SceneLoader loader(scene, assets);          // no SetJobSystem

    ASSERT_TRUE(loader.RequestSwapAsync("test_async_nopool.json", SceneSwapOrigin::Host));
    EXPECT_EQ(loader.prewarmTotal(), 0u);
    EXPECT_FALSE(loader.swapPrewarming());
    EXPECT_TRUE(loader.DrainPendingSwap()) << "the swap never ran without a pool";
    EXPECT_EQ(namedCount(scene), 2);
    std::remove("test_async_nopool.json");
}

// Validation still happens at REQUEST time, which is the whole reason it is
// there rather than at the drain.
TEST(SceneLoaderAsync, ABadFileIsRefusedAtRequestTimeAndWarmsNothing) {
    AssetManager assets;
    JobSystem jobs;
    Scene scene;
    SceneLoader loader(scene, assets);
    loader.SetJobSystem(&jobs);

    EXPECT_FALSE(loader.RequestSwapAsync("no_such_scene_at_all.json", SceneSwapOrigin::Host));
    EXPECT_EQ(loader.lastResult().status, SceneSwapStatus::Invalid);
    EXPECT_EQ(loader.prewarmTotal(), 0u) << "a refused swap still queued decodes";
    EXPECT_FALSE(loader.swapPending());
}

// Superseding a warming swap drops ITS handles, or every scene ever requested
// would stay pinned in the cache.
TEST(SceneLoaderAsync, SupersedingAWarmingSwapReleasesTheModelsItWasWarming) {
    writeModelScene("test_async_a.json", 3);
    writeScene("test_async_b.json", 1);

    AssetManager assets;
    JobSystem jobs;
    Scene scene;
    SceneLoader loader(scene, assets);
    loader.SetJobSystem(&jobs);

    ASSERT_TRUE(loader.RequestSwapAsync("test_async_a.json", SceneSwapOrigin::Host));
    ASSERT_EQ(loader.prewarmTotal(), 3u);

    ASSERT_TRUE(loader.RequestSwapAsync("test_async_b.json", SceneSwapOrigin::Host));
    EXPECT_EQ(loader.prewarmTotal(), 0u)
        << "the superseded swap's models are still being warmed";
    EXPECT_EQ(loader.pendingPath(), "test_async_b.json");

    ASSERT_TRUE(settle(loader, jobs));
    EXPECT_TRUE(loader.DrainPendingSwap());
    EXPECT_EQ(namedCount(scene), 1);

    std::remove("test_async_a.json");
    std::remove("test_async_b.json");
}

// Cancelling releases them too.
TEST(SceneLoaderAsync, CancellingAWarmingSwapReleasesItsModels) {
    writeModelScene("test_async_cancel.json", 3);
    AssetManager assets;
    JobSystem jobs;
    Scene scene;
    SceneLoader loader(scene, assets);
    loader.SetJobSystem(&jobs);

    ASSERT_TRUE(loader.RequestSwapAsync("test_async_cancel.json", SceneSwapOrigin::Host));
    ASSERT_EQ(loader.prewarmTotal(), 3u);
    loader.CancelPendingSwap();
    EXPECT_EQ(loader.prewarmTotal(), 0u);
    EXPECT_FALSE(loader.swapPrewarming());
    EXPECT_FALSE(loader.swapPending());

    jobs.pumpCompletions(1.0f);   // the in-flight decodes must still land safely
    std::remove("test_async_cancel.json");
}
