#include "UIWorld.h"

#include "../render2d/Font.h"
#include "../render2d/Renderer2D.h"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace MyCoreEngine {

using ui::UIAssetDocument;

UIWorld::UIWorld() = default;
UIWorld::~UIWorld() = default;

bool UIWorld::wantsKeyboard() const {
    for (const auto& [e, live] : live_) {
        if (!live.doc || !live.enabled || !live.interactive) continue;
        if (live.doc->document().focused()) return true;
    }
    return false;
}

bool UIWorld::wantsTextInput() const {
    for (const auto& [e, live] : live_) {
        if (!live.doc || !live.enabled || !live.interactive) continue;
        const ui::UIElement* f = live.doc->document().focused();
        if (f && f->textEdit()) return true;
    }
    return false;
}

// Which input GRAMMAR is live, derived from the three states this class already
// receives rather than from a fourth one the hosts would have to feed.
//
// STICKY. Only actual activity flips it, so a pad resting on the desk does not
// keep stealing the prompts back from a keyboard, and a mouse that has not moved
// does not either. Silence changes nothing.
//
// The pointer counts as keyboard/mouse, and it has to: clicking SETTINGS with a
// mouse and then seeing "PRESS A TO SELECT" is the same lie in the other
// direction. A MOVE counts, not just a click -- reaching for the mouse is the
// moment you have switched, before you press anything.
void UIWorld::updateDevice_() {
    if (nav_.device == ui::UINavDevice::Gamepad) {
        device_ = ui::UINavDevice::Gamepad;
    } else if (nav_.device == ui::UINavDevice::KeyboardMouse ||
               !keyboard_.keys.empty() || !keyboard_.text.empty() ||
               pointer_.buttonDown != prevButtonDown_ ||
               pointer_.wheel != glm::vec2{ 0.0f } ||
               pointer_.position != prevPointerPos_) {
        device_ = ui::UINavDevice::KeyboardMouse;
    }
    prevPointerPos_ = pointer_.position;
    prevButtonDown_ = pointer_.buttonDown;

    // PUBLISHED EVERY FRAME, not only on the edge. The setters are
    // equality-gated, so an unchanged value costs one compare and wakes no
    // binding -- and doing it unconditionally means a document that loads
    // later still finds the values already there, rather than showing the
    // wrong prompts until the next time the player switches devices.
    const bool pad = (device_ == ui::UINavDevice::Gamepad);
    // A TEXT FIELD HAS FOCUS, so the keyboard's two ways of moving stop being
    // interchangeable: W A S D are letters now and the field eats all four,
    // while the arrows keep working -- Up and Down navigate out of a
    // single-line field and Left/Right run the caret.
    //
    // Naming WASD in that state is a prompt that is wrong exactly when the
    // player tries it. The pad is unaffected: a stick types nothing.
    //
    // Read from the PREVIOUS frame's focus, since this runs before the
    // document loop. One frame of lag on a label, against telling the truth.
    const bool typing = wantsTextInput();
    shared_.SetBool("uiPad", pad);
    shared_.SetBool("uiKeyboard", !pad);
    shared_.SetBool("uiTyping", typing);
    shared_.SetString("uiDevice", pad ? "gamepad" : "keyboard");
    // Text, so a game with no glyph art still reads correctly. A game WITH
    // glyph art ignores these and gates two icons on uiPad/uiKeyboard instead
    // -- and gets uiTyping for the same reason, because a WASD ICON is the
    // same lie a WASD label is.
    shared_.SetString("uiGlyphSelect", pad ? "A" : "ENTER");
    shared_.SetString("uiGlyphBack",   pad ? "B" : "ESC");
    shared_.SetString("uiGlyphNav",    pad      ? "L STICK"
                                     : typing  ? "ARROWS"
                                               : "WASD");
}

void UIWorld::Clear() {
    live_.clear();
    order_.clear();
    errors_.clear();
}

UIAssetDocument* UIWorld::document(entt::entity e) {
    const auto it = live_.find(e);
    return it == live_.end() ? nullptr : it->second.doc.get();
}

