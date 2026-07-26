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
//   cascade      CSS specificity (#id, .class, type) with later-wins on ties
//   comments     C-style
//
// NOT SUPPORTED (deliberately, and reported as errors rather than ignored)
//   combinators (descendant/child/sibling), pseudo-classes (:hover — interaction
//   state is queried in code today), at-rules, variables, inheritance. Nothing
//   here cascades from parent to child: every element is styled independently.
#include "../core/Core.h"
#include "UIStyle.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace MyCoreEngine::ui {

    class UIElement;

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
            Overflow, PointerEvents,
        };

        Prop        prop{};
        StyleLength length{};   // length-valued props
        glm::vec4   color{ 0.0f };
        float       number = 0.0f;
        int         enumValue = 0;
        Edges       edges{};
        bool        boolean = false;

        void ApplyTo(Style& s) const;
    };

    // A compound selector: all parts must match the same element. Empty type +
    // no name/classes means the universal selector.
    struct ENGINE_API UISelector {
        std::string type;                  // "" = any
        std::string name;                  // "" = any
        std::vector<std::string> classes;  // all must be present

        bool Matches(const UIElement& el) const;
        // CSS specificity, compared as an ordered triple.
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

    private:
        std::vector<UIRule> rules_;
        std::vector<std::string> errors_;
    };

} // namespace MyCoreEngine::ui
