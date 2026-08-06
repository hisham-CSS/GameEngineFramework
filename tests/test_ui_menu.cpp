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
#include "../Engine/src/ui/UIAssetDocument.h"
#include "../Engine/src/ui/UIBinding.h"
#include "../Engine/src/ui/UIElement.h"
#include "../Engine/src/ui/UINav.h"
#include "../Engine/src/ui/UIStyleSheet.h"
#include "../Engine/src/ui/UIWorld.h"

#include <algorithm>
#include <cmath>
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
// four-slot window is a leak with a view.
//
// The COUNTER is separate and monotonic: it is what the SYSTEM tab asks you to
// watch across a session, and tying it to the log length made it silently stop
// climbing once the log hit its cap.
TEST(MenuUIContent, TheSwapLogIsBoundedButTheCounterIsNot) {
    UIWorld world;
    InstallMenuUIContent(world, MenuUIHooks{});
    SceneSwapResult r;
    r.status = SceneSwapStatus::Ok;
    r.path = "a.json";
    for (int i = 0; i < 200; ++i) MenuUIReportSwap(world, r);

    UIDataSource& src = world.shared();
    const int li = src.ListIndexOf("swapLog");
    ASSERT_GE(li, 0);
    EXPECT_LE(src.ListRowCount(li), 32u) << "the swap log grows without bound";
    EXPECT_EQ(src.GetInt("menuSwaps"), 200) << "the counter stopped counting";
}

