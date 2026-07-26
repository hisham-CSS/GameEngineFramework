#pragma once
// A worked example of the UI system: health bar, score readout, clickable
// button, low-health banner, crosshair.
//
// This is SAMPLE CONTENT, not an engine feature — a real game ships its own
// .uxml/.uss and its own class. It lives in the engine only so the editor's
// Game view and the shipped Player can show the SAME UI from one definition,
// which is what makes the Game view an honest preview.
//
// What is left of it is the point. Structure is hud.uxml, appearance is
// hud.uss (including hover and press, via :hover / :active), and values arrive
// by binding from `source_`. There is no cached element pointer, no re-bind
// callback, and no code that reaches into the tree — so this class is a MODEL
// plus a Draw order, and everything else is hot-reloadable content.
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
        // Neither failure is fatal — without a font the bars and crosshair
        // still draw, and a markup file fixed while running is picked up by hot
        // reload, so Draw() stays safe to call regardless.
        bool Init(const std::string& markupPath = "Exported/UI/hud.uxml",
                  const std::string& stylePath = "Exported/UI/hud.uss",
                  const std::string& fontPath = "Exported/Fonts/Roboto.ttf",
                  float fontPixelHeight = 18.0f);

        // Assets loaded clean AND every authored binding resolved. A typo in a
        // bound path is a reported failure rather than a readout that silently
        // never updates.
        bool IsReady() const { return assets_.ok() && assets_.binder().ok(); }
        bool hasFont() const { return font_.IsValid(); }
        const std::vector<std::string>& errors() const { return assets_.errors(); }

        // Gameplay writes the MODEL; hud.uxml decides how it reads. Both
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
        void SetPointer(const UIPointerState& p) { pointer_ = p; }

        // Polls the assets for changes, applies bindings, lays out, runs input
        // and draws. Call from a Renderer::SetUIDraw callback.
        void Draw(Renderer2D& r2d, int widthPx, int heightPx, float dt);

        UIDocument&      document() { return assets_.document(); }
        UIAssetDocument& assets() { return assets_; }

    private:
        Font            font_;
        // DECLARED BEFORE assets_, and that is load-bearing: the binder inside
        // assets_ holds a raw pointer to this source, and members are destroyed
        // in reverse declaration order. UIDataSource also deletes its copy and
        // its move, so relocating one is a compile error rather than a HUD that
        // draws perfectly and silently stops updating.
        UIDataSource    source_;
        UIAssetDocument assets_;
        UIPointerState  pointer_{};
    };

} // namespace MyCoreEngine::ui
