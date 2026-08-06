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

    // Authored -> real. See UIScale.h for the unit rule this enforces: Style
    // is in AUTHORED pixels, everything else in the system is in REAL surface
    // pixels, and the conversion happens here and nowhere else.
    float g_uiScale = 1.0f;

    struct UIPassScope {
        UIPassScope(const Font* f, float scale) {
            g_measureFont = f;
            g_uiScale = scale;
        }
        // Restored to 1, not left: a path that somehow runs without a scope must
        // behave exactly as it did before this feature existed.
        ~UIPassScope() { g_measureFont = nullptr; g_uiScale = 1.0f; }
    };

    // Monotonic counter over every structural change to any element tree. See
    // UIElement::structureEpoch() for why this is process-wide rather than
    // per-document, and why that is safe.
    std::uint32_t g_structureEpoch = 1;

    // The two conversions, and the ONLY two. Below this line a bare
    // `->Measure(` or a bare `s.padding` reaching geometry is a bug, which
    // makes the audit a grep. Adding a length to Style means adding it to
    // pushStyle or to an sx_ site — there is no third option.
    // Set for the duration of a Draw, so draw_ can resolve a background-image
    // id without threading the cache through every recursive call. Same shape,
    // and the same reason, as g_measureFont.
    UITextureCache* g_uiTextures = nullptr;

    inline float sx_(float authored) { return authored * g_uiScale; }
    inline glm::vec2 measure_(const Font* f, const std::string& text, float fontScale) {
        return f->Measure(text, fontScale * g_uiScale);
    }

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
        const glm::vec2 m = measure_(font, el->style().text, el->style().fontScale);
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
        // Points scale; PERCENT does not. A percentage is already relative to
        // a parent that has itself been scaled, so scaling it too would compound
        // and a 50%-wide bar would become 100% at scale 2.
        case StyleLength::Unit::Point:   setPt(n, sx_(v.value)); break;
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

        // flexGrow, flexShrink, aspectRatio and every alignment enum are
        // dimensionless and are deliberately NOT here.
        YGNodeStyleSetMargin(n, YGEdgeLeft,   sx_(s.margin.left));
        YGNodeStyleSetMargin(n, YGEdgeTop,    sx_(s.margin.top));
        YGNodeStyleSetMargin(n, YGEdgeRight,  sx_(s.margin.right));
        YGNodeStyleSetMargin(n, YGEdgeBottom, sx_(s.margin.bottom));
        YGNodeStyleSetPadding(n, YGEdgeLeft,   sx_(s.padding.left));
        YGNodeStyleSetPadding(n, YGEdgeTop,    sx_(s.padding.top));
        YGNodeStyleSetPadding(n, YGEdgeRight,  sx_(s.padding.right));
        YGNodeStyleSetPadding(n, YGEdgeBottom, sx_(s.padding.bottom));
        YGNodeStyleSetGap(n, YGGutterAll, sx_(s.gap));

        YGNodeStyleSetPositionType(n, s.position == PositionType::Absolute
                                          ? YGPositionTypeAbsolute
                                          : YGPositionTypeRelative);
        if (s.position == PositionType::Absolute) {
            YGNodeStyleSetPosition(n, YGEdgeLeft,   sx_(s.inset.left));
            YGNodeStyleSetPosition(n, YGEdgeTop,    sx_(s.inset.top));
            YGNodeStyleSetPosition(n, YGEdgeRight,  sx_(s.inset.right));
            YGNodeStyleSetPosition(n, YGEdgeBottom, sx_(s.inset.bottom));
        }

        YGNodeStyleSetDisplay(n, s.display == DisplayMode::None ? YGDisplayNone
                                                                : YGDisplayFlex);
        // No YGNodeStyleSetOverflow: yoga has ONE overflow value per node and a
        // two-axis style has no honest push, so a call here would have to pick
        // an axis and lie about the other. Nothing ever depended on it — it is
        // layout-inert in 3.1, which is why flex-shrink above does the work.

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

UISliderState& UIElement::MakeSlider() {
    if (!slider_) {
        slider_ = std::make_unique<UISliderState>();
        // A slider you cannot focus cannot be driven by a keyboard or a pad,
        // which is most of the point of having one. Comes with the type, like
        // a field's focusability.
        focusable_ = true;
    }
    return *slider_;
}

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
    if (handler) listeners_.push_back(Listener{ nullptr, type, std::move(handler) });
}

void UIElement::AddOwnedListener(const void* owner, UIEventType type, UIEventHandler handler) {
    if (handler) listeners_.push_back(Listener{ owner, type, std::move(handler) });
}

void UIElement::RemoveOwnedListeners(const void* owner) {
    if (!owner) return;   // null is the app's; never remove those
    listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(),
                                    [owner](const Listener& l) { return l.owner == owner; }),
                     listeners_.end());
}

void UIElement::ClearEventListeners() { listeners_.clear(); }

