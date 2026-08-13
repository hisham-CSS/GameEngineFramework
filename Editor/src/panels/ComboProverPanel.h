// The combo-prover panel: a decision procedure's verdict, in front of a
// designer.
//
// docs/ARCHITECTURE.md section 5.3 specifies this panel and docs/ADR-001 amends
// it. Everything below is why the panel is shaped the way it is, because almost
// every shape here is a correction of a draft that was wrong in a way you would
// not notice by looking at it.
//
// 1. IT NAMES WHERE THE FIGHTERS ARE STANDING, BEFORE IT NAMES A VERDICT.
//    `comboprover.hpp:15-24` scopes itself to a defender pinned against the
//    wall: no distance between the players, therefore no walking forward to
//    stay in range. Phase 0 ran every character twice and THE VERDICT DIFFERS
//    -- Kung Fu Man is TERMINATING midscreen and INFINITE in the corner, and
//    both answers are correct because they are two different questions. All
//    three shipped characters declare `"stage": "midscreen"` in the file while
//    the in-engine answer is the corner one. A panel that prints "the" verdict
//    without saying which question it answered is showing a coin flip, so the
//    stage banner is drawn ABOVE the verdict and it is not collapsible.
//
// 2. THREE STATES, NOT TWO. UNRESOLVED is an answer: the search has a budget
//    (ProverOptions::limit) and an unfinished search dressed up as a clean bill
//    of health is worse than no answer at all. It gets the same visual weight
//    as the other two and a control that raises the budget.
//
// 3. THE RANKING CERTIFICATE IS THE UNCOMMON CASE, SO THE PANEL IS NOT BUILT
//    AROUND IT. `spendOnly` is cleared by any reachable cancel with a positive
//    resource effect, and the certificate is gated on it, so every character
//    that builds meter on hit is TERMINATING with no certificate. ADR-001
//    section 3 gate 3 goes further: the FOUR reasons a certificate can be
//    missing mean four different things, and one of them (NothingLoops) is the
//    strongest termination result the tool can produce. Rendering them
//    identically throws that away, so each one gets its own sentence.
//
// 4. THE DAILY HALF IS THE TOP HALF. Dead cancels, unreachable moves, the
//    settling index and the usable-cancel count are free, they are computed
//    whatever the verdict, and ADR-001 says plainly that they "land before
//    anyone cares about the theorem". They are drawn directly under the verdict
//    with no collapsing header in front of them. The projection-loss table --
//    the part that is about the tool rather than about the character -- is the
//    one that collapses.
//
// 5. IT RUNS WHEN THE DATA CHANGES, NOT WHEN THE FRAME DOES.
//    Section 5.3 asks for a re-run on every edit, and ADR-001 measured
//    0.033-0.041 ms per corner run, so per-edit is affordable. Per-FRAME is not
//    the same thing: a docked panel draws 60-300 times a second whether or not
//    anything was typed, and the cost that matters is not the average character
//    but the worst one -- `analyse` is bounded by `limit` (200000 positions),
//    not by 0.04 ms, and a character that reaches the cap costs whatever the cap
//    costs, every frame, forever.
//
//    So the panel takes NO change notification from the editor and does not want
//    one. It folds the character into a 64-bit fingerprint every draw -- one
//    pass over moves, cancels, resources and gap actions, integer mixing, no
//    allocation, orders of magnitude cheaper than the search -- and re-runs only
//    when the fingerprint moves. The fingerprint covers exactly what
//    `AnalyseCharacter` can observe, INCLUDING the fields that feed only the
//    projection-loss table, because a loss count is part of the answer. The
//    options are folded in too, and that is what makes "raise the budget" work:
//    it edits `limit`, the fingerprint moves, the re-run happens down the
//    ordinary path, and there is no second code path that could rot.
//
//    The road not taken is a dirty flag set by whoever edits the character. It
//    is cheaper per frame and it is wrong the first time somebody adds a second
//    edit path and forgets to set it -- and the failure is SILENT, a stale
//    verdict that looks live. A fingerprint cannot forget.
//
// 6. SYNCHRONOUS, AND THE AMENDMENT IS WHY. Section 5.3 as first written says
//    "asynchronously with a budget". ADR-001 amended that after measuring:
//    corner runs are 0.033-0.041 ms in C++ and "fast enough for C++
//    synchronously"; it is MIDSCREEN, 147-226 ms in Python, that needs the async
//    budget and a cancel-and-supersede path. `ProverStage` has exactly one
//    member today. This panel answers the corner and blocks for 0.04 ms doing
//    it. The day a second `ProverStage` member appears, every switch over it
//    stops compiling, and THAT is the moment to add the worker -- not before,
//    because an async path with nothing slow behind it is a race condition with
//    no benefit.
//
// It does not parse JSON. `CseData` owns the file format and the path sandbox;
// this panel calls `LoadCharacterFile` or is handed a `CharacterData` the editor
// already has.
#pragma once

