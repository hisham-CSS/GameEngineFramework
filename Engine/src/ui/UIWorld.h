#pragma once
// Drives every UIDocumentComponent in a scene: loads its assets, runs the
// per-frame passes in the right order, and draws them back to front.
//
// This is the UI's equivalent of AudioWorld and ScriptWorld — the piece that
// turns "a component exists" into "it does something", and the reason a host
// no longer has to know a game has UI at all.
//
// LIVE DOCUMENTS ARE CACHED BY ENTITY, not rebuilt per frame: a UIAssetDocument
// owns a parsed tree, a binder index and hot-reload stamps, and throwing that
// away every frame would make all three pointless. The cache is reconciled
// against the registry each Update, so adding, removing or re-pointing a
// component takes effect immediately.
#include "../core/Core.h"
#include "UIAssetDocument.h"
#include "UIComponent.h"
#include "UIDataSource.h"
#include "UIEvent.h"

#include <entt/entt.hpp>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace MyCoreEngine {

    class Renderer2D;
    class Font;

    class ENGINE_API UIWorld {
    public:
        UIWorld();
        ~UIWorld();
        UIWorld(const UIWorld&) = delete;
        UIWorld& operator=(const UIWorld&) = delete;

        // The font every document draws with. Optional: without one the boxes
        // still lay out and paint, they just have no text — the same graceful
        // degradation as everywhere else.
        void SetFont(const Font* f) { font_ = f; }
        // Resolves `background-image`. Owned by the HOST, one per GL context —
        // the editor runs a second renderer for its Game view, and a
        // process-wide cache would hand one context's texture names to the
        // other. Null simply paints no images.
        void SetTextureCache(ui::UITextureCache* c) { textures_ = c; }
        ui::UITextureCache* textureCache() const { return textures_; }

        // Values shared by every document in the scene, so gameplay has one
        // place to write and markup can bind to it from any entity. A document
        // may still register its own sources through document(e).
        ui::UIDataSource& shared() { return shared_; }
        static const char* sharedSourceName() { return "scene"; }

        // Converters available to EVERY document in the scene. A converter is a
        // named C++ function, so it cannot live in a file the way values and
        // structure can — this is where scene-wide ones are registered, and it
        // is copied into each document as it loads.
        ui::UIConverterTable& converters() { return converters_; }
        const ui::UIConverterTable& converters() const { return converters_; }

        // Input for this frame, in UI-LOCAL pixels. Supplied by the host for
        // the same reason as always: only it knows where the UI surface sits
        // and whether the keyboard belongs to the game right now.
        void SetPointer(const ui::UIPointerState& p) { pointer_ = p; }
        void SetKeyboard(const ui::UIKeyboardState& k) { keyboard_ = k; }
        // Directional navigation, routed to the same document the keyboard goes
        // to: a pad and a keyboard drive one focus, so they must not disagree
        // about which document owns it.
        void SetNav(const ui::UINavState& n) { nav_ = n; }

        // True for the frame in which BACK was pressed and NOTHING in the UI
        // wanted it: no panel to close, no scope that declared an `on-back`.
        // That is the host's cue to give it a meaning of its own -- close the
        // pause menu, quit to desktop -- and it is the only way to find out,
        // since a document that handles back closes itself silently.
        //
        // Valid after Update() until the next one. Cleared every frame, so a
        // host that forgets to ask simply gets nothing rather than a stale yes.
        bool backWentUnhandled() const { return backUnhandled_; }

        // The system clipboard, for Ctrl+C/X/V in text fields. Handed to every
        // document as it loads. Without it those keys do nothing rather than
        // half-working against a private buffer nothing else can see.
        void SetClipboardHandlers(std::function<void(const std::string&)> write,
                                  std::function<std::string()> read) {
            clipWrite_ = std::move(write);
            clipRead_ = std::move(read);
        }

        // Reconciles live documents against the registry, then runs every
        // enabled one: hot-reload poll, bindings, layout, input, publish,
        // restyle, and a second layout only when something moved.
        //
        // Input goes to the TOPMOST interactive document that wants it — one
        // document at a time, or a pause menu and the HUD beneath it would both
        // react to the same click.
        void Update(entt::registry& reg, int widthPx, int heightPx, float dt);
        // Back to front, so a higher sortOrder paints over a lower one.
        void Draw(Renderer2D& r2d) const;

        // Drops every live document. Call when the scene is replaced, or the
        // cache would keep documents for entities that no longer exist.
        void Clear();

        // The live document for an entity, or null when it has none loaded.
        // How an app reaches a document's binding context to register its own
        // sources, converters and actions.
        ui::UIAssetDocument* document(entt::entity e);

        std::size_t liveCount() const { return live_.size(); }

        // Is the game's UI currently USING the keyboard? Both hosts need this
        // to decide whether a key belongs to the UI or to the game, and until
        // now only the editor could answer it -- for ImGui, not for this.
        //
        // FOCUS, not existence. A HUD is on screen for the whole game and does
        // not thereby own the Escape key; something has to be focused for the
        // UI to have a claim on it. That also keeps a game QUITTABLE: at boot
        // nothing is focused, so Escape still reaches the host.
        //
        // Only INTERACTIVE documents count. A decorative overlay is explicitly
        // marked not-interactive and must not silently eat input.
        bool wantsKeyboard() const;
        // Narrower: a TEXT FIELD has focus, so every printable key is content.
        // This is what must suppress a game action bound to a letter -- and
        // what makes Escape-while-typing not quit the game.
        bool wantsTextInput() const;
        // Every error reported by the last reconcile, one per failed load.
        const std::vector<std::string>& errors() const { return errors_; }

    private:
        struct Live {
            std::unique_ptr<ui::UIAssetDocument> doc;
            std::string markup, stylesheet;
        ui::UIScaleSettings scale{};   // what it was loaded FROM
            int  sortOrder = 0;
            bool enabled = true;
            bool interactive = true;
            // Resolved to surface pixels each Update, so a resize needs no
            // reload — the component stores fractions.
            glm::vec2 origin{ 0.0f }, size{ 0.0f };
        };

        void reconcile_(entt::registry& reg);

        std::unordered_map<entt::entity, Live> live_;
        // Rebuilt each Update and sorted by (sortOrder, entity). Kept as a
        // member so a steady frame allocates nothing.
        std::vector<entt::entity> order_;
        ui::UIDataSource shared_;
        ui::UIConverterTable converters_;
        std::vector<std::string> errors_;
        const Font*    font_ = nullptr;
        ui::UITextureCache* textures_ = nullptr;
        std::function<void(const std::string&)> clipWrite_;
        std::function<std::string()>            clipRead_;
        ui::UIPointerState pointer_{};
        ui::UIKeyboardState keyboard_{};
        ui::UINavState      nav_{};
        bool                backUnhandled_ = false;
        int width_ = 0, height_ = 0;
    };

} // namespace MyCoreEngine
