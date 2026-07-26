// UI as scene content: UIDocumentComponent, its serialization, and the UIWorld
// that turns "a component exists" into "it draws".
//
// Pure CPU. Three things this has to get right, all of them learned the hard
// way elsewhere in this engine:
//
//  - a new component must be wired into the serializer AND the undo snapshot,
//    or undoing an unrelated edit silently strips it (EntitySnapshot is a
//    CLOSED list — anything missing from it vanishes on restore);
//  - authored paths go through the containment gate, because markup and
//    stylesheets flow straight into parsers;
//  - live documents are CACHED by entity, so toggling a flag must not throw
//    away a parsed tree, a binder index and the hot-reload stamps with it.
#include <gtest/gtest.h>

#include "Engine.h"
#include "UndoHistory.h"
#include "../Engine/src/ui/UIComponent.h"
#include "../Engine/src/ui/UIWorld.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace MyCoreEngine;

namespace {

void writeFile(const std::string& path, const std::string& text) {
    std::ofstream o(path, std::ios::binary);
    o << text;
}

const char* kMarkup = R"(<UI data-source="scene"><Label name="l" text="HP {hp}"
                          style="width: 100px; height: 20px"/></UI>)";

} // namespace

// ------------------------------------------------------------ serialization

TEST(UIComponent, RoundTripsThroughTheSceneFile) {
    const char* path = "test_ui_component_scene.json";
    {
        Scene scene;
        AssetManager assets;
        auto e = scene.registry.create();
        scene.registry.emplace<Transform>(e);
        UIDocumentComponent ud;
        ud.markup = "Exported/UI/hud.uxml";
        ud.stylesheet = "Exported/UI/hud.uss";
        ud.sortOrder = 7;
        ud.enabled = false;
        ud.interactive = false;
        scene.registry.emplace<UIDocumentComponent>(e, ud);
        SceneSerializer s(scene, assets);
        ASSERT_TRUE(s.Save(path));
    }
    {
        Scene scene;
        AssetManager assets;
        SceneSerializer s(scene, assets);
        ASSERT_TRUE(s.Load(path));
        auto view = scene.registry.view<UIDocumentComponent>();
        ASSERT_EQ(view.size(), 1u);
        const UIDocumentComponent& ud = view.get<UIDocumentComponent>(view.front());
        EXPECT_EQ(ud.markup, "Exported/UI/hud.uxml");
        EXPECT_EQ(ud.stylesheet, "Exported/UI/hud.uss");
        EXPECT_EQ(ud.sortOrder, 7);
        EXPECT_FALSE(ud.enabled);
        EXPECT_FALSE(ud.interactive);
    }
    std::remove(path);
}

// Markup and stylesheets flow straight into parsers, so their paths get the
// same gate as models, scripts, clips and HDRis. The COMPONENT survives with an
// empty path, exactly like a rejected model — dropping the whole entity's UI
// would be a bigger surprise than a UI that does not appear.
TEST(UIComponent, RejectsPathsOutsideTheProject) {
    const char* path = "test_ui_component_evil.json";
    writeFile(path, R"({
        "version": 1,
        "entities": [
          { "name": "e",
            "uiDocument": { "markup": "../../evil.uxml",
                            "stylesheet": "C:/Windows/evil.uss" } }
        ]
      })");

    Scene scene;
    AssetManager assets;
    SceneSerializer s(scene, assets);
    ASSERT_TRUE(s.Load(path));
    auto view = scene.registry.view<UIDocumentComponent>();
    ASSERT_EQ(view.size(), 1u);
    const UIDocumentComponent& ud = view.get<UIDocumentComponent>(view.front());
    EXPECT_TRUE(ud.markup.empty()) << "a traversal path survived the gate";
    EXPECT_TRUE(ud.stylesheet.empty()) << "an absolute path survived the gate";
    std::remove(path);
}

// ------------------------------------------------------------------- undo