void UIWorld::reconcile_(entt::registry& reg) {
    errors_.clear();

    // Drop documents whose entity or component is gone. Done first, so an
    // entity destroyed this frame cannot be drawn from a stale cache entry.
    for (auto it = live_.begin(); it != live_.end();) {
        if (!reg.valid(it->first) || !reg.all_of<UIDocumentComponent>(it->first)) {
            it = live_.erase(it);
        } else {
            ++it;
        }
    }

    auto view = reg.view<UIDocumentComponent>();
    for (const entt::entity e : view) {
        const UIDocumentComponent& c = view.get<UIDocumentComponent>(e);
        Live& live = live_[e];

        // Reload only when the PATHS changed. The flags are cheap to mirror
        // every frame; re-reading two files because someone toggled `enabled`
        // would throw away the parsed tree and every hot-reload stamp with it.
        const bool pathsChanged =
            !live.doc || live.markup != c.markup || live.stylesheet != c.stylesheet;
        live.sortOrder = c.sortOrder;
        live.enabled = c.enabled;
        live.interactive = c.interactive;
        live.scale = c.scale;
        // Fractions -> pixels. Clamped so a mistyped region cannot produce a
        // negative or off-surface box that lays out to nothing with no clue
        // why; a zero-area one is simply not drawn.
        const float fw = float(width_), fh = float(height_);
        const float x = std::clamp(c.regionX, 0.0f, 1.0f);
        const float y = std::clamp(c.regionY, 0.0f, 1.0f);
        live.origin = { std::round(x * fw), std::round(y * fh) };
        live.size = { std::round(std::clamp(c.regionW, 0.0f, 1.0f - x) * fw),
                      std::round(std::clamp(c.regionH, 0.0f, 1.0f - y) * fh) };
        if (!pathsChanged) continue;

        live.markup = c.markup;
        live.stylesheet = c.stylesheet;
        if (c.markup.empty()) { live.doc.reset(); continue; }

        live.doc = std::make_unique<UIAssetDocument>();
        // Every document sees the shared scene source under one well-known
        // name, so markup can bind to gameplay values without the app wiring up
        // each document by hand. A document is free to register more.
        live.doc->bindingContext().RegisterSource(sharedSourceName(), &shared_);
        // Copied in, not shared by pointer: a document's table outlives nothing
        // and dies with it, and copying keeps UIConverterTable's "an instance,
        // not a global" property intact.
        for (const auto& name : converters_.names()) {
            if (const ui::UIConvertFn* fn = converters_.Find(name)) {
                live.doc->bindingContext().converters().Register(name, *fn);
            }
        }
        live.doc->document().SetClipboardHandlers(clipWrite_, clipRead_);
        if (!live.doc->Load(c.markup, c.stylesheet)) {
            for (const auto& err : live.doc->errors()) {
                errors_.push_back(err);
                std::cerr << "[UI] " << err << "\n";
            }
            // KEPT, not discarded: the document reports and stays, so a fixed
            // file is picked up by the ordinary hot-reload poll rather than
            // needing the component to be re-pointed.
        }
    }
}

