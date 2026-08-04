// UI pointer input: hit-testing, hover/press state, and DOM-style bubbling.
//
// Pure CPU — no GL context needed, because layout and hit-testing are both CPU
// work. That is worth preserving: input logic is exactly the sort of thing you
// want to test exhaustively without a GPU.
#include <gtest/gtest.h>

#include "Engine.h"
#include "../Engine/src/ui/UIElement.h"
#include "ui_shipped_hud.h"

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
    clipper->style().overflowX = clipper->style().overflowY = Overflow::Hidden;
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

// The shipped sample's button must actually work end to end, through the path
// a game actually uses: a scene entity carrying a UIDocumentComponent, driven
// by a UIWorld. A typo in hud.cxml, or a converter the markup names that the
// C++ never registered, fails here rather than shipping.
TEST(UIInput, ShippedHudButtonIncrementsTheScore) {
    ShippedHud hud;
    hud.Frame();
    ASSERT_NE(hud.assets(), nullptr) << "the shipped HUD did not load";
    ASSERT_TRUE(hud.assets()->binder().ok())
        << (hud.assets()->binder().errors().empty() ? ""
                                                    : hud.assets()->binder().errors()[0]);

    UIElement* button = hud.find("scoreButton");
    ASSERT_NE(button, nullptr);
    ASSERT_GT(button->layout().size.x, 0.f) << "the button has no area to click";
    const glm::vec2 c = button->layout().position + button->layout().size * 0.5f;

    const long long before = hud.data().GetInt("score");
    hud.ClickAt(c.x, c.y);
    EXPECT_EQ(hud.data().GetInt("score"), before + 100)
        << "clicking the shipped button did nothing";

    // ...and the readout followed, which is the binding half of the same story.
    hud.Frame();
    EXPECT_EQ(hud.find("scoreLabel")->style().text, "SCORE 100");
}


// ------------------------------------- keyboard / pad activation (U25a)
//
// Until ActivateFocused existed, Enter on a focused Button fell off the end of
// UpdateKeyboard's default chain and the UI could not be operated without a
// mouse. UITabView.cpp said so in its own source: "there is no
// Enter-activates-a-Button anywhere yet."
//
// The reason this is one function rather than a feature: UIBinding attaches
// every authored on-* as an ordinary Click listener, so synthesizing a Click
// lights up every on-click in every document with no markup change.

TEST(UIActivate, EnterOnAFocusedButtonFiresItsOnClick) {
    ShippedHud hud;
    hud.Frame();
    UIElement* btn = hud.find("scoreButton");
    ASSERT_NE(btn, nullptr);
    ASSERT_EQ(hud.data().GetInt("score"), 0);

    hud.doc().SetFocus(btn);
    UIKeyboardState kb;
    kb.keys.push_back(UIKeyEvent{ UIKey::Enter });
    hud.world.SetKeyboard(kb);
    hud.Frame();

    EXPECT_EQ(hud.data().GetInt("score"), 100)
        << "Enter on a focused button did not run its action";
}

TEST(UIActivate, EnterDoesNothingWithNothingFocused) {
    ShippedHud hud;
    hud.Frame();
    hud.doc().SetFocus(nullptr);
    UIKeyboardState kb;
    kb.keys.push_back(UIKeyEvent{ UIKey::Enter });
    hud.world.SetKeyboard(kb);
    hud.Frame();
    EXPECT_EQ(hud.data().GetInt("score"), 0);
}

// A field owns its own Enter. A multi-line one inserts a newline (and consumes
// the key before this is reached); a single-line one leaves it for a container
// that may want to mean "submit". Neither is a click on the field itself.
TEST(UIActivate, EnterInATextFieldDoesNotSynthesizeAClick) {
    ShippedHud hud;
    hud.Frame();
    UIElement* field = hud.find("nameField");
    ASSERT_NE(field, nullptr);
    ASSERT_NE(field->textEdit(), nullptr);

    hud.doc().SetFocus(field);
    EXPECT_FALSE(hud.doc().ActivateFocused())
        << "a focused text field reported itself activatable";
}

// The direct API, which is what a gamepad's A button will call. Proves the
// activation path is not keyboard-specific.
TEST(UIActivate, ActivateFocusedIsTheWholePadPath) {
    ShippedHud hud;
    hud.Frame();
    UIElement* btn = hud.find("scoreButton");
    ASSERT_NE(btn, nullptr);
    hud.doc().SetFocus(btn);

    EXPECT_TRUE(hud.doc().ActivateFocused());
    EXPECT_EQ(hud.data().GetInt("score"), 100);
    EXPECT_TRUE(hud.doc().ActivateFocused());
    EXPECT_EQ(hud.data().GetInt("score"), 200) << "activation is not repeatable";
}

// ------------------------------------------ what the host is told (U25a)
//
// Both hosts ask the UI whether it is using the keyboard, to decide whether a
// key belongs to the UI or to the game. Before this, only the editor could
// answer -- for ImGui, not for the game's UI -- and the shipped Player had NO
// capture provider at all, so Escape and gamepad BACK quit the game even while
// a text field had focus.

TEST(UICaptureQuery, NothingFocusedMeansTheGameKeepsItsKeys) {
    ShippedHud hud;
    hud.Frame();
    hud.doc().SetFocus(nullptr);
    EXPECT_FALSE(hud.world.wantsKeyboard())
        << "a HUD that merely EXISTS claimed the keyboard - the game would "
           "become unquittable";
    EXPECT_FALSE(hud.world.wantsTextInput());
}

TEST(UICaptureQuery, AFocusedTextFieldClaimsBothKeyboardAndText) {
    ShippedHud hud;
    hud.Frame();
    hud.doc().SetFocus(hud.find("nameField"));
    EXPECT_TRUE(hud.world.wantsKeyboard());
    EXPECT_TRUE(hud.world.wantsTextInput()) << "typing would still fire game actions";
}

// A focused BUTTON wants the keyboard (Enter must reach it) but is not typing,
// so a letter bound to a game action is still the game's.
TEST(UICaptureQuery, AFocusedButtonWantsKeysButIsNotTyping) {
    ShippedHud hud;
    hud.Frame();
    hud.doc().SetFocus(hud.find("scoreButton"));
    EXPECT_TRUE(hud.world.wantsKeyboard());
    EXPECT_FALSE(hud.world.wantsTextInput());
}
