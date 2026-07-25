// UI pointer input: hit-testing, hover/press state, and DOM-style bubbling.
//
// Pure CPU — no GL context needed, because layout and hit-testing are both CPU
// work. That is worth preserving: input logic is exactly the sort of thing you
// want to test exhaustively without a GPU.
#include <gtest/gtest.h>

#include "Engine.h"
#include "../Engine/src/ui/UIElement.h"

#include <string>
#include <vector>

using namespace MyCoreEngine;
using namespace MyCoreEngine::ui;

namespace {

// Two boxes side by side, each 100x100, in a 400x200 viewport.
struct TwoBoxes {
    UIDocument doc;
    UIElement* left = nullptr;
    UIElement* right = nullptr;

    TwoBoxes() {
        doc.root().style().direction = FlexDirection::Row;
        left = doc.root().AddChild("left");
        left->style().width = StyleLength::Px(100.f);
        left->style().height = StyleLength::Px(100.f);
        right = doc.root().AddChild("right");
        right->style().width = StyleLength::Px(100.f);
        right->style().height = StyleLength::Px(100.f);
        doc.Layout(400.f, 200.f);
    }
};

UIPointerState at(float x, float y, bool down = false) {
    UIPointerState p;
    p.position = { x, y };
    p.inside = true;
    p.buttonDown = down;
    return p;
}

} // namespace

TEST(UIInput, HitTestFindsTheDeepestElement) {
    UIDocument doc;
    UIElement* panel = doc.root().AddChild("panel");
    panel->style().width = StyleLength::Px(200.f);
    panel->style().height = StyleLength::Px(200.f);
    panel->style().padding = Edges::All(20.f);
    UIElement* inner = panel->AddChild("inner");
    inner->style().width = StyleLength::Px(50.f);
    inner->style().height = StyleLength::Px(50.f);
    doc.Layout(400.f, 400.f);

    // Inside the child -> the CHILD, not the panel that also contains the point.
    EXPECT_EQ(doc.HitTest({ 30.f, 30.f }), inner);
    // In the panel's padding, outside the child -> the panel.
    EXPECT_EQ(doc.HitTest({ 5.f, 5.f }), panel);
    // Outside everything -> the root still contains it (it fills the viewport).
    EXPECT_EQ(doc.HitTest({ 300.f, 300.f }), &doc.root());
}

// Later siblings paint on top, so they must also win the hit.
TEST(UIInput, TopmostSiblingWinsOverlap) {
    UIDocument doc;
    UIElement* under = doc.root().AddChild("under");
    under->style().position = PositionType::Absolute;
    under->style().inset = { 0.f, 0.f, 0.f, 0.f };
    under->style().width = StyleLength::Px(100.f);
    under->style().height = StyleLength::Px(100.f);
    UIElement* over = doc.root().AddChild("over");
    over->style().position = PositionType::Absolute;
    over->style().inset = { 0.f, 0.f, 0.f, 0.f };
    over->style().width = StyleLength::Px(100.f);
    over->style().height = StyleLength::Px(100.f);
    doc.Layout(400.f, 400.f);

    EXPECT_EQ(doc.HitTest({ 50.f, 50.f }), over)
        << "the later (topmost) sibling must win an overlap";
}

// pointer-events: none. A decorative full-screen layer must not swallow clicks.
TEST(UIInput, UnpickableSubtreesAreInert) {
    UIDocument doc;
    UIElement* button = doc.root().AddChild("button");
    button->style().position = PositionType::Absolute;
    button->style().inset = { 0.f, 0.f, 0.f, 0.f };
    button->style().width = StyleLength::Px(100.f);
    button->style().height = StyleLength::Px(100.f);

    UIElement* overlay = doc.root().AddChild("overlay"); // drawn on top
    overlay->style().position = PositionType::Absolute;
    overlay->style().inset = { 0.f, 0.f, 0.f, 0.f };
    overlay->style().pickable = false;
    UIElement* deco = overlay->AddChild("deco");
    deco->style().width = StyleLength::Px(100.f);
    deco->style().height = StyleLength::Px(100.f);
    doc.Layout(400.f, 400.f);

    UIElement* hit = doc.HitTest({ 50.f, 50.f });
    EXPECT_EQ(hit, button) << "an unpickable overlay swallowed the pointer";
    EXPECT_NE(hit, deco) << "a child of an unpickable element must also be inert";
}

TEST(UIInput, OverflowHiddenRejectsClippedChildren) {
    UIDocument doc;
    UIElement* clipper = doc.root().AddChild("clipper");
    clipper->style().width = StyleLength::Px(50.f);
    clipper->style().height = StyleLength::Px(50.f);
    clipper->style().overflowHidden = true;
    UIElement* child = clipper->AddChild("child");
    child->style().position = PositionType::Absolute;
    child->style().inset = { 0.f, 0.f, 0.f, 0.f };
    child->style().width = StyleLength::Px(300.f); // escapes the parent
    child->style().height = StyleLength::Px(300.f);
    doc.Layout(400.f, 400.f);

    EXPECT_EQ(doc.HitTest({ 10.f, 10.f }), child) << "inside the clip, still hittable";
    // Outside the clipping parent: the child is not VISIBLE there, so it must
    // not be clickable there either.
    UIElement* outside = doc.HitTest({ 200.f, 200.f });
    EXPECT_NE(outside, child) << "a clipped-away child was still clickable";
}

