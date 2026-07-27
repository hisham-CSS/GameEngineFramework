#pragma once
// Stands up the SHIPPED sample HUD exactly the way the engine ships it: a scene
// entity carrying a UIDocumentComponent, driven by a UIWorld with the demo's
// named action and converter installed.
//
// Every test that asserts on Exported/UI/hud.cxml + hud.cstyle goes through this,
// so the assertions exercise the real path a game uses — and a typo in either
// file, or a converter the markup names but the C++ forgot to register, fails
// the suite instead of shipping.
#include "Engine.h"

#include <string>

struct ShippedHud {
    MyCoreEngine::Scene   scene;
    MyCoreEngine::UIWorld world;
    entt::entity          entity{ entt::null };

    ShippedHud() {
        // Before the component exists, so the first binding pass has real
        // values — the same ordering a game uses.
        MyCoreEngine::InstallDemoUIContent(world);
        entity = scene.registry.create();
        MyCoreEngine::UIDocumentComponent ud;
        ud.markup = "Exported/UI/hud.cxml";
        ud.stylesheet = "Exported/UI/hud.cstyle";
        scene.registry.emplace<MyCoreEngine::UIDocumentComponent>(entity, ud);
    }

    // One host frame. No font by default: the boxes still lay out and paint,
    // which keeps these tests free of a GL context.
    void Frame(int w = 1280, int h = 720, float dt = 0.016f) {
        world.Update(scene.registry, w, h, dt);
    }

    MyCoreEngine::ui::UIAssetDocument* assets() { return world.document(entity); }
    MyCoreEngine::ui::UIDocument& doc() { return assets()->document(); }
    MyCoreEngine::ui::UIElement* find(const char* n) {
        auto* a = assets();
        return a ? a->document().root().Find(n) : nullptr;
    }
    MyCoreEngine::ui::UIDataSource& data() { return world.shared(); }

    void Point(float x, float y, bool down = false, bool inside = true) {
        MyCoreEngine::ui::UIPointerState p;
        p.position = { x, y };
        p.inside = inside;
        p.buttonDown = down;
        world.SetPointer(p);
    }
    // Press and release over the same point, one frame each, which is what
    // UIDocument requires for a Click.
    void ClickAt(float x, float y, int w = 1280, int h = 720) {
        Point(x, y, true);  Frame(w, h);
        Point(x, y, false); Frame(w, h);
    }
};
