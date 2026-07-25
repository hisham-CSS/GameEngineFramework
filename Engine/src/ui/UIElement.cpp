#include "UIElement.h"

#include "../render2d/Font.h"
#include "../render2d/Renderer2D.h"

#include <yoga/Yoga.h>

#include <algorithm>
#include <cmath>

namespace MyCoreEngine::ui {

namespace {

    YGFlexDirection toYG(FlexDirection d) {
        switch (d) {
        case FlexDirection::Row:           return YGFlexDirectionRow;
        case FlexDirection::RowReverse:    return YGFlexDirectionRowReverse;
        case FlexDirection::ColumnReverse: return YGFlexDirectionColumnReverse;
        case FlexDirection::Column:
        default:                           return YGFlexDirectionColumn;
        }
    }
    YGJustify toYG(Justify j) {
        switch (j) {
        case Justify::Center:       return YGJustifyCenter;
        case Justify::FlexEnd:      return YGJustifyFlexEnd;
        case Justify::SpaceBetween: return YGJustifySpaceBetween;
        case Justify::SpaceAround:  return YGJustifySpaceAround;
        case Justify::SpaceEvenly:  return YGJustifySpaceEvenly;
        case Justify::FlexStart:
        default:                    return YGJustifyFlexStart;
        }
    }
    YGAlign toYG(Align a) {
        switch (a) {
        case Align::FlexStart: return YGAlignFlexStart;
        case Align::Center:    return YGAlignCenter;
        case Align::FlexEnd:   return YGAlignFlexEnd;
        case Align::Stretch:   return YGAlignStretch;
        case Align::Auto:
        default:               return YGAlignAuto;
        }
    }

    // The font in effect for the layout pass currently running. Yoga's measure
    // callback is a bare function pointer with only the node for context, and
    // the node already carries the UIElement — so rather than widening
    // UIElement's interface just to smuggle a font through, the document parks
    // it here for the duration of Layout(). Layout is synchronous and
    // single-threaded (it is called from the render path), so a TU-local is
    // sufficient; it is cleared on the way out, including if layout throws.
    const Font* g_measureFont = nullptr;

    struct MeasureFontScope {
        explicit MeasureFontScope(const Font* f) { g_measureFont = f; }
        ~MeasureFontScope() { g_measureFont = nullptr; }
    };

    // Text leaves size themselves from the font, which is what makes a label
    // behave like it does on the web (shrink-wrapping its content) instead of
    // needing a hand-set width.
    YGSize measureText(YGNodeConstRef node, float width, YGMeasureMode widthMode,
                       float /*height*/, YGMeasureMode /*heightMode*/) {
        auto* el = static_cast<UIElement*>(YGNodeGetContext(const_cast<YGNodeRef>(node)));
        YGSize out{ 0.0f, 0.0f };
        if (!el) return out;
        const Font* font = g_measureFont;
        if (!font || !font->IsValid()) return out; // no font: measures empty, still lays out
        const glm::vec2 m = font->Measure(el->style().text, el->style().fontScale);
        out.width = m.x;
        out.height = m.y;
        // Respect a hard constraint from the parent so a long label cannot blow
        // the row out; we do not wrap yet, so it simply clips to the offer.
        if (widthMode == YGMeasureModeExactly) out.width = width;
        else if (widthMode == YGMeasureModeAtMost) out.width = std::min(out.width, width);
        return out;
    }

    void applyLength(YGNodeRef n, const StyleLength& v,
                     void (*setPt)(YGNodeRef, float),
                     void (*setPct)(YGNodeRef, float),
                     void (*setAuto)(YGNodeRef)) {
        switch (v.unit) {
        case StyleLength::Unit::Point:   setPt(n, v.value); break;
        case StyleLength::Unit::Percent: setPct(n, v.value); break;
        case StyleLength::Unit::Auto:
        default:                         if (setAuto) setAuto(n); break;
        }
    }