// Clearing the CONSOLE is not resetting the counter -- clearing a log has never
// reset an uptime.
TEST(MenuUIContent, ClearEmptiesTheLogAndLeavesTheCounterAlone) {
    UIWorld world;
    InstallMenuUIContent(world, MenuUIHooks{});
    UIDataSource& src = world.shared();
    SceneSwapResult r;
    r.status = SceneSwapStatus::Ok;
    r.path = "a.json";
    for (int i = 0; i < 5; ++i) MenuUIReportSwap(world, r);

    const int li = src.ListIndexOf("swapLog");
    ASSERT_GE(li, 0);
    ASSERT_EQ(src.ListRowCount(li), 5u);
    ASSERT_EQ(src.GetInt("swapLogCursor"), 4);

    ASSERT_TRUE(invoke(src, "menuLogClear"));
    EXPECT_EQ(src.ListRowCount(li), 0u);
    EXPECT_EQ(src.GetInt("swapLogCursor"), 0) << "a cursor left past the end of an "
                                                 "empty list";
    EXPECT_EQ(src.GetInt("menuSwaps"), 5) << "clearing the console reset the counter";

    // ...and the log still works afterwards.
    MenuUIReportSwap(world, r);
    EXPECT_EQ(src.ListRowCount(li), 1u);
    EXPECT_EQ(src.GetInt("menuSwaps"), 6);
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


// ------------------------------------------------- the shipped menu asset
//
// The whole point of a load-time diagnostic is that a typo fails the SUITE
// rather than shipping as a control that silently does nothing. These load the
// real Exported/menu.* through the real path a scene uses, with the real hooks
// installed, and demand SILENCE.
namespace {

struct ShippedMenu {
    Scene   scene;
    UIWorld world;
    entt::entity entity{ entt::null };

    ShippedMenu() {
        // Both, and in this order: the menu's pilot field binds to `playerName`,
        // which InstallDemoUIContent seeds.
        InstallDemoUIContent(world);
        InstallMenuUIContent(world, MenuUIHooks{});
        entity = scene.registry.create();
        UIDocumentComponent ud;
        ud.markup = "Exported/UI/menu.cxml";
        ud.stylesheet = "Exported/UI/menu.cstyle";
        scene.registry.emplace<UIDocumentComponent>(entity, ud);
    }
    void Frame(int w = 1280, int h = 720) { world.Update(scene.registry, w, h, 0.016f); }
    UIDocument& doc() { return world.document(entity)->document(); }
    UIElement* find(const char* n) {
        auto* a = world.document(entity);
        return a ? a->document().root().Find(n) : nullptr;
    }
};

} // namespace

TEST(ShippedMenuAsset, LoadsWithNoErrorsAndNoUnresolvedBindings) {
    ShippedMenu m;
    // With the swap log POPULATED, which is the steady state: an empty pool has
    // absent slots, and a bare hole inside an absent slot resolves to nothing
    // by construction -- see the next test.
    SceneSwapResult r;
    r.status = SceneSwapStatus::Ok;
    r.path = "Exported/scene.json";
    r.report.entitiesCreated = 4;
    for (int i = 0; i < 6; ++i) MenuUIReportSwap(m.world, r);

    m.Frame();
    for (const std::string& e : m.world.errors()) ADD_FAILURE() << e;

    auto* doc = m.world.document(m.entity);
    ASSERT_NE(doc, nullptr) << "menu.cxml did not load at all";
    for (const std::string& e : doc->errors()) ADD_FAILURE() << e;
    // A misspelt path or action is a BINDER diagnostic, not a load error, and
    // it is exactly how a dead button ships.
    EXPECT_EQ(doc->binder().unresolvedCount(), 0u);
}

// An empty `repeat=` list is not an error, but it is not silent either: every
// slot is absent, `$present` hides it, and a bare hole inside it resolves to
// nothing -- which any converter then reports. The shipped HUD does the same
// thing with an unseeded inventory.
//
// Documented rather than fixed. The real fix is for the binder to skip an
// absent slot's bindings entirely (it is display:none and its data does not
// exist), which is a change to the U18 pool contract and does not belong in a
// demo commit. What matters here is that it stays a DIAGNOSTIC: the document
// still loads, the visible rows still bind, and nothing is drawn wrong.
TEST(ShippedMenuAsset, AnEmptySwapLogStillLoadsAndDrawsNothing) {
    ShippedMenu m;
    m.Frame();
    auto* doc = m.world.document(m.entity);
    ASSERT_NE(doc, nullptr);
    for (const std::string& e : doc->errors()) ADD_FAILURE() << e;

    UIElement* log = m.find("swapLog");
    ASSERT_NE(log, nullptr);
    ASSERT_FALSE(log->children().empty()) << "the pool was never built";
    for (const auto& row : log->children()) {
        EXPECT_EQ(row->style().display, DisplayMode::None)
            << "an absent slot is visible with no row behind it";
    }
}

TEST(ShippedMenuAsset, HasTheElementsItsStylesheetAndVerbsAssume) {
    ShippedMenu m;
    m.Frame();
    for (const char* n : { "backdrop", "veil", "ramp", "frame",
                           // the fixed frame: these three persist across every
                           // screen, which is the whole point of them
                           "header", "logo", "title", "subtitle",
                           "stage", "footer", "statusText",
                           "verbs", "newGame", "settings", "system", "quit",
                           "settingsPanel", "volume", "volumeFill",
                           "qLow", "qMed", "qHigh", "vsync", "pilot",
                           "systemPanel", "swapLog",
                           "logPrev", "logNext", "logClear" }) {
        EXPECT_NE(m.find(n), nullptr) << "menu.cxml is missing #" << n;
    }
}

// The trap that would make the whole card land in the top-left corner: an
// absolutely positioned element with only right/bottom written. Style::Edges
// defaults every side to 0 and UIElement pushes all four, so left/top win.
TEST(ShippedMenuAsset, EveryAbsoluteElementWritesAllFourInsets) {
    ShippedMenu m;
    m.Frame();
    for (const char* n : { "backdrop", "veil", "ramp" }) {
        UIElement* e = m.find(n);
        ASSERT_NE(e, nullptr) << n;
        EXPECT_EQ(e->style().position, PositionType::Absolute) << n;
        EXPECT_FLOAT_EQ(e->style().inset.left, 0.0f) << n;
        EXPECT_FLOAT_EQ(e->style().inset.top, 0.0f) << n;
        EXPECT_FLOAT_EQ(e->style().inset.right, 0.0f) << n;
        EXPECT_FLOAT_EQ(e->style().inset.bottom, 0.0f) << n;
    }
}

// A full-screen decorative layer that swallows clicks is the single easiest way
// to ship a menu where no button works.
TEST(ShippedMenuAsset, TheFullScreenLayersDoNotEatClicks) {
    ShippedMenu m;
    m.Frame();
    EXPECT_FALSE(m.find("backdrop")->style().pickable);
    EXPECT_FALSE(m.find("veil")->style().pickable);
    EXPECT_FALSE(m.find("ramp")->style().pickable);
}

// The base rule, same contract the HUD has: font-scale multiplies the 48px
// atlas, and nothing inherits.
TEST(ShippedMenuAsset, DeclaresTheSameBaseTextSizeAsTheBakedAtlas) {
    UIStyleSheet sheet;
    ASSERT_TRUE(sheet.LoadFromFile("Exported/UI/menu.cstyle"))
        << (sheet.errors().empty() ? std::string() : sheet.errors()[0]);
    UIDocument doc;
    sheet.ApplyTo(doc.root());
    EXPECT_NEAR(doc.root().style().fontScale, kUIFontBaseScale, 1e-5f);
}

// The scene file itself: it must load, and it must have a camera. A camera-less
// scene drops the Game view and the shipped player onto a free-fly diagnostic
// camera, which is the exact trap that once made a built game look like it
// ignored the scene's camera.
TEST(ShippedMenuAsset, TheMenuSceneLoadsAndHasACamera) {
    AssetManager assets;
    Scene scene;
    SceneSerializer sz(scene, assets);
    SceneLoadReport rep;
    ASSERT_TRUE(sz.Load("Exported/menu.json", &rep)) << "Exported/menu.json did not load";
    EXPECT_TRUE(rep.complete())
        << rep.failedModels.size() << " model(s) in menu.json did not import";

    int cameras = 0, documents = 0;
    for (auto e : scene.registry.view<CameraComponent>()) { (void)e; ++cameras; }
    for (auto e : scene.registry.view<UIDocumentComponent>()) { (void)e; ++documents; }
    EXPECT_GT(cameras, 0) << "menu.json has no camera - the player would fall back "
                             "to a free-fly debug camera";
    ASSERT_EQ(documents, 1);

    // And it must ask for scaling. Left on the default (Constant) the menu is
    // authored pixels on every screen, which is the bug the HUD shipped with.
    for (auto e : scene.registry.view<UIDocumentComponent>()) {
        const auto& ud = scene.registry.get<UIDocumentComponent>(e);
        EXPECT_EQ(ud.scale.mode, ui::UIScaleMode::ScaleWithScreen);
        EXPECT_FLOAT_EQ(ud.scale.reference.x, 1280.0f);
        EXPECT_FLOAT_EQ(ud.scale.reference.y, 720.0f);
    }
}

// The return leg. hud.cxml names `menuBackToMenu`, which only
// InstallMenuUIContent registers -- so a host that installs one and not the
// other ships a dead button.
TEST(ShippedMenuAsset, TheHudsMenuButtonNamesAnActionThatExists) {
    UIWorld world;
    InstallDemoUIContent(world);
    InstallMenuUIContent(world, MenuUIHooks{});
    EXPECT_TRUE(hasAction(world.shared(), "menuBackToMenu"));

    Scene scene;
    const entt::entity e = scene.registry.create();
    UIDocumentComponent ud;
    ud.markup = "Exported/UI/hud.cxml";
    ud.stylesheet = "Exported/UI/hud.cstyle";
    scene.registry.emplace<UIDocumentComponent>(e, ud);
    world.Update(scene.registry, 1280, 720, 0.016f);

    auto* doc = world.document(e);
    ASSERT_NE(doc, nullptr);
    EXPECT_NE(doc->document().root().Find("menuButton"), nullptr);
    EXPECT_EQ(doc->binder().unresolvedCount(), 0u)
        << "the shipped HUD has an unresolved binding with both contents installed";
}


// ------------------------------------- volume commit-on-release (U25b)
//
// A <Slider> writes menuVolume straight into the shared source through its
// two-way binding, so the persistence can no longer hang off a named action.
// Without a settle, a continuous drag would do a ProjectSettings
// load-modify-save once per input frame -- and the shipped Player takes exactly
// that branch, because it installs no onMasterVolume hook.

TEST(MenuUIContent, ADragAppliesAudioEveryFrameButWritesDiskOnce) {
    UIWorld world;
    int writes = 0;
    float lastWritten = -1.0f;
    MenuUIHooks h;
    h.initialVolume = 0.0f;
    h.onMasterVolume = [&](float v) { ++writes; lastWritten = v; };
    InstallMenuUIContent(world, h);
    UIDataSource& src = world.shared();

    // Prime, so the watch does not treat the seeded value as a change.
    MenuUIPublishCounters(world, h);
    ASSERT_EQ(writes, 0) << "installing wrote to disk";

    // 30 frames of a drag: the value moves every frame.
    for (int i = 1; i <= 30; ++i) {
        src.SetNumber("menuVolume", float(i) / 30.0f);
        MenuUIPublishCounters(world, h);
    }
    EXPECT_EQ(writes, 0) << "the disk was written DURING the drag";
    EXPECT_EQ(src.GetInt("menuVolumePct"), 100) << "the readout did not follow the drag";

    // Let go: the value stops moving and settles.
    for (int i = 0; i < 20; ++i) MenuUIPublishCounters(world, h);
    EXPECT_EQ(writes, 1) << "settling wrote " << writes << " times";
    EXPECT_NEAR(lastWritten, 1.0f, 1e-5f);

    // ...and it stays settled rather than rewriting every frame afterwards.
    for (int i = 0; i < 60; ++i) MenuUIPublishCounters(world, h);
    EXPECT_EQ(writes, 1) << "a settled value kept writing";
}

// The stepped buttons are a different gesture: a click is already settled, so
// it commits immediately rather than waiting a quarter of a second.
TEST(MenuUIContent, TheStepButtonsCommitImmediately) {
    UIWorld world;
    int writes = 0;
    MenuUIHooks h;
    h.initialVolume = 0.5f;
    h.volumeStep = 0.25f;
    h.onMasterVolume = [&](float) { ++writes; };
    InstallMenuUIContent(world, h);
    UIDataSource& src = world.shared();
    MenuUIPublishCounters(world, h);

    ASSERT_TRUE(invoke(src, "menuVolumeUp"));
    EXPECT_EQ(writes, 1) << "a click did not commit";
    EXPECT_NEAR(src.GetNumber("menuVolume"), 0.75f, 1e-5f);

    // The watch must not then ALSO commit it when it settles: the value it sees
    // is the one already written.
    for (int i = 0; i < 30; ++i) MenuUIPublishCounters(world, h);
    EXPECT_EQ(writes, 1) << "the settle double-wrote a value a click already committed";
}


// ------------------------------------ the shipped menu's navigation (U25e)
//
// The redesign is a STACK: the verb column and each panel are focus scopes, and
// opening a panel TAKES navigation instead of adding to it. These assert the
// shipped asset actually wires that up, because getting it wrong is invisible
// until somebody picks up a controller.

TEST(ShippedMenuAsset, TheVerbColumnAndEachPanelAreFocusScopes) {
    ShippedMenu m;
    m.Frame();
    for (const char* n : { "verbs", "settingsPanel", "systemPanel" }) {
        UIElement* e = m.find(n);
        ASSERT_NE(e, nullptr) << n;
        EXPECT_TRUE(e->isFocusScope())
            << "#" << n << " is not a focus scope, so opening it would ADD to the "
               "navigation ring rather than take it";
    }
}

TEST(ShippedMenuAsset, OpeningSettingsTakesNavigationAndBackReturnsIt) {
    ShippedMenu m;
    m.Frame();
    UIDataSource& src = m.world.shared();

    // Boot: the verb column owns navigation.
    UIElement* focused = m.doc().focused();
    ASSERT_NE(focused, nullptr) << "nothing was focused at boot - a pad would do nothing";
    UIElement* verbs = m.find("verbs");
    bool insideVerbs = false;
    for (UIElement* p = focused; p; p = p->parent()) if (p == verbs) insideVerbs = true;
    EXPECT_TRUE(insideVerbs) << "boot focus is not in the verb column";

    // Open SETTINGS the way its button does.
    ASSERT_TRUE(invoke(src, "menuOpenSettings"));
    m.Frame();
    EXPECT_TRUE(src.GetBool("panelOpen")) << "the verb column would still be visible";

    UIElement* panel = m.find("settingsPanel");
    UIElement* f2 = m.doc().focused();
    ASSERT_NE(f2, nullptr) << "opening SETTINGS left nothing focused";
    bool insidePanel = false;
    for (UIElement* p = f2; p; p = p->parent()) if (p == panel) insidePanel = true;
    EXPECT_TRUE(insidePanel) << "opening SETTINGS did not move focus into it";

    // Walk a long way: navigation must never escape back to NEW GAME.
    for (int i = 0; i < 20; ++i) {
        UINavState n;
        n.moves.push_back(UINavDir::Down);
        m.doc().UpdateNav(n);
        bool still = false;
        for (UIElement* p = m.doc().focused(); p; p = p->parent()) if (p == panel) still = true;
        ASSERT_TRUE(still) << "navigation escaped SETTINGS after " << i << " moves";
    }

    // Back closes it through the panel's on-back, and the app's bool follows.
    UINavState back;
    back.back = true;
    m.doc().UpdateNav(back);
    EXPECT_FALSE(src.GetBool("panelOpen")) << "back did not reach the panel's on-back";
    m.Frame();

    UIElement* f3 = m.doc().focused();
    ASSERT_NE(f3, nullptr);
    bool backInVerbs = false;
    for (UIElement* p = f3; p; p = p->parent()) if (p == verbs) backInVerbs = true;
    EXPECT_TRUE(backInVerbs) << "closing SETTINGS did not return navigation to the verbs";
}

// Only one panel at a time, and the verb column hides for BOTH -- otherwise a
// pad user cycles through NEW GAME behind an open page.
TEST(ShippedMenuAsset, OpeningOnePanelClosesTheOtherAndHidesTheVerbs) {
    ShippedMenu m;
    m.Frame();
    UIDataSource& src = m.world.shared();

    ASSERT_TRUE(invoke(src, "menuOpenSettings"));
    m.Frame();
    EXPECT_TRUE(src.GetBool("panelSettings"));
    EXPECT_FALSE(src.GetBool("panelSystem"));
    EXPECT_EQ(m.find("verbs")->style().display, DisplayMode::None);

    ASSERT_TRUE(invoke(src, "menuOpenSystem"));
    m.Frame();
    EXPECT_FALSE(src.GetBool("panelSettings"));
    EXPECT_TRUE(src.GetBool("panelSystem"));
    EXPECT_EQ(m.find("settingsPanel")->style().display, DisplayMode::None);

    ASSERT_TRUE(invoke(src, "menuClosePanel"));
    m.Frame();
    EXPECT_FALSE(src.GetBool("panelOpen"));
    EXPECT_NE(m.find("verbs")->style().display, DisplayMode::None);
}


// ------------------------------------------------- the press flash (U26a)
//
// pressed_ is written only on the pointer-down edge, so `:active` -- the one
// press state a stylesheet can express -- was unreachable from a gamepad or
// from Enter. On the input this menu was rewritten for, confirming anything
// changed no pixels at all.

TEST(UIActivateFlash, ActivatingHoldsThePressLongEnoughToSee) {
    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc,
        R"(<UI><Button name="b" text="go"/></UI>)", errors, "t.cxml"));
    doc.Layout(400.f, 400.f);
    UIElement* b = doc.root().Find("b");
    ASSERT_NE(b, nullptr);
    ASSERT_FALSE(b->isPressed());

    doc.SetFocus(b);
    ASSERT_TRUE(doc.ActivateFocused());
    EXPECT_TRUE(b->isPressed()) << "activating produced no press state at all";

    // Still lit a frame later: a one-frame flash is not a flash.
    doc.AdvanceTime(1.0f / 60.0f);
    EXPECT_TRUE(b->isPressed()) << "the flash lasted a single frame";

    // ...and released once the flash has been on screen long enough.
    doc.AdvanceTime(UIDocument::kActivateFlashSeconds);
    EXPECT_FALSE(b->isPressed()) << "the press never released";
}

