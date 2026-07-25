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

} // namespace MyCoreEngine::ui
