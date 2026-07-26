#pragma once
// Style for a UI element — a deliberately CSS/USS-shaped subset.
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

    // A CSS length: auto, absolute points (pixels), or a percentage of the
    // parent. Point/percent are separate units rather than a bare float because
    // "50" and "50%" mean entirely different things and silently conflating
    // them is a classic layout bug.
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

        // ---- text ----
        // A non-empty `text` makes the element a text leaf: it measures itself
        // from the font, so labels size to their content like they do on the web.
        std::string text;
        glm::vec4   textColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        float       fontScale = 1.0f;

        DisplayMode display = DisplayMode::Flex;

        // Clip children to this element's box (CSS `overflow: hidden`).
        bool overflowHidden = false;

        // Whether the pointer can hit this element (CSS `pointer-events`).
        // Setting it false skips the element AND its subtree, which is what
        // decorative overlays want — a full-screen crosshair or vignette layer
        // must not eat every click meant for the UI beneath it. (CSS lets a
        // descendant re-enable picking; that is deliberately not supported here
        // because "the subtree is inert" is far easier to reason about.)
        bool pickable = true;
    };

} // namespace MyCoreEngine::ui
