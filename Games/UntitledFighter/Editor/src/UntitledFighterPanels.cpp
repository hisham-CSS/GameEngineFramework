// The title's half of the editor panel seam: what this game contributes to a
// general-purpose editor, and the one function the editor calls to get it.
//
// Everything fighting-game-specific about the Combo Prover panel is in
// ComboProverPanel.{h,cpp} beside this file, untouched by the move. This file is
// the ADAPTER: it presents that panel as an editor::IEditorPanel and nothing
// else. Kept separate deliberately -- the panel is 1000+ lines of argued
// reasoning about verdicts and certificates, and none of it should have to know
// what an editor extension point looks like this year.
#include "EditorPanel.h"        // the HOST's seam, from Editor/src

#include "ComboProverPanel.h"

#include <memory>

namespace {

// The Combo Prover, wearing the editor's interface.
//
// The panel is constructed ONCE, at registration, and lives for the editor's
// whole session. That is not incidental: it caches the analysis against a
// content fingerprint (ComboProverPanel.h note 5) and accumulates the latency
// distribution ARCHITECTURE.md 5.5 item 2 asks for. A panel rebuilt whenever it
// became visible would re-run the search on every open and would publish a
// measurement about nothing.
class ComboProverEditorPanel final : public editor::IEditorPanel {
public:
    const char* MenuLabel() const override { return "Combo Prover"; }

    bool VisibleByDefault() const override {
        // Off. It is a fighting-game authoring tool, and an editor session that
        // is not authoring a character should not have it in the way.
        // Window > Combo Prover. This used to be a comment on a bool in
        // EditorApplication::PanelVis; the opinion belongs to the panel, and now
        // it is expressed where somebody changing it would look.
        return false;
    }

    void Draw(const editor::PanelContext& ctx, bool* pOpen) override {
        // Told, not assumed. The panel defaults its content root to "Exported"
        // and resolves a typed path against it through the sandbox; taking the
        // editor's own root means the two cannot disagree the day the editor is
        // pointed somewhere else.
        panel_.SetContentRoot(ctx.contentRoot);

        // nullptr: the panel owns the character it is inspecting (path field +
        // Load button) rather than following the scene selection, because a
        // character file is not a scene entity -- nothing in the ECS represents
        // one yet. The parameter is what the editor would pass the day one does.
        //
        // The returned ComboProverActions is dropped, exactly as the editor
        // dropped it before the move: `jumpToMoveId` asks for a move browser to
        // jump to, and there is not one. Ignoring it costs nothing and loses
        // nothing else on the page -- ComboProverActions says so itself.
        (void)panel_.Draw(nullptr, pOpen);
    }

private:
    ComboProverPanel panel_;
};

} // namespace

namespace editor {

// DECLARED by the editor (Editor/src/EditorPanel.h), DEFINED here. The link is
// where the two meet, the same way MyCoreEngine::CreateApplication works, and
// linking this library is what turns the editor's call site on -- the
// CSE_EDITOR_TITLE_PANELS definition rides in on this target's INTERFACE so the
// link and the macro cannot disagree. See Games/UntitledFighter/Editor/CMakeLists.txt.
void RegisterTitlePanels(PanelRegistry& registry) {
    registry.Add(std::make_unique<ComboProverEditorPanel>());
}

} // namespace editor