bool UIElement::dispatchLocal_(UIEvent& e) {
    // Iterate by INDEX over a snapshot of the count: a handler is allowed to
    // add listeners (or mutate the tree), and a range-for over a vector that
    // reallocates mid-dispatch is undefined behaviour. Handlers added during
    // dispatch deliberately do not run until the next event.
    bool ran = false;
    const size_t n = listeners_.size();
    for (size_t i = 0; i < n && i < listeners_.size(); ++i) {
        if (listeners_[i].type != e.type) continue;
        UIEventHandler h = listeners_[i].fn; // copy: the vector may move
        if (h) { h(e); ran = true; }
        if (e.propagationStopped) return ran;
    }
    return ran;
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
    //
    // The EFFECTIVE scale, not the authored one: the UI scale multiplies
    // fontScale inside the measure callback, so resizing the window changes
    // what a label measures without touching its style at all. Comparing the
    // authored value alone would leave every text leaf holding yoga's cached
    // measurement from the previous surface size — labels laid out at the old
    // size until something else happened to dirty them, which is this exact bug
    // class one level up.
    const float effFontScale = el.style_.fontScale * g_uiScale;
    if (isTextLeaf && effFontScale != el.pushedFontScale_ && el.yogaNode_) {
        YGNodeRef n = static_cast<YGNodeRef>(el.yogaNode_);
        if (YGNodeHasMeasureFunc(n)) YGNodeMarkDirty(n);
    }
    el.pushedFontScale_ = effFontScale;

    pushStyle(static_cast<YGNodeRef>(el.yogaNode_), el.style_, isTextLeaf, parentScrolls);
    // The MAIN axis, not "either axis". flex-shrink is spent against the flex
    // line's main dimension — there is no cross-axis shrink, that is what
    // align-items does — so a column scroller cares about Y and a row scroller
    // about X. Overriding on the wrong axis would leave a row scroller's
    // children squeezed with nothing to scroll.
    const bool row = el.style_.direction == FlexDirection::Row ||
                     el.style_.direction == FlexDirection::RowReverse;
    const bool scrolls = row ? ScrollsOnX(el.style_) : ScrollsOnY(el.style_);
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

    // A multi-line field is a text LEAF with no children, so the element-level
    // offset can never help it — its text moves through UITextEdit::textScroll_.
    // What it CAN share is the bar: measure the text here, mirror the offset
    // into a UIScrollState, and updateScrollBars_ needs no special case at all.
    // Single-line fields get no bar, because no native single-line input has one
    // and it would eat 8px of a 180px field.
    const bool fieldBar = el.edit_ && el.edit_->multiline() && !IsScroller(el.style_);
    if (fieldBar) {
        if (!el.scroll_) el.scroll_ = std::make_unique<UIScrollState>();
        UIScrollState& sc = *el.scroll_;
        YGNodeRef n = static_cast<YGNodeRef>(el.yogaNode_);
        const glm::vec2 box{ YGNodeLayoutGetWidth(n), YGNodeLayoutGetHeight(n) };
        const Style& s = el.style_;
        const glm::vec2 contentBox{
            std::max(0.0f, box.x - sx_(s.padding.left) - sx_(s.padding.right)),
            std::max(0.0f, box.y - sx_(s.padding.top) - sx_(s.padding.bottom))
        };
        // Font::Measure, never the laid-out width: the yoga measure callback
        // clamps its result to the parent's offer, so the element's own size
        // lies about how wide the text is — which is exactly the case that
        // needs a bar. With no font the last measurement stands rather than
        // collapsing to zero and making the bar flicker away.
        if (g_measureFont && g_measureFont->IsValid()) {
            const std::string shown = el.edit_->displayText();
            const float lineH = measure_(g_measureFont, "", s.fontScale).y;
            glm::vec2 extent = measure_(g_measureFont, shown, s.fontScale);
            extent.y = float(UITextEdit::LineIndexOf(shown, shown.size()) + 1) * lineH;
            el.edit_->setMeasured(extent, contentBox);
        }
        sc.contentMin = { 0.0f, 0.0f };
        sc.contentMax = el.edit_->textExtent();
        sc.minOffset = { 0.0f, 0.0f };
        sc.maxOffset = el.edit_->maxTextScroll();
        // Scaled with everything else: the fractional advances this floor
        // exists to swallow are themselves scaled now, so a fixed 1.0f would
        // grow spurious bars at scale 2 and swallow two real pixels of genuine
        // overflow at scale 0.5.
        if (sc.maxOffset.x < sx_(1.0f)) sc.maxOffset.x = 0.0f;
        if (sc.maxOffset.y < sx_(1.0f)) sc.maxOffset.y = 0.0f;
        // MIRRORED, not owned: textScroll_ is where the glyphs actually read
        // from, so a bar derived from anything else would track the cursor
        // perfectly while the text stayed put.
        sc.offset = sc.target = el.edit_->textScroll();
    } else if (!IsScroller(el.style_)) {
        // EVERY element is visited, not just scrollers: a class toggle or a
        // :hover rule can take `scroll` away at any moment while the offset —
        // which deliberately outlives Style — stays behind. Zeroing it here
        // rather than testing overflow down in readLayout_ means a de-scrolled
        // element can never displace its children into unclipped space.
        if (el.scroll_) {
            // A declared extent is authored intent, not derived state, so it
            // survives the element temporarily not being a scroller — exactly
            // as the offset survives a :hover restyle.
            const glm::vec2 declared = el.scroll_->declaredExtent;
            const bool hasDeclared = el.scroll_->hasDeclaredExtent;
            *el.scroll_ = UIScrollState{};
            el.scroll_->declaredExtent = declared;
            el.scroll_->hasDeclaredExtent = hasDeclared;
        }
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
        hi.x += sx_(el.style_.padding.right);
        hi.y += sx_(el.style_.padding.bottom);

        const bool ok = std::isfinite(lo.x) && std::isfinite(lo.y) &&
                        std::isfinite(hi.x) && std::isfinite(hi.y);
        sc.contentMin = ok ? lo : glm::vec2(0.0f);
        sc.contentMax = ok ? hi : glm::vec2(0.0f);
        // A declared extent wins over the measurement. Only the far edge: the
        // near edge is still measured, because a justify rule can put real
        // content at a negative offset and no caller should have to know that.
        if (sc.hasDeclaredExtent) {
            sc.contentMax = sc.contentMin + sc.declaredExtent;
        }
        sc.minOffset = glm::min(sc.contentMin, glm::vec2(0.0f));
        sc.maxOffset = glm::max(sc.contentMax - box, sc.minOffset);
        // Font::Measure accumulates fractional advances, so a row measuring
        // 220.4 in a 220px box is a measurement artefact, not content. Without
        // this floor every vertical list grows a horizontal scrollbar.
        if (sc.maxOffset.x - sc.minOffset.x < sx_(1.0f)) { sc.minOffset.x = sc.maxOffset.x = 0.0f; }
        if (sc.maxOffset.y - sc.minOffset.y < sx_(1.0f)) { sc.minOffset.y = sc.maxOffset.y = 0.0f; }
        // An axis that does not scroll has no valid offset, so its range is
        // pinned shut and the clamp below drives any leftover offset back to 0.
        // AFTER the artefact floor, because the floor can collapse an axis and a
        // pin applied first would simply be undone.
        if (!ScrollsOnX(el.style_)) { sc.minOffset.x = sc.maxOffset.x = 0.0f; }
        if (!ScrollsOnY(el.style_)) { sc.minOffset.y = sc.maxOffset.y = 0.0f; }
        // Finite-checked at the SOURCE: a NaN offset would reach PushClipRect,
        // and lround(NaN) hands glScissor a negative width.
        const bool offOk = std::isfinite(sc.offset.x) && std::isfinite(sc.offset.y);
        sc.offset = glm::clamp(offOk ? sc.offset : glm::vec2(0.0f),
                               sc.minOffset, sc.maxOffset);
        // The TARGET is clamped too, or content shrinking mid-animation would
        // leave the ease pulling forever toward somewhere unreachable.
        const bool tgtOk = std::isfinite(sc.target.x) && std::isfinite(sc.target.y);
        sc.target = glm::clamp(tgtOk ? sc.target : sc.offset,
                               sc.minOffset, sc.maxOffset);

    }
    // Noted for AdvanceTime, which otherwise has to walk the tree every frame
    // to discover there is nothing to animate. Deliberately "has a scroller"
    // rather than "is animating": an app calling SetScrollOffset or
    // ScrollIntoView directly has no way to arm a document flag, and an
    // animation that silently never ran would be a miserable thing to debug.
    if (el.scroll_) hasScrollers_ = true;
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

    // Seeded with this element's own painted rect. A text leaf grows it by the
    // MEASURED text, not the box: measureText clamps its result to the parent's
    // offer while DrawText walks the pen with no clipping of its own, so a long
    // <Label> genuinely paints outside its element. Culling on the box would
    // drop glyphs that are on screen.
    glm::vec2 lo = pos, hi = pos + el.layout_.size;
    if (!el.style_.text.empty() && g_measureFont && g_measureFont->IsValid()) {
        const glm::vec2 m = measure_(g_measureFont, el.style_.text, el.style_.fontScale);
        hi = glm::max(hi, pos + glm::vec2(sx_(el.style_.padding.left),
                                          sx_(el.style_.padding.top)) + m);
    }

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
        // display:none paints nothing, so it contributes nothing. Everything
        // else folds in, INCLUDING a clipping child's own descendants — keeping
        // the union conservative rather than reasoning about what its clip would
        // have removed.
        if (c->style_.display == DisplayMode::None) continue;
        lo = glm::min(lo, c->layout_.subtreeMin);
        hi = glm::max(hi, c->layout_.subtreeMax);
    }
    el.layout_.subtreeMin = lo;
    el.layout_.subtreeMax = hi;
    if (el.scroll_) updateScrollBars_(el);
}

// Pure, so the geometry is assertable with hand-supplied numbers in a suite
// that has no GL and no font — the same reasoning that made FollowCaret pure.
ScrollBarRects ComputeScrollBars(const glm::vec2& boxPos, const glm::vec2& boxSize,
                                 const glm::vec2& offset, const glm::vec2& minOffset,
                                 const glm::vec2& maxOffset,
                                 float barWidth, float minThumb, bool alwaysVisible,
                                 float cornerInsetPx) {
    ScrollBarRects out{};
    if (boxSize.x <= 0.0f || boxSize.y <= 0.0f) return out;

    // A bar wider than the element is not a bar. Clamped to half the smaller
    // side, and a width of 0 paints nothing at all — which is the sanctioned
    // way to hide a bar while keeping the axis scrollable.
    const float bar = std::clamp(barWidth, 0.0f, std::min(boxSize.x, boxSize.y) * 0.5f);
    if (bar <= 0.0f) return out;

    // Range, not extent: with centred overflow the reachable span starts
    // negative, and the thumb has to represent what you can actually reach.
    const glm::vec2 span = maxOffset - minOffset;
    // `always` paints on an axis that COULD scroll even while its content fits;
    // the thumb then fills its track and simply does not move.
    const bool paintY = span.y > 0.0f || (alwaysVisible && maxOffset.y >= minOffset.y);
    const bool paintX = span.x > 0.0f || (alwaysVisible && maxOffset.x >= minOffset.x);
    if (!paintX && !paintY) return out;

    // Each track stops short of the other, or the two overlap in a bar-by-bar
    // square in the corner and each thumb can travel into the other's track.
    // Pulled in from BOTH ends by the corner inset, so a bar on a rounded
    // scroller cannot paint its square end outside the silhouette. Clipping
    // cannot save it — clipping is the axis-aligned scissor, and the corner it
    // would have to cut is not. Clamped to a quarter of the side so an absurd
    // radius cannot collapse the track to nothing.
    const float inset = std::clamp(cornerInsetPx, 0.0f,
                                   std::min(boxSize.x, boxSize.y) * 0.25f);
    const float trackH = boxSize.y - (paintX ? bar : 0.0f) - inset * 2.0f;
    const float trackW = boxSize.x - (paintY ? bar : 0.0f) - inset * 2.0f;

    auto round2 = [](glm::vec2 v) { return glm::round(v); };

    if (paintY && trackH > 0.0f) {
        const float content = boxSize.y + span.y;
        const float thumbH = std::min(trackH,
            std::max(minThumb, trackH * (content > 0.0f ? boxSize.y / content : 1.0f)));
        const float free = std::max(0.0f, trackH - thumbH);
        const float t = span.y > 0.0f ? (offset.y - minOffset.y) / span.y : 0.0f;
        out.trackY.position = round2({ boxPos.x + boxSize.x - bar, boxPos.y + inset });
        out.trackY.size     = round2({ bar, trackH });
        out.thumbY.position = round2({ boxPos.x + boxSize.x - bar,
                                       boxPos.y + inset + free * t });
        out.thumbY.size     = round2({ bar, thumbH });
    }
    if (paintX && trackW > 0.0f) {
        const float content = boxSize.x + span.x;
        const float thumbW = std::min(trackW,
            std::max(minThumb, trackW * (content > 0.0f ? boxSize.x / content : 1.0f)));
        const float free = std::max(0.0f, trackW - thumbW);
        const float t = span.x > 0.0f ? (offset.x - minOffset.x) / span.x : 0.0f;
        out.trackX.position = round2({ boxPos.x + inset, boxPos.y + boxSize.y - bar });
        out.trackX.size     = round2({ trackW, bar });
        out.thumbX.position = round2({ boxPos.x + inset + free * t,
                                       boxPos.y + boxSize.y - bar });
        out.thumbX.size     = round2({ thumbW, bar });
    }
    // Rounded to whole pixels because the CONTENT steps in whole pixels
    // (readLayout_ rounds the offset); an unrounded thumb would drift a pixel
    // out of step with the rows it is describing. The unrounded offset stays the
    // source of truth so a sub-pixel drag still integrates.
    return out;
}

