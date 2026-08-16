#include "EditorPanel.h"

#include "imgui.h"

#include <utility>

namespace editor {

void PanelRegistry::Add(std::unique_ptr<IEditorPanel> panel) {
    if (!panel) return;   // see the header: a failed registration must not
                          // become a null dereference inside a draw loop
    Entry e;
    e.visible = panel->VisibleByDefault();
    e.panel = std::move(panel);
    entries_.push_back(std::move(e));
}

void PanelRegistry::DrawMenuItems() {
    // No panels, no section. The separator is drawn HERE rather than by the
    // caller precisely so that an editor with no title attached shows nothing
    // at all -- a lone separator at the bottom of the Window menu is a hint
    // that something failed to load, and nothing failed.
    if (entries_.empty()) return;
    ImGui::Separator();
    for (Entry& e : entries_) {
        ImGui::MenuItem(e.panel->MenuLabel(), nullptr, &e.visible);
    }
}

void PanelRegistry::DrawVisible(const PanelContext& ctx) {
    for (Entry& e : entries_) {
        if (!e.visible) continue;
        // The same bool for both, so the window's X and the menu checkbox are
        // one piece of state. The panel is expected to pass it to its
        // ImGui::Begin.
        e.panel->Draw(ctx, &e.visible);
    }
}

void PanelRegistry::ResetToDefaults() {
    for (Entry& e : entries_) e.visible = e.panel->VisibleByDefault();
}

} // namespace editor