// EntitySnapshot is a CLOSED list: a component missing from it is silently
// destroyed by any restore, so this is the test that catches the omission.
TEST(UIComponent, SurvivesUndoOfAnUnrelatedEdit) {
    Scene scene;
    UndoHistory undo;
    auto e = scene.registry.create();
    scene.registry.emplace<Transform>(e);
    UIDocumentComponent ud;
    ud.markup = "Exported/UI/hud.uxml";
    ud.sortOrder = 3;
    scene.registry.emplace<UIDocumentComponent>(e, ud);

    // An edit that has nothing to do with the UI.
    undo.record(scene.registry, e, "Move", [&] {
        scene.registry.get<Transform>(e).position = { 5.f, 0.f, 0.f };
    });
    undo.undo(scene.registry, nullptr);

    ASSERT_TRUE(scene.registry.all_of<UIDocumentComponent>(e))
        << "undoing a move destroyed the entity's UI document";
    const auto& back = scene.registry.get<UIDocumentComponent>(e);
    EXPECT_EQ(back.markup, "Exported/UI/hud.uxml");
    EXPECT_EQ(back.sortOrder, 3);
}

TEST(UIComponent, AddAndRemoveAreUndoable) {
    Scene scene;
    UndoHistory undo;
    auto e = scene.registry.create();
    scene.registry.emplace<Transform>(e);

    undo.record(scene.registry, e, "Add UI", [&] {
        UIDocumentComponent ud;
        ud.markup = "Exported/UI/hud.uxml";
        scene.registry.emplace<UIDocumentComponent>(e, ud);
    });
    ASSERT_TRUE(scene.registry.all_of<UIDocumentComponent>(e));
    undo.undo(scene.registry, nullptr);
    EXPECT_FALSE(scene.registry.all_of<UIDocumentComponent>(e)) << "add was not undone";
    undo.redo(scene.registry, nullptr);
    EXPECT_TRUE(scene.registry.all_of<UIDocumentComponent>(e)) << "add was not redone";

    undo.record(scene.registry, e, "Remove UI",
                [&] { scene.registry.remove<UIDocumentComponent>(e); });
    ASSERT_FALSE(scene.registry.all_of<UIDocumentComponent>(e));
    undo.undo(scene.registry, nullptr);
    EXPECT_TRUE(scene.registry.all_of<UIDocumentComponent>(e)) << "remove was not undone";
}

// A change to any field must be a distinct undo step, which means snapEq has to
// compare every one of them.
TEST(UIComponent, EveryFieldIsComparedForUndo) {
    Scene scene;
    auto e = scene.registry.create();
    scene.registry.emplace<Transform>(e);
    scene.registry.emplace<UIDocumentComponent>(e);

    struct Case { const char* what; void (*edit)(UIDocumentComponent&); };
    for (const Case& c : {
             Case{ "markup",      [](UIDocumentComponent& u) { u.markup = "a.uxml"; } },
             Case{ "stylesheet",  [](UIDocumentComponent& u) { u.stylesheet = "a.uss"; } },
             Case{ "sortOrder",   [](UIDocumentComponent& u) { u.sortOrder = 9; } },
             Case{ "enabled",     [](UIDocumentComponent& u) { u.enabled = false; } },
             Case{ "interactive", [](UIDocumentComponent& u) { u.interactive = false; } } }) {
        scene.registry.replace<UIDocumentComponent>(e, UIDocumentComponent{});
        UndoHistory undo;
        undo.record(scene.registry, e, c.what,
                    [&] { c.edit(scene.registry.get<UIDocumentComponent>(e)); });
        undo.undo(scene.registry, nullptr);
        const UIDocumentComponent back = scene.registry.get<UIDocumentComponent>(e);
        const UIDocumentComponent fresh{};
        EXPECT_EQ(back.markup, fresh.markup) << c.what;
        EXPECT_EQ(back.stylesheet, fresh.stylesheet) << c.what;
        EXPECT_EQ(back.sortOrder, fresh.sortOrder) << c.what;
        EXPECT_EQ(back.enabled, fresh.enabled) << c.what;
        EXPECT_EQ(back.interactive, fresh.interactive) << c.what;
    }
}

// ---------------------------------------------------------------- UIWorld