// A deadline on the document clock rather than a countdown, so one long frame
// releases it rather than stretching the flash.
TEST(UIActivateFlash, ALongFrameReleasesItRatherThanStretchingIt) {
    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc,
        R"(<UI><Button name="b" text="go"/></UI>)", errors, "t.cxml"));
    doc.Layout(400.f, 400.f);
    UIElement* b = doc.root().Find("b");
    doc.SetFocus(b);
    doc.ActivateFocused();

    doc.AdvanceTime(2.0f);          // one very long frame
    EXPECT_FALSE(b->isPressed());
}

// Activating a SECOND element while the first is still lit must not leave the
// first stuck pressed forever.
TEST(UIActivateFlash, ASecondActivationReleasesTheFirst) {
    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc,
        R"(<UI><Button name="a" text="a"/><Button name="b" text="b"/></UI>)",
        errors, "t.cxml"));
    doc.Layout(400.f, 400.f);
    UIElement* a = doc.root().Find("a");
    UIElement* b = doc.root().Find("b");

    doc.SetFocus(a);
    doc.ActivateFocused();
    ASSERT_TRUE(a->isPressed());

    doc.SetFocus(b);
    doc.ActivateFocused();
    EXPECT_FALSE(a->isPressed()) << "the first button stayed pressed forever";
    EXPECT_TRUE(b->isPressed());
}

