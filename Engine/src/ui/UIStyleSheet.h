#pragma once
// USS-like stylesheets: a CSS subset for the in-game UI.
//
// Hand-written parser, no dependency. A full CSS engine is enormous and most of
// it is meaningless for game UI (floats, tables, inline layout, @media...), so
// this implements the part that maps onto the Style struct and refuses the rest
// loudly rather than half-supporting it.
//
// SUPPORTED
//   rules        selector-list { prop: value; ... }
//   selectors    Type, .class, #name, *, and compounds like Button.primary#ok
//   states       :hover and :active, which count as a class for specificity
//                (applied by UIInteractionStyler — see there for why undoing a
//                state rule needs a whole re-cascade)
//   cascade      CSS specificity (#id, .class, type) with later-wins on ties
//   comments     C-style
//
// NOT SUPPORTED (deliberately, and reported as errors rather than ignored)
//   combinators (descendant/child/sibling), any other pseudo-class (:focus and
//   :disabled need states this system does not track), at-rules, variables,
//   inheritance. Nothing here cascades from parent to child: every element is
//   styled independently.
#include "../core/Core.h"
#include "UIStyle.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace MyCoreEngine::ui {

    class UIElement;

    // What sort of value a property takes. Lets a binding coerce straight to
    // the right field without going through text, and lets it say what a
    // property needs when it cannot.
    enum class UIPropValueKind : std::uint8_t {
        Length, Number, Color, Enum, Edges, Boolean
    };

    // One `prop: value` pair, parsed once at load so applying a sheet is not a
    // string-parsing exercise every frame.
    struct ENGINE_API UIDeclaration {
        enum class Prop {
            FlexDirection, JustifyContent, AlignItems, AlignSelf,
            FlexGrow, FlexShrink,
            Width, Height, MinWidth, MinHeight, MaxWidth, MaxHeight,
            Margin, Padding, Gap,
            Position, Left, Top, Right, Bottom,
            BackgroundColor, Color, FontScale,
            Overflow, OverflowX, OverflowY, PointerEvents, Display,
            ScrollbarWidth, ScrollbarMinThumb, ScrollbarColor, ScrollbarThumbColor,
            ScrollbarVisibility, ScrollBehavior,
        };

        // True when writing `a` also writes a field `b` writes — i.e. one is a
        // shorthand covering the other, or they are the same property. The
        // shadowed-declaration diagnostic compares with this rather than by
        // equality, or a sheet declaring `overflow-y` and a binding writing
        // `overflow` would never see each other and the note would go silent on
        // exactly the case it exists to catch.
        static bool PropsOverlap(Prop a, Prop b);

        Prop        prop{};
        StyleLength length{};   // length-valued props
        glm::vec4   color{ 0.0f };
        float       number = 0.0f;
        int         enumValue = 0;
        Edges       edges{};
        bool        boolean = false;

        void ApplyTo(Style& s) const;

        // Name -> property, WITHOUT a value. The binding parser needs the
        // vocabulary before it has anything to parse, and sharing this with the
        // declaration parser is what stops a third consumer from disagreeing
        // about what a property is called.
        static bool PropFromName(const std::string& name, Prop& p, UIPropValueKind& k);
        // Parses a value for an ALREADY-KNOWN property. What an interpolated
        // binding calls, once per changed value.
        static bool ParseValueFor(Prop p, const std::string& value,
                                  UIDeclaration& out, std::string& err);
        // The spelling used in messages ("width", "background-color").
        static const char* NameOf(Prop p);
    };

    // Interaction state a selector can require, as CSS pseudo-classes. A
    // BITMASK, so `.btn:hover:active` requires both at once.
    //
    // Each one corresponds to a state the system genuinely tracks; a
    // pseudo-class with nothing behind it would be a selector that silently
    // never matches.
    enum class UIPseudo : std::uint8_t {
        None     = 0,
        Hover    = 1 << 0,   // pointer is over this element OR a descendant
        Active   = 1 << 1,   // pointer is held down on this element
        Focus    = 1 << 2,   // has keyboard focus (at most one per document)
        Disabled = 1 << 3,   // this element or an ancestor is disabled
    };

    // One compound selector: all parts must match the SAME element. Empty type
    // + no name/classes/pseudo means the universal selector.
    struct ENGINE_API UICompound {
        std::string type;                  // "" = any
        std::string name;                  // "" = any
        std::vector<std::string> classes;  // all must be present
        std::uint8_t pseudo = 0;           // UIPseudo bits; all must be set

        bool Matches(const UIElement& el) const;
        // Everything except the interaction state.
        bool MatchesIgnoringState(const UIElement& el) const;
        void AddSpecificity(int& ids, int& cls, int& types) const;
        bool empty() const {
            return type.empty() && name.empty() && classes.empty() && pseudo == 0;
        }
    };

    // How a compound relates to the one before it in a selector.
    enum class UICombinator : std::uint8_t {
        Descendant,      // `.panel .btn`  — any ancestor
        Child,           // `.panel > .btn` — the immediate parent
        AdjacentSibling, // `.a + .b`      — the element immediately before it
        GeneralSibling,  // `.a ~ .b`      — any earlier sibling
    };

    // A full selector: a chain of compounds read left to right, e.g.
    // `.panel > .row .btn:hover` is three compounds. The LAST one is the
    // element being styled; the rest constrain its ancestors.
    struct ENGINE_API UISelector {
        struct Part {
            UICompound   compound;
            UICombinator combinator = UICombinator::Descendant;  // to the PREVIOUS part
        };
        // Never empty; parts[0].combinator is meaningless.
        std::vector<Part> parts;

        // Convenience for the overwhelmingly common single-compound case, and
        // what the whole codebase used before combinators existed.
        const UICompound& subject() const { return parts.back().compound; }
        UICompound& subject() { return parts.back().compound; }
        bool hasAncestors() const { return parts.size() > 1; }
        std::uint8_t pseudo() const { return subject().pseudo; }

        bool Matches(const UIElement& el) const;
        // Everything except interaction state, on every part. Used to decide,
        // once at load, WHICH elements a pseudo rule could ever apply to — an
        // element no such rule can reach is never restyled and never pays for
        // the feature.
        bool MatchesIgnoringState(const UIElement& el) const;
        // CSS specificity, compared as an ordered triple and SUMMED across
        // every compound: `.panel .btn` is two classes, which is what makes a
        // more specific context win without any ordering tricks.
        void Specificity(int& ids, int& cls, int& types) const;
    };

    struct ENGINE_API UIRule {
        std::vector<UISelector> selectors;  // comma-separated list
        std::vector<UIDeclaration> declarations;
        int order = 0;                      // source order, breaks ties
    };

    class ENGINE_API UIStyleSheet {
    public:
        // Both return false and leave the sheet UNCHANGED on a parse error, so
        // a typo during hot-reload cannot blank a working UI. Details land in
        // errors().
        bool ParseString(const std::string& text, const std::string& originName = "<string>");
        // Reads from disk. The path is NOT sandboxed here — callers taking a
        // path from scene/asset content must run PathIsContained first, exactly
        // as the model, script, clip and HDRi loaders do.
        bool LoadFromFile(const std::string& path);

        void Clear();
        bool empty() const { return rules_.empty(); }
        const std::vector<UIRule>& rules() const { return rules_; }

        // Parse diagnostics from the last attempt: one line per problem, with
        // the line number. A stylesheet that silently ignores what it does not
        // understand is the single most frustrating thing to debug.
        const std::vector<std::string>& errors() const { return errors_; }

        // Applies every matching rule to `el` and its whole subtree, in
        // specificity then source order. Existing style values survive where no
        // rule sets them, so code-set styles and sheets compose.
        void ApplyTo(UIElement& root) const;
        // Just this element (no recursion).
        void ApplyToElement(UIElement& el) const;

        // Parses a bare `prop: value; prop: value` list — the body of a markup
        // `style="..."` attribute. Shared with the rule parser so the two can
        // never disagree about what a property means. Returns false and appends
        // to `errors` if anything failed; `out` then holds only the
        // declarations that DID parse, so the caller can decide whether to use
        // a partial result (markup loading does not).
        static bool ParseDeclarationList(const std::string& text,
                                         std::vector<UIDeclaration>& out,
                                         std::vector<std::string>& errors);

        // The individual value parsers, exposed so a bound value coerced from a
        // string means EXACTLY what the same text means in a declaration.
        // Sharing the code is the only way to guarantee that: two parsers that
        // agree today drift the first time one of them learns a new unit.
        //
        // All three reject non-finite input. strtod is C99-mandated to accept
        // "nan" and "inf", so without that check a NaN travelling through a
        // string parses back as a valid number and reaches the layout engine.
        static bool ParseLengthValue(const std::string& s, StyleLength& out);
        static bool ParseColorValue(const std::string& s, glm::vec4& out);
        static bool ParseNumberValue(const std::string& s, float& out);

        // Splits `a: 1; b: {x}` into (name, value) pairs, BRACE-AWARE.
        //
        // The plain split-on-';'-then-find(':') this replaces cannot survive a
        // ';' or ':' inside a {hole}, and `bind=` values are full of them. This
        // is the ONE splitter, used by `style=` and `bind=` alike, so the two
        // attributes can never come to parse differently.
        static void SplitDeclarations(const std::string& text,
                                      std::vector<std::pair<std::string, std::string>>& out,
                                      std::vector<std::string>& errors);

        // Which properties the cascade would set on this element, inline styles
        // included. Used at load to NOTE that a stylesheet declaration is
        // shadowed by a binding — otherwise editing `.fill { width: 100% }`
        // does nothing and says nothing, which is the silent no-op this
        // codebase reports errors to avoid.
        void DeclaredPropsFor(const UIElement& el,
                              std::vector<UIDeclaration::Prop>& out) const;

        // True when some rule targets this element with a pseudo-class, i.e.
        // its appearance can change with interaction. Asked ONCE per element at
        // load, so an element no state rule can reach is never re-cascaded and
        // never pays for the feature — which also confines the re-cascade's
        // one caveat (see UIInteractionStyler) to elements the author opted in.
        bool HasStateRuleFor(const UIElement& el) const;

        // Resets `el` to defaults and re-runs the whole cascade for its CURRENT
        // state and classes.
        //
        // THE CASCADE HAS NO UNDO: applying a rule copies its declarations in
        // and records nothing about what they overwrote, so there is no way to
        // "remove" one when a state ends or a class is dropped. Re-running from
        // scratch is the only correct answer, and both `:hover` restyling and
        // class-toggle bindings need exactly this — which is why it lives here
        // rather than being written twice.
        //
        // Text is carried across explicitly (it is not a cascadable property,
        // so no rule would put it back) and moved rather than routed through
        // setText, because the content is not changing and a needless
        // re-measure on every hover is not free.
        void Recascade(UIElement& el) const;

    private:
        std::vector<UIRule> rules_;
        std::vector<std::string> errors_;
    };

} // namespace MyCoreEngine::ui
