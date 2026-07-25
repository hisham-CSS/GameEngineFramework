#include "DemoHud.h"

#include "../render2d/Renderer2D.h"

#include <algorithm>
#include <string>

namespace MyCoreEngine::ui {

bool DemoHud::Init(const std::string& fontPath, float fontPixelHeight) {
    const bool ok = font_.LoadFromFile(fontPath, fontPixelHeight);

    // Build the tree even without a font: the bars and crosshair are geometry
    // and still work. A missing font should cost you labels, not the HUD.
    UIElement& root = doc_.root();
    root.style().direction = FlexDirection::Column;
    root.style().padding = Edges::All(16.0f);

    // --- top row: health on the left, score on the right ------------------
    UIElement* topBar = root.AddChild("topBar");
    topBar->style().direction = FlexDirection::Row;
    topBar->style().justify = Justify::SpaceBetween; // pushes the two apart at
    topBar->style().alignItems = Align::Center;      // ANY width, no maths

    UIElement* healthTrack = topBar->AddChild("healthTrack");
    healthTrack->style().width = StyleLength::Px(220.0f);
    healthTrack->style().height = StyleLength::Px(18.0f);
    healthTrack->style().backgroundColor = { 0.0f, 0.0f, 0.0f, 0.45f };
    healthTrack->style().padding = Edges::All(3.0f);
    // The fill is a percentage-width child, so "set health" is one style write
    // and flexbox does the rest.
    healthFill_ = healthTrack->AddChild("healthFill");
    healthFill_->style().width = StyleLength::Pct(100.0f);
    healthFill_->style().height = StyleLength::Pct(100.0f);
    healthFill_->style().backgroundColor = { 0.85f, 0.22f, 0.24f, 1.0f };

    scoreLabel_ = topBar->AddChild("scoreLabel");
    scoreLabel_->setText("SCORE 0");
    scoreLabel_->style().textColor = { 1.0f, 1.0f, 1.0f, 0.95f };

    // --- a real, clickable button -----------------------------------------
    // Deliberately below the top bar in the same column, so it participates in
    // normal flow. It exists to prove the input path end to end: hit-testing,
    // hover/press state, and a Click handler that mutates the retained tree.
    UIElement* buttonRow = root.AddChild("buttonRow");
    buttonRow->style().direction = FlexDirection::Row;
    buttonRow->style().margin = { 0.0f, 12.0f, 0.0f, 0.0f };

    button_ = buttonRow->AddChild("scoreButton");
    button_->style().padding = Edges::XY(14.0f, 8.0f);
    button_->style().backgroundColor = kButtonIdle;
    button_->setText("+100");
    button_->style().textColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    // The click lands on the BUTTON even though the text is drawn by the same
    // element; once this is a composite widget (icon + label children) bubbling
    // is what keeps the handler working unchanged.
    button_->OnClick([this](UIEvent&) { SetScore(score_ + 100); });

    // --- centre: crosshair -------------------------------------------------
    // Absolutely positioned and centred by flexbox rather than by arithmetic on
    // the viewport size, so it stays centred when the panel is resized.
    UIElement* centre = root.AddChild("centre");
    centre->style().position = PositionType::Absolute;
    centre->style().inset = { 0.0f, 0.0f, 0.0f, 0.0f };
    centre->style().justify = Justify::Center;
    centre->style().alignItems = Align::Center;
    // A full-screen decorative layer MUST NOT eat the pointer, or it would sit
    // over everything and swallow every click meant for the UI beneath it.
    centre->style().pickable = false;

    UIElement* cross = centre->AddChild("crosshair");
    cross->style().width = StyleLength::Px(10.0f);
    cross->style().height = StyleLength::Px(10.0f);
    cross->style().backgroundColor = { 1.0f, 1.0f, 1.0f, 0.75f };

    built_ = true;
    SetHealth(health_);
    SetScore(score_);
    return ok;
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

void DemoHud::Draw(Renderer2D& r2d, int widthPx, int heightPx) {
    if (!built_) return;
    const Font* f = font_.IsValid() ? &font_ : nullptr;

    // Order matters: layout first (hit-testing reads computed rects), then
    // input (handlers may change styles), then paint — so a press is visible on
    // the very frame it happens rather than one frame late.
    doc_.Layout(float(widthPx), float(heightPx), f);
    doc_.UpdatePointer(pointer_);

    // The system reports interaction STATE; deciding what it looks like is the
    // app's job, since there is no pseudo-class styling yet.
    if (button_) {
        button_->style().backgroundColor =
            button_->isPressed() ? kButtonPressed
          : button_->isHovered() ? kButtonHover
                                 : kButtonIdle;
    }

    doc_.Draw(r2d, f);
}

} // namespace MyCoreEngine::ui
