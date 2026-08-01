#pragma once
// Style for a UI element — a deliberately CSS-shaped subset.
//
// The names and semantics track flexbox, because that is the whole point: the
// system is modelled on web front-end and Unity UI Toolkit, so anyone who knows
// CSS already knows this. Yoga is the layout engine underneath, but NO yoga
// type appears here or anywhere else in the public API — it stays an
// implementation detail so it can be replaced without touching authored UI.
#include "../core/Core.h"

#include <glm/glm.hpp>

#include <string>

namespace MyCoreEngine::ui {

    enum class FlexDirection { Row, Column, RowReverse, ColumnReverse };

    // Distributes children along the MAIN axis (the one FlexDirection picks).
    enum class Justify { FlexStart, Center, FlexEnd, SpaceBetween, SpaceAround, SpaceEvenly };

    // Aligns children on the CROSS axis. Auto is only meaningful for alignSelf,
    // where it means "inherit the parent's alignItems".
    enum class Align { Auto, FlexStart, Center, FlexEnd, Stretch };

    enum class PositionType { Relative, Absolute };

    // CSS `display`. `None` removes the element AND its subtree from layout,
    // painting and hit-testing.
    //
    // This is what lets `if=` be a STYLE WRITE rather than tree surgery: the
    // element keeps its identity, its handlers, its bindings and its place in
    // the markup, and showing it again is one more style write. Removing and
    // re-adding elements would invalidate every cached pointer in the app each
    // time a banner flickered.
    enum class DisplayMode { Flex, None };

    // CSS `overflow`. Hidden clips; Scroll clips AND scrolls.
    //
    // Scroll is a LAYOUT semantic, not just a paint flag. A scroller's in-flow
    // children are laid out at their natural size — the engine forces
    // flex-shrink to 0 for them — because otherwise flexbox squeezes them to
    // fit, the content extent always equals the box, and there is nothing to
    // scroll. yoga's own YGOverflowScroll does NOT do this: measured against
    // the shipped 3.1, it produces byte-identical layout to Visible across
    // every configuration tried (RTL, wrap, absolute children, percentage
    // heights), so flex-shrink is the only lever there is.
    //
    // `auto` is deliberately not a keyword, and stays a reported error. CSS
    // `auto` means "a bar only when needed" — exactly what Scroll already does
    // here — so accepting it as a synonym would make the two indistinguishable
    // forever. The ONE thing CSS `scroll` adds over `auto` is an always-painted
    // bar, and that is a property of the BAR, not of overflow: see
    // ScrollbarVisibility.
    enum class Overflow { Visible, Hidden, Scroll };

    // Whether a scrollable axis paints its bar only when the content currently
    // overflows, or always.
    enum class ScrollbarVisibility { Auto, Always };

    // Instant or eased. Opt-in per element, because Layout() is otherwise a
    // pure function of the tree and making scrolling depend on the clock is a
    // change every existing caller would have to reason about.
    enum class ScrollBehavior { Instant, Smooth };

    // A CSS length: auto, absolute points (pixels), or a percentage of the
    // parent. Point/percent are separate units rather than a bare float because
    // "50" and "50%" mean entirely different things and silently conflating
    // them is a classic layout bug.
    // How a background-image fills its box.
    //
    // Two modes and no more. `contain` leaves letterbox gaps only the fill
    // colour can cover, and 9-slice is nine quads plus four authored insets
    // plus four more scale sites — neither is needed to dress a menu, and both
    // can be added later without changing what these two mean.
    enum class BackgroundSize : std::uint8_t { Stretch, Cover };

    struct StyleLength {
        enum class Unit { Auto, Point, Percent };
        Unit  unit = Unit::Auto;
        float value = 0.0f;

        static StyleLength Auto() { return {}; }
        static StyleLength Px(float v) { return { Unit::Point, v }; }
        static StyleLength Pct(float v) { return { Unit::Percent, v }; }

        bool operator==(const StyleLength& o) const {
            return unit == o.unit && value == o.value;
        }
    };

    // Four sides, CSS order. Undefined (NaN-free) is expressed as 0, which is
    // what flexbox treats an unset edge as anyway.
    struct Edges {
        float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;
        static Edges All(float v) { return { v, v, v, v }; }
        static Edges XY(float x, float y) { return { x, y, x, y }; }
    };

    struct Style {
        // ---- layout ----
        FlexDirection direction = FlexDirection::Column; // flexbox's own default
        Justify       justify = Justify::FlexStart;
        Align         alignItems = Align::Stretch;
        Align         alignSelf = Align::Auto;
        float         flexGrow = 0.0f;
        float         flexShrink = 1.0f;
        StyleLength   width, height;
        StyleLength   minWidth, minHeight, maxWidth, maxHeight;
        Edges         margin{};
        Edges         padding{};
        float         gap = 0.0f;   // space between children on the main axis

        // Absolute positioning takes the element out of flow and places it
        // against the parent's padding box via `inset`.
        PositionType  position = PositionType::Relative;
        Edges         inset{};

        // ---- paint ----
        // Fully transparent by default: an element is a layout box first, and a
        // painted rectangle only if you ask for one.
        glm::vec4 backgroundColor{ 0.0f, 0.0f, 0.0f, 0.0f };