#include "cse/data/CharacterData.h"
#include "cse/data/ProverAdapter.h"

#include <cstdint>
#include <string>
#include <vector>

// What the panel wants done and cannot do itself, in the shape of
// AssetBrowserActions: the panel EMITS intents and the editor executes them. A
// panel that reached into the editor's selection would have to know what the
// editor is.
struct ComboProverActions {
    // The id of a move whose button was clicked -- in the printed loop, in the
    // dead-cancel list, or in the unreachable list. Section 5.3 asks for the
    // loop to be "clickable to jump to each move"; there is no move browser to
    // jump TO yet, so the panel reports the click and the editor decides what
    // revealing a move means. Ignoring this field costs nothing and loses
    // nothing else on the page.
    std::string jumpToMoveId;
};

class ComboProverPanel {
public:
    // The sandbox root that a typed character path is resolved against.
    // `LoadCharacterFile` refuses absolute paths, drive/UNC roots and any ".."
    // component lexically, before touching the filesystem -- authored content is
    // untrusted (docs/MAINTENANCE.md) and this panel is a place where a human
    // types a path, which is the definition of untrusted.
    //
    // The default is the same "Exported" the editor's other relative paths use.
    // NOTE for whoever wires this up: the character files live in the REPOSITORY
    // root's `Exported/Characters`, which `stage_runtime_assets.cmake` does not
    // copy next to the executable -- it stages `Editor/src/Exported`, and that
    // tree has no Characters folder. Either stage them or call this with a path
    // that reaches the source tree. A wrong root is visible rather than silent:
    // the panel prints the loader's own error, which names the file.
    void SetContentRoot(std::string baseDir) { contentRoot_ = std::move(baseDir); }

    // The build-wide resource ORDER, which is assertion A03 and is not a
    // per-file choice. The prover keys its resource vector POSITIONALLY, so a
    // character whose index 0 is `juggle` compares juggle against meter and says
    // nothing at all about it. No single file can check a cross-file rule, so
    // the caller has to supply the build's order; leaving it empty does not pass
    // the check, it SKIPS it, and the panel shows the resulting warning in the
    // same colour as everything else that could make the verdict meaningless.
    void SetExpectedResources(std::vector<std::string> names) {
        expectedResources_ = std::move(names);
    }

    // `character` may be null: the panel then draws whatever it loaded itself
    // from the path field, and an editor with no character-owning system yet can
    // pass nullptr and still get the whole page. When it is non-null it WINS --
    // the editor's live character is the one a designer is editing, and the
    // panel's own copy is a convenience for the day before that system exists.
    //
    // pOpen (optional) drives the window's close button; the editor gates the
    // whole Draw on the same bool so the tab X hides the panel.
    ComboProverActions Draw(const cse::data::CharacterData* character,
                            bool* pOpen = nullptr);

