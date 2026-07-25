#pragma once
// A worked example of the UI system: health bar, score readout, clickable
// button, crosshair.
//
// This is SAMPLE CONTENT, not an engine feature — a real game ships its own
// .uxml/.uss and its own behaviour class. It lives in the engine only so the
// editor's Game view and the shipped Player can show the SAME UI from one
// definition, which is what makes the Game view an honest preview.
//
// It is now authored as ASSETS (Exported/UI/hud.uxml + hud.uss) rather than
// built in C++, which is the point of the milestone: structure and appearance
// are data, and this class only supplies behaviour — attaching handlers and
// pushing values in. Both files hot-reload while the game runs.
#include "../core/Core.h"
#include "UIAssetDocument.h"
#include "UIElement.h"
#include "../render2d/Font.h"

#include <string>

namespace MyCoreEngine {
    class Renderer2D;
}

namespace MyCoreEngine::ui {

    class ENGINE_API DemoHud {
    public:
        // Loads font + markup + stylesheet. Returns false if either the FONT or
        // the MARKUP is missing; check errors() and hasFont() to tell which.
        // Neither failure is fatal — without a font the bars and crosshair still
        // draw, and a markup file fixed while running is picked up by hot
        // reload, so Draw() stays safe to call regardless.
        bool Init(const std::string& markupPath = "Exported/UI/hud.uxml",
                  const std::string& stylePath = "Exported/UI/hud.uss",
                  const std::string& fontPath = "Exported/Fonts/Roboto.ttf",
                  float fontPixelHeight = 18.0f);

        // True once the markup loaded and the named elements were found, i.e.
        // the HUD is wired up and will respond to SetHealth/SetScore/clicks.
        bool IsReady() const { return healthFill_ && scoreLabel_ && button_; }
        bool hasFont() const { return font_.IsValid(); }
        const std::vector<std::string>& errors() const { return assets_.errors(); }

        // Gameplay pokes these; the tree is retained, so only what changed is
        // re-measured on the next layout. Values are re-applied after a
        // hot-reload, so editing the markup does not reset your health bar.
        void SetHealth(float fraction01);
        void SetScore(int score);
        int  score() const { return score_; }

        // Pointer state in UI-LOCAL pixels, supplied by the host (only it knows
        // where the UI surface sits — see UIPointerState).
        void SetPointer(const UIPointerState& p);

        // Polls the assets for changes, lays out, runs input, and draws. Call
        // from a Renderer::SetUIDraw callback.
        void Draw(Renderer2D& r2d, int widthPx, int heightPx, float dt);

        UIDocument& document() { return assets_.document(); }
        UIAssetDocument& assets() { return assets_; }

    private:
        // Re-caches element pointers and re-attaches handlers. Runs after every
        // (re)load, because a reload rebuilds the tree and invalidates both.
        void Bind(UIDocument& doc);

        // Interaction tints. These live in code rather than the stylesheet
        // because there is no pseudo-class (:hover/:active) styling yet — the
        // app maps interaction state to colour itself. The IDLE colour is still
        // the stylesheet's (captured in Bind), so restyling the button in
        // hud.uss works and only the two transient states are hard-coded.
        static constexpr glm::vec4 kButtonHover{ 0.26f, 0.28f, 0.33f, 0.95f };
        static constexpr glm::vec4 kButtonPressed{ 0.85f, 0.55f, 0.15f, 1.00f };

        Font            font_;
        UIAssetDocument assets_;
        UIElement*      healthFill_ = nullptr;
        UIElement*      scoreLabel_ = nullptr;
        UIElement*      button_ = nullptr;
        glm::vec4       buttonIdle_{ 0.16f, 0.17f, 0.20f, 0.90f };
        UIPointerState  pointer_{};
        float           health_ = 1.0f;
        int             score_ = 0;
    };

} // namespace MyCoreEngine::ui
