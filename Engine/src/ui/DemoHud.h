#pragma once
// A worked example of the UI system: health bar, score readout, clickable
// button, crosshair.
//
// This is SAMPLE CONTENT, not an engine feature — a real game ships its own
// .uxml/.uss and its own behaviour class. It lives in the engine only so the
// editor's Game view and the shipped Player can show the SAME UI from one
// definition, which is what makes the Game view an honest preview.
//
// It is authored as ASSETS (Exported/UI/hud.uxml + hud.uss) rather than built
// in C++: structure and appearance are data, and this class supplies only what
// data cannot — behaviour, and the model gameplay writes to. Both files hot
// reload while the game runs.
//
// VALUES arrive by BINDING, not by this class reaching into the tree. The model
// lives in `source_`, which is not part of the document, so a hot reload
// rebuilds every element without losing one value and without this class
// re-pushing anything. That re-push is the step every hot-reloading UI forgets
// exactly once.
#include "../core/Core.h"
#include "UIAssetDocument.h"
#include "UIDataSource.h"
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

        // Assets loaded clean AND every authored binding resolved. Strictly
        // stronger than the null-pointer check this used to be: a typo in a
        // bound path is now a reported failure rather than a readout that
        // silently never updates.
        bool IsReady() const { return assets_.ok() && assets_.binder().ok() && button_; }
        bool hasFont() const { return font_.IsValid(); }
        const std::vector<std::string>& errors() const { return assets_.errors(); }

        // Gameplay writes the MODEL; hud.uxml decides what it looks like. Both
        // survive a hot reload untouched — the model because it is not part of
        // the tree, the look because it IS the tree and gets rebuilt from disk.
        void SetHealth(float fraction01);
        void SetScore(int score);
        int  score() const;

        // The HUD's data source, for a game that wants to write its own values
        // or bind new ones from markup without touching this class.
        UIDataSource& data() { return source_; }

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
        // DECLARED BEFORE assets_, and that is load-bearing: the binder inside
        // assets_ holds a raw pointer to this source, and members are destroyed
        // in reverse declaration order. UIDataSource also deletes its copy and
        // its move, so relocating one is a compile error rather than a HUD that
        // draws perfectly and silently stops updating.
        UIDataSource    source_;
        UIAssetDocument assets_;
        // The ONLY cached element pointer left, and it exists purely to serve
        // the hover/press tint below.
        UIElement*      button_ = nullptr;
        glm::vec4       buttonIdle_{ 0.16f, 0.17f, 0.20f, 0.90f };
        UIPointerState  pointer_{};
    };

} // namespace MyCoreEngine::ui