TEST(UIInput, ClickRequiresPressAndReleaseOnTheSameElement) {
    TwoBoxes t;
    int leftClicks = 0, rightClicks = 0;
    t.left->OnClick([&](UIEvent&) { ++leftClicks; });
    t.right->OnClick([&](UIEvent&) { ++rightClicks; });

    // Press and release on left -> one click.
    t.doc.UpdatePointer(at(50.f, 50.f, true));
    t.doc.UpdatePointer(at(50.f, 50.f, false));
    EXPECT_EQ(leftClicks, 1);
    EXPECT_EQ(rightClicks, 0);

    // Press on left, DRAG off, release on right -> no click anywhere. Being
    // able to slide off a button and let go is how a mis-click is undone.
    t.doc.UpdatePointer(at(50.f, 50.f, true));
    t.doc.UpdatePointer(at(150.f, 50.f, true));
    t.doc.UpdatePointer(at(150.f, 50.f, false));
    EXPECT_EQ(leftClicks, 1) << "a drag-off still fired the click";
    EXPECT_EQ(rightClicks, 0) << "release fired a click on an element never pressed";
}

TEST(UIInput, EventsBubbleAndCanBeStopped) {
    UIDocument doc;
    UIElement* outer = doc.root().AddChild("outer");
    outer->style().width = StyleLength::Px(200.f);
    outer->style().height = StyleLength::Px(200.f);
    UIElement* inner = outer->AddChild("inner");
    inner->style().width = StyleLength::Px(50.f);
    inner->style().height = StyleLength::Px(50.f);
    doc.Layout(400.f, 400.f);

    std::vector<std::string> order;
    UIElement* seenTarget = nullptr;
    UIElement* seenCurrent = nullptr;
    inner->OnClick([&](UIEvent& e) { order.push_back("inner"); });
    outer->OnClick([&](UIEvent& e) {
        order.push_back("outer");
        seenTarget = e.target;
        seenCurrent = e.currentTarget;
    });

    doc.UpdatePointer(at(10.f, 10.f, true));
    doc.UpdatePointer(at(10.f, 10.f, false));

    ASSERT_EQ(order.size(), 2u) << "the click did not bubble to the ancestor";
    EXPECT_EQ(order[0], "inner") << "bubbling must run target-first";
    EXPECT_EQ(order[1], "outer");
    // The container can still tell WHICH child was hit — the reason target and
    // currentTarget are separate.
    EXPECT_EQ(seenTarget, inner);
    EXPECT_EQ(seenCurrent, outer);

    // StopPropagation halts the walk.
    order.clear();
    inner->ClearEventListeners();
    inner->OnClick([&](UIEvent& e) { order.push_back("inner"); e.StopPropagation(); });
    doc.UpdatePointer(at(10.f, 10.f, true));
    doc.UpdatePointer(at(10.f, 10.f, false));
    ASSERT_EQ(order.size(), 1u) << "StopPropagation did not stop the bubble";
    EXPECT_EQ(order[0], "inner");
}

// :hover applies to the whole ancestor chain, and enter/leave fire only on the
// elements that actually changed.
TEST(UIInput, HoverTracksTheAncestorChain) {
    UIDocument doc;
    UIElement* panel = doc.root().AddChild("panel");
    panel->style().direction = FlexDirection::Row;
    panel->style().width = StyleLength::Px(200.f);
    panel->style().height = StyleLength::Px(100.f);
    UIElement* a = panel->AddChild("a");
    a->style().width = StyleLength::Px(100.f);
    a->style().height = StyleLength::Px(100.f);
    UIElement* b = panel->AddChild("b");
    b->style().width = StyleLength::Px(100.f);
    b->style().height = StyleLength::Px(100.f);
    doc.Layout(400.f, 400.f);

    int panelEnters = 0, panelLeaves = 0, aEnters = 0, aLeaves = 0;
    panel->OnPointerEnter([&](UIEvent&) { ++panelEnters; });
    panel->OnPointerLeave([&](UIEvent&) { ++panelLeaves; });
    a->OnPointerEnter([&](UIEvent&) { ++aEnters; });
    a->OnPointerLeave([&](UIEvent&) { ++aLeaves; });

    doc.UpdatePointer(at(50.f, 50.f)); // over a
    EXPECT_TRUE(a->isHovered());
    EXPECT_TRUE(panel->isHovered()) << "the ancestor must be hovered too (CSS :hover)";
    EXPECT_EQ(aEnters, 1);
    EXPECT_EQ(panelEnters, 1);

    doc.UpdatePointer(at(150.f, 50.f)); // move to b: same parent
    EXPECT_FALSE(a->isHovered());
    EXPECT_TRUE(b->isHovered());
    EXPECT_TRUE(panel->isHovered());
    EXPECT_EQ(aLeaves, 1);
    EXPECT_EQ(panelLeaves, 0) << "the shared ancestor must NOT re-fire enter/leave";
    EXPECT_EQ(panelEnters, 1);

    // Pointer leaves the surface entirely.
    UIPointerState out;
    out.inside = false;
    doc.UpdatePointer(out);
    EXPECT_FALSE(b->isHovered());
    EXPECT_FALSE(panel->isHovered());
    EXPECT_EQ(panelLeaves, 1);
}