// A handler fired BY the press may remove the element it was on -- a menu
// button that swaps the scene is exactly that -- so the release revalidates.
TEST(UIActivateFlash, ReleasingSurvivesTheElementBeingRemoved) {
    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc,
        R"(<UI><Element name="wrap"><Button name="b" text="go"/></Element></UI>)",
        errors, "t.cxml"));
    doc.Layout(400.f, 400.f);
    doc.SetFocus(doc.root().Find("b"));
    doc.ActivateFocused();

    ASSERT_NE(doc.root().Find("wrap")->RemoveChild(doc.root().Find("b")), nullptr);
    doc.AdvanceTime(1.0f);          // must not touch freed memory
    SUCCEED();
}

// ------------------------------------------- the shipped sheet's cascade
//
// A pseudo-class counts as a class, so `.chip:focus` (0,2,0) beat a bare
// `.chip-on` (0,1,0) and took ALL of its declarations with it. On a pad focus
// is always somewhere, so the selected quality tier went blank exactly when it
// was being read.

TEST(ShippedMenuStyle, ASelectedChipStaysSelectedWhileFocused) {
    UIStyleSheet sheet;
    ASSERT_TRUE(sheet.LoadFromFile("Exported/UI/menu.cstyle"))
        << (sheet.errors().empty() ? std::string() : sheet.errors()[0]);

    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc,
        R"(<UI><Button name="c" class="chip"/></UI>)", errors, "t.cxml"));
    UIElement* c = doc.root().Find("c");
    ASSERT_NE(c, nullptr);

    // Selected but not focused: the gold fill states the current value.
    c->AddClass("chip-on");
    sheet.ApplyTo(doc.root());
    const glm::vec4 lit = c->style().backgroundColor;
    ASSERT_GT(lit.r, 0.9f);
    ASSERT_GT(lit.a, 0.8f) << "a selected chip is not filled at all";

    // ...and focused, it must still be recognisably the selected one.
    doc.SetFocus(c);
    sheet.Recascade(*c);
    EXPECT_GT(c->style().backgroundColor.a, 0.8f)
        << "the selected chip lost its fill the moment it was focused";
    EXPECT_NEAR(c->style().backgroundColor.r, lit.r, 0.02f);
}

