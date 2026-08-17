// The combo-prover panel: a decision procedure's verdict, in front of a
// designer.
//
// WHERE THIS LIVES, AND WHY IT MOVED. It was Editor/src/panels/ComboProverPanel.h
// and it was compiled into the general-purpose editor, which linked CseData to
// make it build. This is a fighting-game authoring tool: it knows what a move,
// a cancel and a resource are, and the editor is meant to host more than one
// title. So it now belongs to the title, and reaches the editor through the
// registry in Editor/src/EditorPanel.h -- the wrapper that presents it as an
// editor::IEditorPanel is in UntitledFighterPanels.cpp beside this file. NOT ONE
// LINE OF THE PANEL ITSELF CHANGED in that move, which is the property the seam
// was designed to have: an extension point you have to rewrite a panel to fit is
// not an extension point.
//
// docs/ARCHITECTURE.md section 5.3 specifies this panel and docs/adr/ADR-001 amends
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
// 7. THE VERDICT IS ABOUT THE FILE. THE DESIGNER IS SHIPPING THE GAME.
//    Note 1 says a verdict without the question it answers is a coin flip, and
//    treats "which stage" as that question. It is not the only one. WHICH
//    ENGINE is the other, and it was measured on 2026-08-13:
//    `tests/test_ground_truth.cpp` took `fighter_a` -- TERMINATING, with the
//    first ranking certificate in this repository, reading "meter, juggle" --
//    and executed the cycle that certificate is about. THE KERNEL HAS NO
//    JUGGLE. It has no resources at all: `Fighter::meter` exists and nothing
//    writes it. So the `air_mp` self-cancel the model permits FOUR times ran
//    EIGHTEEN times in 200 ticks, defender unable to act on any tick in
//    between. The analysis is sound about the file; the projection from the
//    file to the running game is what loses the thing termination rested on.
//    ARCHITECTURE.md section 5.5 item 4 and ADR-001 section 6.1 record it.
//
//    A TERMINATING verdict carrying a certificate is the single result on this
//    page a designer would trust most, and today it is the one they should
//    trust least. So the panel says so, next to the verdict, and COMPUTES it
//    the way it computes everything else here -- in three parts, each from a
//    different thing it can honestly reach:
//
//      WHAT THE VERDICT RESTS ON   `ProverResult::rankingOrder`: the
//                                  certificate names resource indices into the
//                                  character's own table, so the panel can name
//                                  them without knowing what a resource means.
//                                  With no certificate it falls back to the
//                                  weaker but still honest statement -- these
//                                  are the resources the character moves, and
//                                  the search decided TERMINATING with them in
//                                  play. Note 3 warns against building the page
//                                  around the certificate, and this obeys it:
//                                  the certificate raises the volume, it does
//                                  not gate the check.
//      WHAT THE GAME CARRIES       `BuildFighterData`'s loss table. That is
//                                  CseData's own account of what the projection
//                                  into `MatchData` drops, written by the file
//                                  that performs the projection, so the day the
//                                  kernel grows resources this warning turns
//                                  ITSELF off. A hardcoded "the kernel has no
//                                  juggle" would still be on screen a year
//                                  after it stopped being true, which is the
//                                  same failure mode as the dirty flag in note
//                                  5 and is rejected for the same reason.
//      WHETHER IT ACTUALLY BITES   `AnalyseCharacter`, run a SECOND time over a
//                                  copy of the character with every move and
//                                  cancel `effect` and `guard` emptied and the
//                                  resource declarations left alone -- which is
//                                  precisely "the pool exists and nothing moves
//                                  it", the kernel's actual state. If that run
//                                  still says TERMINATING, the omission cannot
//                                  reach this character and the panel says so
//                                  quietly. If it says INFINITE, the loop it
//                                  prints is what the verdict above was buying
//                                  with a resource the game does not have. On
//                                  `fighter_a` it answers INFINITE with the loop
//                                  `air_mp` -- the same self-cancel the kernel
//                                  went on to perform 18 times.
//
//    WHAT IT CANNOT SEE, and says instead of guessing:
//      - It does not simulate anything. The second run is the same decision
//        procedure asked a second question, NOT the kernel; the kernel differs
//        in other ways and in both directions (it takes contact-gated edges
//        early, it ignores stance, and an edge whose resolved window is empty
//        is inert in the game and live in the model). The projection-loss
//        section below is where those live, and the text says so.
//      - The loss table counts OBJECTS, not resources, so the panel cannot say
//        "juggle specifically did not cross" -- only that the kernel carries no
//        resource deltas at all and juggle is one of them. It says the weaker
//        sentence.
//      - It reads only the loss entries whose inputs the note-5 fingerprint
//        covers (`resources`, `move.effect`, `move.guard`, `cancel.effect`,
//        `cancel.guard`). `move.pushback` is deliberately not fingerprinted, so
//        drawing the whole table here would put a count on screen that the
//        cache is allowed to let go stale. That is also why this panel does not
//        simply become a view of `BuildReport`.
//      - It finds those entries BY NAME, which is a string contract with
//        `MatchBuilder.cpp` and the one part of this that can rot silently. So
//        it counts how many of the five it found, and finding none is reported
//        as a broken check rather than as good news.
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
    // The default is the same "Exported" the editor's other relative paths use,
    // and it resolves next to the executable because the character files are
    // STAGED to `Exported/Characters` beside it. Their SOURCE moved out of the
    // engine's asset root to `Games/UntitledFighter/Assets/Characters` when this
    // title took ownership of its own content; the staged path is deliberately
    // unchanged, because `stage_runtime_assets.cmake` layers every asset root's
    // subdirectories into the one runtime `Exported/` and a title's
    // `Characters/` lands exactly where the engine's used to. So this panel, the
    // game mode and five tests all go on resolving the same relative path, and
    // nothing here had to learn a second place to look.
    //
    // They were briefly in a top-level `Exported/Characters` instead, which is
    // NOT an asset root and was never staged; the panel then found nothing
    // unless the editor happened to be run from the repository root. Moved
    // rather than special-cased, because two directories called Exported is a
    // trap regardless of which one any given tool reads.
    //
    // A wrong root stays visible rather than silent: the panel prints the
    // loader's own error, which names the file.
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
    // Note 7. Runs from inside runIfStale_, so it is cached against the same
    // fingerprint as the verdict and cannot outlive an edit to the character.
    void checkResourceGap_(const cse::data::CharacterData& character);

    void drawSource_(const cse::data::CharacterData* external);
    void drawStageBanner_(const cse::data::CharacterData& character);
    void drawVerdict_(const cse::data::CharacterData& character, ComboProverActions& actions);
    // Drawn from inside drawVerdict_ rather than as a section of its own: the
    // whole point of note 7 is that this sits NEXT TO the verdict, in the same
    // block of text, where nobody can read one without the other.
    void drawResourceGap_(const cse::data::CharacterData& character,
                          ComboProverActions& actions);
    void drawDaily_(const cse::data::CharacterData& character, ComboProverActions& actions);
    void drawProjection_();
    void drawFooter_(const cse::data::CharacterData& character);

    void drawDeadCancels_(const cse::data::CharacterData& character,
                          const std::vector<cse::data::ProverDeadCancel>& list,
                          ComboProverActions& actions);
    void drawSequence_(const cse::data::CharacterData& character,
                       const std::vector<cse::data::MoveIndex>& sequence,
                       ComboProverActions& actions);
    // A SET of moves, wrapped to the panel width -- no arrows, because these
    // have no order. Shared by the unreachable list and by note 7's spenders.
    void drawMoveButtons_(const cse::data::CharacterData& character,
                          const std::vector<cse::data::MoveIndex>& moves,
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
    //
    // The default names THE PROJECT'S OWN CHARACTER. It used to name
    // kung_fu_man.json, and that stopped being a file the editor can open the
    // moment the transcribed MUGEN corpus moved to tests/fixtures/characters --
    // those three are evidence rather than content and are deliberately not
    // staged, so the panel would have greeted every designer with a load error
    // for a file they had done nothing wrong to lose. A default path is a
    // promise that something is there.
    char pathBuf_[260] = "Characters/fighter_a.json";

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

    // --- note 7: does this verdict survive the engine? ----------------------
    //
    // Filled in by checkResourceGap_ from the same inputs the fingerprint
    // covers, and therefore invalidated by exactly the edits that invalidate the
    // verdict it qualifies. A separate cache with its own staleness rule would
    // be a second thing to forget.
    //
    // PLAIN DATA, deliberately. The kernel-side half of this comes from
    // MatchBuilder, whose header pulls in cse/kernel/Combat.h; keeping a
    // BuildReport as a member would put the kernel's structs into every
    // translation unit that includes this panel. This header has stayed free of
    // comboprover.hpp for the same reason, so the direction of each loss is
    // copied out as the string BuildLossDirectionName already produces.
    struct ResourceGap {
        // Whether the check applied at all. False unless the verdict is
        // TERMINATING: an INFINITE verdict is not made worse by an engine more
        // permissive than the model, and an UNRESOLVED one never proved a bound
        // for a resource to be holding up. TERMINATING is the direction that
        // costs the shipped game, so it is the only one worth qualifying.
        bool ran = false;

        // What the verdict rests on. `certified` is the strong case -- these are
        // the resources ProverResult::rankingOrder names -- and the weak case is
        // "these are the resources the character moves, and the search decided
        // with them in play", which is all the panel can honestly say without a
        // certificate. Both are worth printing; only one is a proof.
        bool certified = false;
        std::vector<cse::data::ResourceIndex> restsOn;

        // The moves whose effect SPENDS one of `restsOn`, and how many cancels
        // carry resource data of their own. "Which move is doing this" is the
        // first thing a designer asks, and moveButton_ can answer it.
        std::vector<cse::data::MoveIndex> spenders;
        std::int32_t edgesWithResourceData = 0;

        // What the bridge to the kernel reports. `resourceLossesFound` is how
        // many of the five loss entries this check looks for by name were
        // present at all: finding none means MatchBuilder renamed them and this
        // check is broken, which must not read as good news.
        bool        bridgeRan       = false;
        std::string bridgeError;
        bool        playsAsAnalysed = false;
        int         resourceLossesFound = 0;
        struct Drop {
            std::string  field;
            std::string  direction;
            std::int32_t count = 0;
        };
        std::vector<Drop> drops;   // only the entries with a nonzero count

        // And whether removing the mechanism changes the answer. Status is the
        // second run's verdict over the same character with every move and
        // cancel effect and guard emptied.
        bool                    counterfactualRan    = false;
        cse::data::ProverStatus counterfactualStatus = cse::data::ProverStatus::Unknown;
        std::vector<cse::data::MoveIndex> counterfactualLoop;
        std::string             counterfactualError;
    };
    ResourceGap gap_;

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
    // Note 7's cost, kept OUT of the four numbers above rather than folded into
    // them. Those four are the measurement section 5.5 item 2 asks for -- the
    // latency of `analyse` over a real authoring session -- and quietly adding a
    // second analysis plus a bridge build to them would make this panel's
    // published figure incomparable with every other measurement of the same
    // call. It is shown beside them instead, so the page still tells the truth
    // about what it costs per edit.
    double lastGapMs_  = 0.0;

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
