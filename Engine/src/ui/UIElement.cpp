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
    // No `default:` here, unlike the three above. The /we4062 build flag turns a
    // missing enumerator into a compile error, and that protection is worth more
    // on this enum than anywhere else: a forgotten Scroll case would compile
    // clean and silently mean Visible, which is the one failure a scrolling
    // feature cannot survive.
    YGOverflow toYG(Overflow o) {
        switch (o) {
        case Overflow::Visible: return YGOverflowVisible;
        case Overflow::Hidden:  return YGOverflowHidden;
        case Overflow::Scroll:  return YGOverflowScroll;
        }
        return YGOverflowVisible;   // unreachable: the switch is exhaustive
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

    // Monotonic counter over every structural change to any element tree. See
    // UIElement::structureEpoch() for why this is process-wide rather than
    // per-document, and why that is safe.
    std::uint32_t g_structureEpoch = 1;

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
    void pushStyle(YGNodeRef n, const Style& s, bool isTextLeaf, bool parentScrolls) {
        YGNodeStyleSetFlexDirection(n, toYG(s.direction));
        YGNodeStyleSetJustifyContent(n, toYG(s.justify));
        YGNodeStyleSetAlignItems(n, toYG(s.alignItems));
        YGNodeStyleSetAlignSelf(n, toYG(s.alignSelf));
        YGNodeStyleSetFlexGrow(n, s.flexGrow);

        // A scroller's in-flow children must not be squeezed to fit its box. If
        // they were, the content extent would always equal the box and there
        // would be nothing to scroll — which is exactly what happens today,
        // because flex-shrink defaults to 1.
        //
        // This is not an override of an unrelated property: shrinking is the
        // negation of overflowing, so "do not shrink my children" IS what
        // `overflow: scroll` means. yoga's own YGOverflowScroll does not do it
        // (measured inert against 3.1 in every configuration tried), and yoga
        // has no CSS `min-height: auto` to stop the squeeze at min-content, so
        // flex-shrink is the only mechanism available.
        const float shrink =
            (parentScrolls && s.position != PositionType::Absolute) ? 0.0f : s.flexShrink;
        YGNodeStyleSetFlexShrink(n, shrink);

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

        YGNodeStyleSetDisplay(n, s.display == DisplayMode::None ? YGDisplayNone
                                                                : YGDisplayFlex);
        // Free, self-dirtying, and it keeps the node honest for a yoga that one
        // day acts on it. Nothing here depends on it — see the flex-shrink note.
        YGNodeStyleSetOverflow(n, toYG(s.overflow));

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
    // Anything caching a UIElement* must be forced to re-collect. RemoveChild
    // already bumps, but an element can also die owned by a unique_ptr the
    // caller took ownership of and then dropped.
    ++g_structureEpoch;
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
    ++g_structureEpoch;
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
    ++g_structureEpoch;
    return owned;
}

void UIElement::ClearChildren() {
    YGNodeRemoveAllChildren(static_cast<YGNodeRef>(yogaNode_));
    children_.clear();
    ++g_structureEpoch;
}

void UIElement::setText(std::string t) {
    if (style_.text == t) return;
    style_.text = std::move(t);
    ++textRevision_;
    // Yoga caches measurements, so changing the CONTENT of a measured node must
    // be announced explicitly — otherwise the label keeps its previous width.
    if (children_.empty() && yogaNode_) {
        YGNodeRef n = static_cast<YGNodeRef>(yogaNode_);
        if (YGNodeHasMeasureFunc(n)) YGNodeMarkDirty(n);
    }
}

void UIElement::setBindings(std::vector<UIBinding> b) {
    bindings_ = std::move(b);
    // Not a tree-shape change, but it invalidates the binder's flat index just
    // as thoroughly: every Entry holds a pointer INTO bindings_, and assigning
    // the vector reallocates it.
    ++g_structureEpoch;
}

void UIElement::setBoundActions(std::vector<UIBoundAction> a) {
    actions_ = std::move(a);
    ++g_structureEpoch;
}

void UIElement::setDataSourceName(std::string s) {
    if (dataSource_ == s) return;
    dataSource_ = std::move(s);
    // Changes which source the whole subtree resolves against, so every
    // collected entry below here is stale.
    ++g_structureEpoch;
}

std::uint32_t UIElement::structureEpoch() { return g_structureEpoch; }

UITextEdit& UIElement::MakeTextField() {
    if (!edit_) {
        edit_ = std::make_unique<UITextEdit>();
        // A field you cannot focus is a label, so this comes with the type
        // rather than needing focusable="true" alongside it.
        focusable_ = true;
    }
    return *edit_;
}

void UIElement::SyncTextFromEdit() {
    if (!edit_) return;
    // Through setText, so the measurement is invalidated: a field that grows
    // as you type must be re-measured, and writing style().text directly would
    // leave it at its previous width.
    setText(edit_->displayText());
}

bool UIElement::HasClass(const std::string& c) const {
    return std::find(classes_.begin(), classes_.end(), c) != classes_.end();
}

void UIElement::AddClass(std::string c) {
    if (c.empty() || HasClass(c)) return;
    classes_.push_back(std::move(c));
}

void UIElement::RemoveClass(const std::string& c) {
    classes_.erase(std::remove(classes_.begin(), classes_.end(), c), classes_.end());
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

void UIDocument::pushStyles_(UIElement& el, bool parentScrolls) {
    const bool isTextLeaf = el.children_.empty() && !el.style_.text.empty();

    // fontScale is the one measurement input yoga knows nothing about: it feeds
    // measureText but is never pushed into a yoga style, so changing it leaves
    // the cached measurement stale and the glyphs are drawn at a size their own
    // box was never measured for. setText has the same hazard and solves it the
    // same way; this closes it for every writer of fontScale — a stylesheet
    // rule, a `:hover` restyle, or a `bind="font-scale: {x}"`.
    if (isTextLeaf && el.style_.fontScale != el.pushedFontScale_ && el.yogaNode_) {
        YGNodeRef n = static_cast<YGNodeRef>(el.yogaNode_);
        if (YGNodeHasMeasureFunc(n)) YGNodeMarkDirty(n);
    }
    el.pushedFontScale_ = el.style_.fontScale;

    pushStyle(static_cast<YGNodeRef>(el.yogaNode_), el.style_, isTextLeaf, parentScrolls);
    const bool scrolls = el.style_.overflow == Overflow::Scroll;
    for (auto& c : el.children_) pushStyles_(*c, scrolls);
}

// Runs between the flexbox solve and readLayout_, on yoga's RAW parent-relative
// rects. See the declaration for why that ordering is load-bearing.
void UIDocument::measureScroll_(UIElement& el) {
    // A display:none subtree is zeroed by yoga, so measuring it would compute an
    // extent of 0 and clamp the offset away. Skipping it is what lets a hidden
    // tab keep its scroll position — which is the whole point of `if=` writing
    // `display` rather than removing the element.
    if (el.style_.display == DisplayMode::None) return;

    if (el.style_.overflow != Overflow::Scroll) {
        // EVERY element is visited, not just scrollers: a class toggle or a
        // :hover rule can take `scroll` away at any moment while the offset —
        // which deliberately outlives Style — stays behind. Zeroing it here
        // rather than testing overflow down in readLayout_ means a de-scrolled
        // element can never displace its children into unclipped space.
        if (el.scroll_) *el.scroll_ = UIScrollState{};
    } else {
        if (!el.scroll_) el.scroll_ = std::make_unique<UIScrollState>();
        UIScrollState& sc = *el.scroll_;
        YGNodeRef n = static_cast<YGNodeRef>(el.yogaNode_);
        const glm::vec2 box{ YGNodeLayoutGetWidth(n), YGNodeLayoutGetHeight(n) };

        // A justify/align rule that centres or end-aligns OVERFLOWING content
        // puts it at negative offsets — measured, not assumed, because clamping
        // to [0, ...] would then make the leading half permanently unreachable.
        glm::vec2 lo{ 0.0f }, hi{ 0.0f };
        for (const auto& c : el.children_) {
            if (c->style_.display == DisplayMode::None) continue;      // `if=` writes display
            if (c->style_.position == PositionType::Absolute) continue; // pinned, not content
            YGNodeRef cn = static_cast<YGNodeRef>(c->yogaNode_);
            const float l = YGNodeLayoutGetLeft(cn), t = YGNodeLayoutGetTop(cn);
            lo.x = std::min(lo.x, l - YGNodeLayoutGetMargin(cn, YGEdgeLeft));
            lo.y = std::min(lo.y, t - YGNodeLayoutGetMargin(cn, YGEdgeTop));
            // A yoga rect EXCLUDES margins, so a row with margin-bottom would
            // lose its tail without adding it back.
            hi.x = std::max(hi.x, l + YGNodeLayoutGetWidth(cn)
                                    + YGNodeLayoutGetMargin(cn, YGEdgeRight));
            hi.y = std::max(hi.y, t + YGNodeLayoutGetHeight(cn)
                                    + YGNodeLayoutGetMargin(cn, YGEdgeBottom));
        }
        // Children are positioned from the BORDER box with the leading padding
        // already folded in; the trailing padding is not, and without it the
        // last item can never be scrolled clear of the edge.
        hi.x += el.style_.padding.right;
        hi.y += el.style_.padding.bottom;

        const bool ok = std::isfinite(lo.x) && std::isfinite(lo.y) &&
                        std::isfinite(hi.x) && std::isfinite(hi.y);
        sc.contentMin = ok ? lo : glm::vec2(0.0f);
        sc.contentMax = ok ? hi : glm::vec2(0.0f);
        sc.minOffset = glm::min(sc.contentMin, glm::vec2(0.0f));
        sc.maxOffset = glm::max(sc.contentMax - box, sc.minOffset);
        // Font::Measure accumulates fractional advances, so a row measuring
        // 220.4 in a 220px box is a measurement artefact, not content. Without
        // this floor every vertical list grows a horizontal scrollbar.
        if (sc.maxOffset.x - sc.minOffset.x < 1.0f) { sc.minOffset.x = sc.maxOffset.x = 0.0f; }
        if (sc.maxOffset.y - sc.minOffset.y < 1.0f) { sc.minOffset.y = sc.maxOffset.y = 0.0f; }
        // Finite-checked at the SOURCE: a NaN offset would reach PushClipRect,
        // and lround(NaN) hands glScissor a negative width.
        const bool offOk = std::isfinite(sc.offset.x) && std::isfinite(sc.offset.y);
        sc.offset = glm::clamp(offOk ? sc.offset : glm::vec2(0.0f),
                               sc.minOffset, sc.maxOffset);
    }
    for (auto& c : el.children_) measureScroll_(*c);
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

    // The scroller's OWN rect is never displaced — only what it contains.
    // Rounded to whole pixels: UIWorld already rounds the document origin
    // precisely so every glyph lands on a texel, and a fractional scroll would
    // undo that for the whole subtree. The unrounded value stays in the member
    // so a sub-pixel wheel or drag still integrates.
    const glm::vec2 scrolled = el.scroll_ ? pos - glm::round(el.scroll_->offset) : pos;

    for (auto& c : el.children_) {
        // Absolutely positioned children are PINNED to the scroller's box rather
        // than scrolling with it. A deliberate divergence from CSS, taken
        // because there is no `position: fixed` or `sticky` here to offer
        // instead: scrolling them would make an `inset: 0` overlay stop covering
        // AND stop blocking clicks — which is exactly the shape the shipped
        // sample's `.centre-overlay` already teaches. It buys sticky headers and
        // lock veils for free, and their own descendants inherit the unscrolled
        // origin because it is threaded down as parentOrigin.
        readLayout_(*c, c->style_.position == PositionType::Absolute ? pos : scrolled);
    }
    if (el.scroll_) updateScrollBars_(el);
}

// 8px bars, 24px minimum thumb. Constants rather than style properties: a
// scrollbar nobody has asked to restyle is not worth six more cascade entries,
// and adding them later is additive.
void UIDocument::updateScrollBars_(UIElement& el) {
    UIScrollState& sc = *el.scroll_;
    sc.trackX = sc.thumbX = sc.trackY = sc.thumbY = ComputedLayout{};

    constexpr float kBar = 8.0f, kMinThumb = 24.0f;
    const glm::vec2 p = el.layout_.position, s = el.layout_.size;
    if (s.x <= 0.0f || s.y <= 0.0f) return;

    // Range, not extent: with centred overflow the reachable span starts
    // negative, and the thumb has to represent what you can actually reach.
    const glm::vec2 span = sc.maxOffset - sc.minOffset;

    if (span.y > 0.0f) {
        const float trackH = s.y;
        sc.trackY.position = { p.x + s.x - kBar, p.y };
        sc.trackY.size = { kBar, trackH };
        // content = box + span, so box/content is the visible fraction.
        const float content = s.y + span.y;
        const float thumbH = std::max(kMinThumb, trackH * (s.y / content));
        const float free = std::max(0.0f, trackH - thumbH);
        const float t = (sc.offset.y - sc.minOffset.y) / span.y;
        sc.thumbY.position = { p.x + s.x - kBar, p.y + free * t };
        sc.thumbY.size = { kBar, thumbH };
    }
    if (span.x > 0.0f) {
        const float trackW = s.x;
        sc.trackX.position = { p.x, p.y + s.y - kBar };
        sc.trackX.size = { trackW, kBar };
        const float content = s.x + span.x;
        const float thumbW = std::max(kMinThumb, trackW * (s.x / content));
        const float free = std::max(0.0f, trackW - thumbW);
        const float t = (sc.offset.x - sc.minOffset.x) / span.x;
        sc.thumbX.position = { p.x + free * t, p.y + s.y - kBar };
        sc.thumbX.size = { thumbW, kBar };
    }
}

bool UIElement::SetScrollOffset(glm::vec2 px) {
    if (!scroll_) return false;
    if (!std::isfinite(px.x) || !std::isfinite(px.y)) return false;
    const glm::vec2 next = glm::clamp(px, scroll_->minOffset, scroll_->maxOffset);
    if (next == scroll_->offset) return false;
    scroll_->offset = next;
    return true;
}

bool UIElement::ScrollIntoView(const glm::vec2& posAbs, const glm::vec2& sizePx) {
    if (!scroll_) return false;
    // The rect arrives in ABSOLUTE space, already displaced by the current
    // offset, so the delta needed is a pure screen-space comparison against this
    // element's box — no conversion into content space, and therefore nothing to
    // get wrong when the two disagree by a frame.
    const glm::vec2 boxLo = layout_.position;
    const glm::vec2 boxHi = layout_.position + layout_.size;
    const glm::vec2 lo = posAbs, hi = posAbs + sizePx;

    glm::vec2 d{ 0.0f };
    // Under-scroll before over-scroll, so a rect TALLER than the box lands with
    // its top edge visible rather than its bottom.
    if (hi.x > boxHi.x) d.x = hi.x - boxHi.x;
    if (lo.x - d.x < boxLo.x) d.x = lo.x - boxLo.x;
    if (hi.y > boxHi.y) d.y = hi.y - boxHi.y;
    if (lo.y - d.y < boxLo.y) d.y = lo.y - boxLo.y;
    return SetScrollOffset(scroll_->offset + d);
}

void UIDocument::Layout(float viewportW, float viewportH, const Font* font) {
    // Scoped so the measure callback can find the font, and so it is cleared
    // even if layout throws.
    MeasureFontScope fontScope(font);

    UIElement& r = *root_;
    pushStyles_(r, /*parentScrolls=*/false);
    YGNodeCalculateLayout(static_cast<YGNodeRef>(r.yogaNode_),
                          viewportW, viewportH, YGDirectionLTR);
    // ORDER IS LOAD-BEARING. measureScroll_ reads yoga's raw rects and clamps
    // every offset; readLayout_ then bakes those offsets into absolute
    // positions. Measuring afterwards instead would read rects the offset has
    // already moved — the extent would shrink as you scroll and the clamp would
    // converge on half the content — and clamping afterwards would paint the
    // very frame that needed clamping with out-of-range positions.
    measureScroll_(r);
    // Seeded with the document's ORIGIN rather than zero: readLayout_
    // accumulates absolute positions, so this one value offsets everything the
    // document paints, hit-tests and clips, in one place.
    readLayout_(r, origin_);

    // Only now are the rects a focus reveal can trust. See SetFocus.
    if (pendingFocusReveal_) revealFocus_();
}

void UIDocument::draw_(const UIElement& el, Renderer2D& r2d, const Font* font,
                       int layer, const UIElement* focused, bool caretVisible) {
    const ComputedLayout& L = el.layout_;
    const Style& s = el.style_;
    // Explicit, even though yoga zeroes a display:none subtree and the size
    // test below would therefore cover it. "Invisible" must not depend on an
    // implementation detail of whichever layout engine is underneath.
    if (s.display == DisplayMode::None) return;
    if (L.size.x <= 0.0f || L.size.y <= 0.0f) return; // nothing to paint

    if (s.backgroundColor.a > 0.0f) {
        r2d.DrawQuad(L.position, L.size, s.backgroundColor, layer);
    }

    // Clip BEFORE any text is emitted, not just before the children.
    //
    // The old test was `overflowHidden && !children_.empty()`, which meant an
    // element's own text was never clipped — and since an element with text is a
    // text LEAF by construction, that made `overflow: hidden` unable to clip the
    // one thing it is most often written for.
    //
    // A <TextField> always clips to its own box whatever `overflow` says: it
    // scrolls its text to follow the caret, and a control that scrolls its text
    // and also paints outside itself is incoherent. `overflow` on a field would
    // govern children, and a field has none.
    const bool clips = s.overflow != Overflow::Visible || el.edit_ != nullptr;
    if (clips) r2d.PushClipRect(L.position, L.size);

    // Text sits inside the padding box, matching CSS, shifted by the field's own
    // text scroll. Everything below is expressed relative to `tp`, so this one
    // subtraction moves the glyphs, the selection and the caret together.
    const glm::vec2 textOff =
        el.edit_ ? glm::round(el.edit_->textScroll()) : glm::vec2(0.0f);
    const glm::vec2 tp{ L.position.x + s.padding.left - textOff.x,
                        L.position.y + s.padding.top - textOff.y };
    const bool haveFont = font && font->IsValid();

    // ---- text field decoration -------------------------------------------
    // Selection UNDER the text, caret over it, and both only for the one
    // focused field — a caret on an unfocused field says "type here" when
    // typing would go somewhere else entirely.
    const UITextEdit* edit = el.edit_.get();
    if (edit && haveFont && &el == focused) {
        const std::string shown = edit->displayText();
        const float lineH = font->Measure("", s.fontScale).y;
        // Position within the LINE, not within the whole value: a multi-line
        // field measures the prefix from its own line start and steps down by
        // whole lines, which is what makes a caret land where the glyph is.
        auto place = [&](std::size_t bytes) {
            bytes = std::min(bytes, shown.size());
            const std::size_t ls = UITextEdit::LineStart(shown, bytes);
            const float x = font->Measure(shown.substr(ls, bytes - ls), s.fontScale).x;
            const float y = float(UITextEdit::LineIndexOf(shown, bytes)) * lineH;
            return glm::vec2{ tp.x + x, tp.y + y };
        };
        if (edit->hasSelection()) {
            // One highlight per line the selection covers, so a multi-line
            // selection does not paint a single bar across everything between
            // its endpoints.
            const std::size_t b = edit->selectionBegin(), e = edit->selectionEnd();
            std::size_t lineStart = b;
            while (lineStart < e) {
                const std::size_t lineStop = std::min(UITextEdit::LineEnd(shown, lineStart), e);
                const glm::vec2 p0 = place(lineStart);
                const glm::vec2 p1 = place(lineStop);
                r2d.DrawQuad(p0, { p1.x - p0.x, lineH },
                             { 0.25f, 0.45f, 0.85f, 0.55f }, layer);
                lineStart = lineStop + 1;   // past the newline
            }
        }
        if (caretVisible) {
            // Drawn on the CHILD layer so it sits above the glyphs, which share
            // this element's layer.
            r2d.DrawQuad(place(edit->caret()), { 1.0f, lineH }, s.textColor, layer + 1);
        }
    }

    if (!s.text.empty() && haveFont) {
        r2d.DrawText(*font, s.text, tp, s.textColor, layer, s.fontScale);
    }

    // Parent before child (painter's algorithm) AND on a higher layer, so a
    // child always paints over its parent's background regardless of the order
    // the batcher ends up flushing runs in.
    for (const auto& c : el.children_) {
        draw_(*c, r2d, font, layer + 1, focused, caretVisible);
    }

    // Bars LAST and inside the clip, so they paint over the content they
    // describe and can never escape the box. A zero-size thumb means that axis
    // does not scroll — the one signal the painter and the hit test share.
    if (el.scroll_) {
        const UIScrollState& sc = *el.scroll_;
        const glm::vec4 trackCol{ 1.0f, 1.0f, 1.0f, 0.06f };
        const glm::vec4 thumbCol{ 1.0f, 1.0f, 1.0f, 0.28f };
        const int barLayer = layer + 1;
        if (sc.thumbY.size.y > 0.0f) {
            r2d.DrawQuad(sc.trackY.position, sc.trackY.size, trackCol, barLayer);
            r2d.DrawQuad(sc.thumbY.position, sc.thumbY.size, thumbCol, barLayer);
        }
        if (sc.thumbX.size.x > 0.0f) {
            r2d.DrawQuad(sc.trackX.position, sc.trackX.size, trackCol, barLayer);
            r2d.DrawQuad(sc.thumbX.position, sc.thumbX.size, thumbCol, barLayer);
        }
    }
    if (clips) r2d.PopClipRect();
}

void UIDocument::AdvanceTime(float dt) { caretClock_ += dt; }

void UIDocument::Draw(Renderer2D& r2d, const Font* font, int baseLayer) const {
    // A one-second cycle, on for the first half. Computed once here rather than
    // per element: only the focused field can show a caret.
    const float kBlink = 1.0f;
    const bool caretVisible =
        (caretClock_ - std::floor(caretClock_ / kBlink) * kBlink) < kBlink * 0.5f;
    draw_(*root_, r2d, font, baseLayer, focused_, caretVisible);
}

// ------------------------------------------------------------------- input

namespace {
    bool contains(const ComputedLayout& L, const glm::vec2& p) {
        return p.x >= L.position.x && p.x < L.position.x + L.size.x &&
               p.y >= L.position.y && p.y < L.position.y + L.size.y;
    }
}

UIElement* UIDocument::hitTest_(UIElement& el, const glm::vec2& pos) {
    // Hidden means hidden: an element you cannot see must not be clickable,
    // and the same reasoning as in draw_ applies — assert it here rather than
    // relying on the layout engine to have zeroed the rect.
    if (el.style_.display == DisplayMode::None) return nullptr;
    // Disabled takes the whole SUBTREE out of the pointer's reach. A disabled
    // panel whose buttons still worked would be a trap.
    if (!el.enabled_) return nullptr;
    // pointer-events: none — the element and its whole subtree are inert.
    if (!el.style_.pickable) return nullptr;

    // A clipping parent that does not contain the point cannot have visible
    // descendants there, so reject the subtree outright. Both `hidden` and
    // `scroll` clip, hence the positive test. This is no longer hypothetical:
    // with scrolling, a row that has moved above its container is genuinely
    // painted nowhere, and without this it would still be clickable.
    if (el.style_.overflow != Overflow::Visible && !contains(el.layout_, pos)) return nullptr;

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

// Reverse paint order, like hitTest_, so the topmost bar wins. Kept separate
// because the bars are painted from draw_ and have no elements of their own —
// without this a press on the thumb resolves to the row underneath it, fires
// that row's on-click, lights up :active, and blurs whatever field the user was
// typing in.
UIElement* UIDocument::hitScrollThumb_(UIElement& el, const glm::vec2& pos, int& axisOut) {
    if (el.style_.display == DisplayMode::None) return nullptr;
    if (!el.enabled_ || !el.style_.pickable) return nullptr;
    if (el.style_.overflow != Overflow::Visible && !contains(el.layout_, pos)) return nullptr;

    for (auto it = el.children_.rbegin(); it != el.children_.rend(); ++it) {
        if (UIElement* h = hitScrollThumb_(**it, pos, axisOut)) return h;
    }
    if (el.scroll_) {
        if (el.scroll_->thumbY.size.y > 0.0f && contains(el.scroll_->thumbY, pos)) {
            axisOut = 1; return &el;
        }
        if (el.scroll_->thumbX.size.x > 0.0f && contains(el.scroll_->thumbX, pos)) {
            axisOut = 0; return &el;
        }
    }
    return nullptr;
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

void UIDocument::UpdatePointer(const UIPointerState& p, const Font* font) {
    // Drop cached targets that are no longer in the tree. Gameplay or a handler
    // may have removed the hovered/pressed element since last frame, and
    // dispatching to it would be a use-after-free.
    if (hovered_ && !isInTree_(hovered_)) hovered_ = nullptr;
    if (pressed_ && !isInTree_(pressed_)) pressed_ = nullptr;
    // The drag target is held across frames just like the two above, and a hot
    // reload — which a stylesheet-only save triggers — frees the whole tree.
    if (scrollDrag_ && !isInTree_(scrollDrag_)) scrollDrag_ = nullptr;

    // ---- scrollbar drag ----------------------------------------------------
    // Resolved AHEAD of everything else and allowed to own the pointer, exactly
    // like a native scrollbar. The bars are painted from draw_ and are invisible
    // to hitTest_, so without this a press on the thumb would reach the row
    // underneath: its on-click would fire, :active would light up, and focus
    // would leave whatever field the user was typing in.
    if (p.buttonDown && !wasDown_ && !scrollDrag_ && p.inside) {
        int axis = 0;
        if (UIElement* s = hitScrollThumb_(*root_, p.position, axis)) {
            scrollDrag_ = s;
            scrollDragAxis_ = axis;
            const ComputedLayout& th = axis ? s->scroll_->thumbY : s->scroll_->thumbX;
            scrollDragGrab_ = axis ? p.position.y - th.position.y
                                   : p.position.x - th.position.x;
        }
    }
    if (scrollDrag_) {
        if (!p.buttonDown) {
            scrollDrag_ = nullptr;
        } else {
            UIScrollState& sc = *scrollDrag_->scroll_;
            const int a = scrollDragAxis_;
            const ComputedLayout& track = a ? sc.trackY : sc.trackX;
            const ComputedLayout& thumb = a ? sc.thumbY : sc.thumbX;
            const float trackLen = a ? track.size.y : track.size.x;
            const float thumbLen = a ? thumb.size.y : thumb.size.x;
            const float free = trackLen - thumbLen;
            if (free > 0.0f) {
                const float cursor = a ? p.position.y : p.position.x;
                const float trackTop = a ? track.position.y : track.position.x;
                const float t = std::clamp((cursor - scrollDragGrab_ - trackTop) / free,
                                           0.0f, 1.0f);
                const float lo = a ? sc.minOffset.y : sc.minOffset.x;
                const float hi = a ? sc.maxOffset.y : sc.maxOffset.x;
                glm::vec2 next = sc.offset;
                (a ? next.y : next.x) = lo + (hi - lo) * t;
                if (scrollDrag_->SetScrollOffset(next)) scrollDirty_ = true;
            }
        }
        hadPointer_ = p.inside;
        wasDown_ = p.buttonDown;
        lastPos_ = p.position;
        return;
    }

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

    // ---- wheel --------------------------------------------------------------
    // Dispatched like any other pointer event, with scrolling as the DEFAULT
    // ACTION that runs afterwards — the same shape KeyDown/TextInput already use
    // to reach a text field, so a handler can pre-empt the wheel by calling
    // StopPropagation without the scroller knowing anything about it.
    if (hit && p.inside && (p.wheel.x != 0.0f || p.wheel.y != 0.0f)) {
        UIEvent e;
        e.type = UIEventType::Wheel;
        e.position = p.position;
        e.delta = p.wheel;
        bubble_(hit, e);
        // Handlers may restructure the tree — dispatchLocal_ explicitly allows
        // it — so re-validate before walking parent_ upward, exactly as the
        // Click path re-checks isInTree_ before firing.
        if (!e.propagationStopped && isInTree_(hit)) {
            // ~3 rows at the default 16px line height. One constant, here, so
            // the two hosts only ever have to agree on a SIGN.
            constexpr float kPixelsPerNotch = 48.0f;
            for (UIElement* n = hit; n; n = n->parent_) {
                if (n->style_.overflow != Overflow::Scroll || !n->scroll_) continue;
                const UIScrollState& sc = *n->scroll_;
                const bool wantX = e.delta.x != 0.0f && sc.scrollsX();
                const bool wantY = e.delta.y != 0.0f && sc.scrollsY();
                if (!wantX && !wantY) continue;
                // NO CHAINING: the target is the nearest ancestor that IS
                // scrollable on this axis, not the nearest that can still MOVE
                // on it. SetScrollOffset then clamps, which is a harmless no-op
                // at the end of travel. Choosing by remaining room instead would
                // teleport the outer panel the instant an inner list reached its
                // bottom — which is the resting state of every log.
                if (n->SetScrollOffset(sc.offset - e.delta * kPixelsPerNotch)) {
                    scrollDirty_ = true;
                }
                break;
            }
        }
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

    // ---- click-to-focus ----------------------------------------------------
    // On the PRESS edge, like every desktop toolkit: focus should follow the
    // button going down, not the click completing, so dragging out of a field
    // still leaves it focused.
    if (p.buttonDown && !wasDown_) {
        // Walk up from the hit for the nearest focusable ancestor, so clicking
        // the label inside a field focuses the FIELD — the same reasoning that
        // makes events bubble.
        UIElement* target = nullptr;
        for (UIElement* n = hit; n; n = n->parent_) {
            if (n->focusable_ && isInteractable_(*n)) { target = n; break; }
        }
        // Clicking nothing focusable clears focus, which is what makes a text
        // field commit when you click away from it.
        SetFocus(target);

        // Put the caret where the click landed. Measuring each prefix in turn
        // is O(n) per click on the field's own text — fine for a single-line
        // control, and it is the only way to be correct for a proportional font
        // (character width varies, so there is no arithmetic shortcut).
        if (target && target->edit_ && font && font->IsValid()) {
            UITextEdit& ed = *target->edit_;
            const std::string shown = ed.displayText();
            // The text scroll has to come off the origin, or clicking a VISIBLE
            // glyph in a scrolled field sets the caret to the byte that would
            // have been there unscrolled.
            const float originX = target->layout_.position.x + target->style_.padding.left
                                  - std::round(ed.textScroll().x);
            const float localX = p.position.x - originX;
            std::size_t best = 0;
            float bestDist = std::abs(localX);
            for (std::size_t i = UITextEdit::NextBoundary(shown, 0); ;
                 i = UITextEdit::NextBoundary(shown, i)) {
                const float w = font->Measure(shown.substr(0, i), target->style_.fontScale).x;
                const float d = std::abs(localX - w);
                // Snaps to the NEAREST boundary, not the one before: clicking
                // the right half of a glyph should put the caret after it.
                if (d < bestDist) { bestDist = d; best = i; }
                if (i >= shown.size()) break;
            }
            // Byte offsets in the DISPLAY string and the value only coincide
            // when there is no mask, so a masked field just keeps its caret
            // rather than jumping somewhere arbitrary.
            if (ed.maskCharacter().empty()) ed.SetCaret(best);
            followCaret_(*target, font);
            caretClock_ = 0.0f;
        }
    }

    hadPointer_ = p.inside;
    wasDown_ = p.buttonDown;
    lastPos_ = p.position;
}

bool UIDocument::isInteractable_(const UIElement& el) {
    // Checked against the whole ancestor chain: a hidden or disabled container
    // makes everything beneath it unreachable, and focus that lands there can
    // never be seen or tabbed out of.
    for (const UIElement* n = &el; n; n = n->parent_) {
        if (!n->enabled_) return false;
        if (n->style_.display == DisplayMode::None) return false;
    }
    return true;
}

void UIDocument::collectFocusables_(UIElement& el, std::vector<UIElement*>& out) {
    if (el.style_.display == DisplayMode::None || !el.enabled_) return; // subtree too
    if (el.focusable_) out.push_back(&el);
    for (const auto& c : el.children_) collectFocusables_(*c, out);
}

void UIDocument::SetFocus(UIElement* el) {
    // Refuse anything the user could not reach by other means, so a caller
    // cannot strand focus somewhere invisible with no way to Tab out.
    if (el && (!el->focusable_ || !isInTree_(el) || !isInteractable_(*el))) el = nullptr;
    if (focused_ && !isInTree_(focused_)) focused_ = nullptr;
    if (el == focused_) return;

    if (focused_) {
        focused_->focused_ = false;
        UIEvent e;
        e.type = UIEventType::FocusOut;
        e.target = e.currentTarget = focused_;
        focused_->dispatchLocal_(e);
    }
    focused_ = el;
    if (focused_) {
        focused_->focused_ = true;
        UIEvent e;
        e.type = UIEventType::FocusIn;
        e.target = e.currentTarget = focused_;
        focused_->dispatchLocal_(e);

        // The reveal is DEFERRED to the next Layout rather than done here.
        //
        // ScrollIntoView works on absolute rects, and those are only correct
        // immediately after a Layout. Two focus moves in one frame — two Tabs in
        // one UIKeyboardState, or a handler that moves focus — would have the
        // second one computing its delta from rects the first one already
        // invalidated, overscrolling, clamping, and stranding focus off screen.
        // A flag costs one bool and is exact however stale things were.
        pendingFocusReveal_ = true;
        scrollDirty_ = true;   // make the host run that Layout this frame
    }
}

void UIDocument::revealFocus_() {
    pendingFocusReveal_ = false;
    if (!focused_) return;
    // Innermost outward, re-solving between moves: scrolling the inner scroller
    // changes where the focused element sits inside the outer one, so an outer
    // reveal computed from pre-move rects would be wrong. Bounded by nesting
    // depth, and every iteration after the first is a no-op in the common case
    // of a single scroller.
    for (UIElement* n = focused_->parent_; n; n = n->parent_) {
        if (n->style_.overflow == Overflow::Visible) continue;
        if (n->ScrollIntoView(focused_->layout_.position, focused_->layout_.size)) {
            measureScroll_(*root_);
            readLayout_(*root_, origin_);
        }
    }
}

UIElement* UIDocument::FocusNext(bool backwards) {
    std::vector<UIElement*> order;
    collectFocusables_(*root_, order);
    if (order.empty()) { SetFocus(nullptr); return nullptr; }

    size_t next = backwards ? order.size() - 1 : 0;
    if (focused_) {
        const auto it = std::find(order.begin(), order.end(), focused_);
        if (it != order.end()) {
            const size_t i = size_t(it - order.begin());
            // Wraps, deliberately: a Tab that stops dead at the last field
            // leaves the user with no way back to the first without a mouse.
            next = backwards ? (i == 0 ? order.size() - 1 : i - 1)
                             : (i + 1) % order.size();
        }
    }
    SetFocus(order[next]);
    return focused_;
}

// Scrolls a field's own text so the caret stays visible. Degrades to a no-op
// without a font, like every other font-dependent path here — a missing font
// costs you a feature, never the whole HUD.
void UIDocument::followCaret_(UIElement& el, const Font* font) {
    UITextEdit* ed = el.edit_.get();
    if (!ed || !font || !font->IsValid()) return;

    const Style& s = el.style_;
    const std::string shown = ed->displayText();
    const float lineH = font->Measure("", s.fontScale).y;

    // The caret's position in the field's own TEXT space — the same arithmetic
    // draw_ uses, minus the offset we are about to compute.
    const std::size_t at = std::min(ed->caret(), shown.size());
    const std::size_t ls = UITextEdit::LineStart(shown, at);
    const glm::vec2 caretLocal{
        font->Measure(shown.substr(ls, at - ls), s.fontScale).x,
        float(UITextEdit::LineIndexOf(shown, at)) * lineH
    };

    const glm::vec2 contentBox{
        std::max(0.0f, el.layout_.size.x - s.padding.left - s.padding.right),
        std::max(0.0f, el.layout_.size.y - s.padding.top - s.padding.bottom)
    };
    // Font::Measure, NOT the laid-out width: the yoga measure callback clamps
    // its result to the offer, so the element's own size lies about how wide the
    // text really is — which is exactly the case that needs scrolling.
    glm::vec2 textExtent = font->Measure(shown, s.fontScale);
    textExtent.y = float(UITextEdit::LineIndexOf(shown, shown.size()) + 1) * lineH;

    if (ed->FollowCaret(caretLocal, { 1.0f, lineH }, contentBox, textExtent)) {
        scrollDirty_ = true;
    }
}

void UIDocument::UpdateKeyboard(const UIKeyboardState& kb, const Font* font) {
    // The focused element may have been removed since last frame by gameplay or
    // by a handler; dispatching into it would be a use-after-free.
    if (focused_ && (!isInTree_(focused_) || !isInteractable_(*focused_))) {
        SetFocus(nullptr);
    }

    // Fires ValueChanged on a field that was just edited, and keeps the caret
    // solid so typing never looks like dropped input.
    auto afterEdit = [this, font](UIElement* el, bool changed) {
        el->SyncTextFromEdit();
        // After the value AND the caret have settled: an edit that does not
        // change the value can still move the caret (a plain arrow key), and the
        // view has to follow it either way.
        followCaret_(*el, font);
        caretClock_ = 0.0f;
        if (!changed) return;
        UIEvent ev;
        ev.type = UIEventType::ValueChanged;
        ev.text = el->textEdit()->value();
        bubble_(el, ev);
    };

    for (const UIKeyEvent& k : kb.keys) {
        UIElement* target = focused_;
        UIEvent e;
        e.type = UIEventType::KeyDown;
        e.key = k.key;
        e.shift = k.shift;
        e.ctrl = k.ctrl;
        e.alt = k.alt;
        if (target) bubble_(target, e);
        // A handler may have moved focus or torn the tree apart.
        if (target && (!isInTree_(target) || target != focused_)) target = nullptr;

        // The field's own editing is a DEFAULT ACTION: it runs after handlers
        // have seen the key, and only if none of them claimed it. That is the
        // DOM's ordering, and it is what lets an app pre-empt a shortcut
        // without the field having to know about it.
        bool consumed = e.propagationStopped;
        // Clipboard first, and HERE rather than in UITextEdit, because this is
        // where the host's clipboard hooks live — the edit model has no business
        // knowing the machine has one.
        if (!consumed && target && target->textEdit() && k.ctrl) {
            UITextEdit& ed = *target->textEdit();
            if ((k.key == UIKey::C || k.key == UIKey::X) && clipboardWrite_) {
                if (ed.hasSelection()) {
                    // The real value, never the mask: cutting a password field
                    // must not put a row of asterisks on the clipboard.
                    clipboardWrite_(ed.selectedText());
                    if (k.key == UIKey::X) afterEdit(target, ed.DeleteSelection());
                }
                consumed = true;
            } else if (k.key == UIKey::V && clipboardRead_) {
                const std::string in = clipboardRead_();
                if (!in.empty()) afterEdit(target, ed.InsertText(in));
                consumed = true;
            }
        }
        if (!consumed && target && target->textEdit()) {
            bool changed = false;
            consumed = target->textEdit()->HandleKey(k, changed);
            if (consumed) afterEdit(target, changed);
        }

        // Tab is navigation ONLY if nothing consumed it — a handler, or a field
        // that wanted it. That is what would let a multi-line field keep its
        // literal tabs, and it is why this is a consumption check rather than a
        // hardcoded element-type test.
        if (k.key == UIKey::Tab && !consumed) FocusNext(k.shift);
    }

    if (!kb.text.empty() && focused_) {
        UIElement* target = focused_;
        UIEvent e;
        e.type = UIEventType::TextInput;
        e.text = kb.text;
        bubble_(target, e);
        if (target != focused_ || !isInTree_(target)) return;
        if (!e.propagationStopped && target->textEdit()) {
            afterEdit(target, target->textEdit()->InsertText(kb.text));
        }
    }
}

} // namespace MyCoreEngine::ui