void UIWorld::Update(entt::registry& reg, int widthPx, int heightPx, float dt) {
    width_ = widthPx;
    height_ = heightPx;
    backUnhandled_ = false;   // one frame's answer, never a stale one
    updateDevice_();
    reconcile_(reg);

    order_.clear();
    for (const auto& [e, live] : live_) {
        if (live.doc && live.enabled) order_.push_back(e);
    }
    // Back to front. The entity is the tie-break rather than nothing at all, so
    // two documents at the same sortOrder keep a STABLE order across frames and
    // across a save/reload — otherwise which one painted on top would depend on
    // hash iteration order.
    std::sort(order_.begin(), order_.end(), [this](entt::entity a, entt::entity b) {
        const Live& la = live_[a];
        const Live& lb = live_[b];
        if (la.sortOrder != lb.sortOrder) return la.sortOrder < lb.sortOrder;
        return entt::to_integral(a) < entt::to_integral(b);
    });

    // Input goes to ONE document: the topmost interactive one under the
    // pointer. Without this a pause menu and the HUD beneath it would both
    // react to the same click, which is the classic layered-UI bug.
    entt::entity pointerTarget = entt::null;
    for (auto it = order_.rbegin(); it != order_.rend(); ++it) {
        Live& live = live_[*it];
        if (!live.interactive) continue;
        // The scale is computed from the WHOLE surface, never from this
        // document's region: a quarter-width sidebar must scale by how big the
        // screen is, not by how small it is.
        live.doc->document().SetScaleSettings(live.scale);
        live.doc->document().SetSurfaceSize({ float(width_), float(height_) });
        // Lay out before hit-testing: the rects it reads are computed there.
        // Repeats first — the binder reads what the pools wrote, and hit-testing
        // a stale pool means clicking a row that moved one frame ago.
        live.doc->UpdateRepeats();
        live.doc->UpdateTabs();
        live.doc->binder().UpdateToTarget();
        live.doc->document().SetOrigin(live.origin);
        live.doc->document().Layout(live.size.x, live.size.y, font_);
        if (!pointer_.inside || live.doc->document().HitTest(pointer_.position)) {
            pointerTarget = *it;
            break;
        }
    }
    // TWO targets, not one. The keyboard follows focus so typing survives a
    // mouse twitch; the POINTER must keep following the pointer.
    //
    // These used to be the same variable, so a document holding focus took the
    // pointer as well — harmless while the pointer only meant clicks, because
    // you had to click to move focus in the first place. The wheel breaks that:
    // it arrives with no click, so a panel you were merely hovering would be fed
    // an empty state and silently refuse to scroll while a background document
    // scrolled instead. Every browser scrolls what is under the cursor.
    entt::entity keyboardTarget = pointerTarget;
    for (const entt::entity e : order_) {
        Live& live = live_[e];
        if (live.interactive && live.doc->document().focused()) { keyboardTarget = e; break; }
    }

    for (const entt::entity e : order_) {
        Live& live = live_[e];
        UIAssetDocument& ad = *live.doc;
        ui::UIDocument& doc = ad.document();

        doc.SetScaleSettings(live.scale);
        doc.SetSurfaceSize({ float(width_), float(height_) });
        ad.Update(dt);                  // hot-reload poll
        // AFTER the poll: on a reload frame the pools were rebuilt one line ago
        // and are still empty, and BEFORE the binder, which reads what they write.
        ad.UpdateRepeats();
        ad.UpdateTabs();
        ad.binder().UpdateToTarget();   // before layout, so text re-measures
        doc.AdvanceTime(dt);
        doc.SetOrigin(live.origin);
        doc.Layout(live.size.x, live.size.y, font_);

        // An empty pointer state for everyone else, NOT "skip the call": a
        // document that stops receiving input must also drop its hover and press
        // state, or it stays lit up under a menu that took the pointer away. It
        // zeroes their wheel for free, too.
        doc.UpdatePointer(e == pointerTarget ? pointer_ : ui::UIPointerState{}, font_);
        if (e == keyboardTarget) {
            doc.UpdateKeyboard(keyboard_, font_);
            // PAGE first, then the rest: a shoulder press changes which panel
            // is on screen, and a directional move in the same frame should
            // land in the panel it just switched to.
            if (nav_.page != 0) ad.PageTabs(nav_.page);
            doc.UpdateNav(nav_);
            // Drained AFTER both input passes, because BOTH can raise it:
            // Escape goes through UpdateKeyboard and the pad's B through
            // UpdateNav, and they run the same UIDocument::Back.
            if (doc.ConsumeBackUnhandled()) backUnhandled_ = true;
        }

        ad.PublishToSources();
        bool relayout = ad.RestyleInteractive();
        relayout |= ad.binder().UpdateToTarget().wroteLayout;
        // After every binder pass this frame, so a re-collect from any of the
        // three is reported in the frame it happened rather than the next one.
        ad.DrainBinderDiagnostics();
        // A wheel notch, a thumb drag or a caret-follow moved an offset that
        // readLayout_ consumes, so it has to be re-applied THIS frame. Without
        // it the thumb trails the cursor for the whole drag and the caret leaves
        // the box for a frame on every newline.
        relayout |= doc.ConsumeScrollDirty();
        if (relayout) doc.Layout(live.size.x, live.size.y, font_);
    }

    // Consumed once, like every other edge-triggered input in this system.
    keyboard_.clear();
    nav_.clear();
    // The wheel is a per-frame DELTA living inside an otherwise level-triggered
    // struct, so it is cleared here rather than by the host: a host that updates
    // without a matching SetPointer would otherwise replay one flick forever.
    pointer_.wheel = { 0.0f, 0.0f };
}

void UIWorld::Draw(Renderer2D& r2d) const {
    // Each document gets its own layer band, so a higher sortOrder paints over
    // a lower one no matter how deep either tree is. 64 is far more nesting
    // than any real UI, and the batcher sorts by layer before flushing.
    int band = 0;
    for (const entt::entity e : order_) {
        const auto it = live_.find(e);
        if (it == live_.end() || !it->second.doc) continue;
        it->second.doc->document().Draw(r2d, font_, band, textures_);
        band += 64;
    }
}

} // namespace MyCoreEngine
