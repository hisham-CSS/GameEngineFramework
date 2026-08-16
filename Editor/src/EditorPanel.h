// The seam a TITLE plugs an editor panel into.
//
// ---------------------------------------------------------------------------
// WHY THIS FILE EXISTS
// ---------------------------------------------------------------------------
// The Combo Prover panel used to be compiled into this editor, and
// Editor/CMakeLists.txt linked CseData to make it build. That is a
// fighting-game authoring tool inside a general-purpose editor, and it is the
// same mistake as the engine linking a game: the second title starts by
// deleting the first title's panel out of a shared executable, and the editor's
// dependency list slowly becomes the union of every game anyone ever shipped
// with it. Games/UntitledFighter/CMakeLists.txt states the rule this seam
// enforces on the editor's side -- A TITLE MAY DEPEND ON THE ENGINE AND ON THE
// EDITOR; NEITHER MAY DEPEND ON A TITLE.
//
// So the panel moved to Games/UntitledFighter/Editor/, and what is left here is
// the shape of the hole it fits into. Nothing in Editor/ names a fighter, a
// hitbox or a move any more; the word "UntitledFighter" appears in the editor's
// build exactly once, in the ROOT CMakeLists.txt, which is already the one file
// allowed to know a title exists.
//
// ---------------------------------------------------------------------------
// THE SHAPE IS DICTATED BY WHAT THE EDITOR ALREADY DOES
// ---------------------------------------------------------------------------
// A panel in this editor is three things and has been since before this file:
// a bool in EditorApplication::PanelVis, a checkbox in the Window menu bound to
// that bool, and a `if (bool) panel.Draw(&bool)` in the UI loop. This registry
// is those same three things for panels the editor did not compile, and
// deliberately nothing more -- an extension point that is richer than the thing
// it extends is an invitation to build a second, divergent panel system.
//
// The built-in panels (Hierarchy, Inspector, Assets, ...) are NOT moved onto
// this interface. They take the registry, the selection and the undo history by
// reference and hand back an actions struct, and forcing them through a uniform
// Draw(bool*) would either widen this interface to the union of their needs or
// strip them of arguments they genuinely use. They stay as they are; this is
// the seam for panels that arrive from outside, which is a different problem.
//
// ---------------------------------------------------------------------------
// HOW A TITLE ACTUALLY GETS ITS PANEL IN
// ---------------------------------------------------------------------------
// By defining editor::RegisterTitlePanels, declared at the bottom of this file
// and defined nowhere in Editor/. That is the same idiom the engine already
// uses for MyCoreEngine::CreateApplication (Application.h: "defined in other
// projects") -- the host declares the function, somebody else defines it, and
// the LINK is where the two meet.
//
// It is guarded by CSE_EDITOR_TITLE_PANELS, and that macro is not set by
// Editor/CMakeLists.txt. It rides in as an INTERFACE compile definition on the
// title's editor library, so linking the library IS turning the hook on: the
// two cannot disagree. An editor built with no title compiles the call out and
// links clean with an empty Window > (no title panels); an editor that links
// the library and somehow lost the definition fails at LINK time, loudly, which
// is the failure mode this codebase prefers to a panel that silently vanished.
//
// The road not taken is a self-registering static object in the title's
// library. It needs no hook and no macro, and it does not work: a static
// library member with no referenced symbol is dropped by both linkers, so the
// constructor never runs and the panel is simply absent with no error anywhere.
// /WHOLEARCHIVE and --whole-archive fix it and are a per-host, per-platform
// build incantation that nobody remembers to add to the second host.
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace editor {

// What the editor can honestly tell a panel about the session, without the
// panel having to know what a Scene or an entt::registry is.
//
// TWO FIELDS, BOTH REAL. The temptation is to pass the registry, the selection
// and the undo history so a title panel can do everything a built-in one can.
// That would make every title's editor library link the Engine to see
// entt::entity, and it would be inventing requirements: the one panel that
// exists today reads neither. Both fields here are things the editor already
// tracks and that ANY authoring tool has a use for, which is the bar for adding
// a third.
struct PanelContext {
    // The editor's asset root -- the directory Exported/ is staged into next to
    // the executable. A panel that opens authored files resolves them against
    // this rather than guessing, and authored paths are untrusted, so a panel
    // is expected to put it through a sandbox check (docs/MAINTENANCE.md).
    std::string contentRoot = "Exported";

    // Play-in-editor is running. A panel that mutates authored content must
    // refuse while this is true for the same reason the asset browser's drops
    // are gated: an edit made during play is discarded by Stop's restore, so it
    // looks like the editor ate it.
    bool playing = false;
};

// One panel, supplied by a title.
//
// The editor owns the visibility bool and the menu item; the panel owns its own
// window, its own state and its own lifetime-of-the-session caches. Draw is
// called only on frames the panel is visible, so a panel that caches an
// expensive result against a fingerprint (which the one panel that exists does,
// deliberately, rather than taking a change notification) pays nothing while
// hidden.
class IEditorPanel {
public:
    virtual ~IEditorPanel() = default;

    // The Window menu entry, and by convention the ImGui window title too.
    // Returned rather than stored so a panel can name itself from its own data
    // -- and const char* rather than std::string because it is read once per
    // frame into an ImGui call that wants one anyway.
    virtual const char* MenuLabel() const = 0;

    // Called once per frame while visible. `pOpen` drives the window's close
    // button and is the SAME bool the Window menu checkbox toggles, so the tab
    // X and the menu item are one piece of state rather than two that drift.
    virtual void Draw(const PanelContext& ctx, bool* pOpen) = 0;

    // Whether the panel is on when the editor starts, and what "Show All
    // Panels" restores it to.
    //
    // FALSE by default, and the default is the interesting half: a title panel
    // is a tool for authoring THAT title's content, and an editor session that
    // is not doing that should not have it in the way. The panel decides,
    // rather than the editor -- the editor has no business holding an opinion
    // about how central somebody else's tool is to their work.
    virtual bool VisibleByDefault() const { return false; }

protected:
    IEditorPanel() = default;
};

// The editor's collection of them.
//
// Owns the panels and their visibility bools together, because those two are
// the pair that has to stay in step: the menu item writes the bool, the draw
// loop reads it, and the panel's own close button writes it through `pOpen`.
class PanelRegistry {
public:
    // Takes ownership. Panels are drawn in registration order, which is also
    // the order they appear in the Window menu, so a title that registers
    // several controls how they read.
    //
    // A null panel is ignored rather than stored: a registration that failed to
    // allocate should not turn into a null dereference one frame later, in a
    // draw loop, far from the call that caused it.
    void Add(std::unique_ptr<IEditorPanel> panel);

    bool empty() const { return entries_.empty(); }
    int  count() const { return static_cast<int>(entries_.size()); }

    // One ImGui::MenuItem per panel, bound straight to its visibility bool --
    // exactly what the built-in panels' entries do. Draws NOTHING when there
    // are no panels, including the separator, so an editor with no title
    // attached has no empty section and no hint that something is missing.
    // Call from inside the Window menu.
    void DrawMenuItems();

    // Draw every visible panel. Call from the editor's UI loop.
    void DrawVisible(const PanelContext& ctx);

    // Restore every panel to its own VisibleByDefault. This is what "Show All
    // Panels" does to registered panels, and note that it is not "show all of
    // them": the built-in reset is `panels_ = PanelVis{}`, which leaves the
    // off-by-default ones off, and a panel that asked to start hidden means it.
    void ResetToDefaults();

private:
    struct Entry {
        std::unique_ptr<IEditorPanel> panel;
        bool visible = false;
    };
    std::vector<Entry> entries_;
};

#ifdef CSE_EDITOR_TITLE_PANELS
// DEFINED BY THE TITLE, not by the editor. See the note at the top of this
// file: the macro arrives as an INTERFACE compile definition on the title's
// editor library, so this declaration exists exactly when something is linked
// that defines it.
//
// Called once, during editor startup, before the first frame.
void RegisterTitlePanels(PanelRegistry& registry);
#endif

} // namespace editor