TEST(UIWorldTest, LoadsDrawsAndReconcilesAgainstTheRegistry) {
    const std::string m = "test_uiworld.uxml";
    writeFile(m, kMarkup);

    Scene scene;
    UIWorld world;
    world.shared().SetInt("hp", 42);

    auto e = scene.registry.create();
    UIDocumentComponent ud;
    ud.markup = m;
    scene.registry.emplace<UIDocumentComponent>(e, ud);

    world.Update(scene.registry, 800, 600, 0.016f);
    EXPECT_EQ(world.liveCount(), 1u);
    ASSERT_NE(world.document(e), nullptr);
    // The shared source is registered for every document under one name, so
    // markup can bind to gameplay values without per-document wiring.
    EXPECT_EQ(world.document(e)->document().root().Find("l")->style().text, "HP 42");

    // Removing the component drops the live document, or the cache would keep
    // drawing UI for an entity that no longer declares any.
    scene.registry.remove<UIDocumentComponent>(e);
    world.Update(scene.registry, 800, 600, 0.016f);
    EXPECT_EQ(world.liveCount(), 0u);
    EXPECT_EQ(world.document(e), nullptr);
    std::remove(m.c_str());
}

TEST(UIWorldTest, ADestroyedEntityDropsItsDocument) {
    const std::string m = "test_uiworld_destroy.uxml";
    writeFile(m, kMarkup);
    Scene scene;
    UIWorld world;
    auto e = scene.registry.create();
    UIDocumentComponent ud;
    ud.markup = m;
    scene.registry.emplace<UIDocumentComponent>(e, ud);
    world.Update(scene.registry, 800, 600, 0.016f);
    ASSERT_EQ(world.liveCount(), 1u);

    scene.registry.destroy(e);
    world.Update(scene.registry, 800, 600, 0.016f);
    EXPECT_EQ(world.liveCount(), 0u) << "a destroyed entity left a live document";
    std::remove(m.c_str());
}

// Reloading only on a PATH change is the whole reason documents are cached: a
// UIAssetDocument owns a parsed tree, a binder index and hot-reload stamps, and
// re-reading two files because a bool flipped would throw all three away.
TEST(UIWorldTest, TogglingAFlagDoesNotReloadTheDocument) {
    const std::string m = "test_uiworld_flags.uxml";
    writeFile(m, kMarkup);
    Scene scene;
    UIWorld world;
    auto e = scene.registry.create();
    UIDocumentComponent ud;
    ud.markup = m;
    scene.registry.emplace<UIDocumentComponent>(e, ud);
    world.Update(scene.registry, 800, 600, 0.016f);

    ui::UIAssetDocument* before = world.document(e);
    ASSERT_NE(before, nullptr);

    scene.registry.get<UIDocumentComponent>(e).sortOrder = 5;
    scene.registry.get<UIDocumentComponent>(e).interactive = false;
    world.Update(scene.registry, 800, 600, 0.016f);
    EXPECT_EQ(world.document(e), before) << "a flag change rebuilt the document";

    // ...but re-pointing it DOES reload.
    const std::string m2 = "test_uiworld_flags2.uxml";
    writeFile(m2, R"(<UI><Label name="other" text="x"/></UI>)");
    scene.registry.get<UIDocumentComponent>(e).markup = m2;
    world.Update(scene.registry, 800, 600, 0.016f);
    ASSERT_NE(world.document(e), nullptr);
    EXPECT_NE(world.document(e)->document().root().Find("other"), nullptr);
    std::remove(m.c_str());
    std::remove(m2.c_str());
}

TEST(UIWorldTest, DisabledDocumentsAreSkipped) {
    const std::string m = "test_uiworld_disabled.uxml";
    writeFile(m, kMarkup);
    Scene scene;
    UIWorld world;
    auto e = scene.registry.create();
    UIDocumentComponent ud;
    ud.markup = m;
    ud.enabled = false;
    scene.registry.emplace<UIDocumentComponent>(e, ud);
    world.Update(scene.registry, 800, 600, 0.016f);
    // Still LOADED — flipping it back on must not re-read the file — but not run.
    EXPECT_EQ(world.liveCount(), 1u);
    EXPECT_NE(world.document(e), nullptr);
    std::remove(m.c_str());
}