    // The character the panel loaded itself, or null. Exposed so the editor can
    // adopt it rather than load a second copy.
    const cse::data::CharacterData* loadedCharacter() const {
        return ownedLoaded_ ? &owned_ : nullptr;
    }

private:
    void loadFromPath_();
    void runIfStale_(const cse::data::CharacterData& character);

    void drawSource_(const cse::data::CharacterData* external);
    void drawStageBanner_(const cse::data::CharacterData& character);
    void drawVerdict_(const cse::data::CharacterData& character, ComboProverActions& actions);
    void drawDaily_(const cse::data::CharacterData& character, ComboProverActions& actions);
    void drawProjection_();
    void drawFooter_(const cse::data::CharacterData& character);

    void drawDeadCancels_(const cse::data::CharacterData& character,
                          const std::vector<cse::data::ProverDeadCancel>& list,
                          ComboProverActions& actions);
    void drawSequence_(const cse::data::CharacterData& character,
                       const std::vector<cse::data::MoveIndex>& sequence,
                       ComboProverActions& actions);
    // One clickable move id. Every one of these needs its own ImGui id: a loop
    // that revisits a move draws the same button label twice, and two widgets
    // with the same id are ONE widget as far as ImGui is concerned -- the second
    // press would activate the first. widgetId_ is the counter that prevents it.
    void moveButton_(const cse::data::CharacterData& character,
                     cse::data::MoveIndex move, ComboProverActions& actions);

    std::string contentRoot_ = "Exported";
    std::vector<std::string> expectedResources_;

    // The path field. A fixed buffer because ImGui::InputText wants one; 260 is
    // the same width the asset drag-drop payload uses.
    char pathBuf_[260] = "Characters/kung_fu_man.json";

    // The panel's own copy, used only when the caller passes nullptr.
    cse::data::CharacterData owned_;
    cse::data::LoadReport    ownedReport_;
    bool                     ownedLoaded_ = false;
    bool                     loadAttempted_ = false;

    // The cached answer and the fingerprint of the input that produced it. See
    // note 5 at the top of this file. haveResult_ is separate from a zero
    // fingerprint because 0 is a perfectly reachable hash value and "never ran"
    // must not be confused with "ran and hashed to zero".
    cse::data::ProverOptions options_;
    cse::data::ProverResult  result_;
    cse::data::ProverReport  report_;
    bool                     haveResult_  = false;
    bool                     resultOk_    = false;   // AnalyseCharacter returned true
    std::uint64_t            fingerprint_ = 0;

    // Bumped by the Re-run button and folded into the fingerprint, so a forced
    // run goes down the same path as a data change instead of needing its own.
    std::uint32_t forceNonce_ = 0;

    // ARCHITECTURE.md section 5.5 item 2 asks for a latency distribution over a
    // real authoring session, not a single number from a benchmark. These four
    // are that measurement, gathered for free by the panel that is already
    // running the analysis, and they are shown in the footer.
    int    runs_       = 0;
    double lastRunMs_  = 0.0;
    double worstRunMs_ = 0.0;
    double totalRunMs_ = 0.0;

    // Which dead-cancel list is on screen. PRE-DECAY by default, and ADR-001's
    // amendment to 5.3 is the reason: both implementations evaluate every edge
    // at the SETTLED hitstun, so a decay rule reports real cancels as dead --
    // 128 of Kung Fu Girl's 134 under this project's own draft house rule. The
    // settled list is the model's; the pre-decay list is the one that blames the
    // designer only for what the designer actually authored.
    bool showSettledDeadCancels_ = false;
    // Projection losses with count 0 are hidden by default and revealed by this.
    // They are worth having: a check that ran and found nothing is different
    // from a check that does not exist, and ADR-001 records exactly that about
    // the projectile contact frame.
    bool showInertLosses_ = false;

    // Reset at the top of every Draw; see moveButton_.
    int widgetId_ = 0;
};