        // Painted INSIDE the border box, and deliberately NOT part of layout —
        // a divergence from CSS, where border-width sits between padding and
        // margin and shrinks the content box.
        //
        // Making it layout-participating means pushing it into yoga plus seven
        // sites that currently inset from the border box with padding alone,
        // including the text origin and the click-to-caret arithmetic of every
        // TextField. A caret silently moving because somebody gave a field an
        // outline is a far worse bug than content sitting under a 2px rule.
        //
        // Corollary for authors: keep `padding` >= `border-width`.
        //
        // Both are AUTHORED px and are scaled where they reach the renderer.
        // The radius is clamped in the shader to min(halfW, halfH), so
        // `border-radius: 9999px` is the pill idiom rather than an error.
        float     borderRadius = 0.0f;
        float     borderWidth  = 0.0f;
        glm::vec4 borderColor{ 0.0f, 0.0f, 0.0f, 0.0f };

        // Interned path id (UIAssetPath.h), 0 = none. An INT rather than a GL
        // name or a std::string: Recascade does `style() = Style{}` on every
        // :hover edge, so anything here must be reconstructible by a
        // declaration's ApplyTo, and a declaration is copied per element per
        // cascade.
        int            backgroundImage = 0;
        BackgroundSize backgroundSize = BackgroundSize::Stretch;

        // ---- text ----
        // A non-empty `text` makes the element a text leaf: it measures itself
        // from the font, so labels size to their content like they do on the web.
        std::string text;
        glm::vec4   textColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        float       fontScale = 1.0f;

        DisplayMode display = DisplayMode::Flex;

        // Clip to this element's box, and optionally scroll inside it. Per
        // axis, but read them through the predicates below rather than
        // directly — the CSS promotion rule makes a raw read misleading.
        //
        // A <TextField> always clips to its own box whatever these say: it
        // scrolls its text to follow the caret, and a control that scrolls its
        // text and also paints outside itself is incoherent.
        Overflow overflowX = Overflow::Visible;
        Overflow overflowY = Overflow::Visible;

        // ---- scrollbar ----
        // Not inherited — nothing in this system is, because ApplyToElement
        // matches rules against one element and never consults its parent. To
        // restyle every bar at once use the universal selector:
        //   * { scrollbar-width: 10px; scrollbar-thumb-color: #888; }
        float     scrollbarWidth = 8.0f;
        float     scrollbarMinThumb = 24.0f;
        glm::vec4 scrollbarColor{ 1.0f, 1.0f, 1.0f, 0.06f };
        glm::vec4 scrollbarThumbColor{ 1.0f, 1.0f, 1.0f, 0.28f };
        // `always` paints a bar on every scrollable axis even when the content
        // currently fits, for a panel whose scrollability should be advertised.
        // This is what CSS spells `overflow: scroll` as opposed to `auto`; it
        // lives here instead because it is a property of the BAR, and because
        // conflating it with `overflow` would mean two keywords that differ
        // only in whether a strip of pixels is painted.
        ScrollbarVisibility scrollbarVisibility = ScrollbarVisibility::Auto;

        // How a programmatic or input-driven scroll reaches its destination.
        // Instant by default: `Layout()` is otherwise a pure function of the
        // tree, and making every scroll depend on the clock would be a large
        // behavioural change to buy an animation most HUDs do not want.
        ScrollBehavior scrollBehavior = ScrollBehavior::Instant;

        // Whether the pointer can hit this element (CSS `pointer-events`).
        // Setting it false skips the element AND its subtree, which is what
        // decorative overlays want — a full-screen crosshair or vignette layer
        // must not eat every click meant for the UI beneath it. (CSS lets a
        // descendant re-enable picking; that is deliberately not supported here
        // because "the subtree is inert" is far easier to reason about.)
        bool pickable = true;
    };

    // ---- resolved overflow -------------------------------------------------
    //
    // Read overflow THROUGH these, never straight off the Style. CSS has a
    // computed-value rule: when exactly one axis is `visible` and the other is
    // not, the `visible` one computes to `auto`. That rule is not decoration
    // here, it is forced — clipping is a RECT. `Renderer2D::PushClipRect` takes
    // a position and a size and intersects with its parent internally, so
    // "clip Y but not X" has no representation, and there is no sentinel wide
    // enough to fake it (the scissor path lrounds both edges into ints).
    //
    // The promotion targets Scroll rather than a fourth `Auto` enumerator
    // because Scroll already means what CSS `auto` means. That keeps the enum
    // three-valued, keeps both switches exhaustive under /we4062 — and, the
    // decisive part, is a no-op for every sheet that exists today: `overflow:
    // scroll` sets both axes, so nothing shipped changes behaviour.
    inline Overflow ResolvedOverflowX(const Style& s) {
        return (s.overflowX == Overflow::Visible && s.overflowY != Overflow::Visible)
                   ? Overflow::Scroll : s.overflowX;
    }
    inline Overflow ResolvedOverflowY(const Style& s) {
        return (s.overflowY == Overflow::Visible && s.overflowX != Overflow::Visible)
                   ? Overflow::Scroll : s.overflowY;
    }
    // After the promotion an element clips on both axes or neither, which is
    // what lets every clip site keep one boolean and push one rect.
    inline bool ClipsBox(const Style& s)   { return ResolvedOverflowX(s) != Overflow::Visible; }
    inline bool ScrollsOnX(const Style& s) { return ResolvedOverflowX(s) == Overflow::Scroll; }
    inline bool ScrollsOnY(const Style& s) { return ResolvedOverflowY(s) == Overflow::Scroll; }
    inline bool IsScroller(const Style& s) { return ScrollsOnX(s) || ScrollsOnY(s); }

} // namespace MyCoreEngine::ui
