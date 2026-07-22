#pragma once
#include <entt/entt.hpp>

class UndoHistory;

class SceneHierarchyPanel {
public:
    // returns true if selection changed; selected will be updated.
    // pOpen (optional) drives the window's close button — the editor gates the
    // whole Draw on the same bool, so clicking the tab X hides the panel.
    bool Draw(entt::registry& reg, entt::entity& selected, UndoHistory& undo,
              bool* pOpen = nullptr);
};