// `border-width` is paint-only and never insets the content box, so a fill at
// 100% paints straight over the track's focus ring -- and they were the same
// colour. A focused slider at full volume was pixel-identical to an unfocused
// one.
TEST(ShippedMenuStyle, AFocusedSliderIsDistinguishableAtFullValue) {
    UIStyleSheet sheet;
    ASSERT_TRUE(sheet.LoadFromFile("Exported/UI/menu.cstyle"))
        << (sheet.errors().empty() ? std::string() : sheet.errors()[0]);

    UIDocument doc;
    std::vector<std::string> errors;
    ASSERT_TRUE(UIMarkup::LoadInto(doc,
        R"(<UI><Slider name="s" class="slider" min="0" max="1" value="1">)"
        R"(<Element name="f" class="slider-fill"/></Slider></UI>)",
        errors, "t.cxml")) << (errors.empty() ? "" : errors[0]);
    sheet.ApplyTo(doc.root());

    UIElement* s = doc.root().Find("s");
    UIElement* f = doc.root().Find("f");
    ASSERT_NE(s, nullptr);
    ASSERT_NE(f, nullptr);
    const glm::vec4 idle = f->style().backgroundColor;

    doc.SetFocus(s);
    sheet.RecascadeSubtree(*s);
    const glm::vec4 lit = f->style().backgroundColor;
    EXPECT_GT(std::abs(lit.r - idle.r) + std::abs(lit.g - idle.g) +
              std::abs(lit.b - idle.b), 0.05f)
        << "the FILL does not change on focus, and at 100% it covers the ring - "
           "a focused slider is invisible";
}

