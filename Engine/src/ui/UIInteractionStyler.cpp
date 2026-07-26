#include "UIInteractionStyler.h"

#include <string>
#include <utility>

namespace MyCoreEngine::ui {

void UIInteractionStyler::Clear() {
    watched_.clear();
    doc_ = nullptr;
    sheet_ = nullptr;
    binder_ = nullptr;
    seenEpoch_ = 0;
    lastRestyled_ = 0;
}

bool UIInteractionStyler::resolvedDisabled_(const UIElement& el) {
    for (const UIElement* n = &el; n; n = n->parent()) {
        if (!n->isEnabled()) return true;
    }
    return false;
}

void UIInteractionStyler::collect_(UIElement& el) {
    if (sheet_->HasStateRuleFor(el)) {
        // Seed with the CURRENT state. At a normal load that is all default and
        // the cascade has just run for exactly that, so the two agree.
        watched_.push_back(Watched{ &el, el.isHovered(), el.isPressed(),
                                    el.isFocused(), resolvedDisabled_(el) });
    }
    for (const auto& c : el.children()) collect_(*c);
}

void UIInteractionStyler::Rebuild(UIDocument& doc, const UIStyleSheet& sheet,
                                  UIBinder* binder) {
    watched_.clear();
    doc_ = &doc;
    sheet_ = &sheet;
    binder_ = binder;
    lastRestyled_ = 0;
    collect_(doc.root());
    seenEpoch_ = UIElement::structureEpoch();
}

void UIInteractionStyler::restyle_(UIElement& el) const {
    // Text is not a cascadable property — there is no Prop::Text — so the reset
    // would destroy it and no rule would put it back. Moved out and back rather
    // than routed through setText, because the content is not actually
    // changing: setText would mark the yoga node dirty and force a needless
    // re-measure on every hover.
    std::string keepText = std::move(el.style().text);
    el.style() = Style{};
    // Rules for the element's CURRENT state, in specificity order, then its
    // inline style — the same call the initial load makes, which is what keeps
    // hover styling and load styling from ever disagreeing.
    sheet_->ApplyToElement(el);
    el.style().text = std::move(keepText);

    // Bindings wrote into the Style that was just discarded, so they have to
    // run again. Without this, hovering a bound health bar would snap it back
    // to its stylesheet default until the next value change.
    if (binder_) binder_->ReapplyFor(&el);
}

bool UIInteractionStyler::Update() {
    lastRestyled_ = 0;
    if (!doc_ || !sheet_) return false;

    // Same guard as the binder, for the same reason: every entry holds a raw
    // UIElement*, and handlers are explicitly allowed to restructure the tree
    // during UpdatePointer — which is the call immediately before this one.
    const std::uint32_t epoch = UIElement::structureEpoch();
    if (epoch != seenEpoch_) {
        UIDocument& doc = *doc_;
        const UIStyleSheet& sheet = *sheet_;
        UIBinder* binder = binder_;
        Rebuild(doc, sheet, binder);

        // Re-collecting SEEDS each entry with the state the element has right
        // now, which silently absorbs any change that happened in the same
        // frame as the structural one — and the element's Style still holds the
        // PREVIOUS state's declarations, which nothing else will ever undo.
        // Both directions break: a hover that turned ON is never applied, and a
        // hover that turned OFF is never removed.
        //
        // So re-cascade EVERY watched element unconditionally. Testing "is its
        // state non-default" is not enough, because `Watched` records the state
        // OBSERVED, not the state last APPLIED — after a re-collect those are
        // the same value and the comparison is vacuous.
        //
        // This mirrors what makes the binder's identical epoch guard safe:
        // UIBinder::Rebuild ends by force-applying every binding. The cost is
        // bounded — only elements a state rule can reach are watched, and this
        // runs only on frames where a tree actually changed shape.
        for (Watched& w : watched_) {
            restyle_(*w.el);
            ++lastRestyled_;
        }
        return lastRestyled_ > 0;
    }

    for (Watched& w : watched_) {
        const bool h = w.el->isHovered();
        const bool p = w.el->isPressed();
        const bool f = w.el->isFocused();
        const bool d = resolvedDisabled_(*w.el);
        if (h == w.hovered && p == w.pressed && f == w.focused && d == w.disabled) continue;
        w.hovered = h;
        w.pressed = p;
        w.focused = f;
        w.disabled = d;
        restyle_(*w.el);
        ++lastRestyled_;
    }
    return lastRestyled_ > 0;
}

} // namespace MyCoreEngine::ui
