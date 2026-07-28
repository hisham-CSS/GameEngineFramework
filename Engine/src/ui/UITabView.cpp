#include "UITabView.h"

#include "UIElement.h"

#include <algorithm>

namespace MyCoreEngine::ui {

bool UITabView::isWithin_(const UIElement* el, const UIElement* root) {
    for (const UIElement* p = el; p; p = p->parent()) {
        if (p == root) return true;
    }
    return false;
}

UIElement* UITabView::header(std::size_t i) const {
    return i < headers_.size() ? headers_[i] : nullptr;
}
UIElement* UITabView::panel(std::size_t i) const {
    return i < panels_.size() ? panels_[i] : nullptr;
}

void UITabView::Build(const UITabSpec& spec, UIDocument& doc, UIDataSource& src,
                      std::vector<std::string>& errors, const std::string& origin) {
    spec_ = spec;
    doc_ = &doc;
    src_ = &src;
    headers_.clear();
    panels_.clear();
    flagIdx_.clear();
    strip_ = nullptr;

    UIElement* view = doc.root().Find(spec_.name);
    if (!view) {
        // Unreachable through the loader, which names the element it built.
        // Reported rather than asserted because a caller may Build against a
        // tree it assembled itself.
        errors.push_back(origin + ": tabview '" + spec_.name +
                         "': no element with that name in the document");
        return;
    }
    // The expansion's shape, and the only thing this depends on: child 0 is the
    // generated strip, children 1..N are the authored panels in order.
    if (view->children().empty()) return;
    strip_ = view->children()[0].get();
    for (auto& h : strip_->children()) headers_.push_back(h.get());
    for (std::size_t i = 1; i < view->children().size(); ++i) {
        panels_.push_back(view->children()[i].get());
    }

    selected_ = std::clamp(spec_.initialSelected, 0, int(panels_.size()) - 1);

    // Seed every property BEFORE the binder resolves, for the same reason a
    // repeat seeds every column: a binding that names a property which does not
    // exist yet is counted unresolved, and an unresolved entry arms the
    // binder's pending-retry check for the life of the process.
    src.SetInt(spec_.indexProp(), selected_);
    indexIdx_ = src.IndexOf(spec_.indexProp());
    flagIdx_.reserve(headers_.size());
    for (std::size_t i = 0; i < headers_.size(); ++i) {
        src.SetBool(spec_.flagProp(int(i)), int(i) == selected_);
        flagIdx_.push_back(src.IndexOf(spec_.flagProp(int(i))));
    }

    // OWNED listeners, so a re-collect or a rebuild can take exactly these off
    // again — the same reason UIBinder tags its own. Attached through the
    // ordinary listener path, so a header click still bubbles and composes with
    // a hand-written handler.
    for (std::size_t i = 0; i < headers_.size(); ++i) {
        UIElement* h = headers_[i];
        h->RemoveOwnedListeners(this);
        const int idx = int(i);
        h->AddOwnedListener(this, UIEventType::Click, [this, idx](UIEvent&) { Select(idx); });
    }

    // Keyboard on the STRIP rather than on each header, so one listener covers
    // the set and the arrow keys can move BETWEEN headers.
    //
    // Automatic activation — an arrow moves focus AND selection — because Tab
    // can land on a header without an arrow ever being pressed, and a focused
    // header that shows a different panel than the one it highlights is worse
    // than either behaviour on its own. This is the engine's first keyboard
    // ACTIVATION path (there is no Enter-activates-a-Button anywhere yet), so
    // it sets the convention for whatever comes next.
    if (strip_) {
        strip_->RemoveOwnedListeners(this);
        strip_->AddOwnedListener(this, UIEventType::KeyDown, [this](UIEvent& e) {
            if (headers_.empty()) return;
            // Only when the keystroke is aimed at one of OUR headers. A key
            // pressed inside a panel bubbles through the strip's ancestor chain
            // and must not be stolen from a text field.
            bool onHeader = false;
            for (UIElement* h : headers_) {
                if (isWithin_(e.target, h)) { onHeader = true; break; }
            }
            if (!onHeader) return;

            int next = selected_;
            switch (e.key) {
            case UIKey::Left:  next = selected_ > 0 ? selected_ - 1 : int(headers_.size()) - 1; break;
            case UIKey::Right: next = selected_ + 1 < int(headers_.size()) ? selected_ + 1 : 0; break;
            case UIKey::Home:  next = 0; break;
            case UIKey::End:   next = int(headers_.size()) - 1; break;
            // Enter on an already-focused header re-selects it, which is a
            // no-op but consumes the key rather than letting a form below
            // interpret it as submit.
            case UIKey::Enter: break;
            default: return;
            }
            Select(next);
            if (UIElement* h = header(std::size_t(selected_))) doc_->SetFocus(h);
            e.StopPropagation();
        });
    }

    publish_();
}

void UITabView::Select(int i) {
    if (panels_.empty()) return;
    const int next = std::clamp(i, 0, int(panels_.size()) - 1);
    if (next == selected_) { publish_(); return; }

    // BEFORE the panel is hidden, while the focused element is still findable
    // through a live parent chain.
    const bool focusWasInside =
        doc_ && doc_->focused() && std::size_t(selected_) < panels_.size() &&
        isWithin_(doc_->focused(), panels_[std::size_t(selected_)]);

    selected_ = next;
    publish_();

    // Focus follows the switch rather than being dropped. The document evicts
    // focus from a hidden subtree lazily, in UpdateKeyboard, which only the
    // keyboard-target document ever runs — and even there it evicts to null,
    // sending the next Tab back to the top of the whole HUD.
    if (focusWasInside && doc_) {
        if (UIElement* h = header(std::size_t(selected_))) doc_->SetFocus(h);
        else doc_->ClearFocus();
    }
}

void UITabView::publish_() {
    if (!src_) return;
    // Equality-gated by UIDataSource, so republishing an unchanged selection
    // bumps no version and re-applies no binding.
    src_->WriteAt(indexIdx_, UIValue::Int(selected_));
    for (std::size_t i = 0; i < flagIdx_.size(); ++i) {
        src_->WriteAt(flagIdx_[i], UIValue::Bool(int(i) == selected_));
    }
    // The header's look is a CLASS, and it is written here rather than through
    // a binding so it cannot disagree with the flags by a frame. AddClass and
    // RemoveClass re-cascade on their own and do not move the structure epoch.
    for (std::size_t i = 0; i < headers_.size(); ++i) {
        if (int(i) == selected_) headers_[i]->AddClass(kTabSelectedClass);
        else                     headers_[i]->RemoveClass(kTabSelectedClass);
    }
}

void UITabView::Refresh() { publish_(); }

} // namespace MyCoreEngine::ui
