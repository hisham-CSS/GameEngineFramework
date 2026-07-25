#pragma once
#include <entt/entt.hpp>

class UndoHistory;

class SceneHierarchyPanel {
public:
    // returns true if selection changed; selected will be updated.
    // pOpen (optional) drives the window's close button — the editor gates the
    // whole Draw on the same bool, so clicking the tab X hides the panel.
    // outCasterSetChanged (optional) is set when this frame destroyed entities:
    // deleting a caster changes the shadow-caster set WITHOUT dirtying any
    // transform, so the editor must force a CSM rebuild or the deleted object's
    // shadow stays baked on the ground until something else happens to
    // invalidate the cascades.
    bool Draw(entt::registry& reg, entt::entity& selected, UndoHistory& undo,
              bool* pOpen = nullptr, bool* outCasterSetChanged = nullptr);
};