// Input goes to ONE document, or a pause menu and the HUD beneath it both react
// to the same click.
TEST(UIWorldTest, InputGoesToTheTopmostInteractiveDocument) {
    const std::string lo = "test_uiworld_lo.uxml";
    const std::string hi = "test_uiworld_hi.uxml";
    writeFile(lo, R"(<UI><Element name="b" focusable="true"
                       style="width: 400px; height: 400px"/></UI>)");
    writeFile(hi, R"(<UI><Element name="b" focusable="true"
                       style="width: 400px; height: 400px"/></UI>)");

    Scene scene;
    UIWorld world;
    auto low = scene.registry.create();
    auto high = scene.registry.create();
    { UIDocumentComponent u; u.markup = lo; u.sortOrder = 0;
      scene.registry.emplace<UIDocumentComponent>(low, u); }
    { UIDocumentComponent u; u.markup = hi; u.sortOrder = 10;
      scene.registry.emplace<UIDocumentComponent>(high, u); }

    ui::UIPointerState p;
    p.inside = true;
    p.position = { 50.f, 50.f };
    p.buttonDown = true;
    world.SetPointer(p);
    world.Update(scene.registry, 400, 400, 0.016f);
    p.buttonDown = false;
    world.SetPointer(p);
    world.Update(scene.registry, 400, 400, 0.016f);

    ASSERT_NE(world.document(high), nullptr);
    ASSERT_NE(world.document(low), nullptr);
    EXPECT_NE(world.document(high)->document().focused(), nullptr)
        << "the topmost document did not get the click";
    EXPECT_EQ(world.document(low)->document().focused(), nullptr)
        << "the document underneath also reacted";
    std::remove(lo.c_str());
    std::remove(hi.c_str());
}

// A decorative overlay must not swallow clicks meant for what is under it.
TEST(UIWorldTest, ANonInteractiveDocumentLetsInputThrough) {
    const std::string a = "test_uiworld_deco.uxml";
    const std::string b = "test_uiworld_under.uxml";
    writeFile(a, R"(<UI><Element name="x" style="width: 400px; height: 400px"/></UI>)");
    writeFile(b, R"(<UI><Element name="b" focusable="true"
                      style="width: 400px; height: 400px"/></UI>)");

    Scene scene;
    UIWorld world;
    auto deco = scene.registry.create();
    auto under = scene.registry.create();
    { UIDocumentComponent u; u.markup = a; u.sortOrder = 10; u.interactive = false;
      scene.registry.emplace<UIDocumentComponent>(deco, u); }
    { UIDocumentComponent u; u.markup = b; u.sortOrder = 0;
      scene.registry.emplace<UIDocumentComponent>(under, u); }

    ui::UIPointerState p;
    p.inside = true;
    p.position = { 50.f, 50.f };
    p.buttonDown = true;
    world.SetPointer(p);
    world.Update(scene.registry, 400, 400, 0.016f);
    p.buttonDown = false;
    world.SetPointer(p);
    world.Update(scene.registry, 400, 400, 0.016f);

    EXPECT_NE(world.document(under)->document().focused(), nullptr)
        << "the decorative overlay swallowed the click";
    std::remove(a.c_str());
    std::remove(b.c_str());
}

TEST(UIWorldTest, ABrokenDocumentIsReportedAndTheRestKeepRunning) {
    const std::string good = "test_uiworld_good.uxml";
    const std::string bad = "test_uiworld_bad.uxml";
    writeFile(good, kMarkup);
    writeFile(bad, "<UI><Element");   // unterminated

    Scene scene;
    UIWorld world;
    world.shared().SetInt("hp", 7);
    auto g = scene.registry.create();
    auto b = scene.registry.create();
    { UIDocumentComponent u; u.markup = good; scene.registry.emplace<UIDocumentComponent>(g, u); }
    { UIDocumentComponent u; u.markup = bad;  scene.registry.emplace<UIDocumentComponent>(b, u); }

    world.Update(scene.registry, 800, 600, 0.016f);
    EXPECT_FALSE(world.errors().empty()) << "a broken document loaded silently";
    ASSERT_NE(world.document(g), nullptr);
    EXPECT_EQ(world.document(g)->document().root().Find("l")->style().text, "HP 7")
        << "one broken document took the others down with it";
    std::remove(good.c_str());
    std::remove(bad.c_str());
}

