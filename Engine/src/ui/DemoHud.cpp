#include "DemoHud.h"

#include "../render2d/Renderer2D.h"

#include <algorithm>
#include <iostream>
#include <string>

namespace MyCoreEngine::ui {

bool DemoHud::Init(const std::string& markupPath, const std::string& stylePath,
                   const std::string& fontPath, float fontPixelHeight) {
    // Font first: the bind callback runs inside Load and may want to measure.
    // A missing font costs you labels, not the HUD.
    const bool fontOk = font_.LoadFromFile(fontPath, fontPixelHeight);
    if (!fontOk) std::cerr << "[UI] HUD font not loaded: '" << fontPath << "'\n";

    // Seeded and registered BEFORE Load, so the very first binding pass has
    // real values and a clean start reports nothing unresolved. Registering
    // later would still work — the bindings resolve on the next frame — but the
    // report is noise nobody needs.
    source_.SetNumber("health", 1.0f);
    source_.SetInt("score", 0);
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

    const bool markupOk =
        assets_.Load(markupPath, stylePath, [this](UIDocument& doc) { Bind(doc); });

    return fontOk && markupOk;
}

void DemoHud::Bind(UIDocument& doc) {
    // BEHAVIOUR only, and only the part binding cannot express yet. The
    // "re-push everything you cached" step this function used to end with
    // (SetHealth(health_); SetScore(score_);) is gone: the model is not in the
    // tree, so a rebuild cannot lose it.
    //
    // The one element still cached here is cached for the hover/press tint in
    // Draw, which is a missing :hover pseudo-class rather than a missing
    // binding. Both lines disappear the day USS grows one.
    button_ = doc.root().Find("scoreButton");
    if (!button_) {
        std::cerr << "[UI] HUD markup has no element named 'scoreButton'\n";
        return;
    }
    // Bind runs after the cascade, so this is the authored idle colour.
    buttonIdle_ = button_->style().backgroundColor;
    // The click lands on the BUTTON even though the text is drawn by the same
    // element; once this is a composite widget (icon + label children) bubbling
    // is what keeps this handler working unchanged.
    button_->OnClick([this](UIEvent&) { SetScore(score() + 100); });
}

// No style writes at all: hud.uxml binds the fill's width to {health|percent}
// and its colour to {health|healthTint}, so both the unit and the drain ramp
// are hot-reloadable content now.
void DemoHud::SetHealth(float fraction01) {
    source_.SetNumber("health", std::clamp(fraction01, 0.0f, 1.0f));
}

// No setText anywhere: hud.uxml says text="SCORE {score}", so writing the model
// is the whole job and the label's format is hot-reloadable content.
void DemoHud::SetScore(int score) { source_.SetInt("score", score); }
int  DemoHud::score() const { return int(source_.GetInt("score")); }

void DemoHud::SetPointer(const UIPointerState& p) { pointer_ = p; }

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

    // Then layout (hit-testing reads computed rects), then input (handlers may
    // change styles), then paint — so a press is visible on the very frame it
    // happens rather than one frame late.
    doc.Layout(float(widthPx), float(heightPx), f);
    doc.UpdatePointer(pointer_);

    // The system reports interaction STATE; deciding what it looks like is the
    // app's job, since there is no pseudo-class styling yet.
    if (button_) {
        button_->style().backgroundColor =
            button_->isPressed() ? kButtonPressed
          : button_->isHovered() ? kButtonHover
                                 : buttonIdle_;
    }

    // A click handler wrote the model AFTER layout. Run the pass again and
    // re-solve only if something that can change a box moved — a colour-only
    // write costs nothing, and on a quiet frame this whole line is a handful of
    // integer compares. This call also re-checks the structure epoch, which is
    // what makes a handler's RemoveChild safe.
    if (assets_.binder().UpdateToTarget().wroteLayout) {
        doc.Layout(float(widthPx), float(heightPx), f);
    }

    doc.Draw(r2d, f);
}

} // namespace MyCoreEngine::ui
