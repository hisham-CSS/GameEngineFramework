#include "UIBinding.h"

#include "UIElement.h"

#include <algorithm>
#include <cctype>

namespace MyCoreEngine::ui {

namespace {

    std::string trim(const std::string& s) {
        size_t b = 0, e = s.size();
        while (b < e && std::isspace((unsigned char)s[b])) ++b;
        while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
        return s.substr(b, e - b);
    }

    std::string join(const std::vector<std::string>& v, const char* sep = ", ") {
        std::string out;
        for (size_t i = 0; i < v.size(); ++i) {
            if (i) out += sep;
            out += v[i];
        }
        return out.empty() ? std::string("none") : out;
    }

    // `path | conv | conv : N`, already stripped of its braces.
    bool parseHole(const std::string& body, UIHole& out, std::string& err) {
        std::string work = body;

        // Format spec last, and searched from the RIGHT so a converter name
        // could in principle contain a colon without ambiguity.
        const size_t colon = work.rfind(':');
        if (colon != std::string::npos) {
            const std::string spec = trim(work.substr(colon + 1));
            if (spec.empty() || !std::all_of(spec.begin(), spec.end(),
                                             [](unsigned char c) { return std::isdigit(c); })) {
                err = "bad format spec ':" + spec + "' (use :N for N decimals)";
                return false;
            }
            out.decimals = std::atoi(spec.c_str());
            work = work.substr(0, colon);
        }

        // Converter chain, left to right.
        size_t bar = work.find('|');
        std::string pathPart = trim(work.substr(0, bar));
        while (bar != std::string::npos) {
            const size_t next = work.find('|', bar + 1);
            const std::string conv = trim(work.substr(bar + 1, next == std::string::npos
                                                                  ? std::string::npos
                                                                  : next - bar - 1));
            if (conv.empty()) { err = "empty converter in '" + body + "'"; return false; }
            out.converters.push_back(conv);
            bar = next;
        }

        if (pathPart.empty()) {
            err = "empty {} - a hole needs a path";
            return false;
        }
        // The dot ALWAYS separates source from property. One rule, no
        // ambiguity, and a property name may not contain one.
        const size_t dot = pathPart.find('.');
        if (dot != std::string::npos) {
            if (pathPart.find('.', dot + 1) != std::string::npos) {
                err = "path '" + pathPart + "' has more than one '.' (write source.property)";
                return false;
            }
            out.sourceName = trim(pathPart.substr(0, dot));
            out.propName = trim(pathPart.substr(dot + 1));
            if (out.sourceName.empty() || out.propName.empty()) {
                err = "path '" + pathPart + "' is missing a source or a property name";
                return false;
            }
        } else {
            out.propName = pathPart;
        }
        return true;
    }

} // namespace

// ------------------------------------------------------------ UITextTemplate

bool UITextTemplate::Compile(const std::string& text, UITextTemplate& out, std::string& err) {
    out.literals_.clear();
    out.holes_.clear();

    std::string lit;
    for (size_t i = 0; i < text.size();) {
        const char c = text[i];
        if (c == '{') {
            if (i + 1 < text.size() && text[i + 1] == '{') { lit += '{'; i += 2; continue; }
            const size_t close = text.find('}', i + 1);
            if (close == std::string::npos) {
                err = "unterminated '{' in '" + text + "'";
                return false;
            }
            UIHole h;
            if (!parseHole(trim(text.substr(i + 1, close - i - 1)), h, err)) return false;
            out.literals_.push_back(lit);
            lit.clear();
            out.holes_.push_back(std::move(h));
            i = close + 1;
            continue;
        }
        if (c == '}') {
            if (i + 1 < text.size() && text[i + 1] == '}') { lit += '}'; i += 2; continue; }
            // A lone '}' is almost always a typo'd hole, and letting it through
            // would make the {{ }} escape rule silently optional in one
            // direction only.
            err = "unexpected '}' (write '}}' for a literal brace)";
            return false;
        }
        lit += c;
        ++i;
    }
    // Invariant: literals == holes + 1, so rendering is an alternating walk
    // with no special case at either end.
    out.literals_.push_back(lit);
    return true;
}

// ------------------------------------------------------------------ UIBinder

void UIBinder::Clear() {
    entries_.clear();
    actions_.clear();
    convPool_.clear();
    doc_ = nullptr;
    ctx_ = nullptr;
    seenEpoch_ = 0;
    seenCtxRev_ = 0;
    // errors_/notes_ survive: they describe the last Rebuild, and the caller
    // reads them after the call that produced them.
}

std::string UIBinder::where_(const UIElement& el, const UIBinding& b) const {
    std::string s = origin_ + ": <" + el.type();
    if (!el.name().empty()) s += " name='" + el.name() + "'";
    s += "> " + b.label;
    return s;
}

void UIBinder::collect_(UIElement& el, const std::string& inheritedSource) {
    // An element's own data-source= wins for it and everything beneath it,
    // exactly like Unity's VisualElement.dataSource.
    const std::string& scope =
        el.dataSourceName().empty() ? inheritedSource : el.dataSourceName();

    for (const UIBinding& b : el.bindings()) {
        Entry e;
        e.el = &el;
        e.binding = &b;
        // Only a write that can change a BOX forces a re-layout. Colour is the
        // common case that cannot, and exempting it is what keeps the
        // post-input pass free on the frames that do not need it.
        const bool colourOnly =
            b.target.kind == UIBindTarget::Kind::Style &&
            (b.target.styleProp == UIDeclaration::Prop::BackgroundColor ||
             b.target.styleProp == UIDeclaration::Prop::Color);
        e.layoutAffecting = !colourOnly;
        resolve_(e, scope);
        resolvePush_(e, scope);
        entries_.push_back(e);
    }

    for (const UIBoundAction& a : el.boundActions()) {
        const std::string& srcName = a.sourceName.empty() ? scope : a.sourceName;
        UIDataSource* src = srcName.empty() ? nullptr : ctx_->Find(srcName);
        std::string loc = origin_ + ": <" + el.type();
        if (!el.name().empty()) loc += " name='" + el.name() + "'";
        loc += "> " + a.label;

        if (!src) {
            errors_.push_back(loc + ": unknown data source '" + srcName +
                              "' (registered: " + join(ctx_->sourceNames()) + ")");
            continue;
        }
        const int idx = src->ActionIndexOf(a.actionName);
        if (idx < 0) {
            errors_.push_back(loc + ": unknown action '" + a.actionName +
                              "' (known: " + join(src->actionNames()) + ")");
            continue;
        }
        ActionEntry ae{ &el, src, idx };
        actions_.push_back(ae);
        // Attach through the ordinary listener path, so a bound action bubbles
        // and composes with hand-written handlers exactly like any other.
        UIDataSource* s = src;
        el.AddEventListener(a.type, [s, idx](UIEvent&) { s->InvokeAction(idx); });
    }

    if (sheet_) noteShadowedDeclarations_(el, *sheet_);

    for (const auto& c : el.children()) collect_(*c, scope);
}

void UIBinder::resolvePush_(Entry& e, const std::string& inheritedSource) {
    const UIBinding& b = *e.binding;
    e.pushSrc = nullptr;
    e.pushIndex = -1;
    if (b.pushProp.empty()) return;

    const std::string srcName = b.pushSource.empty() ? inheritedSource : b.pushSource;
    if (srcName.empty()) {
        errors_.push_back(where_(*e.el, b) +
                          ": no data source in scope - add data-source= to an "
                          "ancestor, or write \"source.property\"");
        return;
    }
    UIDataSource* src = ctx_->Find(srcName);
    if (!src) {
        errors_.push_back(where_(*e.el, b) + ": unknown data source '" + srcName +
                          "' (registered: " + join(ctx_->sourceNames()) + ")");
        return;
    }
    // A write-back target that does not exist yet is CREATED, unlike a read
    // path: the whole point is that the element owns this value, so the app
    // does not have to declare it before the UI can publish it.
    if (src->IndexOf(b.pushProp) < 0) src->SetValue(b.pushProp, UIValue{});
    const int idx = src->IndexOf(b.pushProp);
    if (!src->IsWritableAt(idx)) {
        // Reported rather than dropped: an observed property with no setter is
        // read-only, and a push that silently did nothing would look exactly
        // like a UI that is not wired up.
        errors_.push_back(where_(*e.el, b) + ": '" + b.pushProp +
                          "' is read-only, so it cannot be written");
        return;
    }
    e.pushSrc = src;
    e.pushIndex = idx;
}

bool UIBinder::resolve_(Entry& e, const std::string& inheritedSource) {
    const UIBinding& b = *e.binding;
    const auto& holes = b.tmpl.holes();
    e.src = nullptr;
    e.propIndex = -1;
    e.convFirst = std::uint16_t(convPool_.size());
    e.convCount = 0;
    e.lastSourceVersion = 0;

    // A constant template has nothing to bind; it is authored text that simply
    // never changes, and it is applied once by the force-apply at the end of
    // Rebuild.
    if (holes.empty()) return true;

    bool allResolved = true;
    for (const UIHole& h : holes) {
        const std::string srcName = h.sourceName.empty() ? inheritedSource : h.sourceName;
        if (srcName.empty()) {
            errors_.push_back(where_(*e.el, b) +
                              ": no data source in scope - add data-source= to an "
                              "ancestor, or write path=\"source.property\"");
            allResolved = false;
            break;
        }
        UIDataSource* src = ctx_->Find(srcName);
        if (!src) {
            errors_.push_back(where_(*e.el, b) + ": unknown data source '" + srcName +
                              "' (registered: " + join(ctx_->sourceNames()) + ")");
            allResolved = false;
            break;
        }
        const int idx = src->IndexOf(h.propName);
        if (idx < 0) {
            errors_.push_back(where_(*e.el, b) + ": data source '" + srcName +
                              "' has no property '" + h.propName +
                              "' (has: " + join(src->propertyNames()) + ")");
            allResolved = false;
            break;
        }
        // The first hole owns the entry's change detection. A multi-hole
        // template re-renders when ANY of its sources moves, which the
        // per-source version compare below covers because they share a source
        // in every case a HUD actually writes.
        if (!e.src) { e.src = src; e.propIndex = idx; }

        for (const std::string& cname : h.converters) {
            const UIConvertFn* fn = ctx_->converters().Find(cname);
            if (!fn) fn = BuiltinUIConverters().Find(cname);
            if (!fn) {
                std::vector<std::string> avail = BuiltinUIConverters().names();
                for (const auto& n : ctx_->converters().names()) avail.push_back(n);
                errors_.push_back(where_(*e.el, b) + ": unknown converter '" + cname +
                                  "' (available: " + join(avail) + ")");
                allResolved = false;
                break;
            }
            convPool_.push_back(fn);
            ++e.convCount;
        }
        if (!allResolved) break;
    }

    if (!allResolved) { e.src = nullptr; e.propIndex = -1; }
    return allResolved;
}

void UIBinder::noteShadowedDeclarations_(const UIElement& el, const UIStyleSheet& sheet) {
    if (el.bindings().empty()) return;
    std::vector<UIDeclaration::Prop> declared;
    sheet.DeclaredPropsFor(el, declared);
    if (declared.empty()) return;

    for (const UIBinding& b : el.bindings()) {
        if (b.target.kind != UIBindTarget::Kind::Style) continue;
        for (UIDeclaration::Prop p : declared) {
            if (p != b.target.styleProp) continue;
            // Binding a property makes its stylesheet rule dead. That is a
            // legitimate pattern — the rule is the pre-bind default — but
            // discovering it by editing the .uss and watching nothing happen is
            // exactly the silent no-op this codebase reports errors to avoid.
            std::string s = "the stylesheet declares '" + std::string(UIDeclaration::NameOf(p)) +
                            "' for <" + el.type();
            if (!el.name().empty()) s += " name='" + el.name() + "'";
            s += ">, which " + origin_ +
                 " binds - the stylesheet value is only the pre-bind default";
            notes_.push_back(std::move(s));
            break;
        }
    }
}

void UIBinder::Rebuild(UIDocument& doc, UIBindingContext& ctx, std::string originName,
                       const UIStyleSheet* sheet) {
    entries_.clear();
    actions_.clear();
    convPool_.clear();
    errors_.clear();
    notes_.clear();
    doc_ = &doc;
    ctx_ = &ctx;
    sheet_ = sheet;
    origin_ = std::move(originName);

    collect_(doc.root(), std::string());

    // Note a shadowed builtin: a game may legitimately replace `percent`, but
    // doing it by accident is a genuinely confusing afternoon.
    for (const auto& n : ctx.converters().names()) {
        if (BuiltinUIConverters().Find(n)) {
            notes_.push_back("converter '" + n + "' shadows a builtin");
        }
    }

    // Force-apply everything, unconditionally. This is what replaces the app's
    // old "re-push everything you cached" step after a reload: the tree carries
    // current values BEFORE the first Layout, so a label never shows its
    // authored placeholder for a frame as you save the file.
    for (Entry& e : entries_) {
        apply_(e);
        // Record the version the force-apply just consumed, or the very next
        // UpdateToTarget would re-apply every binding for nothing — turning the
        // first frame after a reload into a full re-layout. A `bind-value`
        // tracks its push source, which is the one it reads from.
        if (e.src) e.lastSourceVersion = e.src->version();
        else if (e.pushSrc) e.lastSourceVersion = e.pushSrc->version();
    }

    // Record these AFTER collecting, because collect_ attaches listeners and
    // therefore does not move the epoch, but resolving may have.
    seenEpoch_ = UIElement::structureEpoch();
    seenCtxRev_ = ctx.revision();
}

void UIBinder::reportOnce_(Entry& e, const UIValue& v, const std::string& what) {
    // Latched per entry and re-armed only when the value's KIND changes: a
    // different kind is a genuinely different problem, while a second bad
    // string is the same one. Latching on the VALUE instead would flood the
    // console for a drifting NaN.
    const std::uint8_t k = std::uint8_t(v.kind);
    if (e.reported && e.reportedKind == k) return;
    e.reported = true;
    e.reportedKind = k;
    errors_.push_back(where_(*e.el, *e.binding) + ": " + what);
}

bool UIBinder::render_(Entry& e, const UIHole* holes, std::size_t holeCount) {
    const UIBinding& b = *e.binding;
    const auto& lits = b.tmpl.literals();
    scratch_.clear();

    std::size_t convCursor = e.convFirst;
    for (std::size_t i = 0; i < holeCount; ++i) {
        scratch_ += lits[i];

        const UIHole& h = holes[i];
        const std::string srcName = h.sourceName.empty() ? std::string() : h.sourceName;
        UIDataSource* src = srcName.empty() ? e.src : ctx_->Find(srcName);
        if (!src) return false;

        UIValue v;
        if (!src->ReadAt(src->IndexOf(h.propName), v)) return false;

        for (std::size_t c = 0; c < h.converters.size(); ++c, ++convCursor) {
            if (convCursor >= convPool_.size()) return false;
            UIValue outv;
            std::string err;
            if (!(*convPool_[convCursor])(v, outv, err)) {
                reportOnce_(e, v, err);
                return false;
            }
            v = std::move(outv);
        }

        // BEFORE stringification, deliberately: %g turns NaN into "nan" and
        // strtod accepts "nan", so a value not gated here would parse back as a
        // perfectly valid number further down the line.
        std::string why;
        if (!v.IsFinite(why)) { reportOnce_(e, v, why); return false; }

        scratch_ += v.ToDisplayString(h.decimals);
    }
    scratch_ += lits[holeCount];
    return true;
}

bool UIBinder::evalSingle_(Entry& e, UIValue& out) {
    const UIHole& h = e.binding->tmpl.holes()[0];
    UIDataSource* src = h.sourceName.empty() ? e.src : ctx_->Find(h.sourceName);
    if (!src) return false;
    if (!src->ReadAt(src->IndexOf(h.propName), out)) return false;

    std::size_t cursor = e.convFirst;
    for (std::size_t c = 0; c < h.converters.size(); ++c, ++cursor) {
        if (cursor >= convPool_.size()) return false;
        UIValue next;
        std::string err;
        if (!(*convPool_[cursor])(out, next, err)) { reportOnce_(e, out, err); return false; }
        out = std::move(next);
    }
    std::string why;
    if (!out.IsFinite(why)) { reportOnce_(e, out, why); return false; }
    return true;
}

bool UIBinder::applyStyle_(Entry& e) {
    const UIBinding& b = *e.binding;
    const UIDeclaration::Prop prop = b.target.styleProp;
    UIDeclaration decl;
    decl.prop = prop;

    // FAST PATH: one hole and no surrounding literals, so the value goes
    // straight into the right field. A bound colour or length never round-trips
    // through a string, which is both faster and lossless.
    if (b.tmpl.isSingleHole()) {
        UIValue v;
        if (!evalSingle_(e, v)) return false;

        bool ok = false;
        switch (b.target.valueKind) {
        case UIPropValueKind::Length:  ok = v.AsLength(decl.length); break;
        case UIPropValueKind::Number:  ok = v.AsNumber(decl.number); break;
        case UIPropValueKind::Color:   ok = v.AsColor(decl.color); break;
        case UIPropValueKind::Boolean: ok = v.AsBool(decl.boolean); break;
        case UIPropValueKind::Enum:
        case UIPropValueKind::Edges:
            // No direct UIValue representation: these only arrive as text, so
            // they take the string path below.
            ok = false;
            break;
        }
        if (!ok) {
            // A string can always try the declaration grammar — that is how
            // "auto", "row" and "8px 14px" reach a bound property.
            if (v.kind == UIValue::Kind::String) {
                std::string err;
                if (!UIDeclaration::ParseValueFor(prop, v.text, decl, err)) {
                    reportOnce_(e, v, err);
                    return false;
                }
            } else {
                reportOnce_(e, v, std::string("cannot use ") + v.KindName() + " as a value for '" +
                                      UIDeclaration::NameOf(prop) + "'");
                return false;
            }
        }
    } else {
        // Literals around the hole, e.g. "{h}%" or "0px {gap}px": build the text
        // and hand it to the ordinary declaration parser, so an interpolated
        // value means exactly what the same text means in a .uss file.
        if (!render_(e, b.tmpl.holes().data(), b.tmpl.holes().size())) return false;
        std::string err;
        if (!UIDeclaration::ParseValueFor(prop, scratch_, decl, err)) {
            reportOnce_(e, UIValue::Str(scratch_), err);
            return false;
        }
    }

    // A negative width or height is not a layout, it is a bug upstream, and
    // parseLength happily accepts "-100%" today. Sizes only — margin and the
    // inset edges are legitimately negative.
    if (b.target.valueKind == UIPropValueKind::Length && decl.length.value < 0.0f) {
        reportOnce_(e, UIValue::Len(decl.length),
                    std::string("'") + UIDeclaration::NameOf(prop) + "' cannot be negative, got " +
                        UIValue::Len(decl.length).ToDisplayString());
        return false;
    }

    decl.ApplyTo(e.el->style());
    return true;
}

bool UIBinder::apply_(Entry& e) {
    const UIBinding& b = *e.binding;
    const auto& holes = b.tmpl.holes();

    // Unresolved: the app may register the source later, so this is pending,
    // not failed. UpdateToTarget re-resolves when the context revision moves.
    if (!holes.empty() && !e.src) return false;

    switch (b.target.kind) {
    case UIBindTarget::Kind::Text: {
        if (!render_(e, holes.data(), holes.size())) return false;
        // setText, NEVER style().text: the latter compiles and skips the
        // measurement invalidation, so the label would keep its old width.
        e.el->setText(scratch_);
        return true;
    }
    case UIBindTarget::Kind::Style:
        return applyStyle_(e);
    case UIBindTarget::Kind::Display: {
        UIValue v;
        if (!evalSingle_(e, v)) return false;
        bool show = false;
        if (!v.AsBool(show)) {
            reportOnce_(e, v, std::string("if: needs a bool, got ") + v.KindName());
            return false;
        }
        if (b.negate) show = !show;
        // A style write, not tree surgery: the element keeps its identity, its
        // handlers and its bindings, and showing it again costs one more write.
        e.el->style().display = show ? DisplayMode::Flex : DisplayMode::None;
        return true;
    }
    case UIBindTarget::Kind::Value: {
        // The source -> element half of a text field's two-way binding. Guarded
        // on inequality, because assigning the same string back would be a
        // needless caret clamp every time the source version moves for some
        // unrelated reason.
        UITextEdit* ed = e.el->textEdit();
        if (!ed || !e.pushSrc) return false;
        UIValue v;
        if (!e.pushSrc->ReadAt(e.pushIndex, v)) return false;
        std::string s = v.kind == UIValue::Kind::None ? std::string() : v.ToDisplayString();
        if (s == ed->value()) return false;
        ed->setValue(std::move(s));
        e.el->SyncTextFromEdit();
        return true;
    }
    case UIBindTarget::Kind::State:
        // Element -> source only; UpdateToSource handles it. Not an error and
        // not a no-op: there is simply nothing to write in this direction.
        return false;
    case UIBindTarget::Kind::Class:
        // Not built. The markup parser refuses to produce this kind, so it is
        // unreachable rather than silently ignored.
        return false;
    }
    return false;
}

UIBindTick UIBinder::UpdateToSource() {
    UIBindTick tick;
    if (!doc_ || !ctx_) return tick;

    // The SAME guard as UpdateToTarget, and it matters more here: this runs
    // immediately after UpdatePointer and UpdateKeyboard, which is the one
    // point in the frame where a handler may have removed the very element
    // being dispatched on.
    if (UIElement::structureEpoch() != seenEpoch_ || ctx_->revision() != seenCtxRev_) {
        UIDocument& doc = *doc_;
        UIBindingContext& ctx = *ctx_;
        const UIStyleSheet* sheet = sheet_;
        std::string origin = origin_;
        Rebuild(doc, ctx, std::move(origin), sheet);
        return tick;
    }

    for (Entry& e : entries_) {
        if (!e.pushSrc || e.pushIndex < 0) continue;
        const UIBindTarget::Kind kind = e.binding->target.kind;

        UIValue now;
        if (kind == UIBindTarget::Kind::State) {
            switch (e.binding->target.state) {
            case UIBindTarget::StateProp::Hovered: now = UIValue::Bool(e.el->isHovered()); break;
            case UIBindTarget::StateProp::Pressed: now = UIValue::Bool(e.el->isPressed()); break;
            case UIBindTarget::StateProp::Focused: now = UIValue::Bool(e.el->isFocused()); break;
            }
        } else if (kind == UIBindTarget::Kind::Value) {
            const UITextEdit* ed = e.el->textEdit();
            if (!ed) continue;
            now = UIValue::Str(ed->value());
        } else {
            continue;
        }

        // Compared against what the SOURCE holds rather than a cached copy:
        // no cache to go stale when something else writes the same property,
        // and an unchanged element costs one compare and no write.
        UIValue held;
        e.pushSrc->ReadAt(e.pushIndex, held);
        if (held == now) continue;
        e.pushSrc->WriteAt(e.pushIndex, now);
        ++tick.applied;
    }
    return tick;
}

UIBindTick UIBinder::UpdateToTarget() {
    UIBindTick tick;
    if (!doc_ || !ctx_) return tick;

    // One integer buys the entire no-dangling-pointer property. Handlers are
    // explicitly allowed to restructure the tree (UIDocument::UpdatePointer
    // says so), and every Entry holds a raw UIElement*.
    const std::uint32_t epoch = UIElement::structureEpoch();
    const std::uint32_t rev = ctx_->revision();
    if (epoch != seenEpoch_ || rev != seenCtxRev_) {
        // A FULL re-resolve, not just the pending entries: partial
        // re-resolution is exactly how a removed or re-pointed source ends up
        // still being read through a stale pointer.
        UIDocument& doc = *doc_;
        UIBindingContext& ctx = *ctx_;
        const UIStyleSheet* sheet = sheet_;
        std::string origin = origin_;
        Rebuild(doc, ctx, std::move(origin), sheet);
        tick.applied = entries_.size();
        tick.wroteLayout = true;
        return tick;
    }

    for (Entry& e : entries_) {
        const UIBindTarget::Kind kind = e.binding->target.kind;
        // Element -> source only; it has nothing to do in this direction.
        if (kind == UIBindTarget::Kind::State) continue;

        // A `bind-value` carries no template, so it resolves through pushSrc
        // rather than the read path every other binding uses. Without this it
        // would be skipped by the `!e.src` test below and the source -> field
        // half would silently never run.
        if (kind == UIBindTarget::Kind::Value) {
            if (!e.pushSrc) continue;
            const std::uint32_t pv = e.pushSrc->version();
            if (pv == e.lastSourceVersion && !e.pushSrc->hasPolled()) continue;
            if (apply_(e)) { ++tick.applied; tick.wroteLayout |= e.layoutAffecting; }
            e.lastSourceVersion = pv;
            continue;
        }

        if (!e.src) continue;                       // pending
        if (e.binding->tmpl.isConstant()) continue; // authored text, applied at Rebuild

        const std::uint32_t sv = e.src->version();
        // One compare skips every binding on an unchanged source. Polled
        // properties have to be read regardless — that is what makes Poll the
        // only per-frame cost here that does not vanish.
        if (sv == e.lastSourceVersion && !e.src->hasPolled()) continue;

        if (apply_(e)) {
            ++tick.applied;
            tick.wroteLayout |= e.layoutAffecting;
        }
        e.lastSourceVersion = sv;
    }
    return tick;
}

std::size_t UIBinder::ReapplyFor(const UIElement* el) {
    if (!el) return 0;
    std::size_t n = 0;
    for (Entry& e : entries_) {
        if (e.el != el) continue;
        if (apply_(e)) ++n;
    }
    return n;
}

std::size_t UIBinder::unresolvedCount() const {
    std::size_t n = 0;
    for (const Entry& e : entries_) {
        if (!e.binding->tmpl.isConstant() && !e.src) ++n;
    }
    return n;
}

std::vector<std::string> UIBinder::Describe() const {
    std::vector<std::string> out;
    out.reserve(entries_.size());
    for (const Entry& e : entries_) {
        std::string s = "<" + e.el->type();
        if (!e.el->name().empty()) s += " name='" + e.el->name() + "'";
        s += "> " + e.binding->label + " <- ";
        if (e.binding->tmpl.isConstant()) {
            s += "(constant)";
        } else if (!e.src) {
            s += "(unresolved)";
        } else {
            UIValue v;
            e.src->ReadAt(e.propIndex, v);
            s += e.binding->tmpl.holes()[0].propName;
            s += " (" + std::string(v.KindName()) + ") v=" + v.ToDisplayString();
        }
        out.push_back(std::move(s));
    }
    return out;
}

} // namespace MyCoreEngine::ui
