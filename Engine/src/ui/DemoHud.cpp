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

    const bool markupOk =
        assets_.Load(markupPath, stylePath, [this](UIDocument& doc) { Bind(doc); });

    return fontOk && markupOk;
}

void DemoHud::Bind(UIDocument& doc) {
    // A reload rebuilds the tree, so everything cached from the previous
    // version is dangling by now. Re-find, re-attach, then re-apply the live
    // values — editing the markup must not reset your health bar or score.
    healthFill_ = doc.root().Find("healthFill");
    scoreLabel_ = doc.root().Find("scoreLabel");
    button_ = doc.root().Find("scoreButton");

    // Names are the contract between markup and behaviour, and a typo would
    // otherwise show up as a button that silently does nothing.
    if (!healthFill_) std::cerr << "[UI] HUD markup has no element named 'healthFill'\n";
    if (!scoreLabel_) std::cerr << "[UI] HUD markup has no element named 'scoreLabel'\n";
    if (!button_)     std::cerr << "[UI] HUD markup has no element named 'scoreButton'\n";

    if (button_) {
        // Bind runs after the stylesheet has been applied, so this is the
        // authored idle colour — capture it instead of hard-coding one, and
        // restyling .btn in hud.uss keeps working.
        buttonIdle_ = button_->style().backgroundColor;
        // The click lands on the BUTTON even though the text is drawn by the
        // same element; once this is a composite widget (icon + label children)
        // bubbling is what keeps this handler working unchanged.
        button_->OnClick([this](UIEvent&) { SetScore(score_ + 100); });
    }

    SetHealth(health_);
    SetScore(score_);
}

void DemoHud::SetHealth(float fraction01) {
    health_ = std::clamp(fraction01, 0.0f, 1.0f);
    if (healthFill_) {
        healthFill_->style().width = StyleLength::Pct(health_ * 100.0f);
        // Colour shifts toward amber as it drains — a visible sign that a plain
        // style write on a retained tree takes effect next frame.
        healthFill_->style().backgroundColor =
            glm::vec4(0.85f, 0.22f + (1.0f - health_) * 0.45f, 0.24f, 1.0f);
    }
}

void DemoHud::SetScore(int score) {
    score_ = score;
    // setText (not style().text) so the measured width is invalidated.
    if (scoreLabel_) scoreLabel_->setText("SCORE " + std::to_string(score_));
}

void DemoHud::SetPointer(const UIPointerState& p) { pointer_ = p; }

void DemoHud::Draw(Renderer2D& r2d, int widthPx, int heightPx, float dt) {
    // Poll before laying out, so a reload takes effect on the same frame it is
    // detected. Cheap — it only stats the files a few times a second.
    assets_.Update(dt);

    UIDocument& doc = assets_.document();
    const Font* f = font_.IsValid() ? &font_ : nullptr;

    // Order matters: layout first (hit-testing reads computed rects), then
    // input (handlers may change styles), then paint — so a press is visible on
    // the very frame it happens rather than one frame late.
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

    doc.Draw(r2d, f);
}

} // namespace MyCoreEngine::ui