// The shipped menu's own settings page, which is where the complaint came from.
TEST(ShippedMenuAsset, DownClearsTheQualityRowInOnePress) {
    ShippedMenu m;
    m.Frame();
    UIDataSource& src = m.world.shared();
    ASSERT_TRUE(invoke(src, "menuOpenSettings"));
    m.Frame();

    // Walk the whole panel top to bottom and record where focus lands.
    //
    // Counting by CLASS would be wrong: VSYNC is a chip too, in a row of its
    // own, and stopping on it is correct. The invariant is about the QUALITY
    // ROW -- three controls sharing one line -- so name those three.
    std::vector<std::string> walk;
    for (int i = 0; i < 12; ++i) {
        UINavState n;
        n.moves.push_back(UINavDir::Down);
        m.doc().UpdateNav(n);
        UIElement* f = m.doc().focused();
        if (!f) break;
        if (!walk.empty() && f->name() == walk.back()) break;  // bottom, no wrap
        walk.push_back(f->name());
    }
    const auto stops = [&](const char* n) {
        return (int)std::count(walk.begin(), walk.end(), std::string(n));
    };
    const std::string trail = [&] {
        std::string s;
        for (const std::string& n : walk) s += (s.empty() ? "" : " -> ") + n;
        return s;
    }();

    EXPECT_LE(stops("qLow") + stops("qMed") + stops("qHigh"), 1)
        << "the quality row cost more than one press: " << trail;

    // ...and the row AFTER it is not skipped on the way past. VSYNC sits alone
    // in its row while PILOT below is full width, which is the geometry that
    // used to let the field steal the press.
    EXPECT_EQ(stops("vsync"), 1) << "VSYNC was skipped walking down: " << trail;
    EXPECT_EQ(stops("pilot"), 1) << "PILOT was never reached: " << trail;
}