void UIDocument::updateScrollBars_(UIElement& el) {
    UIScrollState& sc = *el.scroll_;
    const Style& st = el.style_;
    const ScrollBarRects r = ComputeScrollBars(
        el.layout_.position, el.layout_.size, sc.offset, sc.minOffset, sc.maxOffset,
        sx_(st.scrollbarWidth), sx_(st.scrollbarMinThumb),
        st.scrollbarVisibility == ScrollbarVisibility::Always,
        // The corner inset IS the radius: that is exactly how far in the
        // silhouette has pulled away from the box at the ends of each track.
        sx_(st.borderRadius));
    sc.trackX = r.trackX; sc.thumbX = r.thumbX;
    sc.trackY = r.trackY; sc.thumbY = r.thumbY;
}

bool UIElement::SetScrollOffset(glm::vec2 px) {
    if (!scroll_) return false;
    if (!std::isfinite(px.x) || !std::isfinite(px.y)) return false;

    // A multi-line field's offset lives in its edit model, because that is what
    // the glyphs are drawn from. Writing scroll_->offset instead would make a
    // dragged thumb track the cursor perfectly while the text never moved.
    if (edit_) {
        if (!edit_->SetTextScroll(px)) return false;
        scroll_->offset = scroll_->target = edit_->textScroll();
        return true;
    }

    const glm::vec2 next = glm::clamp(px, scroll_->minOffset, scroll_->maxOffset);
    // Sampled BEFORE the write, or an instant scroll reports that it did not
    // move — which silently defeats every caller that relayouts on `true`.
    const bool moved = next != scroll_->offset;
    scroll_->target = next;
    // `smooth` moves the DESTINATION and lets AdvanceTime walk the offset there.
    // Instant is the default precisely so Layout() stays a pure function of the
    // tree unless an author asks otherwise.
    if (style_.scrollBehavior == ScrollBehavior::Instant) scroll_->offset = next;
    return moved;
}

void UIElement::SetContentExtent(const glm::vec2& px) {
    if (!std::isfinite(px.x) || !std::isfinite(px.y)) return;
    if (!scroll_) scroll_ = std::make_unique<UIScrollState>();
    scroll_->declaredExtent = glm::max(px, glm::vec2(0.0f));
    scroll_->hasDeclaredExtent = true;
}