TEST(UIWorldTest, ClearDropsEverything) {
    const std::string m = "test_uiworld_clear.uxml";
    writeFile(m, kMarkup);
    Scene scene;
    UIWorld world;
    auto e = scene.registry.create();
    UIDocumentComponent ud;
    ud.markup = m;
    scene.registry.emplace<UIDocumentComponent>(e, ud);
    world.Update(scene.registry, 800, 600, 0.016f);
    ASSERT_EQ(world.liveCount(), 1u);
    world.Clear();
    EXPECT_EQ(world.liveCount(), 0u);
    EXPECT_EQ(world.document(e), nullptr);
    std::remove(m.c_str());
}

TEST(UIWorldTest, AnEmptyMarkupPathLoadsNothingAndIsNotAnError) {
    Scene scene;
    UIWorld world;
    auto e = scene.registry.create();
    scene.registry.emplace<UIDocumentComponent>(e);   // markup is ""
    world.Update(scene.registry, 800, 600, 0.016f);
    EXPECT_EQ(world.document(e), nullptr);
    EXPECT_TRUE(world.errors().empty());
}

// ------------------------------------------------------- input regions

// A region lets a document occupy PART of the surface. Layout runs at the
// region's size and the rects are offset into place, so painting, hit-testing
// and clipping all follow from one origin.
TEST(UIWorldTest, ARegionOffsetsAndSizesTheDocument) {
    const std::string m = "test_uiworld_region.uxml";
    writeFile(m, R"(<UI><Element name="fill" style="width: 100%; height: 100%"/></UI>)");

    Scene scene;
    UIWorld world;
    auto e = scene.registry.create();
    UIDocumentComponent ud;
    ud.markup = m;
    // The right half, lower two-thirds.
    ud.regionX = 0.5f; ud.regionY = 0.25f;
    ud.regionW = 0.5f; ud.regionH = 0.75f;
    scene.registry.emplace<UIDocumentComponent>(e, ud);
    world.Update(scene.registry, 800, 400, 0.016f);

    ASSERT_NE(world.document(e), nullptr);
    ui::UIElement* fill = world.document(e)->document().root().Find("fill");
    ASSERT_NE(fill, nullptr);
    // 100% is 100% of the REGION, and the rect is absolute on the surface.
    EXPECT_FLOAT_EQ(fill->layout().size.x, 400.f);
    EXPECT_FLOAT_EQ(fill->layout().size.y, 300.f);
    EXPECT_FLOAT_EQ(fill->layout().position.x, 400.f);
    EXPECT_FLOAT_EQ(fill->layout().position.y, 100.f);
}

// Hit-testing reads the same absolute rects, so a click outside the region
// simply misses — no separate containment check needed anywhere.
TEST(UIWorldTest, ARegionAlsoBoundsInput) {
    const std::string m = "test_uiworld_region_input.uxml";
    writeFile(m, R"(<UI><Element name="b" focusable="true"
                      style="width: 100%; height: 100%"/></UI>)");
    Scene scene;
    UIWorld world;
    auto e = scene.registry.create();
    UIDocumentComponent ud;
    ud.markup = m;
    ud.regionX = 0.5f; ud.regionW = 0.5f;
    scene.registry.emplace<UIDocumentComponent>(e, ud);

    ui::UIPointerState p;
    p.inside = true;
    p.position = { 100.f, 100.f };   // left half: outside the region
    p.buttonDown = true;
    world.SetPointer(p);
    world.Update(scene.registry, 800, 400, 0.016f);
    p.buttonDown = false;
    world.SetPointer(p);
    world.Update(scene.registry, 800, 400, 0.016f);
    EXPECT_EQ(world.document(e)->document().focused(), nullptr)
        << "a click outside the region reached the document";

    p.position = { 600.f, 100.f };   // right half: inside
    p.buttonDown = true;
    world.SetPointer(p);
    world.Update(scene.registry, 800, 400, 0.016f);
    p.buttonDown = false;
    world.SetPointer(p);
    world.Update(scene.registry, 800, 400, 0.016f);
    EXPECT_NE(world.document(e)->document().focused(), nullptr)
        << "a click inside the region did not reach it";
    std::remove(m.c_str());
}

