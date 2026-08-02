// The main menu's C++ half, and the one thing about the UI that a scene swap
// touches.
//
// Pure CPU. Every verb that needs an Application (load a scene, quit, vsync) is
// inert with a null one BY DESIGN rather than by accident, which is most of
// what makes this file testable at all.
#include <gtest/gtest.h>

#include "Engine.h"
#include "../Engine/src/core/SceneLoader.h"
#include "../Engine/src/ui/MenuUIContent.h"
#include "../Engine/src/ui/UIDataSource.h"
#include "../Engine/src/ui/UIWorld.h"

#include <fstream>
#include <string>
#include <vector>

using namespace MyCoreEngine;
using namespace MyCoreEngine::ui;

namespace {

// A scene file with `n` plain entities plus, optionally, one carrying a UI
// document pointed at the SHIPPED sample HUD (which the test runtime stages).
std::string writeUIScene(const char* path, int n, bool withDocument) {
    std::string s = R"({"version":1,"entities":[)";
    for (int i = 0; i < n; ++i) {
        if (i) s += ",";
        s += R"({"name":"E)" + std::to_string(i) + R"("})";
    }
    if (withDocument) {
        if (n) s += ",";
        s += R"({"name":"HUD","uiDocument":{"markup":"Exported/UI/hud.cxml",)"
             R"("stylesheet":"Exported/UI/hud.cstyle"}})";
    }
    s += "]}";
    std::ofstream(path) << s;
    return path;
}

// UIDataSource addresses actions by INDEX (resolved once at load, not per
// click), so a test that wants to click a button by name resolves it the same
// way the binder does.
bool invoke(UIDataSource& src, const char* name) {
    const int i = src.ActionIndexOf(name);
    return i >= 0 && src.InvokeAction(i);
}
bool hasAction(const UIDataSource& src, const char* name) {
    return src.ActionIndexOf(name) >= 0;
}

int countDocs(Scene& s) {
    int n = 0;
    for (auto e : s.registry.view<UIDocumentComponent>()) { (void)e; ++n; }
    return n;
}

} // namespace

// ---------------------------------------------------- the UI and the swap
//
// Physics, scripting and audio each subscribe a teardown to SceneLoader because
// each keys state on entt::entity and nothing else would ever drop it. The UI
// deliberately does NOT, and this is why: UIWorld RECONCILES against the
// registry at the top of every Update -- it erases any document whose entity is
// no longer valid, then rebuilds its draw order from what survived. That is a
// stronger guarantee than an observer, because it also covers an entity deleted
// by hand, by undo, or by a play-mode restore, none of which fire a swap.
//
// If this test ever fails, the right fix is an observer (a UIInstall.h beside
// PhysicsInstall.h), not a patch here.
TEST(UIWorldSwap, DocumentsFromTheOldSceneDoNotSurviveIntoTheNew) {
    writeUIScene("test_menu_a.json", 3, /*withDocument=*/true);
    writeUIScene("test_menu_b.json", 2, /*withDocument=*/false);

    AssetManager assets;
    Scene scene;
    UIWorld world;
    SceneLoader loader(scene, assets);
    // Both hosts install this, and the scene below points at the shipped HUD.
    // Without it every one of that document's bindings resolves against a
    // source that has no `health` or `score`, which is a wall of correct
    // diagnostics about a situation no host ever produces.
    InstallDemoUIContent(world);

    ASSERT_TRUE(loader.RequestSwap("test_menu_a.json"));
    ASSERT_TRUE(loader.DrainPendingSwap());
    ASSERT_EQ(countDocs(scene), 1);
    world.Update(scene.registry, 1280, 720, 0.016f);
    EXPECT_EQ(world.liveCount(), 1u);

    // A scene with NO document at all: the strictest version of the question.
    ASSERT_TRUE(loader.RequestSwap("test_menu_b.json"));
    ASSERT_TRUE(loader.DrainPendingSwap());
    ASSERT_EQ(countDocs(scene), 0);
    world.Update(scene.registry, 1280, 720, 0.016f);
    EXPECT_EQ(world.liveCount(), 0u)
        << "a document from the departed scene is still live - it would be "
           "drawn, and its entity no longer exists";

    // ...and back, to prove the reconcile is not one-way.
    ASSERT_TRUE(loader.RequestSwap("test_menu_a.json"));
    ASSERT_TRUE(loader.DrainPendingSwap());
    world.Update(scene.registry, 1280, 720, 0.016f);
    EXPECT_EQ(world.liveCount(), 1u);

    std::remove("test_menu_a.json");
    std::remove("test_menu_b.json");
}