TEST(UIInput, PressedStateTracksTheButton) {
    TwoBoxes t;
    EXPECT_FALSE(t.left->isPressed());
    t.doc.UpdatePointer(at(50.f, 50.f, true));
    EXPECT_TRUE(t.left->isPressed());
    EXPECT_EQ(t.doc.pressed(), t.left);
    t.doc.UpdatePointer(at(50.f, 50.f, false));
    EXPECT_FALSE(t.left->isPressed()) << "press state outlived the release";
    EXPECT_EQ(t.doc.pressed(), nullptr);
}

TEST(UIInput, MultipleListenersAllRunInOrder) {
    TwoBoxes t;
    std::vector<int> hits;
    t.left->OnClick([&](UIEvent&) { hits.push_back(1); });
    t.left->OnClick([&](UIEvent&) { hits.push_back(2); });
    t.doc.UpdatePointer(at(50.f, 50.f, true));
    t.doc.UpdatePointer(at(50.f, 50.f, false));
    ASSERT_EQ(hits.size(), 2u) << "a second listener replaced the first";
    EXPECT_EQ(hits[0], 1);
    EXPECT_EQ(hits[1], 2);
}

// A handler is allowed to mutate the tree — including removing the very element
// it is attached to. That must not leave the document dispatching into freed
// memory on the next frame.
TEST(UIInput, RemovingTheHoveredElementInAHandlerIsSafe) {
    UIDocument doc;
    UIElement* victim = doc.root().AddChild("victim");
    victim->style().width = StyleLength::Px(100.f);
    victim->style().height = StyleLength::Px(100.f);
    doc.Layout(400.f, 400.f);

    std::unique_ptr<UIElement> detached;
    victim->OnClick([&](UIEvent& e) {
        // Detach (and keep alive) mid-dispatch, as a "close panel" button would.
        detached = doc.root().RemoveChild(e.currentTarget);
    });

    doc.UpdatePointer(at(10.f, 10.f, true));
    doc.UpdatePointer(at(10.f, 10.f, false)); // fires the click, detaches
    ASSERT_NE(detached, nullptr);

    // The document still caches pointers to it; the next update must notice it
    // is gone rather than dispatch into it.
    doc.Layout(400.f, 400.f);
    doc.UpdatePointer(at(10.f, 10.f));
    EXPECT_EQ(doc.hovered(), &doc.root());
    EXPECT_EQ(doc.pressed(), nullptr);

    // ...and once it is actually destroyed, still safe.
    detached.reset();
    doc.Layout(400.f, 400.f);
    doc.UpdatePointer(at(10.f, 10.f, true));
    doc.UpdatePointer(at(10.f, 10.f, false));
    SUCCEED();
}

// The shipped sample's button must actually work end to end — loaded from the
// SHIPPED assets, so a typo in hud.uxml/hud.uss (a renamed element, an
// unparseable rule) fails here rather than silently shipping a dead button.
TEST(UIInput, DemoHudButtonIncrementsTheScore) {
    ui::DemoHud hud;
    // Geometry works without a font, so a deliberately missing one keeps this
    // test free of a GL context while still exercising the real asset path.
    hud.Init("Exported/UI/hud.uxml", "Exported/UI/hud.uss",
             "definitely_not_a_font.ttf", 16.f);
    ASSERT_TRUE(hud.errors().empty())
        << "shipped HUD assets did not load: " << hud.errors()[0];
    ASSERT_TRUE(hud.IsReady()) << "hud.uxml no longer has the elements DemoHud binds to";

    UIElement* button = hud.document().root().Find("scoreButton");
    ASSERT_NE(button, nullptr);

    // Lay out once so the button has a rect to aim at.
    hud.SetPointer({});
    UIDocument& doc = hud.document();
    doc.Layout(1280.f, 720.f, nullptr);
    const glm::vec2 c = button->layout().position + button->layout().size * 0.5f;
    ASSERT_GT(button->layout().size.x, 0.f) << "the button has no area to click";

    const int before = hud.score();
    UIPointerState p;
    p.inside = true;
    p.position = c;
    p.buttonDown = true;
    doc.Layout(1280.f, 720.f, nullptr);
    doc.UpdatePointer(p);
    p.buttonDown = false;
    doc.Layout(1280.f, 720.f, nullptr);
    doc.UpdatePointer(p);

    EXPECT_EQ(hud.score(), before + 100) << "clicking the demo button did nothing";
}