TEST(UIWorldTest, RegionsAreClampedAndDefaultToTheWholeSurface) {
    const std::string m = "test_uiworld_region_clamp.uxml";
    writeFile(m, R"(<UI><Element name="fill" style="width: 100%; height: 100%"/></UI>)");
    Scene scene;
    UIWorld world;
    auto e = scene.registry.create();
    scene.registry.emplace<UIDocumentComponent>(e);   // defaults
    world.Update(scene.registry, 800, 400, 0.016f);
    EXPECT_EQ(world.document(e), nullptr) << "no markup, nothing loaded";

    scene.registry.get<UIDocumentComponent>(e).markup = m;
    world.Update(scene.registry, 800, 400, 0.016f);
    ui::UIElement* fill = world.document(e)->document().root().Find("fill");
    ASSERT_NE(fill, nullptr);
    EXPECT_FLOAT_EQ(fill->layout().size.x, 800.f) << "the default is the whole surface";

    // Nonsense values are clamped rather than producing a negative box.
    auto& c = scene.registry.get<UIDocumentComponent>(e);
    c.regionX = 0.8f; c.regionW = 5.0f;   // would run off the right edge
    world.Update(scene.registry, 800, 400, 0.016f);
    EXPECT_FLOAT_EQ(fill->layout().size.x, 160.f) << "width was not clamped to what is left";
    c.regionX = -3.0f; c.regionW = -1.0f;
    world.Update(scene.registry, 800, 400, 0.016f);
    EXPECT_GE(fill->layout().size.x, 0.f) << "a negative region produced a negative box";
    std::remove(m.c_str());
}

TEST(UIComponent, RegionRoundTripsAndIsUndoable) {
    const char* path = "test_ui_region_scene.json";
    {
        Scene scene;
        AssetManager assets;
        auto e = scene.registry.create();
        UIDocumentComponent ud;
        ud.markup = "Exported/UI/hud.uxml";
        ud.regionX = 0.25f; ud.regionY = 0.5f; ud.regionW = 0.5f; ud.regionH = 0.25f;
        scene.registry.emplace<UIDocumentComponent>(e, ud);
        SceneSerializer s(scene, assets);
        ASSERT_TRUE(s.Save(path));
    }
    {
        Scene scene;
        AssetManager assets;
        SceneSerializer s(scene, assets);
        ASSERT_TRUE(s.Load(path));
        auto view = scene.registry.view<UIDocumentComponent>();
        ASSERT_EQ(view.size(), 1u);
        const auto& ud = view.get<UIDocumentComponent>(view.front());
        EXPECT_FLOAT_EQ(ud.regionX, 0.25f);
        EXPECT_FLOAT_EQ(ud.regionY, 0.5f);
        EXPECT_FLOAT_EQ(ud.regionW, 0.5f);
        EXPECT_FLOAT_EQ(ud.regionH, 0.25f);
    }
    std::remove(path);

    // A scene saved before regions existed must load to the whole surface, not
    // to a zero-area document.
    const char* old = "test_ui_region_old.json";
    writeFile(old, R"({"version":1,"entities":[
        {"name":"e","uiDocument":{"markup":"Exported/UI/hud.uxml"}}]})");
    {
        Scene scene;
        AssetManager assets;
        SceneSerializer s(scene, assets);
        ASSERT_TRUE(s.Load(old));
        auto view = scene.registry.view<UIDocumentComponent>();
        ASSERT_EQ(view.size(), 1u);
        const auto& ud = view.get<UIDocumentComponent>(view.front());
        EXPECT_FLOAT_EQ(ud.regionW, 1.0f);
        EXPECT_FLOAT_EQ(ud.regionH, 1.0f);
    }
    std::remove(old);

    // ...and every region field is compared for undo.
    Scene scene;
    auto e = scene.registry.create();
    scene.registry.emplace<Transform>(e);
    scene.registry.emplace<UIDocumentComponent>(e);
    UndoHistory undo;
    undo.record(scene.registry, e, "Region",
                [&] { scene.registry.get<UIDocumentComponent>(e).regionW = 0.5f; });
    undo.undo(scene.registry, nullptr);
    EXPECT_FLOAT_EQ(scene.registry.get<UIDocumentComponent>(e).regionW, 1.0f);
}