// The shared data source is host-lifetime, NOT scene-lifetime, and that is the
// whole reason a settings menu works: the volume you set in the menu has to
// still be set after the game scene loads over it.
TEST(UIWorldSwap, TheSharedDataSourceOutlivesTheScene) {
    writeUIScene("test_menu_a.json", 2, false);
    AssetManager assets;
    Scene scene;
    UIWorld world;
    SceneLoader loader(scene, assets);

    world.shared().SetNumber("menuVolume", 0.42f);
    loader.RequestSwap("test_menu_a.json");
    ASSERT_TRUE(loader.DrainPendingSwap());
    world.Update(scene.registry, 1280, 720, 0.016f);

    EXPECT_NEAR(world.shared().GetNumber("menuVolume"), 0.42f, 1e-5f)
        << "settings died with the scene that set them";
    std::remove("test_menu_a.json");
}

// ------------------------------------------------------------- the verbs

TEST(MenuUIContent, SeedsEverythingTheMarkupBindsToBeforeAnyDocumentLoads) {
    UIWorld world;
    MenuUIHooks h;
    h.initialVolume = 0.6f;
    InstallMenuUIContent(world, h);

    UIDataSource& src = world.shared();
    EXPECT_NEAR(src.GetNumber("menuVolume"), 0.6f, 1e-5f);
    EXPECT_EQ(src.GetInt("menuVolumePct"), 60);
    EXPECT_EQ(src.GetInt("menuSwaps"), 0);
    EXPECT_EQ(src.GetInt("swapLogCursor"), 0);
    EXPECT_FALSE(src.GetString("menuQualityName").empty());
    EXPECT_TRUE(src.GetBool("menuStatusOk"));
}

// No Application, no crash and no silent half-action. Every host verb checks
// its pointer, because the editor installs this before its own Play session
// exists and a test installs it with nothing at all.
TEST(MenuUIContent, HostVerbsAreInertWithoutAnApplication) {
    UIWorld world;
    InstallMenuUIContent(world, MenuUIHooks{});
    UIDataSource& src = world.shared();

    for (const char* verb : { "menuNewGame", "menuBackToMenu", "menuQuit",
                              "menuVSyncToggle" }) {
        EXPECT_TRUE(hasAction(src, verb)) << verb;
        invoke(src, verb);   // must not crash
    }
    SUCCEED();
}

TEST(MenuUIContent, VolumeStepsAndClampsAndPersistsThroughTheHostHook) {
    UIWorld world;
    float persisted = -1.0f;
    int writes = 0;
    MenuUIHooks h;
    h.initialVolume = 0.5f;
    h.volumeStep = 0.25f;
    h.onMasterVolume = [&](float v) { persisted = v; ++writes; };
    InstallMenuUIContent(world, h);
    UIDataSource& src = world.shared();

    invoke(src, "menuVolumeUp");
    EXPECT_NEAR(src.GetNumber("menuVolume"), 0.75f, 1e-5f);
    EXPECT_NEAR(persisted, 0.75f, 1e-5f) << "the host was never told";
    EXPECT_EQ(src.GetInt("menuVolumePct"), 75);

    invoke(src, "menuVolumeUp");
    invoke(src, "menuVolumeUp");
    EXPECT_NEAR(src.GetNumber("menuVolume"), 1.0f, 1e-5f) << "clamped at the top";

    for (int i = 0; i < 8; ++i) invoke(src, "menuVolumeDown");
    EXPECT_NEAR(src.GetNumber("menuVolume"), 0.0f, 1e-5f) << "clamped at the bottom";
    EXPECT_GT(writes, 0);
}

// The editor's Game panel dispatches the running document's clicks even while
// STOPPED. The edit-mode gate guards SWAPS; it says nothing about the master
// volume, so mutation gets its own predicate.
TEST(MenuUIContent, AHostCanRefuseMutationWithoutRefusingTheWholeMenu) {
    UIWorld world;
    bool live = false;
    MenuUIHooks h;
    h.initialVolume = 0.5f;
    h.volumeStep = 0.25f;
    h.allowHostMutation = [&] { return live; };
    InstallMenuUIContent(world, h);
    UIDataSource& src = world.shared();

    invoke(src, "menuVolumeUp");
    EXPECT_NEAR(src.GetNumber("menuVolume"), 0.5f, 1e-5f)
        << "the volume moved while the host had mutation refused";

    live = true;
    invoke(src, "menuVolumeUp");
    EXPECT_NEAR(src.GetNumber("menuVolume"), 0.75f, 1e-5f);
}

