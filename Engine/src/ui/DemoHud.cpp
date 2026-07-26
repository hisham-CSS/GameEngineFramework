#include "DemoHud.h"

#include "../render2d/Renderer2D.h"

#include <algorithm>
#include <iostream>
#include <string>

namespace MyCoreEngine::ui {

bool DemoHud::Init(const std::string& markupPath, const std::string& stylePath,
                   const std::string& fontPath, float fontPixelHeight) {
    const bool fontOk = font_.LoadFromFile(fontPath, fontPixelHeight);
    if (!fontOk) std::cerr << "[UI] HUD font not loaded: '" << fontPath << "'\n";

    // Seeded and registered BEFORE Load, so the very first binding pass has
    // real values and a clean start reports nothing unresolved. Registering
    // later would still work — the bindings resolve on the next frame — but the
    // report is noise nobody needs.
    source_.SetNumber("health", 1.0f);
    source_.SetInt("score", 0);
    source_.SetBool("lowHealth", false);
    // Seeded so the field starts with something; after that the field OWNS it,
    // and hud.uxml's bind-value publishes every keystroke straight back here.
    source_.SetString("playerName", "player one");
    // A named action, so hud.uxml can write on-click="addScore" and the handler
    // is authored rather than attached.
    source_.AddAction("addScore", [this] { SetScore(score() + 100); });
    assets_.bindingContext().RegisterSource("hud", &source_);

    // Registered on THIS DOCUMENT'S table, not a process-wide one. This lambda
    // captures nothing today, but the day one captures `this` it must die with
    // the HUD rather than outliving it in a global with no way to take it back
    // out — and a per-document table also keeps one test from changing the
    // behaviour of the next.
    //
    // The drain ramp lives here rather than in gameplay because it is a LOOK.
    // Markup asks for {health | healthTint} and this decides what that means.
    assets_.bindingContext().converters().Register(
        "healthTint", [](const UIValue& in, UIValue& out, std::string& err) {
            float h = 0.0f;
            if (!in.AsNumber(h)) {
                err = std::string("healthTint needs a number, got ") + in.KindName();
                return false;
            }
            out = UIValue::Color4({ 0.85f, 0.22f + (1.0f - h) * 0.45f, 0.24f, 1.0f });
            return true;
        });

    // NO BIND CALLBACK. There is nothing left for one to do: values arrive by
    // binding, the click is a named action, and hover/press styling is
    // :hover / :active in hud.uss. Nothing in this class holds a pointer into
    // the tree, so a hot reload has nothing to invalidate.
    const bool markupOk = assets_.Load(markupPath, stylePath);

    return fontOk && markupOk;
}

// No style writes and no setText anywhere below: hud.uxml binds the fill's
// width to {health|percent}, its colour to {health|healthTint}, the readout to
// "SCORE {score}", and the banner's visibility to if="lowHealth". The threshold
// is the one piece of POLICY, so it stays in code; everything downstream of it
// is content.
void DemoHud::SetHealth(float fraction01) {
    const float h = std::clamp(fraction01, 0.0f, 1.0f);
    source_.SetNumber("health", h);
    source_.SetBool("lowHealth", h < 0.3f);
}

void DemoHud::SetScore(int score) { source_.SetInt("score", score); }
int  DemoHud::score() const { return int(source_.GetInt("score")); }

void DemoHud::Draw(Renderer2D& r2d, int widthPx, int heightPx, float dt) {
    // Poll before laying out, so a reload takes effect on the same frame it is
    // detected. Cheap — it only stats the files a few times a second.
    assets_.Update(dt);

    UIDocument& doc = assets_.document();
    const Font* f = font_.IsValid() ? &font_ : nullptr;

    // Bindings BEFORE layout, so a changed label is MEASURED at its new width
    // on the frame it changes. A setText from an input handler never was: it
    // lands after the layout solve and paints at the previous frame's size.
    assets_.binder().UpdateToTarget();

    // Then layout (hit-testing reads computed rects), then input.
    doc.AdvanceTime(dt);   // caret blink today; transitions later
    doc.Layout(float(widthPx), float(heightPx), f);
    // The font goes in so a click can place a text field's caret; without it
    // the click still focuses, it just leaves the caret alone.
    doc.UpdatePointer(pointer_, f);
    // Keyboard AFTER the pointer, so clicking a field and typing into it works
    // within a single frame. Consumed here: a keystroke must be delivered once,
    // and the host keeps refilling this between frames.
    doc.UpdateKeyboard(keyboard_);
    keyboard_.clear();

    // Hover and press are decided by UpdatePointer, so :hover / :active
    // styling can only be applied after it. A state rule may change padding or
    // size and not just colour, so a restyle means laying out again — which is
    // why this is folded into the same condition as a binding write.
    // Element -> source BEFORE the styler and the second binding pass, so a
    // value the user just typed is in the model for anything reading it this
    // frame rather than one frame stale.
    assets_.PublishToSources();

    bool relayout = assets_.RestyleInteractive();

    // A click handler wrote the model AFTER layout. Run the binding pass again
    // and re-solve only if something that can change a box moved — a
    // colour-only write costs nothing, and on a quiet frame this is a handful
    // of integer compares. This also re-checks the structure epoch, which is
    // what makes a handler's RemoveChild safe.
    relayout |= assets_.binder().UpdateToTarget().wroteLayout;
    if (relayout) doc.Layout(float(widthPx), float(heightPx), f);

    doc.Draw(r2d, f);
}

} // namespace MyCoreEngine::ui
