#pragma once
// A worked example of the UI system: health bar, score readout, crosshair.
//
// This is SAMPLE CONTENT, not an engine feature — a real game builds its own
// tree (or, once markup lands, loads one from a .uxml-style asset). It lives in
// the engine only so the editor's Game view and the shipped Player can show the
// SAME UI from one definition, which is the whole point of the Game view.
//
// It is also the smallest honest demonstration of the milestone: everything
// here reflows for any resolution and aspect because it is flexbox, and the
// health/score values are mutated on a retained tree rather than re-declared
// every frame.
#include "../core/Core.h"
#include "UIElement.h"
#include "../render2d/Font.h"

#include <string>

namespace MyCoreEngine {
    class Renderer2D;
}

namespace MyCoreEngine::ui {

    class ENGINE_API DemoHud {
    public:
        // Loads the font and builds the tree. Returns false if the font is
        // missing; the HUD then draws nothing rather than failing the frame.
        bool Init(const std::string& fontPath = "Exported/Fonts/Roboto.ttf",
                  float fontPixelHeight = 18.0f);
        bool IsReady() const { return font_.IsValid(); }

        // Gameplay pokes these; the tree is retained, so only what changed is
        // re-measured on the next layout.
        void SetHealth(float fraction01);
        void SetScore(int score);

        // Lays out for this viewport and emits draws. Call from a
        // Renderer::SetUIDraw callback (the pass has already put the renderer
        // in screen space).
        void Draw(Renderer2D& r2d, int widthPx, int heightPx);

        UIDocument& document() { return doc_; }

    private:
        Font       font_;
        UIDocument doc_;
        UIElement* healthFill_ = nullptr;
        UIElement* scoreLabel_ = nullptr;
        float      health_ = 1.0f;
        int        score_ = 0;
        bool       built_ = false;
    };

} // namespace MyCoreEngine::ui