    // Pushes one element's style into its yoga node. Yoga's setters compare
    // before marking dirty, so re-pushing an unchanged style every frame is
    // cheap and self-invalidating — no manual dirty tracking needed here.
    void pushStyle(YGNodeRef n, const Style& s, bool isTextLeaf) {
        YGNodeStyleSetFlexDirection(n, toYG(s.direction));
        YGNodeStyleSetJustifyContent(n, toYG(s.justify));
        YGNodeStyleSetAlignItems(n, toYG(s.alignItems));
        YGNodeStyleSetAlignSelf(n, toYG(s.alignSelf));
        YGNodeStyleSetFlexGrow(n, s.flexGrow);
        YGNodeStyleSetFlexShrink(n, s.flexShrink);

        applyLength(n, s.width, YGNodeStyleSetWidth, YGNodeStyleSetWidthPercent,
                    YGNodeStyleSetWidthAuto);
        applyLength(n, s.height, YGNodeStyleSetHeight, YGNodeStyleSetHeightPercent,
                    YGNodeStyleSetHeightAuto);
        applyLength(n, s.minWidth, YGNodeStyleSetMinWidth, YGNodeStyleSetMinWidthPercent, nullptr);
        applyLength(n, s.minHeight, YGNodeStyleSetMinHeight, YGNodeStyleSetMinHeightPercent, nullptr);
        applyLength(n, s.maxWidth, YGNodeStyleSetMaxWidth, YGNodeStyleSetMaxWidthPercent, nullptr);
        applyLength(n, s.maxHeight, YGNodeStyleSetMaxHeight, YGNodeStyleSetMaxHeightPercent, nullptr);

        YGNodeStyleSetMargin(n, YGEdgeLeft,   s.margin.left);
        YGNodeStyleSetMargin(n, YGEdgeTop,    s.margin.top);
        YGNodeStyleSetMargin(n, YGEdgeRight,  s.margin.right);
        YGNodeStyleSetMargin(n, YGEdgeBottom, s.margin.bottom);
        YGNodeStyleSetPadding(n, YGEdgeLeft,   s.padding.left);
        YGNodeStyleSetPadding(n, YGEdgeTop,    s.padding.top);
        YGNodeStyleSetPadding(n, YGEdgeRight,  s.padding.right);
        YGNodeStyleSetPadding(n, YGEdgeBottom, s.padding.bottom);
        YGNodeStyleSetGap(n, YGGutterAll, s.gap);

        YGNodeStyleSetPositionType(n, s.position == PositionType::Absolute
                                          ? YGPositionTypeAbsolute
                                          : YGPositionTypeRelative);
        if (s.position == PositionType::Absolute) {
            YGNodeStyleSetPosition(n, YGEdgeLeft,   s.inset.left);
            YGNodeStyleSetPosition(n, YGEdgeTop,    s.inset.top);
            YGNodeStyleSetPosition(n, YGEdgeRight,  s.inset.right);
            YGNodeStyleSetPosition(n, YGEdgeBottom, s.inset.bottom);
        }

        // A measure function is only legal on a LEAF. Yoga asserts if a node
        // has both children and a measure func, so this must track the tree,
        // not just the presence of text.
        YGNodeSetMeasureFunc(n, isTextLeaf ? &measureText : nullptr);
    }

} // namespace

UIElement::UIElement(std::string name) : name_(std::move(name)) {
    YGNodeRef n = YGNodeNew();
    YGNodeSetContext(n, this);
    yogaNode_ = n;
}

UIElement::~UIElement() {
    children_.clear(); // children free their own nodes first
    if (yogaNode_) {
        // Free only THIS node: it was removed from its parent by RemoveChild,
        // and children have already freed themselves above. FreeRecursive here
        // would double-free them.
        YGNodeFree(static_cast<YGNodeRef>(yogaNode_));
        yogaNode_ = nullptr;
    }
}

UIElement* UIElement::AddChild(std::unique_ptr<UIElement> child) {
    if (!child) return nullptr;
    UIElement* raw = child.get();
    raw->parent_ = this;
    YGNodeInsertChild(static_cast<YGNodeRef>(yogaNode_),
                      static_cast<YGNodeRef>(raw->yogaNode_),
                      uint32_t(children_.size()));
    // Gaining a child makes this element a non-leaf; a stale measure function
    // would trip yoga's leaf assertion on the next layout.
    YGNodeSetMeasureFunc(static_cast<YGNodeRef>(yogaNode_), nullptr);
    children_.push_back(std::move(child));
    return raw;
}

UIElement* UIElement::AddChild(std::string name) {
    return AddChild(std::make_unique<UIElement>(std::move(name)));
}

std::unique_ptr<UIElement> UIElement::RemoveChild(UIElement* child) {
    auto it = std::find_if(children_.begin(), children_.end(),
                           [child](const std::unique_ptr<UIElement>& c) {
                               return c.get() == child;
                           });
    if (it == children_.end()) return nullptr;
    YGNodeRemoveChild(static_cast<YGNodeRef>(yogaNode_),
                      static_cast<YGNodeRef>(child->yogaNode_));
    std::unique_ptr<UIElement> owned = std::move(*it);
    children_.erase(it);
    owned->parent_ = nullptr;
    return owned;
}

void UIElement::ClearChildren() {
    YGNodeRemoveAllChildren(static_cast<YGNodeRef>(yogaNode_));
    children_.clear();
}

void UIElement::setText(std::string t) {
    if (style_.text == t) return;
    style_.text = std::move(t);
    // Yoga caches measurements, so changing the CONTENT of a measured node must
    // be announced explicitly — otherwise the label keeps its previous width.
    if (children_.empty() && yogaNode_) {
        YGNodeRef n = static_cast<YGNodeRef>(yogaNode_);
        if (YGNodeHasMeasureFunc(n)) YGNodeMarkDirty(n);
    }
}

void UIElement::AddEventListener(UIEventType type, UIEventHandler handler) {
    if (handler) listeners_.emplace_back(type, std::move(handler));
}

void UIElement::ClearEventListeners() { listeners_.clear(); }

void UIElement::dispatchLocal_(UIEvent& e) {
    // Iterate by INDEX over a snapshot of the count: a handler is allowed to
    // add listeners (or mutate the tree), and a range-for over a vector that
    // reallocates mid-dispatch is undefined behaviour. Handlers added during
    // dispatch deliberately do not run until the next event.
    const size_t n = listeners_.size();
    for (size_t i = 0; i < n && i < listeners_.size(); ++i) {
        if (listeners_[i].first != e.type) continue;
        UIEventHandler h = listeners_[i].second; // copy: the vector may move
        if (h) h(e);
        if (e.propagationStopped) return;
    }
}

UIElement* UIElement::Find(const std::string& n) {
    if (name_ == n) return this;
    for (auto& c : children_) {
        if (UIElement* hit = c->Find(n)) return hit;
    }
    return nullptr;
}

// ---------------------------------------------------------------- UIDocument

UIDocument::UIDocument() : root_(std::make_unique<UIElement>("root")) {}
UIDocument::~UIDocument() = default;

void UIDocument::pushStyles_(UIElement& el) {
    const bool isTextLeaf = el.children_.empty() && !el.style_.text.empty();
    pushStyle(static_cast<YGNodeRef>(el.yogaNode_), el.style_, isTextLeaf);
    for (auto& c : el.children_) pushStyles_(*c);
}

void UIDocument::readLayout_(UIElement& el, const glm::vec2& parentOrigin) {
    YGNodeRef n = static_cast<YGNodeRef>(el.yogaNode_);
    // Yoga reports positions RELATIVE to the parent; accumulate so every
    // element ends up with an ABSOLUTE screen rect, which is what drawing —
    // and, later, hit-testing and clipping — actually want.
    const glm::vec2 pos = parentOrigin + glm::vec2(YGNodeLayoutGetLeft(n),
                                                   YGNodeLayoutGetTop(n));
    el.layout_.position = pos;
    el.layout_.size = { YGNodeLayoutGetWidth(n), YGNodeLayoutGetHeight(n) };
    for (auto& c : el.children_) readLayout_(*c, pos);
}

void UIDocument::Layout(float viewportW, float viewportH, const Font* font) {
    // Scoped so the measure callback can find the font, and so it is cleared
    // even if layout throws.
    MeasureFontScope fontScope(font);

    UIElement& r = *root_;
    pushStyles_(r);
    YGNodeCalculateLayout(static_cast<YGNodeRef>(r.yogaNode_),
                          viewportW, viewportH, YGDirectionLTR);
    readLayout_(r, glm::vec2(0.0f));
}

void UIDocument::draw_(const UIElement& el, Renderer2D& r2d,
                       const Font* font, int layer) {
    const ComputedLayout& L = el.layout_;
    const Style& s = el.style_;
    if (L.size.x <= 0.0f || L.size.y <= 0.0f) return; // nothing to paint

    if (s.backgroundColor.a > 0.0f) {
        r2d.DrawQuad(L.position, L.size, s.backgroundColor, layer);
    }
    if (!s.text.empty() && font && font->IsValid()) {
        // Text sits inside the padding box, matching CSS.
        const glm::vec2 tp{ L.position.x + s.padding.left,
                            L.position.y + s.padding.top };
        r2d.DrawText(*font, s.text, tp, s.textColor, layer, s.fontScale);
    }

    const bool clip = s.overflowHidden && !el.children_.empty();
    if (clip) r2d.PushClipRect(L.position, L.size);
    // Parent before child (painter's algorithm) AND on a higher layer, so a
    // child always paints over its parent's background regardless of the order
    // the batcher ends up flushing runs in.
    for (const auto& c : el.children_) draw_(*c, r2d, font, layer + 1);
    if (clip) r2d.PopClipRect();
}

void UIDocument::Draw(Renderer2D& r2d, const Font* font, int baseLayer) const {
    draw_(*root_, r2d, font, baseLayer);
}

// ------------------------------------------------------------------- input

namespace {
    bool contains(const ComputedLayout& L, const glm::vec2& p) {
        return p.x >= L.position.x && p.x < L.position.x + L.size.x &&
               p.y >= L.position.y && p.y < L.position.y + L.size.y;
    }
}

UIElement* UIDocument::hitTest_(UIElement& el, const glm::vec2& pos) {
    // pointer-events: none — the element and its whole subtree are inert.
    if (!el.style_.pickable) return nullptr;

    // A clipping parent that does not contain the point cannot have visible
    // descendants there, so reject the subtree outright. Without this test a
    // scrolled-away child would still be clickable through its own container.
    if (el.style_.overflowHidden && !contains(el.layout_, pos)) return nullptr;

    // Children in REVERSE order: they are painted front-to-back in order, so
    // the last one drawn is the topmost and must win the hit.
    for (auto it = el.children_.rbegin(); it != el.children_.rend(); ++it) {
        if (UIElement* h = hitTest_(**it, pos)) return h;
    }

    // Only after the children: the deepest element wins. Note this is tested
    // even when the parent's own rect misses, because an absolutely positioned
    // child may legitimately paint outside its parent (that is why the
    // overflowHidden check above is the thing that constrains a subtree).
    return contains(el.layout_, pos) ? &el : nullptr;
}

UIElement* UIDocument::HitTest(const glm::vec2& pos) {
    return hitTest_(*root_, pos);
}

void UIDocument::bubble_(UIElement* target, UIEvent& e) {
    e.target = target;
    for (UIElement* cur = target; cur; cur = cur->parent_) {
        e.currentTarget = cur;
        cur->dispatchLocal_(e);
        if (e.propagationStopped) return;
    }
}

bool UIDocument::isInTree_(const UIElement* el) const {
    if (!el) return false;
    // Address comparison only — never dereferences `el`, so this stays safe
    // even if the element was destroyed since we cached the pointer.
    struct W {
        const UIElement* want;
        bool operator()(const UIElement& n) const {
            if (&n == want) return true;
            for (const auto& c : n.children_) if ((*this)(*c)) return true;
            return false;
        }
    };
    return W{ el }(*root_);
}

void UIDocument::UpdatePointer(const UIPointerState& p) {
    // Drop cached targets that are no longer in the tree. Gameplay or a handler
    // may have removed the hovered/pressed element since last frame, and
    // dispatching to it would be a use-after-free.
    if (hovered_ && !isInTree_(hovered_)) hovered_ = nullptr;
    if (pressed_ && !isInTree_(pressed_)) pressed_ = nullptr;

    UIElement* hit = p.inside ? HitTest(p.position) : nullptr;

    // ---- enter/leave over the ancestor CHAIN -----------------------------
    // CSS :hover applies to every ancestor of the hovered element, so a button
    // and the panel containing it are both hovered. Fire Leave on elements
    // leaving the chain and Enter on those joining it; neither bubbles, since
    // the chain walk already visits every affected element exactly once.
    if (hit != hovered_) {
        auto inChainOf = [](UIElement* node, UIElement* leaf) {
            for (UIElement* c = leaf; c; c = c->parent_) if (c == node) return true;
            return false;
        };
        for (UIElement* o = hovered_; o; o = o->parent_) {
            if (inChainOf(o, hit)) break; // shared ancestors stay hovered
            o->hovered_ = false;
            UIEvent e;
            e.type = UIEventType::PointerLeave;
            e.position = p.position;
            e.target = o;
            e.currentTarget = o;
            o->dispatchLocal_(e);
        }
        for (UIElement* n = hit; n; n = n->parent_) {
            if (n->hovered_) break; // already in the chain
            n->hovered_ = true;
            UIEvent e;
            e.type = UIEventType::PointerEnter;
            e.position = p.position;
            e.target = n;
            e.currentTarget = n;
            n->dispatchLocal_(e);
        }
        hovered_ = hit;
    }

    // ---- move -------------------------------------------------------------
    if (hit && p.inside && (p.position != lastPos_ || !hadPointer_)) {
        UIEvent e;
        e.type = UIEventType::PointerMove;
        e.position = p.position;
        bubble_(hit, e);
    }

    // ---- press / release --------------------------------------------------
    if (p.buttonDown && !wasDown_) {
        if (pressed_) pressed_->pressed_ = false;
        pressed_ = hit;
        if (hit) {
            hit->pressed_ = true;
            UIEvent e;
            e.type = UIEventType::PointerDown;
            e.position = p.position;
            bubble_(hit, e);
        }
    }
    else if (!p.buttonDown && wasDown_) {
        if (hit) {
            UIEvent e;
            e.type = UIEventType::PointerUp;
            e.position = p.position;
            bubble_(hit, e);
        }
        // A click requires press AND release on the same element: dragging off
        // a button and letting go must NOT activate it, which is what users
        // expect and what makes a mis-click recoverable.
        if (hit && hit == pressed_ && isInTree_(hit)) {
            UIEvent e;
            e.type = UIEventType::Click;
            e.position = p.position;
            bubble_(hit, e);
        }
        if (pressed_ && isInTree_(pressed_)) pressed_->pressed_ = false;
        pressed_ = nullptr;
    }

    hadPointer_ = p.inside;
    wasDown_ = p.buttonDown;
    lastPos_ = p.position;
}

} // namespace MyCoreEngine::ui
