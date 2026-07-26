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
        // Every error reported by the last reconcile, one per failed load.
        const std::vector<std::string>& errors() const { return errors_; }

    private:
        struct Live {
            std::unique_ptr<ui::UIAssetDocument> doc;
            std::string markup, stylesheet;   // what it was loaded FROM
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
        ui::UIPointerState pointer_{};
        ui::UIKeyboardState keyboard_{};
        int width_ = 0, height_ = 0;
    };

} // namespace MyCoreEngine