void UIElement::ClearContentExtent() {
    if (scroll_) scroll_->hasDeclaredExtent = false;
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
    // Computed from the SURFACE, never from the viewport this document was
    // given: a document that occupies a corner of the screen must scale by how
    // big the SCREEN is, or a quarter-width panel would shrink its own text on
    // the large display the feature exists for. Falling back to the viewport
    // keeps a standalone UIDocument — every test, and any host that never calls
    // SetSurfaceSize — behaving exactly as it always has.
    const glm::vec2 surface = (surfaceSize_.x > 0.0f && surfaceSize_.y > 0.0f)
                                  ? surfaceSize_
                                  : glm::vec2(viewportW, viewportH);
    scale_ = ComputeUIScale(scaleSettings_, surface);

    // Scoped so the measure callback can find the font AND the scale, and so
    // both are cleared even if layout throws.
    UIPassScope pass(font, scale_);

    UIElement& r = *root_;
    // Still the document's REAL region. Layout solves at real size; only the
    // authored lengths going into it are scaled.
    viewport_ = { viewportW, viewportH };
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

namespace {
    // Half-open on the max edges, so two rects that merely touch do not count as
    // overlapping — an element flush against the bottom of a clip paints nothing.
    bool aabbHitsRect(const glm::vec2& lo, const glm::vec2& hi, const ComputedLayout& c) {
        return !(hi.x <= c.position.x || lo.x >= c.position.x + c.size.x ||
                 hi.y <= c.position.y || lo.y >= c.position.y + c.size.y);
    }
    ComputedLayout intersectRect(const ComputedLayout* outer, const ComputedLayout& inner) {
        if (!outer) return inner;
        const glm::vec2 lo = glm::max(outer->position, inner.position);
        const glm::vec2 hi = glm::min(outer->position + outer->size,
                                      inner.position + inner.size);
        ComputedLayout out;
        out.position = lo;
        out.size = glm::max(hi - lo, glm::vec2(0.0f));
        return out;
    }
} // namespace

void UIDocument::draw_(const UIElement& el, Renderer2D& r2d, const Font* font,
                       int layer, const UIElement* focused, bool caretVisible,
                       const ComputedLayout* clip) {
    const ComputedLayout& L = el.layout_;
    const Style& s = el.style_;
    // Explicit, even though yoga zeroes a display:none subtree and the size
    // test below would therefore cover it. "Invisible" must not depend on an
    // implementation detail of whichever layout engine is underneath.
    if (s.display == DisplayMode::None) return;
    if (L.size.x <= 0.0f || L.size.y <= 0.0f) return; // nothing to paint

    // Cost only, never behaviour: the subtree AABB is a superset of what this
    // element and its descendants paint, so a subtree that misses the clip
    // entirely could not have put a pixel on screen. Scrolling is what makes
    // this worth having — before it, everything in the tree was on screen by
    // construction. Placed before the background quad so the element's own paint
    // is skipped too. draw_ is static and takes a const element, so the compiler
    // guarantees a skipped subtree has no side effect to lose.
    if (clip && !aabbHitsRect(L.subtreeMin, L.subtreeMax, *clip)) return;

    // Radius and border are AUTHORED lengths reaching geometry, so they are
    // scaled here — the two new entries on this file's sx_ audit list.
    Renderer2D::BoxStyle boxStyle;
    boxStyle.radiusPx = sx_(s.borderRadius);
    // A gradient forces the box path even with no radius and no border -- see
    // DrawBox. Mapped rather than cast: the two enums are independent types and
    // nothing should quietly depend on their orders matching.
    switch (s.backgroundGradient) {
    case BackgroundGradient::Vertical:
        boxStyle.gradient = Renderer2D::BoxGradient::Vertical; break;
    case BackgroundGradient::Horizontal:
        boxStyle.gradient = Renderer2D::BoxGradient::Horizontal; break;
    default:
        boxStyle.gradient = Renderer2D::BoxGradient::None; break;
    }
    boxStyle.fillTo = s.backgroundColorTo;
    boxStyle.borderPx = sx_(s.borderWidth);
    boxStyle.borderColor = s.borderColor;
    const bool shaped = boxStyle.radiusPx > 0.0f || boxStyle.borderPx > 0.0f;

    // The image is a SEPARATE quad over the background colour, never a tint of
    // it. Multiplying by backgroundColor looks obvious and is fatal: the
    // default is fully transparent, so the first panel anybody writes would
    // paint nothing at all.
    UITextureCache::Entry img;
    if (s.backgroundImage != 0 && g_uiTextures) img = g_uiTextures->Get(s.backgroundImage);

    if (s.backgroundColor.a > 0.0f || shaped) {
        // DrawBox degrades to a plain sprite when it needs neither a radius nor
        // a border, so an unstyled background costs exactly what it always did.
        r2d.DrawBox(L.position, L.size, s.backgroundColor, boxStyle, 0, TexRegion{}, layer);
    }
    if (img.texture) {
        TexRegion region;
        if (s.backgroundSize == BackgroundSize::Cover) {
            region = CoverRegion(L.size, glm::vec2(float(img.size.x), float(img.size.y)));
        }
        // Same layer as the fill and emitted after it, so it composites over
        // the colour and no child's layer can land between the two.
        r2d.DrawBox(L.position, L.size, glm::vec4(1.0f), boxStyle,
                    img.texture, region, layer);
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
    const bool clips = ClipsBox(s) || el.edit_ != nullptr;
    if (clips) r2d.PushClipRect(L.position, L.size);

    // Text sits inside the padding box, matching CSS, shifted by the field's own
    // text scroll. Everything below is expressed relative to `tp`, so this one
    // subtraction moves the glyphs, the selection and the caret together.
    const glm::vec2 textOff =
        el.edit_ ? glm::round(el.edit_->textScroll()) : glm::vec2(0.0f);
    const glm::vec2 tp{ L.position.x + sx_(s.padding.left) - textOff.x,
                        L.position.y + sx_(s.padding.top) - textOff.y };
    const bool haveFont = font && font->IsValid();

    // ---- text field decoration -------------------------------------------
    // Selection UNDER the text, caret over it, and both only for the one
    // focused field — a caret on an unfocused field says "type here" when
    // typing would go somewhere else entirely.
    const UITextEdit* edit = el.edit_.get();
    if (edit && haveFont && &el == focused) {
        const std::string shown = edit->displayText();
        const float lineH = measure_(font, "", s.fontScale).y;
        // Position within the LINE, not within the whole value: a multi-line
        // field measures the prefix from its own line start and steps down by
        // whole lines, which is what makes a caret land where the glyph is.
        auto place = [&](std::size_t bytes) {
            bytes = std::min(bytes, shown.size());
            const std::size_t ls = UITextEdit::LineStart(shown, bytes);
            const float x = measure_(font, shown.substr(ls, bytes - ls), s.fontScale).x;
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
            const float caretW = std::max(1.0f, std::round(sx_(1.0f)));
            r2d.DrawQuad(place(edit->caret()), { caretW, lineH }, s.textColor, layer + 1);
        }
    }

    if (!s.text.empty() && haveFont) {
        r2d.DrawText(*font, s.text, tp, s.textColor, layer, s.fontScale * g_uiScale);
    }

    // Parent before child (painter's algorithm) AND on a higher layer, so a
    // child always paints over its parent's background regardless of the order
    // the batcher ends up flushing runs in.
    const ComputedLayout myClip = clips ? intersectRect(clip, L) : ComputedLayout{};
    for (const auto& c : el.children_) {
        draw_(*c, r2d, font, layer + 1, focused, caretVisible, clips ? &myClip : clip);
    }

    // Bars LAST and inside the clip, so they paint over the content they
    // describe and can never escape the box. A zero-size thumb means that axis
    // does not scroll — the one signal the painter and the hit test share.
    if (el.scroll_) {
        const UIScrollState& sc = *el.scroll_;
        // From the STYLE. These were literals equal to the defaults, which made
        // `scrollbar-color` and `scrollbar-thumb-color` parse, validate, store —
        // and do nothing. A property that is accepted and then ignored is the
        // exact silent no-op this codebase reports errors to avoid, and it was
        // invisible precisely because the literals matched the defaults.
        const glm::vec4 trackCol = s.scrollbarColor;
        const glm::vec4 thumbCol = s.scrollbarThumbColor;
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

// Eases every animating scroller one step toward its target.
void UIDocument::advanceScroll_(UIElement& el, float dt) {
    if (el.scroll_ && !el.edit_) {
        UIScrollState& sc = *el.scroll_;
        if (sc.offset != sc.target) {
            if (el.style_.scrollBehavior == ScrollBehavior::Smooth) {
                // Frame-rate INDEPENDENT: a plain `offset += (target-offset)*k`
                // moves further per second at 144Hz than at 60, so the same
                // flick would feel different on two machines.
                const float t = 1.0f - std::exp(-kSmoothScrollRate * dt);
                glm::vec2 next = sc.offset + (sc.target - sc.offset) * t;
                // An exponential approach never arrives. Snap inside half a
                // pixel — below what readLayout_'s rounding can even show — or
                // the document would relayout forever.
                if (std::abs(sc.target.x - next.x) < sx_(0.5f)) next.x = sc.target.x;
                if (std::abs(sc.target.y - next.y) < sx_(0.5f)) next.y = sc.target.y;
                sc.offset = next;
            } else {
                sc.offset = sc.target;   // behaviour changed mid-animation
            }
            scrollDirty_ = true;
        }
    }
    for (auto& c : el.children_) advanceScroll_(*c, dt);
}

void UIDocument::AdvanceTime(float dt) {
    caretClock_ += dt;
    // A monotonic document clock. The wheel needs it to tell one GESTURE from
    // the next, and UpdatePointer has no dt of its own — this runs before it in
    // the same frame, which is the ordering UIWorld::Update guarantees.
    docClock_ += dt;

    // Release a synthesized press once its flash has been on screen long
    // enough to see. Revalidated, because a handler fired BY that press may
    // have removed the element it was on -- a menu button that swaps the scene
    // is exactly that case.
    if (activated_ && docClock_ >= activateUntil_) {
        if (isInTree_(activated_)) activated_->pressed_ = false;
        activated_ = nullptr;
    }

    // Gated on the document containing a scroller AT ALL, not on one being
    // known to animate: the target can be moved by anything, including app code
    // calling ScrollIntoView, and none of those can arm a document flag. A HUD
    // with no scroller pays nothing; one with a scroller pays a walk that
    // touches only the elements holding scroll state.
    if (hasScrollers_ && dt > 0.0f) advanceScroll_(*root_, dt);
}

// Scrolls the nearest ancestor of `from` that has range on the key's axis.
// Returns true if it moved anything, which is what makes the key CONSUMED.
bool UIDocument::keyboardScroll_(UIElement* from, UIKey key) {
    for (UIElement* n = from; n; n = n->parent_) {
        if (!IsScroller(n->style_) || !n->scroll_) continue;
        const UIScrollState& sc = *n->scroll_;
        if (!sc.scrollsY()) continue;      // vertical keys only; see below
        glm::vec2 next = sc.offset;
        switch (key) {
        case UIKey::PageUp:   next.y -= pageAmount_(*n, 1); break;
        case UIKey::PageDown: next.y += pageAmount_(*n, 1); break;
        case UIKey::Home:     next.y = sc.minOffset.y; break;
        case UIKey::End:      next.y = sc.maxOffset.y; break;
        default: return false;
        }
        if (n->SetScrollOffset(next)) { scrollDirty_ = true; return true; }
        // Found the owner but it is already at that end: still consumed, or the
        // key would fall through to something further out and jump twice.
        return true;
    }
    return false;
}

void UIDocument::Draw(Renderer2D& r2d, const Font* font, int baseLayer,
                      UITextureCache* textures) const {
    UIPassScope pass(font, scale_);
    // Restored on the way out rather than left set, so a document drawn without
    // a cache cannot inherit the previous document's.
    struct Restore {
        UITextureCache* prev;
        ~Restore() { g_uiTextures = prev; }
    } restore{ g_uiTextures };
    g_uiTextures = textures;
    // A one-second cycle, on for the first half. Computed once here rather than
    // per element: only the focused field can show a caret.
    const float kBlink = 1.0f;
    const bool caretVisible =
        (caretClock_ - std::floor(caretClock_ / kBlink) * kBlink) < kBlink * 0.5f;
    // Seeded with the SURFACE, not with nothing: the only clipping element in a
    // typical HUD is one scroller, so a cull that tested only "the clip it is
    // under" would be a no-op everywhere else — including for a list scrolled
    // entirely off a small viewport.
    ComputedLayout surface;
    surface.position = origin_;
    surface.size = viewport_;
    const bool haveSurface = viewport_.x > 0.0f && viewport_.y > 0.0f;
    draw_(*root_, r2d, font, baseLayer, focused_, caretVisible,
          haveSurface ? &surface : nullptr);
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
    if (ClipsBox(el.style_) && !contains(el.layout_, pos)) return nullptr;

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
// without this a press on one resolves to the row underneath it, fires that
// row's on-click, lights up :active, and blurs whatever field the user was
// typing in. That used to be true of the TRACK across ~80% of the bar's area,
// because only the thumbs were tested here.
//
// Within an element: thumb before track (a thumb sits inside its own track),
// and Y before X (the two tracks meet in the bottom-right corner). Every track
// test is gated on its own thumb having size, so "a zero-size thumb means this
// axis does not scroll" stays the one signal the painter and the hit test share.
UIElement* UIDocument::hitScrollBar_(UIElement& el, const glm::vec2& pos,
                                     int& axisOut, bool& onThumbOut) {
    if (el.style_.display == DisplayMode::None) return nullptr;
    if (!el.enabled_ || !el.style_.pickable) return nullptr;
    if (ClipsBox(el.style_) && !contains(el.layout_, pos)) return nullptr;

    for (auto it = el.children_.rbegin(); it != el.children_.rend(); ++it) {
        if (UIElement* h = hitScrollBar_(**it, pos, axisOut, onThumbOut)) return h;
    }
    if (el.scroll_) {
        const UIScrollState& sc = *el.scroll_;
        const bool hasY = sc.thumbY.size.y > 0.0f;
        const bool hasX = sc.thumbX.size.x > 0.0f;
        if (hasY && contains(sc.thumbY, pos)) { axisOut = 1; onThumbOut = true;  return &el; }
        if (hasX && contains(sc.thumbX, pos)) { axisOut = 0; onThumbOut = true;  return &el; }
        if (hasY && contains(sc.trackY, pos)) { axisOut = 1; onThumbOut = false; return &el; }
        if (hasX && contains(sc.trackX, pos)) { axisOut = 0; onThumbOut = false; return &el; }
    }
    return nullptr;
}

// A page is most of the visible box, with an overlap so the line you were
// reading stays on screen — the browser convention. Shared by the track click
// and PageUp/PageDown, so the two move by the same amount.
float UIDocument::pageAmount_(const UIElement& el, int axis) {
    const float box = axis ? el.layout_.size.y : el.layout_.size.x;
    return std::max(1.0f, box * 0.9f);
}

// The wheel delta this element should actually receive.
//
// A mouse has ONE wheel, so an element that scrolls only horizontally — a
// hotbar, a card strip, an inventory row, the common non-list scroller in a HUD
// — would otherwise be dead to it: a vertical notch is {0, ±1}, which fails the
// X test, and the element fails the Y test too, so the wheel walks straight past
// it to an ancestor. Browsers do exactly this transfer for an overflow-x-only
// box. Evaluated per candidate, because scrollability is per element.
glm::vec2 UIDocument::axisLockedDelta_(const UIElement& el, glm::vec2 raw) {
    if (!el.scroll_) return { 0.0f, 0.0f };
    const UIScrollState& sc = *el.scroll_;
    if (raw.x == 0.0f && sc.scrollsX() && !sc.scrollsY()) { raw.x = raw.y; raw.y = 0.0f; }
    return { sc.scrollsX() ? raw.x : 0.0f, sc.scrollsY() ? raw.y : 0.0f };
}

bool UIDocument::bubble_(UIElement* target, UIEvent& e) {
    bool ran = false;
    e.target = target;
    for (UIElement* cur = target; cur; cur = cur->parent_) {
        e.currentTarget = cur;
        ran |= cur->dispatchLocal_(e);
        if (e.propagationStopped) return ran;
    }
    return ran;
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

// Cursor -> slider value, in the element's OWN box.
//
// layout() is in REAL surface pixels and so is the pointer, so no scale
// conversion belongs here — that happens where authored lengths ENTER layout,
// not where laid-out rects come out of it.
void UIDocument::applySliderFromCursor_(UIElement& el, const glm::vec2& cursor) {
    UISliderState& sl = *el.slider_;
    const ComputedLayout& L = el.layout();
    const bool v = sl.vertical;
    const float len = v ? L.size.y : L.size.x;
    if (!(len > 0.0f)) return;
    float t = ((v ? cursor.y : cursor.x) - (v ? L.position.y : L.position.x)) / len;
    // Vertical sliders read bottom-up: the top of the box is the MAXIMUM, which
    // is what a mixer fader looks like and the opposite of screen-space y.
    if (v) t = 1.0f - t;
    if (sl.SetNormalised(t)) {
        // Same flag the scroll drag sets: the value has to reach the source and
        // the bound fill has to relayout THIS frame, or the bar trails the
        // cursor for the whole drag.
        scrollDirty_ = true;
    }
}

void UIDocument::UpdatePointer(const UIPointerState& p, const Font* font) {
    UIPassScope pass(font, scale_);
    // Drop cached targets that are no longer in the tree. Gameplay or a handler
    // may have removed the hovered/pressed element since last frame, and
    // dispatching to it would be a use-after-free.
    if (hovered_ && !isInTree_(hovered_)) hovered_ = nullptr;
    if (pressed_ && !isInTree_(pressed_)) pressed_ = nullptr;
    // The bar targets are held across frames just like the two above, and a hot
    // reload — which a stylesheet-only save triggers — frees the whole tree.
    if (capture_ && !isInTree_(capture_)) { capture_ = nullptr; captureKind_ = Capture::None; }
    if (wheelLatch_ && !isInTree_(wheelLatch_)) wheelLatch_ = nullptr;

    // ---- scrollbar press ---------------------------------------------------
    // Resolved AHEAD of everything else and allowed to own the pointer, exactly
    // like a native scrollbar. The bars are painted from draw_ and are invisible
    // to hitTest_, so without this a press on one would reach the row
    // underneath: its on-click would fire, :active would light up, and focus
    // would leave whatever field the user was typing in.
    if (p.buttonDown && !wasDown_ && !capture_ && p.inside) {
        int axis = 0;
        bool onThumb = false;
        if (UIElement* s = hitScrollBar_(*root_, p.position, axis, onThumb)) {
            // A native scrollbar focuses its scroller on press. Without this,
            // "drag the thumb, then press PageDown" is a coin flip.
            if (s->focusable_) SetFocus(s);
            if (onThumb) {
                capture_ = s;
                captureKind_ = Capture::ScrollThumb;
                captureAxis_ = axis;
                const ComputedLayout& th = axis ? s->scroll_->thumbY : s->scroll_->thumbX;
                captureGrab_ = axis ? p.position.y - th.position.y
                                    : p.position.x - th.position.x;
            } else {
                // ONE page per press, toward the click. Hold-to-repeat would
                // need "how long held", and UpdatePointer has no dt — it is the
                // second-order half of what is really a click-through fix, and
                // the manual says plainly that a held press does not repeat.
                const ComputedLayout& th = axis ? s->scroll_->thumbY : s->scroll_->thumbX;
                const float cursor = axis ? p.position.y : p.position.x;
                const float thumbLo = axis ? th.position.y : th.position.x;
                const float dir = cursor < thumbLo ? -1.0f : 1.0f;
                glm::vec2 next = s->scroll_->offset;
                (axis ? next.y : next.x) += dir * pageAmount_(*s, axis);
                if (s->SetScrollOffset(next)) scrollDirty_ = true;
                capture_ = s;
                captureKind_ = Capture::ScrollTrack;
            }
        }
    }
    // A SLIDER press, resolved after the scrollbars (which are painted over
    // everything and must win) but before the ordinary hit-test, because the
    // press has to become a value immediately: clicking anywhere on a slider
    // jumps to that value, exactly like a native one.
    if (p.buttonDown && !wasDown_ && !capture_ && p.inside) {
        if (UIElement* hit = HitTest(p.position)) {
            for (UIElement* e = hit; e; e = e->parent_) {
                if (!e->slider_ || !isInteractable_(*e)) continue;
                capture_ = e;
                captureKind_ = Capture::Slider;
                captureAxis_ = e->slider_->vertical ? 1 : 0;
                // No grab offset: a press JUMPS to the pressed value rather
                // than picking the thumb up where it was. That is what makes
                // the whole track clickable, and the drag then continues from
                // there with no discontinuity because the value already
                // matches the cursor.
                captureGrab_ = 0.0f;
                if (e->focusable_) SetFocus(e);
                applySliderFromCursor_(*e, p.position);
                break;
            }
        }
    }

    // A press on the track owns the pointer for as long as it is held, so the
    // row underneath receives no move, no hover and no Click on release.
    if (captureKind_ == Capture::ScrollTrack) {
        if (!p.buttonDown) { capture_ = nullptr; captureKind_ = Capture::None; }
        hadPointer_ = p.inside;
        wasDown_ = p.buttonDown;
        lastPos_ = p.position;
        return;
    }
    if (captureKind_ == Capture::Slider) {
        if (!p.buttonDown) {
            capture_ = nullptr;
            captureKind_ = Capture::None;
        } else {
            applySliderFromCursor_(*capture_, p.position);
        }
        hadPointer_ = p.inside;
        wasDown_ = p.buttonDown;
        lastPos_ = p.position;
        return;
    }
    if (captureKind_ == Capture::ScrollThumb) {
        if (!p.buttonDown) {
            capture_ = nullptr;
            captureKind_ = Capture::None;
        } else {
            UIScrollState& sc = *capture_->scroll_;
            const int a = captureAxis_;
            const ComputedLayout& track = a ? sc.trackY : sc.trackX;
            const ComputedLayout& thumb = a ? sc.thumbY : sc.thumbX;
            const float trackLen = a ? track.size.y : track.size.x;
            const float thumbLen = a ? thumb.size.y : thumb.size.x;
            const float free = trackLen - thumbLen;
            if (free > 0.0f) {
                const float cursor = a ? p.position.y : p.position.x;
                const float trackTop = a ? track.position.y : track.position.x;
                const float t = std::clamp((cursor - captureGrab_ - trackTop) / free,
                                           0.0f, 1.0f);
                const float lo = a ? sc.minOffset.y : sc.minOffset.x;
                const float hi = a ? sc.maxOffset.y : sc.maxOffset.x;
                glm::vec2 next = sc.offset;
                (a ? next.y : next.x) = lo + (hi - lo) * t;
                if (capture_->SetScrollOffset(next)) scrollDirty_ = true;
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
        e.shift = p.shift;
        bubble_(hit, e);
        // Handlers may restructure the tree — dispatchLocal_ explicitly allows
        // it — so re-validate before walking parent_ upward, exactly as the
        // Click path re-checks isInTree_ before firing.
        if (!e.propagationStopped && isInTree_(hit)) {
            // ~3 rows at the default 16px line height. One constant, here, so
            // the two hosts only ever have to agree on a SIGN.
            constexpr float kPixelsPerNotch = 48.0f;
            // A new GESTURE begins after a gap with no wheel at all. Within one
            // gesture the latch below keeps whatever it first moved.
            constexpr float kGestureGap = 0.15f;
            const bool newGesture = (docClock_ - lastWheelTime_) > kGestureGap;
            lastWheelTime_ = docClock_;
            if (newGesture) wheelLatch_ = nullptr;

            // Shift swaps the axes, the desktop convention for reaching a
            // horizontal scroller with a one-wheel mouse. Done HERE, once, so
            // the Editor's Game view and the shipped Player cannot disagree
            // about it — each host only reports whether shift is held.
            glm::vec2 raw = e.delta;
            if (e.shift) std::swap(raw.x, raw.y);

            auto canMove = [](const UIScrollState& sc, int a, float d) {
                // d < 0 pushes the offset up (content moves up), d > 0 down.
                const float off = a ? sc.offset.y : sc.offset.x;
                return d < 0.0f ? off < (a ? sc.maxOffset.y : sc.maxOffset.x)
                                : off > (a ? sc.minOffset.y : sc.minOffset.x);
            };

            UIElement* target = wheelLatch_;
            glm::vec2 d{ 0.0f };
            if (target) {
                // Mid-gesture: the latched element keeps the wheel even at the
                // end of its travel. This is what makes chaining safe — without
                // it, reading a list to its bottom and rolling once more would
                // carry the whole outer panel out from under the cursor.
                d = axisLockedDelta_(*target, raw);
            } else {
                // A new gesture CHAINS: the first ancestor that can still move
                // in this direction wins, so a list already at its end hands the
                // next gesture to its container, exactly as a browser does.
                UIElement* firstScrollable = nullptr;
                for (UIElement* n = hit; n; n = n->parent_) {
                    // A multi-line field is not `overflow: scroll` — it is a
                    // text leaf — but it does scroll, and it now paints a bar. A
                    // bar you can drag but not wheel would be a strange control.
                    const bool field = n->edit_ && n->edit_->multiline();
                    if ((!IsScroller(n->style_) && !field) || !n->scroll_) continue;
                    const glm::vec2 nd = axisLockedDelta_(*n, raw);
                    if (nd.x == 0.0f && nd.y == 0.0f) continue;
                    if (!firstScrollable) { firstScrollable = n; d = nd; }
                    const UIScrollState& sc = *n->scroll_;
                    if ((nd.x != 0.0f && canMove(sc, 0, nd.x)) ||
                        (nd.y != 0.0f && canMove(sc, 1, nd.y))) {
                        target = n; d = nd; break;
                    }
                }
                // Nothing up the chain has room: keep it with the innermost
                // scroller anyway, so the gesture latches there and a clamp is a
                // harmless no-op rather than a hand-off to the document behind.
                if (!target) target = firstScrollable;
            }

            if (target && (d.x != 0.0f || d.y != 0.0f)) {
                wheelLatch_ = target;
                if (target->SetScrollOffset(target->scroll_->offset - d * sx_(kPixelsPerNotch))) {
                    scrollDirty_ = true;
                }
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
            const float originX = target->layout_.position.x + sx_(target->style_.padding.left)
                                  - std::round(ed.textScroll().x);
            const float localX = p.position.x - originX;
            std::size_t best = 0;
            float bestDist = std::abs(localX);
            for (std::size_t i = UITextEdit::NextBoundary(shown, 0); ;
                 i = UITextEdit::NextBoundary(shown, i)) {
                const float w = measure_(font, shown.substr(0, i), target->style_.fontScale).x;
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
    // The tree may have been rebuilt since SetFocus asked for this: a hot reload
    // frees every element, and the request deliberately survives from one frame
    // into the next Layout. Validate before walking parent_ upward, exactly as
    // UpdatePointer validates its cached hover/press/drag pointers — clicking a
    // button and then saving the markup is an everyday sequence, and without
    // this it reads freed memory.
    if (focused_ && !isInTree_(focused_)) focused_ = nullptr;
    if (!focused_) return;
    // Innermost outward, re-solving between moves: scrolling the inner scroller
    // changes where the focused element sits inside the outer one, so an outer
    // reveal computed from pre-move rects would be wrong. Bounded by nesting
    // depth, and every iteration after the first is a no-op in the common case
    // of a single scroller.
    for (UIElement* n = focused_->parent_; n; n = n->parent_) {
        if (!ClipsBox(n->style_)) continue;
        if (n->ScrollIntoView(focused_->layout_.position, focused_->layout_.size)) {
            measureScroll_(*root_);
            readLayout_(*root_, origin_);
        }
    }
}

// Depth-first, so a nested scope is always found after the one containing it —
// which is what makes the collected order a valid STACK with no sorting.
static void collectVisibleScopes_(UIElement& el, std::vector<UIElement*>& out) {
    if (el.style().display == DisplayMode::None || !el.isEnabled()) return;
    if (el.isFocusScope()) out.push_back(&el);
    for (const auto& c : el.children()) collectVisibleScopes_(*c, out);
}

UIElement& UIDocument::focusRoot() {
    // Innermost open scope, or the whole document. Revalidated because a hot
    // reload frees the tree and a handler may have removed a panel.
    while (!scopeStack_.empty()) {
        UIElement* top = scopeStack_.back();
        if (top && isInTree_(top) && top->style().display != DisplayMode::None) return *top;
        scopeStack_.pop_back();
    }
    return *root_;
}

// What was last focused inside `scope`, or null. Revalidated at the point of
// use like every other cached element pointer in this file.
UIElement* UIDocument::scopeMemoryFor_(UIElement* scope) {
    for (auto& kv : scopeMemory_) {
        if (kv.first != scope) continue;
        UIElement* el = kv.second;
        if (el && isInTree_(el) && isInteractable_(*el)) return el;
        kv.second = nullptr;
        return nullptr;
    }
    return nullptr;
}

void UIDocument::rememberScopeFocus_(UIElement* scope) {
    if (!scope) return;
    // Only remember something that is actually INSIDE this scope: focus may
    // already have moved elsewhere by the time a scope closes.
    UIElement* f = focused_;
    bool inside = false;
    for (UIElement* p = f; p; p = p->parent_) {
        if (p == scope) { inside = true; break; }
    }
    if (!inside) f = nullptr;
    for (auto& kv : scopeMemory_) {
        if (kv.first == scope) { kv.second = f; return; }
    }
    scopeMemory_.emplace_back(scope, f);
}

// Focus the remembered element for the current top scope, or its first
// focusable. A scope with nothing focused cannot be driven by a pad at all.
void UIDocument::focusIntoScope_(UIElement* scope) {
    if (UIElement* remembered = scopeMemoryFor_(scope)) {
        SetFocus(remembered);
        return;
    }
    std::vector<UIElement*> inside;
    collectFocusables_(scope ? *scope : *root_, inside);
    SetFocus(inside.empty() ? nullptr : inside.front());
}

void UIDocument::SyncFocusScopes() {
    std::vector<UIElement*> open;
    collectVisibleScopes_(*root_, open);

    bool changed = false;

    // POP anything no longer open, innermost first, REMEMBERING where focus was
    // inside it. That memory is the whole feature: opening SETTINGS hides the
    // verb column and therefore pops it, and closing SETTINGS has to come back
    // to the SETTINGS verb rather than to the top of the list.
    while (!scopeStack_.empty()) {
        UIElement* top = scopeStack_.back();
        const bool stillOpen = top && isInTree_(top) &&
                               std::find(open.begin(), open.end(), top) != open.end();
        if (stillOpen) break;
        rememberScopeFocus_(top);
        scopeStack_.pop_back();
        changed = true;
    }

    // PUSH anything newly open, outermost first, so the stack ends up ordered.
    for (UIElement* s : open) {
        if (std::find(scopeStack_.begin(), scopeStack_.end(), s) != scopeStack_.end()) continue;
        // The scope being LEFT keeps its memory too, so a sibling panel opening
        // does not erase where you were in the one you came from.
        if (!scopeStack_.empty()) rememberScopeFocus_(scopeStack_.back());
        scopeStack_.push_back(s);
        changed = true;
    }

    // One focus decision, after the stack has settled -- rather than one per
    // push and pop, which is how the intermediate states used to clobber each
    // other when a panel opening also closed the column behind it.
    if (!changed) return;
    UIElement* top = scopeStack_.empty() ? nullptr : scopeStack_.back();
    // Focus already inside the new top scope is left exactly where it is: a
    // scope closing somewhere ELSE should not move it.
    //
    // ...but only if it is still reachable. Closing an inner panel leaves focus
    // on an element that is inside the outer scope AND inside the hidden inner
    // one, so the containment test alone would strand focus on something the
    // user cannot see or tab out of. isInteractable_ walks the whole ancestor
    // chain for exactly this.
    if (focused_ && isInTree_(focused_) && isInteractable_(*focused_)) {
        for (UIElement* p = focused_; p; p = p->parent_) {
            if (p == top) return;
        }
    }
    focusIntoScope_(top);
}

UIElement* UIDocument::FocusNext(bool backwards) {
    std::vector<UIElement*> order;
    // THE SCOPE, not the document. This one substitution is what turns "opening
    // a panel adds more places to go" into "opening a panel takes navigation".
    collectFocusables_(focusRoot(), order);
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
    const float lineH = measure_(font, "", s.fontScale).y;

    // The caret's position in the field's own TEXT space — the same arithmetic
    // draw_ uses, minus the offset we are about to compute.
    const std::size_t at = std::min(ed->caret(), shown.size());
    const std::size_t ls = UITextEdit::LineStart(shown, at);
    const glm::vec2 caretLocal{
        measure_(font, shown.substr(ls, at - ls), s.fontScale).x,
        float(UITextEdit::LineIndexOf(shown, at)) * lineH
    };

    const glm::vec2 contentBox{
        std::max(0.0f, el.layout_.size.x - sx_(s.padding.left) - sx_(s.padding.right)),
        std::max(0.0f, el.layout_.size.y - sx_(s.padding.top) - sx_(s.padding.bottom))
    };
    // Font::Measure, NOT the laid-out width: the yoga measure callback clamps
    // its result to the offer, so the element's own size lies about how wide the
    // text really is — which is exactly the case that needs scrolling.
    glm::vec2 textExtent = measure_(font, shown, s.fontScale);
    textExtent.y = float(UITextEdit::LineIndexOf(shown, shown.size()) + 1) * lineH;

    if (ed->FollowCaret(caretLocal, { 1.0f, lineH }, contentBox, textExtent)) {
        scrollDirty_ = true;
    }
}

UIElement* UIDocument::FocusMove(UINavDir dir) {
    if (dir == UINavDir::None) return focused_;
    // Nothing focused yet: the first press lands somewhere rather than nowhere.
    // FocusNext already starts from the beginning when focused_ is null.
    if (!focused_ || !isInTree_(focused_)) return FocusNext(false);

    std::vector<UIElement*> order;
    collectFocusables_(focusRoot(), order);
    if (order.size() < 2) return focused_;

    const ComputedLayout& from = focused_->layout();
    // Nothing has been laid out yet -- a document updated before its first
    // Layout, or a scope whose panel opened this very frame. Geometry cannot
    // answer, so fall back to the walk rather than trapping focus in place.
    if (!(from.size.x > 0.0f) || !(from.size.y > 0.0f)) {
        return FocusNext(dir == UINavDir::Up || dir == UINavDir::Left);
    }

    const bool horizontal = (dir == UINavDir::Left || dir == UINavDir::Right);

    // ROW-MAJOR, in two stages: find the nearest ROW in this direction, then
    // pick the best candidate WITHIN it.
    //
    // A single weighted score cannot express this, and the menu proved it. The
    // VSYNC toggle sits at the far RIGHT of its row (that row is
    // space-between), while the text field two rows further down is full width
    // and therefore overlaps the quality chips horizontally. Scored together,
    // the field won on a zero off-axis distance and Down SKIPPED VSYNC
    // entirely. No choice of penalty fixes that -- it is a false comparison
    // between a near row and a far one.
    //
    // Two stages says the obvious thing instead: vertical movement is between
    // ROWS and horizontal movement is within one.
    //
    // EDGES, not centres, decide what counts as the next row. A candidate is
    // "below" only if it starts at or after where the current element ENDS, so
    // same-row siblings are not below one another by construction -- which is
    // what makes a row of chips cost one press to pass rather than three.
    const float kEdgeSlack = 1.0f;

    const float fromLo = horizontal ? from.position.x : from.position.y;
    const float fromHi = fromLo + (horizontal ? from.size.x : from.size.y);
    const float crossLo = horizontal ? from.position.y : from.position.x;
    const float crossHi = crossLo + (horizontal ? from.size.y : from.size.x);
    const bool forward = (dir == UINavDir::Right || dir == UINavDir::Down);

    // How far apart two candidates may START and still count as one row.
    // Derived from the current element's own size so it scales with the UI and
    // tolerates a row whose items are centre-aligned at different heights.
    const float band = std::max(8.0f, 0.5f * (horizontal ? from.size.x : from.size.y));

    struct Cand { UIElement* el; float along; float off; };
    std::vector<Cand> cands;
    bool anyGeometry = false;

    for (UIElement* c : order) {
        if (c == focused_) continue;
        const ComputedLayout& to = c->layout();
        if (!(to.size.x > 0.0f) || !(to.size.y > 0.0f)) continue;
        anyGeometry = true;

        const float toLo = horizontal ? to.position.x : to.position.y;
        const float toHi = toLo + (horizontal ? to.size.x : to.size.y);

        float along;
        if (forward) {
            if (toLo < fromHi - kEdgeSlack) continue;
            along = toLo - fromHi;
        } else {
            if (toHi > fromLo + kEdgeSlack) continue;
            along = fromLo - toHi;
        }
        if (along < 0.0f) along = 0.0f;

        // Off-axis is the GAP BETWEEN RECTS, zero when they overlap.
        const float cLo = horizontal ? to.position.y : to.position.x;
        const float cHi = cLo + (horizontal ? to.size.y : to.size.x);
        float off = 0.0f;
        if (cLo > crossHi)      off = cLo - crossHi;
        else if (crossLo > cHi) off = crossLo - cHi;

        cands.push_back(Cand{ c, along, off });
    }

    UIElement* best = nullptr;
    if (!cands.empty()) {
        // STAGE ONE: the nearest row wins outright, however far to the side its
        // members are. This is the rule that stops a full-width control further
        // down from stealing a press from a right-aligned one just above it.
        float nearest = cands.front().along;
        for (const Cand& c : cands) nearest = std::min(nearest, c.along);

        // STAGE TWO: within that row, the closest across. Along-axis breaks a
        // tie, so two controls stacked inside one band still resolve.
        float bestOff = 0.0f, bestAlong = 0.0f;
        for (const Cand& c : cands) {
            if (c.along > nearest + band) continue;
            if (!best || c.off < bestOff ||
                (c.off == bestOff && c.along < bestAlong)) {
                best = c.el; bestOff = c.off; bestAlong = c.along;
            }
        }
    }

    // NOTHING laid out anywhere: same fallback as above, for the same reason.
    if (!anyGeometry) {
        return FocusNext(dir == UINavDir::Up || dir == UINavDir::Left);
    }
    // Nothing in that direction. Deliberately NO WRAP: pressing Down at the
    // bottom of a list should do nothing, which is what every console UI does
    // and what stops a menu feeling like a carousel. Tab still wraps, because
    // Tab is a different affordance with no direction to run out of.
    if (!best) return focused_;

    SetFocus(best);
    return focused_;
}

bool UIDocument::UpdateNav(const UINavState& nav) {
    // Before anything reads focus: a panel opened by last frame's click has to
    // own navigation by the time this frame's d-pad press is dispatched.
    SyncFocusScopes();

    bool consumed = false;

    // ---- a focused slider takes the stick, CONTINUOUSLY ----
    //
    // Discrete moves are how you traverse a menu; a slider is not a menu. A
    // stick pushed on one moves it at a rate, which is what makes a pad feel
    // analog instead of ratcheting through notches.
    //
    // Consuming the axis here also SUPPRESSES the discrete moves along the same
    // axis below, because the repeater is producing both from one stick and the
    // value would otherwise move twice.
    bool analogAte = false;
    if (focused_ && isInTree_(focused_) && focused_->slider() && nav.dt > 0.0f) {
        UISliderState& sl = *focused_->slider();
        const float a = sl.vertical ? nav.axisY : nav.axisX;
        // Above the same threshold the discrete path uses, so there is no band
        // where a stick does neither.
        if (a > 0.5f || a < -0.5f) {
            const float rate = sl.analogSeconds > 0.0f
                                   ? sl.span() / sl.analogSeconds : 0.0f;
            if (sl.SetValue(sl.value + a * rate * nav.dt)) scrollDirty_ = true;
            analogAte = true;
            consumed = true;
        }
    }

    for (const UINavDir d : nav.moves) {
        // A focused SLIDER eats the along-axis moves. With a D-PAD (or the
        // arrow keys) that is a notch; with a STICK the analog path above has
        // already moved it and this only has to stay out of the way.
        if (focused_ && isInTree_(focused_) && focused_->slider()) {
            UISliderState& sl = *focused_->slider();
            const UINavDir lo = sl.vertical ? UINavDir::Down : UINavDir::Left;
            const UINavDir hi = sl.vertical ? UINavDir::Up   : UINavDir::Right;
            if (d == lo || d == hi) {
                if (!analogAte) {
                    sl.Nudge(d == hi ? +1.0f : -1.0f);
                    scrollDirty_ = true;   // reach the source and relayout now
                    consumed = true;
                }
                continue;
            }
        }
        if (FocusMove(d)) consumed = true;
    }

    if (nav.activate && ActivateFocused()) consumed = true;

    if (nav.back && Back()) consumed = true;

    return consumed;
}

bool UIDocument::Back() {
    // A TEXT FIELD YOU ARE TYPING IN is the innermost thing you are in, ahead
    // of any panel around it. Back means "back out of what I am in", so it has
    // to leave the field first and close the panel only on a second press.
    //
    // Without this the shipped menu had a trap: PILOT sits inside
    // settingsPanel, which is a scope WITH an `on-back`, so the scope branch
    // below matched and one Escape mid-name threw away the whole page. It is
    // here rather than in the Escape branch on purpose -- the pad's B is the
    // same gesture and would have had the same trap.
    if (focused_ && isInTree_(focused_) && focused_->textEdit()) {
        SetFocus(nullptr);
        return true;
    }

    // Ask the innermost open SCOPE to close itself. The document cannot do it:
    // a panel is visible because an `if=` reads app state, so the app has to
    // flip that state and the stack then follows visibility like everything
    // else. One direction of causality, no second notion of "open" to fall out
    // of step.
    if (!scopeStack_.empty() && isInTree_(scopeStack_.back())) {
        UIEvent e;
        e.type = UIEventType::Back;
        // Handled only if something actually RAN. A scope root with no
        // `on-back` -- the menu's own verb column is one -- declares a
        // navigation region, not a back action, so back there belongs to the
        // HOST: close the menu, quit, whatever the game means by it.
        //
        // Claiming it unconditionally is what made the escape hatch below
        // unreachable in the one document that needed it: the root scope is
        // always on the stack, so control never got past this branch.
        // No blur here either -- inside a declared scope, an unwanted back
        // should leave you exactly where you were rather than stranding the
        // page with nothing focused and no pointer to recover it.
        if (bubble_(scopeStack_.back(), e)) return true;
        backUnhandled_ = true;
        return false;
    }
    if (focused_) {
        // No scope open: back out of the control instead. That is the one
        // meaning true in every UI.
        SetFocus(nullptr);
        return true;
    }
    backUnhandled_ = true;
    return false;
}

bool UIDocument::ActivateFocused() {
    // Revalidated exactly like the pointer path does: a handler may have
    // removed the focused element since it was focused.
    if (!focused_ || !isInTree_(focused_) || !isInteractable_(*focused_)) return false;
    // A text edit owns its own Enter — see the header.
    if (focused_->textEdit()) return false;

    // Hold the press briefly so `:active` can be seen. The frame order does the
    // rest for free: this runs inside UpdateNav, and RestyleInteractive later in
    // the same frame sees the rising edge; AdvanceTime clears it next frame and
    // the falling edge is seen then.
    if (activated_ && activated_ != focused_ && isInTree_(activated_)) {
        activated_->pressed_ = false;
    }
    activated_ = focused_;
    activated_->pressed_ = true;
    activateUntil_ = docClock_ + kActivateFlashSeconds;

    UIEvent e;
    e.type = UIEventType::Click;
    // The element's CENTRE. A Click carries a position and handlers may read
    // it; a synthesized one should be somewhere honest rather than {0,0}.
    e.position = focused_->layout().position + focused_->layout().size * 0.5f;
    bubble_(focused_, e);
    return true;
}

void UIDocument::UpdateKeyboard(const UIKeyboardState& kb, const Font* font) {
    UIPassScope pass(font, scale_);
    // BEFORE anything reads the scope stack, for exactly the reason UpdateNav
    // says the same thing: a panel opened by last frame's click has to own
    // navigation by the time this frame's key is dispatched.
    //
    // It has to be here TOO, not only in UpdateNav, because UIWorld runs the
    // keyboard pass FIRST. Without it Escape and the pad's B were not the same
    // gesture after all: B was preceded by a sync and Escape was not, so for
    // one frame after opening SETTINGS, Escape bubbled Back into the verb
    // column that had just been hidden -- which has no `on-back`, so the press
    // was reported unhandled and the panel stayed open. Idempotent when the
    // stack has not moved, so the second call costs a comparison.
    SyncFocusScopes();
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

        // Scrolling is the next default action, after the focused element's own
        // editing and before Tab navigation.
        //
        // The precedence with a focused <TextField> is a deliberate asymmetry,
        // and it matches a browser's <textarea>:
        //   Home/End  never get here — UITextEdit consumes them unconditionally,
        //             and inside a multi-line field they are already LINE-aware.
        //             So "Home scrolls to the top" works only when focus is on
        //             the scroller itself.
        //   PageUp/Dn do get here — UITextEdit deliberately does not take them —
        //             so a focused field pages its container while its own caret
        //             stays put.
        // Walked from `target` (the revalidated focused element), never from
        // hovered_: UIWorld routes the keyboard and the pointer to different
        // documents on purpose, so the hovered scroller may not be receiving
        // keys at all.
        if (!consumed && (k.key == UIKey::PageUp || k.key == UIKey::PageDown ||
                          k.key == UIKey::Home || k.key == UIKey::End)) {
            consumed = keyboardScroll_(target, k.key);
        }

        // A FOCUSED SLIDER takes the arrows along its own axis, ahead of the
        // scroll defaults below -- otherwise Left/Right on a slider inside a
        // scroller would page the container instead of moving the value, which
        // is the one thing the user is looking at.
        //
        // The cross-axis arrows are deliberately left alone, so Up/Down still
        // reach navigation on a horizontal slider.
        if (!consumed && target && target->slider()) {
            UISliderState& sl = *target->slider();
            const UIKey lo = sl.vertical ? UIKey::Down : UIKey::Left;
            const UIKey hi = sl.vertical ? UIKey::Up   : UIKey::Right;
            if (k.key == lo || k.key == hi) {
                sl.Nudge(k.key == hi ? +1.0f : -1.0f);
                // The value has to reach the source and the bound fill has to
                // relayout THIS frame, exactly as a drag does.
                scrollDirty_ = true;
                consumed = true;
            } else if (k.key == UIKey::Home || k.key == UIKey::End) {
                sl.SetValue(k.key == UIKey::Home ? sl.min : sl.max);
                scrollDirty_ = true;
                consumed = true;
            }
        }

        // ENTER ACTIVATES. Until this existed the key simply fell off the end
        // of this chain: a focused Button received KeyDown, nothing consumed
        // it, and the UI was unusable without a mouse. A consumption check for
        // the same reason as Tab below -- a handler that took Enter, or a
        // multi-line field inserting a newline, has already had its say.
        //
        // Space is deliberately NOT here -- see UIKey for why.
        if (!consumed && k.key == UIKey::Enter) consumed = ActivateFocused();

        // ESCAPE IS BACK, and it is the SAME code the pad's B runs -- one
        // notion of "back out of what I am in", not a keyboard one and a
        // controller one that drift. Placed after the field's own editing, so
        // Escape in a text field blurs it (UITextEdit deliberately declines
        // the key) rather than closing the panel around it.
        if (!consumed && k.key == UIKey::Escape) consumed = Back();

        // AN UNCLAIMED ARROW IS NAVIGATION. Last in the chain, which is the
        // entire design: a text caret, a tab strip, a focused slider's notch
        // and page scrolling have each already declined it, so moving focus
        // cannot steal a key one of them wanted.
        //
        // This is why the arrows are NOT bound as InputMap nav actions -- they
        // would arrive here AND through UpdateNav in the same frame, and a
        // slider would move two notches per press. WASD has no such chain
        // available (UIKey has no letters, because a letter is text) and so
        // goes the other way. See BindDefaultActions.
        if (!consumed && (k.key == UIKey::Up || k.key == UIKey::Down ||
                          k.key == UIKey::Left || k.key == UIKey::Right)) {
            const UINavDir d = k.key == UIKey::Up   ? UINavDir::Up
                             : k.key == UIKey::Down ? UINavDir::Down
                             : k.key == UIKey::Left ? UINavDir::Left
                                                    : UINavDir::Right;
            consumed = (FocusMove(d) != nullptr);
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