// ------------------------------------------------------------ the swap log

TEST(MenuUIContent, ReportSwapAppendsToTheLogAndFollowsTheNewestRow) {
    UIWorld world;
    InstallMenuUIContent(world, MenuUIHooks{});
    UIDataSource& src = world.shared();

    SceneSwapResult ok;
    ok.status = SceneSwapStatus::Ok;
    ok.path = "Exported/level_two.json";
    ok.report.entitiesCreated = 7;
    MenuUIReportSwap(world, ok);

    EXPECT_EQ(src.GetInt("menuSwaps"), 1);
    EXPECT_EQ(src.GetInt("swapLogCursor"), 0);
    EXPECT_TRUE(src.GetBool("menuStatusOk"));
    // The BASE name: a full project-relative path would push the status line
    // past the width of the card it lives in.
    EXPECT_NE(src.GetString("menuStatus").find("level_two.json"), std::string::npos);
    EXPECT_EQ(src.GetString("menuStatus").find("Exported/"), std::string::npos);

    SceneSwapResult bad;
    bad.status = SceneSwapStatus::Invalid;
    bad.path = "Exported/typo.json";
    MenuUIReportSwap(world, bad);
    EXPECT_EQ(src.GetInt("menuSwaps"), 2);
    EXPECT_EQ(src.GetInt("swapLogCursor"), 1) << "the cursor did not follow the new row";
    EXPECT_FALSE(src.GetBool("menuStatusOk"));
}

// A demo instrument, not a diagnostic record. An unbounded vector behind a
// six-slot window is a leak with a view.
TEST(MenuUIContent, TheSwapLogIsBounded) {
    UIWorld world;
    InstallMenuUIContent(world, MenuUIHooks{});
    SceneSwapResult r;
    r.status = SceneSwapStatus::Ok;
    r.path = "a.json";
    for (int i = 0; i < 200; ++i) MenuUIReportSwap(world, r);
    EXPECT_LE(world.shared().GetInt("menuSwaps"), 32)
        << "the swap log grows without bound";
}

// Two hosts in one process must not share a log. The editor is one UIWorld;
// nothing stops a test, or a future tool, from standing up another.
TEST(MenuUIContent, EachWorldHasItsOwnSwapLog) {
    UIWorld a, b;
    InstallMenuUIContent(a, MenuUIHooks{});
    InstallMenuUIContent(b, MenuUIHooks{});

    SceneSwapResult r;
    r.status = SceneSwapStatus::Ok;
    r.path = "a.json";
    MenuUIReportSwap(a, r);
    MenuUIReportSwap(a, r);

    EXPECT_EQ(a.shared().GetInt("menuSwaps"), 2);
    EXPECT_EQ(b.shared().GetInt("menuSwaps"), 0) << "the two worlds share a log";
}

// Installing again must not leave the previous run's rows behind -- the editor
// reinstalls nothing today, but a host that did would otherwise inherit a log
// describing scenes from a session that no longer exists.
TEST(MenuUIContent, ReinstallingClearsTheLog) {
    UIWorld world;
    InstallMenuUIContent(world, MenuUIHooks{});
    SceneSwapResult r;
    r.status = SceneSwapStatus::Ok;
    r.path = "a.json";
    MenuUIReportSwap(world, r);
    ASSERT_EQ(world.shared().GetInt("menuSwaps"), 1);

    InstallMenuUIContent(world, MenuUIHooks{});
    EXPECT_EQ(world.shared().GetInt("menuSwaps"), 0);
}

TEST(MenuUIContent, CountersReflectTheLiveWorld) {
    writeUIScene("test_menu_a.json", 1, true);
    AssetManager assets;
    Scene scene;
    UIWorld world;
    SceneLoader loader(scene, assets);
    InstallDemoUIContent(world);          // the scene points at the shipped HUD
    InstallMenuUIContent(world, MenuUIHooks{});

    loader.RequestSwap("test_menu_a.json");
    ASSERT_TRUE(loader.DrainPendingSwap());
    world.Update(scene.registry, 1280, 720, 0.016f);

    MenuUIPublishCounters(world, MenuUIHooks{});
    EXPECT_EQ(world.shared().GetInt("menuLiveDocs"), 1);
    std::remove("test_menu_a.json");
}
