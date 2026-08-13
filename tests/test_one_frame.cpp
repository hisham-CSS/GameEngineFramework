// ONE FRAME: the sharpest claim this project can make about the analysis.
//
// tests/test_ground_truth.cpp took a character the prover calls INFINITE and
// executed its printed loop. This file asks the question from the other end, and
// it is the question a designer actually lives with: how close is a SAFE
// character to being an unsafe one? A combo prover that only answers characters
// which are obviously broken is a linter. A combo prover whose answer turns on
// the smallest mistake the genre has -- a cancel window one frame too wide -- is
// a tool.
//
// `fighter_a` authors, deliberately, a probe: `crouch_lp` cancelling into ITSELF
// at delay 10, marked `kind: "link"` and captioned "AUTHORED TO BE REPORTED
// DEAD ... the probe is dead by exactly one frame and that margin IS the
// character's finiteness". This file moves that one integer and measures what
// happens, in both systems that read it.
//
// ---------------------------------------------------------------------------
// THE ARITHMETIC WAS CHECKED RATHER THAN INHERITED, AND IT DID NOT SURVIVE
// ---------------------------------------------------------------------------
// The brief this file was written from stated the kernel window as
//
//     [startup + delay, min(hitstun, duration - 1)]  =  [13, 12],  empty by 1
//
// and predicted that 10 -> 8 would open it. Neither is what MatchBuilder.cpp
// does. `buildCancels` computes
//
//     earliest = startup + delay,     raised to `cancelWindowOpen`  if authored
//     latest   = duration - 1,        lowered to `cancelWindowClose` if authored
//
// and HITSTUN APPEARS NOWHERE IN IT. `crouch_lp` authors
// `engine.cancel_window_ticks: [5, 9]`, so its `latest` is 9, not 12, and the
// authored probe resolves to [13, 9] -- empty by FOUR frames of window, not one.
// A delay of 8 resolves to [11, 9] and is still empty. Every number below comes
// from MatchBuilder itself for exactly this reason; the test asks the bridge for
// the window rather than re-deriving one beside it.
//
// The one-frame claim survives the correction. It simply lives somewhere else,
// and finding out where is the whole content of this file.
//
// ---------------------------------------------------------------------------
// WHAT IT MEASURED: TWO ONE-FRAME BOUNDARIES, THREE FRAMES APART
// ---------------------------------------------------------------------------
// The two systems that read `Cancel::delay` do not read it the same way.
//
//   THE ANALYSIS.  ProverAdapter.cpp `edgeUsable` keeps an edge when
//                  `startup(to) <= settledHitstun(from) - delay`. For this probe
//                  that is `3 <= 12 - delay`, so the edge goes live at delay 9.
//                  The character is TERMINATING at 10 and INFINITE at 9.
//                  ONE FRAME, on one integer, out of 18 moves and 73 cancels.
//
//   THE GAME.      The kernel takes an edge when `moveFrame` lands inside
//                  MatchBuilder's resolved window, and that window is closed by
//                  the move's authored `cancel_window_ticks`, not by hitstun. So
//                  it opens at `3 + delay <= 9`, i.e. at delay 6. The kernel
//                  performs the loop at 6 and cannot touch it at 7.
//                  ONE FRAME AGAIN -- a different one, three frames further out.
//
// Between them sits a band -- delays 7, 8 and 9 -- in which THE PROVER REPORTS
// AN INFINITE THE GAME CANNOT PERFORM. That is the CONSERVATIVE direction in
// ProverAdapter.h's vocabulary and the exact mirror of test_ground_truth.cpp's
// section 5, where the prover certified a character TERMINATING and the kernel
// looped its `air_mp` eighteen times. Both directions of the D8 model/game gap
// are now measured, on ONE edge, by moving ONE integer -- which is a stronger
// statement of ARCHITECTURE.md D8 than either file makes alone, because it
// shows the two errors are not opposite hazards in different characters but
// adjacent values of the same field.
//
// So the shipped character is ONE frame from an infinite the tool would report
// and FOUR frames from one a player could perform, and those are different
// numbers for a reason nothing in the file says out loud.
//
// The sweep, measured (120 ticks, silent defender, bodies 8 px apart):
//
//   delay  window   prover       kernel
//   0..2   [ 5, 9]  INFINITE     connects every 5 ticks, defender never free
//   3      [ 6, 9]  INFINITE     connects every 6
//   4      [ 7, 9]  INFINITE     connects every 7
//   5      [ 8, 9]  INFINITE     connects every 8
//   6      [ 9, 9]  INFINITE     connects every 9, defender never free   <-- game
//   7..9   [10..12, 9]  INFINITE INERT: the move runs out, restarts every 14,
//                                and the defender is free between every pair
//   10     [13, 9]  TERMINATING  INERT                                   <-- SHIPPED
//   11..16 empty    TERMINATING  INERT
//
// Two things in that table are worth a designer's attention beyond the
// boundaries themselves. Delays 0, 1 and 2 are INDISTINGUISHABLE in the kernel,
// because `cancelWindowOpen` of 5 clamps all three to the same frame -- the
// file's own window is doing the work an author might think the delay is doing.
// And there is no "opens but does not connect" row at all: `cancelWindowClose`
// of 9 is three ticks inside `crouch_lp`'s own 12 ticks of hitstun, so on THIS
// edge "the window opened" and "the follow-up lands in hitstun" are the same
// sentence. The classifier below still tests for the middle case, because it is
// a property of these numbers rather than of the rule, and a character with a
// later window would land in it.
//
// ---------------------------------------------------------------------------
// WHY THE CHARACTER IS MUTATED IN MEMORY AND NOT COPIED TO A THIRD FILE
// ---------------------------------------------------------------------------
// The claim is that ONE INTEGER does this. A second character file could always
// differ somewhere the reader did not check, and "everything else is identical"
// would be a promise rather than a fact. Mutating the shipped document through
// nlohmann::json -- the technique tests/test_character_data.cpp uses to make
// every load assertion fail on purpose -- holds the rest identical BY
// CONSTRUCTION, and section 1 then measures that it did: the round trip with no
// mutation must reproduce the shipped verdict exactly, and the mutated character
// must differ from it in one field of one cancel and nowhere else.
//
// It also keeps a deliberately-broken character out of Editor/src/Exported,
// which is a shipping asset directory, and out of the Editor's character list.
//
// NOTHING HERE REPAIRS THE FILE'S PROSE. The mutated document still carries
// `engine.delay_derivation: "...DEAD, short by 1"` on an edge that is no longer
// dead. That is deliberate: every assertion below reads the loaded NUMBERS, and
// a test that also rewrote the commentary would be quietly asserting that the
// loader reads it. It does not.
//
// This test links CseData AND CseKernel, like test_ground_truth and
// test_match_bridge, and additionally nlohmann_json -- which tests/CMakeLists.txt
// must name explicitly, because that target is created by find_package in
// Engine/'s scope and a test linking only CseData/CseKernel never sees it.
#include <gtest/gtest.h>

#include "cse/data/CharacterData.h"
#include "cse/data/MatchBuilder.h"
#include "cse/data/ProverAdapter.h"
#include "cse/kernel/Simulate.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace cse::data;
using cse::kernel::GameState;
using cse::kernel::InputPair;
using cse::kernel::MatchData;
using json = nlohmann::json;

namespace {

// ============================================================================
// The subject
// ============================================================================

const char* kSubjectFile = "fighter_a.json";

// The move the probe leaves and returns to. Named once, because every assertion
// below is about this one edge and a reader should be able to change the subject
// in one place if the character is ever re-authored around a different probe.
const char* kProbeMove = "crouch_lp";

// The build-wide positional resource order (ADR-001 section 8 item 7). Passing
// it is what arms load assertion A03; without it the loader warns that A03 was
// not checked at all.
const std::vector<std::string> kBuildResources = { "meter", "juggle" };

LoadOptions loadOptions() {
    LoadOptions o;
    o.expectedResources = kBuildResources;
    return o;
}

ProverOptions proverOptions() {
    ProverOptions o;
    o.expectedResources = kBuildResources;
    return o;
}

// ============================================================================
// Finding the character on disk
// ============================================================================

// The same two-stage walk-up test_ground_truth.cpp uses, and duplicated for the
// same reason it is duplicated there: each test is its own executable and this
// repository has no test-support library, so the alternative is a shared header
// that exists to be included twice.
//
// `Exported/Characters` first -- the staged copy, which is the artifact that
// would actually ship -- then the source tree, so the test still runs from a
// shell anywhere in the repository. Neither can make anything pass vacuously,
// because every load below ASSERTs.
std::string charactersDir() {
#ifdef CSE_CHARACTERS_DIR
    return CSE_CHARACTERS_DIR;
#else
    namespace fs = std::filesystem;

    fs::path here = fs::current_path();
    for (int i = 0; i < 8; ++i) {
        const fs::path staged = here / "Exported" / "Characters";
        if (fs::exists(staged / kSubjectFile)) return staged.string();
        if (!here.has_parent_path() || here.parent_path() == here) break;
        here = here.parent_path();
    }

    here = fs::current_path();
    for (int i = 0; i < 8; ++i) {
        const fs::path source = here / "Editor" / "src" / "Exported" / "Characters";
        if (fs::exists(source / kSubjectFile)) return source.string();
        if (!here.has_parent_path() || here.parent_path() == here) break;
        here = here.parent_path();
    }

    return "Exported/Characters";
#endif
}

// The shipped file's TEXT. Read directly rather than through LoadCharacterFile,
// because the mutation happens on the document and not on the loaded struct:
// CharacterData is what the loader PRODUCED, and editing it would skip every
// check the loader performs on the way. A mutated document goes back through the
// whole loader, so a delay this file invents is subject to the same validation
// as one a designer types.
std::string subjectText() {
    const std::filesystem::path p = std::filesystem::path(charactersDir()) / kSubjectFile;
    std::ifstream in(p, std::ios::binary);
    EXPECT_TRUE(in.good())
        << "cannot open " << p.string()
        << "\nThis test reads the STAGED shipping tree. If it is missing, "
           "tests/CMakeLists.txt has not given this target its dependency on "
           "test_runtime_deps.";
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

json subjectDoc() {
    const std::string text = subjectText();
    json doc = json::parse(text.begin(), text.end(), nullptr, false);
    EXPECT_FALSE(doc.is_discarded()) << kSubjectFile << " is not valid JSON";
    return doc;
}

// Where the probe sits in `doc["cancels"]`. FOUND, not written down as 71: an
// index is the most fragile thing a test can hardcode about a file somebody
// else is still editing, and the edge is uniquely identified by its endpoints.
// A file that grew a second self-cancel on this move would make the claim
// ambiguous, so that is an error rather than a first-wins pick.
bool findProbe(const json& doc, std::size_t& out, std::string& why) {
    const json* cancels = doc.contains("cancels") ? &doc["cancels"] : nullptr;
    if (cancels == nullptr || !cancels->is_array()) {
        why = "the document has no `cancels` array";
        return false;
    }
    int found = 0;
    for (std::size_t i = 0; i < cancels->size(); ++i) {
        const json& e = (*cancels)[i];
        // Every member checked before it is read. nlohmann's `value()` throws on a
        // type mismatch, and this file is not entitled to throw where the loader
        // deliberately does not -- CharacterData.cpp parses with
        // allow_exceptions = false precisely so authored content cannot unwind.
        if (!e.is_object()) continue;
        if (!e.contains("from") || !e["from"].is_string()) continue;
        if (!e.contains("to")   || !e["to"].is_string())   continue;
        if (e["from"].get<std::string>() != kProbeMove) continue;
        if (e["to"].get<std::string>()   != kProbeMove) continue;
        out = i;
        ++found;
    }
    if (found == 0) {
        why = std::string("no cancel from `") + kProbeMove + "` into itself. This "
              "file is about that one probe, and the character no longer authors it.";
        return false;
    }
    if (found > 1) {
        why = std::string("there are ") + std::to_string(found) + " cancels from `" +
              kProbeMove + "` into itself, so `the one integer` names nothing.";
        return false;
    }
    return true;
}

// ============================================================================
// One delay, taken all the way through
// ============================================================================

// file text -> document -> loader -> prover -> kernel, for a single value of the
// probe's delay. Everything a test wants to say about a delay is in here, and it
// is all measured through the real seams: the loader parses the mutated
// document, ProverAdapter answers it, MatchBuilder resolves the window.
struct Attempt {
    std::int32_t  delay = 0;         // read back off the loaded character, not remembered

    CharacterData character;
    ProverResult  verdict;
    MatchBuild    build;

    CancelIndex   fileCancel = 0;    // index into character.cancels
    bool          crossed    = false;// the edge reached FighterData::cancels at all
    std::int32_t  earliest   = 0;    // MatchBuilder's resolved window, READ OFF THE
    std::int32_t  latest     = 0;    // BUILT EDGE rather than recomputed here

    bool Inert() const { return earliest > latest; }

    std::string Window() const {
        std::ostringstream s;
        s << "[" << earliest << ", " << latest << "]";
        if (Inert()) s << " EMPTY";
        return s.str();
    }
};

// The body is the CALLER's number and is supplied deliberately: CharacterData
// carries no body at all (MatchBuilder.h says why), so leaving it out would make
// MatchBuilder's documented default look like the character's own measurement.
// fighter_a's own `engine.constants.default_pushbox_sub` is [-3328, -15360,
// 3328, 0] with `height_px` 60, and 3328 == 13 px, 15360 == 60 px -- so these
// are the file's figures restated in the unit this test reads in.
constexpr std::int32_t px(std::int32_t pixels) {
    return pixels * cse::kernel::kSubUnitsPerPixel;
}
constexpr std::int32_t kHalfWidth = px(13);
constexpr std::int32_t kHeight    = px(60);

// Origins 34 px apart, so the bodies are (34 - 2*13) = 8 px apart. `crouch_lp`
// reaches 40 px, so no assertion in this file can pass or fail because of
// distance -- which is the point. ResetMatch's own opening is 200 px, a 174 px
// body gap, further than anything this character reaches, so every measurement
// would be about an empty room.
//
// NOBODY MOVES. The attacker holds an attack button and no direction, so
// stepFighter sets velX to 0; the defender is in hitstun, which zeroes velX
// anyway; and the kernel applies no pushback on hit at all (MatchBuilder's
// `move.pushback` row: "the kernel moves nobody on hit"). The gap is 8 px on
// every tick of every run below.
constexpr std::int32_t kP0X = -px(17);
constexpr std::int32_t kP1X =  px(17);

BodySpec body() {
    BodySpec b{};
    b.halfWidthSub = kHalfWidth;
    b.heightSub    = kHeight;
    return b;
}

MoveBinding bind(std::string id, std::uint16_t button) {
    MoveBinding b{};
    b.moveId = std::move(id);
    b.button = button;
    return b;
}

// Both sides get the same bindings, so one MatchData serves both the silent and
// the mashing defender. A defender handed no bindings could not act even when
// actionable, and "the defender never acted" would then be a fact about the
// harness rather than about the combo.
bool buildMirror(const CharacterData& c, const std::vector<MoveBinding>& bindings,
                 MatchBuild& out) {
    BuildOptions options{};
    options.body     = body();
    options.bindings = bindings;
    return BuildMatchData(c, options, c, options, out);
}

// Returns false with a reason rather than asserting, because the sweep calls
// this seventeen times inside a loop: gtest's ASSERT_ returns from the function
// it is written in, so a fatal assertion in a helper aborts the helper and lets
// the loop carry on with a half-built result.
bool bringUp(const json& base, std::size_t probe, bool mutate, std::int32_t delay,
             const std::vector<MoveBinding>& bindings, Attempt& out, std::string& why) {
    out = Attempt{};

    json doc = base;
    if (mutate) doc["cancels"][probe]["delay"] = delay;

    // Back through the WHOLE loader, including every A0x assertion. `dump()`
    // rather than string surgery on the file text, for test_character_data.cpp's
    // reason: a test that edits JSON with find-and-replace stops testing what it
    // claims the moment somebody reformats the file.
    LoadReport load{};
    if (!LoadCharacterJson(kSubjectFile, doc.dump(), loadOptions(), out.character, load)) {
        why = "the mutated document did not load. rule: " +
              (load.rule.empty() ? std::string("(none named)") : load.rule) +
              " error: " + load.error;
        return false;
    }
    if (!load.rule.empty()) {
        why = "the mutated document loaded but named load assertion " + load.rule;
        return false;
    }

    ProverReport report{};
    if (!AnalyseCharacter(out.character, proverOptions(), out.verdict, report)) {
        why = "the mutated character could not be projected into the decision "
              "procedure. rule: " + report.rule + " error: " + report.error;
        return false;
    }

    if (!buildMirror(out.character, bindings, out.build)) {
        why = "the mutated character did not build: p0 " + out.build.report[0].error +
              " / p1 " + out.build.report[1].error;
        return false;
    }

    // The probe, located in the LOADED character by the same endpoint rule used
    // on the document. Located again rather than assumed to be at `probe`,
    // because "the loader preserves file order" is a property worth checking
    // once here instead of relying on everywhere.
    const MoveIndex move = out.character.FindMove(kProbeMove);
    if (move == kInvalidMove) {
        why = std::string("the loaded character has no move `") + kProbeMove + "`";
        return false;
    }
    int found = 0;
    for (std::size_t i = 0; i < out.character.cancels.size(); ++i) {
        const Cancel& e = out.character.cancels[i];
        if (e.from == move && e.to == move) {
            out.fileCancel = static_cast<CancelIndex>(i);
            ++found;
        }
    }
    if (found != 1) {
        why = "the loaded character has " + std::to_string(found) + " self-cancels on `" +
              kProbeMove + "`; exactly one is what this file is about";
        return false;
    }
    if (out.fileCancel != static_cast<CancelIndex>(probe)) {
        why = "the probe is cancels[" + std::to_string(probe) + "] in the document and "
              "cancels[" + std::to_string(out.fileCancel) + "] in the loaded character, "
              "so the loader did not preserve file order and the mutation landed on a "
              "different edge than the one measured";
        return false;
    }

    // Read back rather than remembered, so `Attempt::delay` is the delay this
    // attempt ACTUALLY has -- which for the unmutated control is the file's own
    // number and not the zero the caller had to pass to mean "do not touch it".
    out.delay = out.character.cancels[out.fileCancel].delay;

    // THE WINDOW, ASKED OF MatchBuilder RATHER THAN RE-DERIVED. `fileCancelByEdge`
    // is the inverse of the build's own drop, so this finds the edge the bridge
    // actually produced for this file cancel -- and if the bridge stops producing
    // one, that is visible as `crossed` being false rather than as a window of
    // [0, 0] that happens to read as open.
    //
    // The window does NOT depend on `bindings`: `buildCancels` reads the move's
    // frame data and the edge's delay and nothing else, and BuildOptions reaches
    // it only through the move table. That is why the payoff test may resolve a
    // window from one build and then execute against another built with the
    // witness's bindings.
    if (out.build.moves[0].Find(kProbeMove) == 0) {
        why = std::string("`") + kProbeMove + "` did not reach the kernel's move table";
        return false;
    }
    for (std::size_t k = 0; k < out.build.moves[0].fileCancelByEdge.size(); ++k) {
        if (out.build.moves[0].fileCancelByEdge[k] != out.fileCancel) continue;
        const cse::kernel::CancelEdge& e = out.build.data.p[0].cancels[k];
        out.earliest = e.earliestFrame;
        out.latest   = e.latestFrame;
        out.crossed  = true;
        break;
    }
    if (!out.crossed) {
        why = "the probe did not cross into the kernel at all, so there is no "
              "resolved window to measure. MatchBuilder drops an edge only when an "
              "endpoint names no move slot.";
        return false;
    }
    return true;
}

// ============================================================================
// The trace, and watching a run from outside
// ============================================================================

// Six single bits, and single on purpose. StepAttack takes the first move in
// slot order all of whose bits are held, so a binding whose mask is a superset
// of an earlier one's can never start. No mask here is a subset of any other, so
// the shadowing rule cannot bite any trace in this file.
const std::uint16_t kButtonPool[] = {
    cse::kernel::kInputLP, cse::kernel::kInputMP, cse::kernel::kInputHP,
    cse::kernel::kInputLK, cse::kernel::kInputMK, cse::kernel::kInputHK,
};
constexpr std::size_t kButtonPoolSize = sizeof(kButtonPool) / sizeof(kButtonPool[0]);

const char* buttonName(std::uint16_t bits) {
    switch (bits) {
        case cse::kernel::kInputLP: return "LP";
        case cse::kernel::kInputMP: return "MP";
        case cse::kernel::kInputHP: return "HP";
        case cse::kernel::kInputLK: return "LK";
        case cse::kernel::kInputMK: return "MK";
        case cse::kernel::kInputHK: return "HK";
        case 0:                     return "--";
        default:                    return "??";
    }
}

// THE SWEEP'S BINDING TABLE, AND WHY IT IS NOT DERIVED FROM THE VERDICT.
//
// The sweep asks what the KERNEL does with this edge at each delay, and it has
// to ask that at delays where the prover prints no loop at all -- a trace
// derived from a witness would simply not exist for delay 10. So the sweep binds
// the probe's move to one button and holds it, which is the whole trace a
// self-cancel needs: the loop's target is its own source, so there is no motion
// input, no release and no timing to get right.
//
// The payoff test, which is about the printed loop rather than about the edge,
// derives its bindings from the witness instead. See `bindingsForWitness`.
std::vector<MoveBinding> probeBinding() {
    return { bind(kProbeMove, kButtonPool[0]) };
}

// One button per DISTINCT move a witness names, in first-appearance order. A
// witness naming more distinct moves than there are single-bit buttons gets a
// binding of ZERO for the surplus rather than a shorter list, so the shortfall
// surfaces as a move nothing can ask for rather than as a trace that silently
// stops following the witness.
std::vector<MoveBinding> bindingsForWitness(const std::vector<std::string>& sequence) {
    std::vector<MoveBinding> out;
    for (const std::string& id : sequence) {
        bool already = false;
        for (const MoveBinding& b : out)
            if (b.moveId == id) { already = true; break; }
        if (already) continue;
        const std::uint16_t button =
            out.size() < kButtonPoolSize ? kButtonPool[out.size()] : std::uint16_t{0};
        out.push_back(bind(id, button));
    }
    return out;
}

// The move IDS a verdict names, `prefix` then `loop`. Ids and not indices,
// because a move index is per-character and this file compares characters that
// differ by one field -- but also because an id is what a failure message can
// print without a lookup table beside it.
struct Witness {
    std::vector<std::string> sequence;
    std::size_t              loopStart = 0;

    std::string ToString() const {
        std::ostringstream s;
        if (sequence.empty()) return "(no witness)";
        for (std::size_t i = 0; i < sequence.size(); ++i) {
            if (i) s << " > ";
            if (i == loopStart) s << "[loop] ";
            s << sequence[i];
        }
        return s.str();
    }
};

Witness witnessOf(const CharacterData& c, const ProverResult& r) {
    Witness w{};
    for (MoveIndex m : r.prefix)
        if (m < c.moves.size()) w.sequence.push_back(c.moves[m].id);
    w.loopStart = w.sequence.size();
    for (MoveIndex m : r.loop)
        if (m < c.moves.size()) w.sequence.push_back(c.moves[m].id);
    return w;
}

// A cursor over a sequence of kernel move slots: press the button of the move
// the sequence says comes next, and advance when the attacker actually ENTERS
// that move. Past the end the cursor returns to `loopStart`, which is what makes
// the loop repeat.
//
// Advancing on `moveFrame == 0` rather than on a change of `moveId` is forced
// by the subject: this loop is a move cancelling into ITSELF, so the id never
// changes and a transition detector would see nothing happen at all. The frame
// counter is what resets, so it is what says a move began.
//
// If the attacker never enters the move being asked for, the cursor never
// advances and the trace holds one button forever. That is the correct failure
// mode -- it stalls visibly, and the per-tick table shows what the attacker was
// doing instead.
class Trace {
public:
    Trace(const Witness& w, const MoveIndexMap& map,
          const std::vector<MoveBinding>& bindings)
        : loopStart_(w.loopStart) {
        for (const std::string& id : w.sequence) {
            slots_.push_back(map.Find(id));
            std::uint16_t button = 0;
            for (const MoveBinding& b : bindings)
                if (b.moveId == id) { button = b.button; break; }
            buttons_.push_back(button);
            ids_.push_back(id);
        }
    }

    bool Usable(std::string& why) const {
        for (std::size_t i = 0; i < slots_.size(); ++i) {
            if (slots_[i] == 0) {
                why = "this character has no move called `" + ids_[i] + "`";
                return false;
            }
            if (buttons_[i] == 0) {
                why = "`" + ids_[i] + "` was given no button, so nothing can ask for it";
                return false;
            }
        }
        return !slots_.empty();
    }

    std::uint16_t Bits() const { return buttons_.empty() ? 0 : buttons_[cursor_]; }

    void Observe(std::uint16_t attackerMove, std::uint16_t attackerFrame) {
        if (slots_.empty()) return;
        if (attackerMove != slots_[cursor_] || attackerFrame != 0) return;
        cursor_ = (cursor_ + 1 < slots_.size()) ? cursor_ + 1 : loopStart_;
    }

    std::size_t LoopLength() const { return slots_.size() - loopStart_; }

private:
    std::vector<std::uint16_t> slots_;
    std::vector<std::uint16_t> buttons_;
    std::vector<std::string>   ids_;
    std::size_t                loopStart_ = 0;
    std::size_t                cursor_    = 0;
};

// Silent is the clean measurement: the defender's escapes are read straight off
// Fighter::hitstun with nothing else moving.
//
// MashesOnceHit is the stronger experiment and it has to start LATE. Both sides
// hold the same MatchData, so at tick 0 both are idle and both would start the
// same move, both boxes would go live on the same tick, and ResolveHits -- which
// is deliberately symmetric -- would land BOTH. That is a correct simulation of
// two people mashing at each other and it is not the question. Starting the mash
// on the tick AFTER the combo opens asks the question a player asks: "I have
// been hit; can I get out?"
enum class DefenderPolicy { Silent, MashesOnceHit };

// One tick, observed from outside. Every field is read off the state the
// simulation produced; nothing is inferred from what the script asked for.
struct Sample {
    std::int32_t  tick       = 0;
    std::uint16_t inputBits  = 0;
    std::uint16_t atkMove    = 0;
    std::uint16_t atkFrame   = 0;
    std::uint16_t defStun    = 0;
    std::uint16_t defMove    = 0;
    std::int32_t  defHealth  = 0;
    bool          hit        = false;   // the defender's health fell this tick
    bool          defEntered = false;   // the defender began a move this tick
};

// Named TickLog rather than Run: a TEST body is a member function of a class
// deriving from ::testing::Test, and that class already has a member function
// `Run`. Class-scope lookup wins over the enclosing namespace, so `Run x = ...`
// inside a test body fails with a message about a pointer to member.
struct TickLog {
    std::vector<Sample>       samples;
    std::vector<std::int32_t> hitTicks;
    std::vector<std::int32_t> hitHealth;
    std::vector<std::int32_t> defenderActedTicks;
    std::int32_t              firstHitTick = -1;
    std::int32_t              lastHitTick  = -1;
    GameState                 finalState{};

    std::size_t Hits() const { return hitTicks.size(); }

    // The interval between the first two hits, which for a periodic loop is the
    // loop's period. -1 when there were not two.
    std::int32_t Period() const {
        return hitTicks.size() >= 2 ? hitTicks[1] - hitTicks[0] : -1;
    }

    // The ticks, strictly inside the combo, on which the defender's hitstun had
    // run out. THE DIRECT READING of "the defender left hitstun", taken off
    // Fighter::hitstun rather than derived from frame arithmetic.
    //
    // The tick each hit lands on is excluded and has to be: hitstun is
    // decremented at the top of stepFighter and re-set by ResolveHits at the
    // bottom, so on the very first hit's tick the defender was legitimately free
    // -- that is what being hit out of neutral means.
    std::vector<std::int32_t> FreeTicks() const {
        std::vector<std::int32_t> out;
        if (firstHitTick < 0) return out;
        for (const Sample& s : samples) {
            if (s.tick < firstHitTick || s.tick > lastHitTick) continue;
            if (s.defStun == 0 && !s.hit) out.push_back(s.tick);
        }
        return out;
    }
};

TickLog drive(const MatchData& data, Trace& trace, int ticks,
              DefenderPolicy policy, std::uint16_t defenderBits) {
    TickLog run{};
    GameState s{};
    // The seed only feeds GameState::rng, which nothing in a combat tick reads;
    // it is advanced every tick so the stream is a function of the tick count
    // alone. Fixed here so a failing run is reproducible verbatim.
    cse::kernel::ResetMatch(s, 0xC0FFEEu);
    s.p[0].posX = kP0X;
    s.p[1].posX = kP1X;

    run.samples.reserve(static_cast<std::size_t>(ticks));
    for (int t = 0; t < ticks; ++t) {
        const std::int32_t healthBefore = s.p[1].health;

        InputPair in{};
        in.p[0].bits = trace.Bits();
        if (policy == DefenderPolicy::MashesOnceHit && run.firstHitTick >= 0 &&
            t > run.firstHitTick) {
            in.p[1].bits = defenderBits;
        }

        cse::kernel::Simulate(s, in, data);
        trace.Observe(s.p[0].moveId, s.p[0].moveFrame);

        Sample sample{};
        sample.tick       = t;
        sample.inputBits  = in.p[0].bits;
        sample.atkMove    = s.p[0].moveId;
        sample.atkFrame   = s.p[0].moveFrame;
        sample.defStun    = s.p[1].hitstun;
        sample.defMove    = s.p[1].moveId;
        sample.defHealth  = s.p[1].health;
        sample.hit        = s.p[1].health < healthBefore;
        sample.defEntered = s.p[1].moveId != 0 && s.p[1].moveFrame == 0;

        if (sample.hit) {
            if (run.firstHitTick < 0) run.firstHitTick = t;
            run.lastHitTick = t;
            run.hitTicks.push_back(t);
            run.hitHealth.push_back(s.p[1].health);
        }
        if (sample.defEntered) run.defenderActedTicks.push_back(t);
        run.samples.push_back(sample);
    }
    run.finalState = s;
    return run;
}

// Drive one Attempt with the sweep's single held button. Split out because four
// tests want it and every one of them wants exactly the same trace.
TickLog driveProbe(const Attempt& a, int ticks, DefenderPolicy policy) {
    Witness w{};
    w.sequence.push_back(kProbeMove);
    w.loopStart = 0;
    Trace trace(w, a.build.moves[0], probeBinding());
    return drive(a.build.data, trace, ticks, policy, kButtonPool[0]);
}

// ============================================================================
// Saying what happened
// ============================================================================

std::string moveName(const MoveIndexMap& map, std::uint16_t slot) {
    if (slot == 0) return "idle";
    const std::string_view id = map.IdOf(slot);
    return id.empty() ? ("slot" + std::to_string(slot)) : std::string(id);
}

// A per-tick table, so a reader can see WHICH TICK a loop broke on and WHAT THE
// DEFENDER'S STATE WAS without re-running anything.
std::string Table(const TickLog& run, const MoveIndexMap& map,
                  std::int32_t from, std::int32_t count) {
    std::ostringstream s;
    s << "\n  tick  in   attacker move        fr   hit   def stun  def move      def hp\n";
    for (const Sample& sample : run.samples) {
        if (sample.tick < from) continue;
        if (sample.tick >= from + count) break;
        s << "  " << std::setw(4) << sample.tick
          << "  " << buttonName(sample.inputBits)
          << "   " << std::setw(18) << std::left << moveName(map, sample.atkMove)
          << std::right
          << "  " << std::setw(2) << sample.atkFrame
          << "  " << (sample.hit ? "HIT" : " . ")
          << "  " << std::setw(8) << sample.defStun
          << "  " << std::setw(12) << std::left << moveName(map, sample.defMove)
          << std::right
          << "  " << std::setw(6) << sample.defHealth
          << (sample.defEntered ? "   <-- DEFENDER ACTED" : "")
          << "\n";
    }
    return s.str();
}

std::string Summary(const TickLog& run, const MoveIndexMap& map) {
    std::ostringstream s;
    s << "\n  hits            " << run.Hits()
      << "\n  first hit tick  " << run.firstHitTick
      << "\n  period          " << run.Period()
      << "\n  hit ticks       ";
    for (std::size_t i = 0; i < run.hitTicks.size() && i < 24; ++i)
        s << run.hitTicks[i] << (i + 1 < run.hitTicks.size() ? ", " : "");
    if (run.hitTicks.size() > 24) s << "...";
    const std::vector<std::int32_t> free = run.FreeTicks();
    s << "\n  defender OUT OF HITSTUN on ticks (inside the combo): ";
    if (free.empty()) s << "none";
    for (std::size_t i = 0; i < free.size() && i < 24; ++i)
        s << free[i] << (i + 1 < free.size() ? ", " : "");
    s << "\n  defender STARTED A MOVE on ticks: ";
    if (run.defenderActedTicks.empty()) s << "none";
    for (std::size_t i = 0; i < run.defenderActedTicks.size() && i < 24; ++i)
        s << run.defenderActedTicks[i]
          << (i + 1 < run.defenderActedTicks.size() ? ", " : "");
    s << "\n  final health    attacker " << run.finalState.p[0].health
      << ", defender " << run.finalState.p[1].health;
    // Which move actually landed the first hit, by name. Cheap, and it is the
    // one thing a reader cannot infer from the tick numbers when a trace has
    // stopped following its witness.
    for (const Sample& sample : run.samples)
        if (sample.hit) {
            s << "\n  first hit from  " << moveName(map, sample.atkMove)
              << " at frame " << sample.atkFrame;
            break;
        }
    s << "\n";
    return s.str();
}

const BuildLoss* findLoss(const BuildReport& report, const char* field) {
    for (const BuildLoss& loss : report.losses)
        if (loss.field == field) return &loss;
    return nullptr;
}

// ============================================================================
// The sweep
// ============================================================================

// What the kernel does with the edge at one delay, in the three categories the
// experiment is arranged around.
//
//   Inert                 MatchBuilder resolved an empty window, so Combat.cpp's
//                         two comparisons can never both hold and the edge does
//                         nothing at all.
//   OpensButDoesNotLock   the window is real and the follow-up starts, but the
//                         defender's hitstun runs out somewhere inside the
//                         string. Not a combo -- a blockstring with gaps.
//   Connects              the window is real and the defender is never free
//                         between the first hit and the last.
//
// The middle one is EMPTY for this edge and that is a measured property of these
// numbers rather than a hole in the classifier: `cancel_window_ticks` closes at
// 9 and `crouch_lp` inflicts 12 ticks of hitstun, so any frame the window is
// open on is a frame the follow-up lands in stun. Section 5 asserts it is empty
// rather than assuming it, because a character with a later-closing window would
// put rows there and the boundary claim would then need a third edge.
enum class KernelVerdict { Inert, OpensButDoesNotLock, Connects };

const char* kernelVerdictName(KernelVerdict v) {
    switch (v) {
        case KernelVerdict::Inert:               return "INERT";
        case KernelVerdict::OpensButDoesNotLock: return "opens, no lock";
        case KernelVerdict::Connects:            return "CONNECTS";
    }
    return "?";
}

// So a failed EXPECT_EQ prints `CONNECTS` rather than `2`. gtest falls back to
// printing an enum as its underlying integer, and the whole point of the three
// categories is that a reader can tell them apart at a glance.
std::ostream& operator<<(std::ostream& os, KernelVerdict v) {
    return os << kernelVerdictName(v);
}

struct Row {
    std::int32_t  delay      = 0;
    std::int32_t  earliest   = 0;
    std::int32_t  latest     = 0;
    bool          inert      = false;
    ProverStatus  status     = ProverStatus::Unknown;
    bool          proverKeepsEdge = false;   // the edge is NOT in deadCancels
    std::int32_t  shortfall  = 0;            // startup - advantage, when dead
    std::size_t   hits       = 0;
    std::int32_t  period     = -1;
    std::size_t   freeTicks  = 0;            // silent defender, inside the combo
    std::size_t   defActs    = 0;            // mashing defender
    std::int32_t  defHealth  = 0;
    std::int32_t  inertLinks = 0;            // MatchBuilder's own count, whole character
    KernelVerdict verdict    = KernelVerdict::Inert;

    // The source move's own numbers, carried per row so the assertions can be
    // written against the character rather than against constants that would
    // silently stop meaning anything if `crouch_lp` were re-timed.
    std::int32_t  srcStartup  = 0;
    std::int32_t  srcHitstun  = 0;
    std::int32_t  srcDuration = 0;

    // The first frame on which the kernel can BOTH see the window open and see
    // that the source connected. A hit lands on `moveFrame == startup` and
    // ResolveHits sets `alreadyHitBits` at the BOTTOM of that tick, so the cancel
    // test first sees it at `startup + 1` -- which is also, therefore, the loop's
    // period when the window opens earlier than that.
    std::int32_t FirstTakeableFrame() const {
        const std::int32_t contact = srcStartup + 1;
        return earliest > contact ? earliest : contact;
    }
};

// How long each sweep run gets. Chosen against the file rather than picked: the
// fastest configuration repeats every 5 ticks for 25 damage, so 120 ticks is 24
// hits and 600 damage against 1000 health. It stays clear of the KO on purpose
// -- Fighter::health clamps at zero and a hit that takes a clamped number to a
// clamped number is not observable as a hit at all, so a KO inside the window
// would silently truncate every count in the table. The sweep ASSERTS the
// defender survives, so a character whose damage outgrew this budget says so
// rather than quietly measuring less.
constexpr int kSweepTicks = 120;

// The delays the sweep covers. Zero is the most permissive edge the vocabulary
// can express; the top end is three past `duration - 1` (13), beyond which
// `startup + delay` exceeds any possible `latest` and nothing can change again.
constexpr std::int32_t kSweepLo = 0;
constexpr std::int32_t kSweepHi = 16;

bool sweep(std::vector<Row>& out, std::size_t& probeOut, std::string& why) {
    const json doc = subjectDoc();
    std::size_t probe = 0;
    if (!findProbe(doc, probe, why)) return false;
    probeOut = probe;

    for (std::int32_t d = kSweepLo; d <= kSweepHi; ++d) {
        Attempt a{};
        if (!bringUp(doc, probe, true, d, probeBinding(), a, why)) {
            why = "delay " + std::to_string(d) + ": " + why;
            return false;
        }

        // The mutation actually took. Without this, a wrong path into the
        // document would produce seventeen identical rows and a table that looked
        // like a finding about a flat response.
        if (a.delay != d) {
            why = "asked for delay " + std::to_string(d) + " and the loaded probe "
                  "carries " + std::to_string(a.delay) + ", so the mutation did not "
                  "reach the edge being measured";
            return false;
        }

        const Move& src = a.character.moves[a.character.cancels[a.fileCancel].from];

        Row row{};
        row.delay       = d;
        row.earliest    = a.earliest;
        row.latest      = a.latest;
        row.inert       = a.Inert();
        row.status      = a.verdict.status;
        row.srcStartup  = src.startup;
        row.srcHitstun  = src.hitstun;
        row.srcDuration = src.startup + src.active + src.recovery;

        row.proverKeepsEdge = true;
        for (const ProverDeadCancel& dead : a.verdict.deadCancels)
            if (dead.cancel == a.fileCancel) {
                row.proverKeepsEdge = false;
                row.shortfall       = dead.shortfall();
            }

        const BuildLoss* links = findLoss(a.build.report[0], "cancels (link, not cancel)");
        row.inertLinks = links != nullptr ? links->count : -1;

        // Silent for the measurement, mashing for the escape. Two runs rather
        // than one, because a defender who fights back changes the ATTACKER's
        // timing -- it lands its own hit and the attacker goes into hitstun --
        // so `hits` and `period` from a mashing run would be about a fight
        // rather than about the edge.
        const TickLog silent = driveProbe(a, kSweepTicks, DefenderPolicy::Silent);
        const TickLog mashed = driveProbe(a, kSweepTicks, DefenderPolicy::MashesOnceHit);

        row.hits      = silent.Hits();
        row.period    = silent.Period();
        row.freeTicks = silent.FreeTicks().size();
        row.defActs   = mashed.defenderActedTicks.size();
        row.defHealth = silent.finalState.p[1].health;

        if (row.inert)              row.verdict = KernelVerdict::Inert;
        else if (row.freeTicks > 0) row.verdict = KernelVerdict::OpensButDoesNotLock;
        else                        row.verdict = KernelVerdict::Connects;

        out.push_back(row);
    }
    return true;
}

std::string SweepTable(const std::vector<Row>& rows, std::int32_t authored,
                       std::int32_t proverBoundary, std::int32_t kernelBoundary) {
    std::ostringstream s;
    s << "\n  delay  kernel window   prover        hits  period  def free  def acts"
         "  what the kernel does\n"
         "  -----  -------------   -----------   ----  ------  --------  --------"
         "  --------------------\n";
    for (const Row& r : rows) {
        std::ostringstream w;
        w << "[" << std::setw(2) << r.earliest << "," << std::setw(3) << r.latest << "]";
        s << "  " << std::setw(5) << r.delay
          << "  " << std::setw(13) << w.str()
          << "   " << std::setw(11) << std::left << ProverStatusName(r.status) << std::right
          << "   " << std::setw(4) << r.hits
          << "  " << std::setw(6) << r.period
          << "  " << std::setw(8) << r.freeTicks
          << "  " << std::setw(8) << r.defActs
          << "  " << std::setw(14) << std::left << kernelVerdictName(r.verdict) << std::right;
        if (r.delay == authored)       s << "  <-- AS SHIPPED";
        if (r.delay == proverBoundary) s << "  <-- the ANALYSIS turns here";
        if (r.delay == kernelBoundary) s << "  <-- the GAME turns here";
        s << "\n";
    }
    return s.str();
}

// ============================================================================
// The numbers this file is written against
// ============================================================================
//
// Each is DERIVED below from the loaded character as well as asserted, so a
// re-authored `crouch_lp` moves the boundary and the test follows it rather than
// failing on a constant. They are named here so a reader can check the arithmetic
// without running anything.

// `fighter_a.json` cancels[71]: crouch_lp -> crouch_lp, delay 10, on hit.
constexpr std::int32_t kAuthoredDelay = 10;

// THE ANALYSIS. ProverAdapter.cpp `edgeUsable`: an edge survives when
// `startup(to) <= settledHitstun(from) - delay`. `crouch_lp` is startup 3 and
// hitstun 12, and `decay.kind` is `none` so the settled hitstun IS 12:
// 3 <= 12 - delay holds for delay <= 9.
constexpr std::int32_t kProverBoundary = 9;

// THE GAME. MatchBuilder.cpp `buildCancels`: the window is
// [max(startup + delay, cancelWindowOpen), min(duration - 1, cancelWindowClose)]
// = [max(3 + delay, 5), min(13, 9)] = [max(3 + delay, 5), 9], which is non-empty
// for delay <= 6.
constexpr std::int32_t kKernelBoundary = 6;

// ResetMatch's opening health (Simulate.cpp). Named so the damage arithmetic
// reads as a subtraction from a known starting point rather than as a magic 1000.
constexpr std::int32_t kStartingHealth = 1000;

// The payoff run. Longer than the sweep because it asserts a period holds over
// many turns; still short of the KO, at 22 hits for 550 damage in the fastest
// configuration it uses.
constexpr int kPayoffTicks = 200;
constexpr int kMinTurns    = 18;

// The relationships between those three, as compile-time facts rather than as
// runtime expectations, because they are arithmetic over constants and a test
// that checks them at runtime is checking the compiler.
static_assert(kProverBoundary == kAuthoredDelay - 1,
              "the shipped probe is supposed to be exactly ONE frame from the "
              "delay at which the decision procedure calls it live; if it is not, "
              "this file is making a different claim than its title");
static_assert(kKernelBoundary < kProverBoundary,
              "the kernel is supposed to open its window LATER than the prover "
              "keeps the edge, which is what creates the band of infinites the "
              "game cannot perform");
static_assert(kSweepLo <= kKernelBoundary && kAuthoredDelay <= kSweepHi,
              "the sweep must cover both boundaries and the shipped value");

}  // namespace

// ============================================================================
// 1. THE ANCHOR -- and the harness's own honesty
// ============================================================================

// Cheap, and it is what makes everything below an experiment rather than an
// anecdote: the subject really is a character the tool certifies.
TEST(OneFrameAnchor, TheShippedCharacterTerminatesWithACertificate) {
    CharacterData c{};
    LoadReport load{};
    ASSERT_TRUE(LoadCharacterFile(charactersDir(), kSubjectFile, loadOptions(), c, load))
        << kSubjectFile << " did not load from " << charactersDir()
        << ".\n  rule : " << (load.rule.empty() ? "(none named)" : load.rule)
        << "\n  error: " << load.error;
    ASSERT_TRUE(load.rule.empty()) << "loaded but named load assertion " << load.rule;

    ProverResult verdict{};
    ProverReport report{};
    ASSERT_TRUE(AnalyseCharacter(c, proverOptions(), verdict, report))
        << report.rule << ": " << report.error;

    EXPECT_EQ(verdict.status, ProverStatus::Terminating)
        << kSubjectFile << " is the SAFE character this whole file is measured "
           "against and the decision procedure does not agree. THE EXPECTATION "
           "MUST NOT BE RELAXED -- there is no one-frame margin to measure if the "
           "character is already broken.\n"
        << DescribeVerdict(c, verdict);
    EXPECT_TRUE(verdict.loop.empty())
        << "a terminating verdict came with a loop witness, which is a "
           "contradiction in the result rather than a fact about the character";
    EXPECT_FALSE(verdict.capped);

    // The certificate. Its existence is what makes the mutation below a loss of
    // something rather than a change of mood: at delay 9 there is no ranking at
    // all, because the new cycle spends nothing.
    EXPECT_TRUE(verdict.hasRanking)
        << "the character terminates without a ranking certificate ("
        << RankingAbsenceName(verdict.rankingAbsence)
        << "), so this file cannot say a certificate was lost.";
}

// The guard that makes "ONE integer" a fact rather than a promise. If a round
// trip through nlohmann changes anything on its own, every difference measured
// below is contaminated -- so it is measured first, with the mutation switched
// off.
TEST(OneFrameAnchor, ARoundTripThroughJsonWithNoMutationChangesNothing) {
    CharacterData fromFile{};
    LoadReport load{};
    ASSERT_TRUE(LoadCharacterFile(charactersDir(), kSubjectFile, loadOptions(), fromFile, load))
        << load.error;

    const json doc = subjectDoc();
    std::size_t probe = 0;
    std::string why;
    ASSERT_TRUE(findProbe(doc, probe, why)) << why;

    Attempt control{};
    ASSERT_TRUE(bringUp(doc, probe, /*mutate*/ false, /*delay*/ 0, probeBinding(),
                        control, why)) << why;

    ASSERT_EQ(control.character.moves.size(), fromFile.moves.size());
    ASSERT_EQ(control.character.cancels.size(), fromFile.cancels.size());
    ASSERT_EQ(control.character.resources.size(), fromFile.resources.size());
    EXPECT_EQ(control.character.id, fromFile.id);

    for (std::size_t i = 0; i < fromFile.moves.size(); ++i) {
        const Move& a = fromFile.moves[i];
        const Move& b = control.character.moves[i];
        ASSERT_EQ(a.id, b.id) << "move " << i << " changed identity in a round trip";
        EXPECT_EQ(a.startup, b.startup)   << a.id;
        EXPECT_EQ(a.active, b.active)     << a.id;
        EXPECT_EQ(a.recovery, b.recovery) << a.id;
        EXPECT_EQ(a.hitstun, b.hitstun)   << a.id;
        EXPECT_EQ(a.damageHundredths, b.damageHundredths) << a.id;
        EXPECT_EQ(a.reachSub, b.reachSub) << a.id;
        EXPECT_EQ(a.hasCancelWindow, b.hasCancelWindow)   << a.id;
        EXPECT_EQ(a.cancelWindowOpen, b.cancelWindowOpen) << a.id;
        EXPECT_EQ(a.cancelWindowClose, b.cancelWindowClose) << a.id;
    }
    for (std::size_t i = 0; i < fromFile.cancels.size(); ++i) {
        const Cancel& a = fromFile.cancels[i];
        const Cancel& b = control.character.cancels[i];
        EXPECT_EQ(a.from, b.from)   << "cancels[" << i << "]";
        EXPECT_EQ(a.to, b.to)       << "cancels[" << i << "]";
        EXPECT_EQ(a.delay, b.delay) << "cancels[" << i << "]";
        EXPECT_TRUE(a.on == b.on)   << "cancels[" << i << "]";
    }

    // And the verdict survives the round trip, which is the property that
    // actually matters: the prover is what section 3 watches change.
    ProverResult direct{};
    ProverReport report{};
    ASSERT_TRUE(AnalyseCharacter(fromFile, proverOptions(), direct, report));
    EXPECT_EQ(control.verdict.status, direct.status)
        << "a no-op round trip through nlohmann changed the verdict, so nothing "
           "this file measures can be attributed to the mutation.";
    EXPECT_EQ(control.verdict.hasRanking, direct.hasRanking);
    EXPECT_EQ(control.verdict.deadCancels.size(), direct.deadCancels.size());
}

// ============================================================================
// 2. THE EDGE IS INERT AS AUTHORED
// ============================================================================
//
// THE WHOLE CLAIM RESTS ON THIS. If the probe is live as shipped then fighter_a
// already has an executable self-cancel and there is no margin to be one frame
// away from -- so this is an ASSERT, and it reports the resolved window rather
// than a bare false.

TEST(OneFrameEdge, TheAuthoredProbeResolvesToAnEmptyKernelWindow) {
    const json doc = subjectDoc();
    std::size_t probe = 0;
    std::string why;
    ASSERT_TRUE(findProbe(doc, probe, why)) << why;

    Attempt a{};
    ASSERT_TRUE(bringUp(doc, probe, false, 0, probeBinding(), a, why)) << why;

    const Cancel& edge = a.character.cancels[a.fileCancel];
    const Move&   src  = a.character.moves[edge.from];

    ASSERT_EQ(edge.delay, kAuthoredDelay)
        << "the probe's authored delay is " << edge.delay << " and this file was "
           "written against " << kAuthoredDelay << ". Every boundary below is "
           "measured relative to it, so the character has been re-authored and the "
           "test must be re-read, not re-pointed.";
    EXPECT_TRUE(edge.on == Contact::Hit)
        << "the probe is not gated on contact, so the kernel would take it on a "
           "whiff and the loop measured below would not be a combo";

    // The frame data the two boundaries are computed from, asserted so that a
    // re-timed move fails here -- next to the arithmetic -- rather than four
    // tests later as an unexplained count.
    EXPECT_EQ(src.startup, 3);
    EXPECT_EQ(src.active, 2);
    EXPECT_EQ(src.recovery, 9);
    EXPECT_EQ(src.hitstun, 12);
    ASSERT_TRUE(src.hasCancelWindow)
        << "`" << src.id << "` authors no engine.cancel_window_ticks. MatchBuilder "
           "then lets the window run to the last frame of the move, which moves the "
           "kernel boundary and invalidates every number in this file's header.";
    EXPECT_EQ(src.cancelWindowOpen, 5);
    EXPECT_EQ(src.cancelWindowClose, 9);

    // THE MEASUREMENT, taken from MatchBuilder and not re-derived here.
    ASSERT_TRUE(a.crossed);
    ASSERT_GT(a.earliest, a.latest)
        << "the probe resolves to " << a.Window() << ", which is a window the "
           "kernel CAN match. `" << src.id << "` therefore cancels into itself in "
           "the running game as shipped, and fighter_a has an executable infinite "
           "the prover certified away. THAT IS THE FINDING -- do not relax this "
           "assertion.\n"
        << DescribeVerdict(a.character, a.verdict);

    // Combat.h: "An edge with earliestFrame > latestFrame is INERT rather than
    // malformed." MatchBuilder counts it rather than dropping it, and the count
    // is what section 5 watches move by exactly one.
    const BuildLoss* links = findLoss(a.build.report[0], "cancels (link, not cancel)");
    ASSERT_NE(links, nullptr)
        << "MatchBuilder no longer reports inert links, so the projection's own "
           "record of this edge is gone";
    EXPECT_EQ(links->direction, BuildLossDirection::KernelPermits)
        << "`cancels (link, not cancel)` is recorded as "
        << BuildLossDirectionName(links->direction)
        << ". An edge the kernel cannot take is a thing the GAME allows that the "
           "file does not -- the ordinary button start does it, without requiring "
           "contact -- which is what KernelPermits means here.";
    EXPECT_GT(links->count, 0);

    // AND THE PROVER'S OWN MARGIN, WHICH IS WHERE THE ONE FRAME ACTUALLY LIVES.
    // The tool prints it: `advantage` 2 against a `startup` of 3, short by 1.
    // This is the number the file's own caveat claims ("DEAD, short by 1") and it
    // is checked against the tool rather than against the prose.
    const ProverDeadCancel* dead = nullptr;
    for (const ProverDeadCancel& d : a.verdict.deadCancels)
        if (d.cancel == a.fileCancel) dead = &d;
    ASSERT_NE(dead, nullptr)
        << "the decision procedure does not report the probe as a dead cancel, so "
           "it considers the edge USABLE -- and the character should not have been "
           "TERMINATING.\n"
        << DescribeVerdict(a.character, a.verdict);
    EXPECT_EQ(dead->advantage, src.hitstun - edge.delay);
    EXPECT_EQ(dead->startup, src.startup);
    EXPECT_EQ(dead->shortfall(), 1)
        << "the probe is short by " << dead->shortfall() << " frames, not 1. The "
           "file exists to be one frame from live; at any other margin the claim "
           "this test makes is a different claim.";

    std::cout
        << "\n[ ONE FRAME ] `" << kSubjectFile << "` cancels[" << a.fileCancel << "]: "
        << src.id << " -> " << src.id << ", delay " << edge.delay << ", on hit\n"
        << "  " << src.id << "  startup " << src.startup << "  active " << src.active
        << "  recovery " << src.recovery << "  hitstun " << src.hitstun
        << "  cancel_window [" << src.cancelWindowOpen << ", "
        << src.cancelWindowClose << "]\n"
        << "  THE ANALYSIS  advantage " << dead->advantage << " against startup "
        << dead->startup << ": DEAD, short by " << dead->shortfall() << ".\n"
        << "  THE GAME      MatchBuilder resolves the window to " << a.Window()
        << ", so the kernel can never take it.\n"
        << "  Note the two are not the same margin, and section 5 measures how far "
           "apart they are.\n\n";
}

// ============================================================================
// 3. THE ONE-FRAME MISTAKE
// ============================================================================

// THE HEADLINE. One integer, changed by one, on a file of 18 moves and 73
// cancels, and the tool's answer about the whole character inverts.
TEST(OneFrameMutation, OneFrameOnOneDelayFlipsTheVerdict) {
    const json doc = subjectDoc();
    std::size_t probe = 0;
    std::string why;
    ASSERT_TRUE(findProbe(doc, probe, why)) << why;

    Attempt asShipped{}, oneFrameWider{};
    ASSERT_TRUE(bringUp(doc, probe, true, kAuthoredDelay, probeBinding(), asShipped, why)) << why;
    ASSERT_TRUE(bringUp(doc, probe, true, kAuthoredDelay - 1, probeBinding(), oneFrameWider, why)) << why;

    // The control: re-authoring the delay to the value it already has must not
    // change anything, which is what tells "the mutation did it" apart from "the
    // mutation harness did it".
    EXPECT_EQ(asShipped.verdict.status, ProverStatus::Terminating)
        << "re-writing the authored delay with its own value changed the verdict.\n"
        << DescribeVerdict(asShipped.character, asShipped.verdict);

    // THE FLIP.
    ASSERT_EQ(oneFrameWider.verdict.status, ProverStatus::Infinite)
        << "widening the probe from " << kAuthoredDelay << " to "
        << (kAuthoredDelay - 1)
        << " left the character TERMINATING, and the kernel CAN loop this edge at "
           "a wide enough window (section 4 executes it). A model that certifies a "
           "character the game can loop is the DANGEROUS direction of D8 and is the "
           "most important thing this file could find -- REPORT IT, do not move the "
           "boundary to make this pass.\n"
        << DescribeVerdict(oneFrameWider.character, oneFrameWider.verdict);

    // The certificate is gone, and for the reason the character was designed
    // around: the new cycle spends nothing, so no resource strictly runs down
    // across it. `hasRanking` is a Terminating-only field, so this is stated as
    // the status change plus the absence of a witness on the safe side.
    EXPECT_TRUE(asShipped.verdict.hasRanking);
    EXPECT_FALSE(oneFrameWider.verdict.loop.empty())
        << "INFINITE with an empty loop is a word with nothing behind it";

    // AND THE LOOP NAMES THE EDGE THAT WAS MUTATED. Without this the verdict
    // could have flipped for some unrelated reason and the attribution would be
    // a coincidence.
    const Witness w = witnessOf(oneFrameWider.character, oneFrameWider.verdict);
    for (MoveIndex m : oneFrameWider.verdict.loop) {
        ASSERT_LT(m, oneFrameWider.character.moves.size());
        EXPECT_EQ(oneFrameWider.character.moves[m].id, kProbeMove)
            << "the printed loop is `" << w.ToString() << "` and the only edge this "
               "test changed is `" << kProbeMove << "` into itself, so the verdict "
               "flipped for a reason this file did not cause.\n"
            << DescribeVerdict(oneFrameWider.character, oneFrameWider.verdict);
    }
    for (MoveIndex m : oneFrameWider.verdict.prefix)
        ASSERT_LT(m, oneFrameWider.character.moves.size());

    // The prover keeps the edge it previously printed as dead, with exactly the
    // advantage the one frame bought.
    for (const ProverDeadCancel& d : oneFrameWider.verdict.deadCancels)
        EXPECT_NE(d.cancel, oneFrameWider.fileCancel)
            << "the edge is still reported DEAD at delay " << (kAuthoredDelay - 1)
            << ", so the INFINITE above came from somewhere else";

    RecordProperty("witness", w.ToString());
    RecordProperty("prover_boundary_delay", kProverBoundary);
}

// EVERYTHING ELSE IS IDENTICAL, MEASURED RATHER THAN PROMISED. This is the whole
// argument for mutating in memory instead of shipping a third character file,
// and it is worth an assertion rather than a sentence in a comment.
TEST(OneFrameMutation, NothingBesidesThatOneIntegerMoved) {
    const json doc = subjectDoc();
    std::size_t probe = 0;
    std::string why;
    ASSERT_TRUE(findProbe(doc, probe, why)) << why;

    Attempt before{}, after{};
    ASSERT_TRUE(bringUp(doc, probe, false, 0, probeBinding(), before, why)) << why;
    ASSERT_TRUE(bringUp(doc, probe, true, kProverBoundary, probeBinding(), after, why)) << why;

    ASSERT_EQ(before.character.moves.size(), after.character.moves.size());
    ASSERT_EQ(before.character.cancels.size(), after.character.cancels.size());
    ASSERT_EQ(before.character.resources.size(), after.character.resources.size());

    for (std::size_t i = 0; i < before.character.moves.size(); ++i) {
        const Move& a = before.character.moves[i];
        const Move& b = after.character.moves[i];
        ASSERT_EQ(a.id, b.id);
        EXPECT_EQ(a.startup, b.startup)                     << a.id;
        EXPECT_EQ(a.active, b.active)                       << a.id;
        EXPECT_EQ(a.recovery, b.recovery)                   << a.id;
        EXPECT_EQ(a.hitstun, b.hitstun)                     << a.id;
        EXPECT_EQ(a.damageHundredths, b.damageHundredths)   << a.id;
        EXPECT_EQ(a.reachSub, b.reachSub)                   << a.id;
        EXPECT_TRUE(a.stance == b.stance)                   << a.id;
        EXPECT_EQ(a.cancelWindowOpen, b.cancelWindowOpen)   << a.id;
        EXPECT_EQ(a.cancelWindowClose, b.cancelWindowClose) << a.id;
    }
    for (std::size_t i = 0; i < before.character.resources.size(); ++i) {
        EXPECT_EQ(before.character.resources[i].name, after.character.resources[i].name);
        EXPECT_EQ(before.character.resources[i].initial, after.character.resources[i].initial);
        EXPECT_EQ(before.character.resources[i].floor, after.character.resources[i].floor);
    }
    EXPECT_TRUE(before.character.decay.kind == after.character.decay.kind);
    EXPECT_EQ(before.character.decay.floor, after.character.decay.floor);
    EXPECT_EQ(before.character.decay.step, after.character.decay.step);

    int differing = 0;
    for (std::size_t i = 0; i < before.character.cancels.size(); ++i) {
        const Cancel& a = before.character.cancels[i];
        const Cancel& b = after.character.cancels[i];
        EXPECT_EQ(a.from, b.from) << "cancels[" << i << "]";
        EXPECT_EQ(a.to, b.to)     << "cancels[" << i << "]";
        EXPECT_TRUE(a.on == b.on) << "cancels[" << i << "]";
        EXPECT_EQ(a.certain, b.certain) << "cancels[" << i << "]";
        if (a.delay != b.delay) {
            ++differing;
            EXPECT_EQ(i, static_cast<std::size_t>(after.fileCancel))
                << "cancels[" << i << "]'s delay moved and it is not the probe";
        }
    }
    EXPECT_EQ(differing, 1)
        << differing << " cancel delays differ between the shipped character and "
           "the mutated one. The claim of this file is that ONE integer does it.";
    EXPECT_EQ(before.character.cancels[before.fileCancel].delay, kAuthoredDelay);
    EXPECT_EQ(after.character.cancels[after.fileCancel].delay, kProverBoundary);
}

// ============================================================================
// 4. THE PAYOFF -- and the gap between the two boundaries
// ============================================================================

// THIS TEST PASSES AND WHAT IT ASSERTS IS A DEFECT IN THE PROJECTION. Read the
// header before reading it.
//
// At the prover's boundary the analysis reports an infinite and the kernel is
// INERT: MatchBuilder's window is still empty, because it is closed by
// `cancel_window_ticks` rather than by hitstun. So the loop the tool prints
// cannot be performed, and the defender is out of hitstun between every pair of
// hits.
//
// This is the CONSERVATIVE direction in ProverAdapter.h's vocabulary -- the
// analysis is wrong in the safe direction, crying wolf rather than missing one --
// and it is the exact mirror of test_ground_truth.cpp section 5, where the same
// character's `air_mp` looped in the kernel while the model certified it away.
// One character, one field, and the two opposite failures of ARCHITECTURE.md D8
// three frames apart on it. That is a sharper statement of D8 than either test
// makes alone, because it shows they are not opposite hazards belonging to
// different characters.
TEST(OneFramePayoff, AtTheProversBoundaryTheKernelCannotPerformThePrintedLoop) {
    const json doc = subjectDoc();
    std::size_t probe = 0;
    std::string why;
    ASSERT_TRUE(findProbe(doc, probe, why)) << why;

    Attempt a{};
    ASSERT_TRUE(bringUp(doc, probe, true, kProverBoundary, probeBinding(), a, why)) << why;
    ASSERT_EQ(a.verdict.status, ProverStatus::Infinite)
        << DescribeVerdict(a.character, a.verdict);

    // The window MatchBuilder produced for the very edge the loop names.
    ASSERT_TRUE(a.crossed);
    EXPECT_TRUE(a.Inert())
        << "the kernel window is " << a.Window() << ", which is NOT empty. The "
           "prover and the kernel now agree at this delay, so the three-frame band "
           "this test documents has closed -- which is good news and a change to "
           "MatchBuilder, and the header of this file needs rewriting rather than "
           "this expectation.";

    const TickLog run = driveProbe(a, kPayoffTicks, DefenderPolicy::MashesOnceHit);
    ASSERT_GT(run.Hits(), 0u)
        << "nothing connected at all, so this measures distance or bindings rather "
           "than the edge." << Table(run, a.build.moves[0], 0, 24);

    // The direct reading: the defender leaves hitstun, repeatedly, between hits.
    const std::vector<std::int32_t> free = run.FreeTicks();
    EXPECT_FALSE(free.empty())
        << "the defender never left hitstun, so the kernel IS performing the loop "
           "the prover printed -- and the model and the game agree at this delay "
           "after all. That would make this file's central measurement wrong; "
           "report it." << Summary(run, a.build.moves[0]);
    EXPECT_FALSE(run.defenderActedTicks.empty())
        << "the defender held a button through the whole string and the kernel "
           "never let them start a move, though the edge is inert."
        << Summary(run, a.build.moves[0]);

    // And the repetition that DOES happen is the move running out and the held
    // button starting it again -- period `duration`, not period `earliest`.
    const Move& src = a.character.moves[a.character.cancels[a.fileCancel].from];
    const std::int32_t duration = src.startup + src.active + src.recovery;
    EXPECT_EQ(run.Period(), duration)
        << "the string repeats every " << run.Period() << " ticks and the move is "
        << duration << " ticks long, so something other than the button start is "
           "restarting it." << Summary(run, a.build.moves[0]);

    // The bridge already says the game is not the analysed character. What this
    // test adds is which edge, and in which direction.
    EXPECT_FALSE(a.build.report[0].playsAsAnalysed);
    EXPECT_GT(a.build.report[0].lossesThatBite, 0);

    RecordProperty("prover_boundary_delay", kProverBoundary);
    RecordProperty("kernel_free_ticks_at_prover_boundary", static_cast<int>(free.size()));

    std::cout
        << "\n[ ONE FRAME ] a measured model/game gap on `" << a.character.id
        << "`, in the SAFE direction\n"
        << "  at delay " << kProverBoundary << " the decision procedure reports "
           "INFINITE and prints `"
        << witnessOf(a.character, a.verdict).ToString() << "`.\n"
        << "  MatchBuilder resolves the same edge to " << a.Window()
        << ", so the kernel never takes it: the\n"
        << "  string repeats every " << run.Period() << " ticks (the move's own "
           "duration) and the defender is out of\n"
        << "  hitstun on " << free.size() << " tick(s) inside it, starting a move "
           "on " << run.defenderActedTicks.size() << " of them.\n"
        << "  This is the MIRROR of test_ground_truth.cpp section 5. There the "
           "model said TERMINATING and\n"
        << "  the kernel looped; here the model says INFINITE and the kernel "
           "cannot. Same character, one field,\n"
        << "  and the two opposite failures of D8 " << (kProverBoundary - kKernelBoundary)
        << " frames apart on it.\n\n";
}

// THE PAYOFF PROPER: the printed loop, executed. Same technique as
// test_ground_truth.cpp -- the trace is derived from the witness rather than
// written down, so what is demonstrated is that the ENGINE can read the verdict
// and not that a human can.
TEST(OneFramePayoff, TheLoopExecutesOnceTheKernelWindowOpens) {
    const json doc = subjectDoc();
    std::size_t probe = 0;
    std::string why;
    ASSERT_TRUE(findProbe(doc, probe, why)) << why;

    // Built with the probe binding first, only so the verdict can be read; the
    // real build below uses the bindings the witness asks for.
    Attempt a{};
    ASSERT_TRUE(bringUp(doc, probe, true, kKernelBoundary, probeBinding(), a, why)) << why;

    ASSERT_EQ(a.verdict.status, ProverStatus::Infinite)
        << DescribeVerdict(a.character, a.verdict);
    ASSERT_FALSE(a.verdict.loop.empty());
    ASSERT_FALSE(a.Inert())
        << "the kernel window is " << a.Window() << " at delay " << kKernelBoundary
        << ", so there is no loop to execute. The kernel boundary has moved and "
           "the sweep in section 5 will say where to.";

    // --- Turn the witness into inputs ---------------------------------------
    const Witness w = witnessOf(a.character, a.verdict);
    for (MoveIndex m : a.verdict.loop) {
        ASSERT_LT(m, a.character.moves.size())
            << "the witness names move index " << m << " and the character has "
            << a.character.moves.size() << " moves";
        ASSERT_EQ(a.character.moves[m].id, kProbeMove)
            << "the printed loop is `" << w.ToString() << "` and this test executes "
               "a loop over the mutated edge.\n"
            << DescribeVerdict(a.character, a.verdict);
    }
    for (MoveIndex m : a.verdict.prefix)
        ASSERT_LT(m, a.character.moves.size());

    // THE TRACE IS THE LOOP, NOT THE WHOLE WITNESS, and the licence for that is
    // CHECKED rather than assumed. `starters` is empty, which comboprover reads as
    // "any move may open a combo" (comboprover.hpp:350-354), so the loop's own
    // first move is a legal opening and any prefix the search reports is the route
    // it happened to walk in on rather than a precondition. Driving the prefix
    // would additionally exercise whatever edges that route used -- a different
    // claim, which could fail for reasons this file is not about, and which
    // test_ground_truth.cpp already covers on a witness whose prefix and loop are
    // the same move.
    ASSERT_TRUE(a.character.starters.empty())
        << "the character now declares " << a.character.starters.size()
        << " starter(s), so the loop's first move may not be a legal opening and "
           "the prefix can no longer be skipped. The witness is `" << w.ToString()
        << "`.";

    Witness loopOnly{};
    for (MoveIndex m : a.verdict.loop) loopOnly.sequence.push_back(a.character.moves[m].id);
    loopOnly.loopStart = 0;

    const std::vector<MoveBinding> bindings = bindingsForWitness(loopOnly.sequence);
    ASSERT_FALSE(bindings.empty());
    ASSERT_LE(bindings.size(), kButtonPoolSize)
        << "the loop names " << bindings.size() << " distinct moves and there are "
           "only " << kButtonPoolSize << " single-bit buttons. Two moves sharing a "
           "mask would let StepAttack's first-wins rule pick the wrong one, so the "
           "trace would stop being the loop.";

    MatchBuild build{};
    ASSERT_TRUE(buildMirror(a.character, bindings, build))
        << "p0: " << build.report[0].error << " / p1: " << build.report[1].error;

    Trace trace(loopOnly, build.moves[0], bindings);
    ASSERT_TRUE(trace.Usable(why)) << "the printed loop cannot be driven: " << why;

    // --- Execute -------------------------------------------------------------
    TickLog run = drive(build.data, trace, kPayoffTicks, DefenderPolicy::Silent, 0);

    const std::size_t loopLength = trace.LoopLength();
    ASSERT_GT(loopLength, 0u);
    const std::size_t wanted = loopLength * static_cast<std::size_t>(kMinTurns);

    ASSERT_GE(run.Hits(), wanted)
        << "THE PRINTED LOOP DID NOT EXECUTE at the delay where the kernel window "
           "is open.\n"
        << "  witness: " << w.ToString() << "\n"
        << "  window : " << a.Window() << "\n"
        << "  asked for " << kMinTurns << " turns (" << wanted << " hits) in "
        << kPayoffTicks << " ticks and got " << run.Hits() << ".\n"
        << "This is a MEASURED GAP BETWEEN THE MODEL AND THE GAME "
           "(ARCHITECTURE.md 5.5 item 4), not a flaky test."
        << Summary(run, build.moves[0])
        << Table(run, build.moves[0], 0, 40);

    // --- The defender never left hitstun between them ------------------------
    const std::vector<std::int32_t> free = run.FreeTicks();
    EXPECT_TRUE(free.empty())
        << "the defender was out of hitstun on " << free.size()
        << " tick(s) inside the combo, so this is a long string with gaps in it "
           "rather than an unescapable loop."
        << Summary(run, build.moves[0])
        << Table(run, build.moves[0], run.firstHitTick, 32);

    // --- It repeats on a fixed period, and the period IS MatchBuilder's number.
    //
    // The loop restarts on the first frame of the resolved window that the source
    // is known to have connected on. A hit lands on `moveFrame == startup` and
    // `alreadyHitBits` is set by ResolveHits at the BOTTOM of that tick, so the
    // cancel test first sees it at `startup + 1`; the edge is therefore taken at
    // max(earliestFrame, startup + 1). Asserting the measured period against that
    // ties what the kernel did back to the number the bridge produced, rather than
    // to a constant somebody recorded.
    const Move& src = a.character.moves[a.character.cancels[a.fileCancel].from];
    const std::int32_t expectedPeriod =
        a.earliest > src.startup + 1 ? a.earliest : src.startup + 1;
    ASSERT_GE(run.hitTicks.size(), loopLength + 1);
    EXPECT_EQ(run.Period(), expectedPeriod)
        << "the loop repeats every " << run.Period() << " ticks; MatchBuilder opened "
           "the window at frame " << a.earliest << " and the source connects on frame "
        << src.startup << ", so it should repeat every " << expectedPeriod << "."
        << Summary(run, build.moves[0]);
    for (std::size_t i = 0; i + loopLength < wanted; ++i)
        EXPECT_EQ(run.hitTicks[i + loopLength] - run.hitTicks[i], run.Period())
            << "turn " << i << " of the loop took a different number of ticks than "
               "the first, so the loop is not periodic." << Summary(run, build.moves[0]);

    // --- The damage keeps accruing -------------------------------------------
    for (std::size_t i = 1; i < run.Hits(); ++i)
        ASSERT_LT(run.hitHealth[i], run.hitHealth[i - 1])
            << "hit " << i << " at tick " << run.hitTicks[i]
            << " did not reduce the defender's health";
    EXPECT_GT(run.finalState.p[1].health, 0)
        << "the defender was knocked out inside the measured window, so the last "
           "hits were measured against Fighter::health's clamp at zero rather than "
           "against real damage. Lower kPayoffTicks.";
    EXPECT_EQ(run.finalState.p[0].health, kStartingHealth)
        << "the attacker took damage, so this is a trade being read as a combo";

    // --- And the defender cannot mash out ------------------------------------
    //
    // The strongest available statement of "cannot get out": hand them the same
    // character, the same bindings and a held button from the tick after the combo
    // opens, and require the kernel never to let them start a move. Unlike reading
    // Fighter::hitstun, this asks the simulation itself.
    Trace mashTrace(loopOnly, build.moves[0], bindings);
    const TickLog mashed = drive(build.data, mashTrace, kPayoffTicks,
                                 DefenderPolicy::MashesOnceHit, bindings[0].button);
    EXPECT_TRUE(mashed.defenderActedTicks.empty())
        << "the defender started a move on " << mashed.defenderActedTicks.size()
        << " tick(s) while holding a button through the combo, so it is escapable."
        << Summary(mashed, build.moves[0])
        << Table(mashed, build.moves[0], mashed.firstHitTick, 32);
    EXPECT_EQ(mashed.hitTicks, run.hitTicks)
        << "holding a button changed the combo, so the defender did get a turn "
           "somewhere even though no move started.";

    RecordProperty("kernel_boundary_delay", kKernelBoundary);
    RecordProperty("payoff_hits", static_cast<int>(run.Hits()));
    RecordProperty("payoff_period_ticks", run.Period());

    std::cout
        << "\n[ ONE FRAME ] the printed loop executed on a `" << a.character.id
        << "` whose probe was widened to delay " << kKernelBoundary << "\n"
        << "  witness         " << w.ToString() << "\n"
        << "  driven          " << loopOnly.ToString()
        << "  (the loop; every move is a starter, so its first move opens a combo)\n"
        << "  derived trace   hold " << buttonName(bindings[0].button)
        << " (one binding; the loop's target is its own source)\n"
        << "  kernel window   " << a.Window() << "\n"
        << "  executed        " << run.Hits() << " hits in " << kPayoffTicks
        << " ticks, one every " << run.Period() << ", first on tick "
        << run.firstHitTick << "\n"
        << "  defender        out of hitstun on " << free.size()
        << " tick(s) inside the combo, and started "
        << mashed.defenderActedTicks.size() << " move(s) while mashing\n"
        << "  health          " << kStartingHealth << " -> "
        << run.finalState.p[1].health << "\n\n";
}

// ============================================================================
// 5. THE BOUNDARY -- the whole range, and the frame on either side
// ============================================================================
//
// A one-frame claim is only worth making if the frame on either side of it
// behaves differently, so this sweeps the delay across its whole range and
// asserts where each boundary sits. The table it prints IS the finding: it shows
// a designer that widening one cancel window turns a provably terminating
// character into one with an infinite, that the tool notices three frames before
// the game does, and that nothing else changed.

TEST(OneFrameBoundary, TheSweepPutsTheAnalysisThreeFramesAheadOfTheGame) {
    std::vector<Row> rows;
    std::size_t probe = 0;
    std::string why;
    ASSERT_TRUE(sweep(rows, probe, why)) << why;
    ASSERT_EQ(rows.size(), static_cast<std::size_t>(kSweepHi - kSweepLo + 1));

    // Printed FIRST, so it is in the log whether or not anything below fails.
    // A failing assertion with the table already on screen is a finding; one
    // without it is a puzzle.
    std::cout << "\n[ ONE FRAME ] the delay sweep on `" << kSubjectFile
              << "`, cancels[" << probe << "]: " << kProbeMove << " -> " << kProbeMove
              << "\n  " << kSweepTicks
              << " ticks per row, bodies 8 px apart, one button held.\n"
              << SweepTable(rows, kAuthoredDelay, kProverBoundary, kKernelBoundary)
              << "\n";

    for (const Row& r : rows) {
        SCOPED_TRACE("delay " + std::to_string(r.delay));

        // Nothing in the table may be measured against a knocked-out defender:
        // health clamps at zero and further hits would stop being observable.
        EXPECT_GT(r.defHealth, 0)
            << "the defender was knocked out inside the " << kSweepTicks
            << "-tick window, so this row's hit count is truncated. Lower kSweepTicks.";

        // THE ANALYSIS's boundary.
        const bool proverSaysInfinite = r.status == ProverStatus::Infinite;
        EXPECT_EQ(proverSaysInfinite, r.delay <= kProverBoundary)
            << "the decision procedure says " << ProverStatusName(r.status)
            << " at delay " << r.delay << ". Its rule is `startup(to) <= "
               "settledHitstun(from) - delay`, which for this probe is `3 <= 12 - "
               "delay` and turns at " << kProverBoundary << ".";
        EXPECT_NE(r.status, ProverStatus::Unknown)
            << "the search gave up, which no configuration of these characters has "
               "ever produced";
        EXPECT_EQ(r.proverKeepsEdge, r.delay <= kProverBoundary)
            << "the prover's own dead-cancel list disagrees with its verdict about "
               "which side of the boundary delay " << r.delay << " is on";

        // The margin the tool PRINTS, against the frame data it printed it from.
        // This is what turns section 2's single "short by 1" reading into a
        // measured line: at every dead delay the shortfall must be exactly
        // `startup - (hitstun - delay)`, so the one frame at delay 10 is the point
        // where that line crosses zero rather than a number somebody recorded.
        if (!r.proverKeepsEdge)
            EXPECT_EQ(r.shortfall, r.srcStartup - (r.srcHitstun - r.delay))
                << "the decision procedure reports the probe short by " << r.shortfall
                << " at delay " << r.delay << ", and its own rule says "
                << (r.srcStartup - (r.srcHitstun - r.delay)) << ".";

        // THE GAME's boundary, taken off MatchBuilder's resolved window.
        EXPECT_EQ(r.inert, r.delay > kKernelBoundary)
            << "MatchBuilder resolved delay " << r.delay << " to [" << r.earliest
            << ", " << r.latest << "]. Its rule is `[max(startup + delay, "
               "cancelWindowOpen), min(duration - 1, cancelWindowClose)]`, which for "
               "this probe is `[max(3 + delay, 5), 9]` and turns at "
            << kKernelBoundary << ".";

        // And the kernel does what the window says, which is the claim that makes
        // the window worth measuring at all.
        if (r.inert) {
            EXPECT_EQ(r.verdict, KernelVerdict::Inert)
                << "the window is empty and the kernel did `"
                << kernelVerdictName(r.verdict) << "`";
            EXPECT_GT(r.freeTicks, 0u)
                << "the edge is inert and the defender still never left hitstun, so "
                   "something other than this cancel is holding them";
            EXPECT_GT(r.defActs, 0u)
                << "the edge is inert and a mashing defender never got to act";
            // What the kernel does INSTEAD: the move runs to the end of its
            // recovery and the held button starts it again, so the string repeats
            // on the move's own duration. Asserted rather than left as "slower",
            // because it is the sentence that says the repetition is an ordinary
            // button press and not a cancel that happens to be late.
            EXPECT_EQ(r.period, r.srcDuration)
                << "the string repeats every " << r.period << " ticks and the move "
                   "is " << r.srcDuration << " ticks long, so something other than "
                   "the button start is restarting it.";
        } else {
            EXPECT_EQ(r.verdict, KernelVerdict::Connects)
                << "the window is [" << r.earliest << ", " << r.latest
                << "] and the defender was free on " << r.freeTicks
                << " tick(s) inside the combo.";
            EXPECT_EQ(r.freeTicks, 0u);
            EXPECT_EQ(r.defActs, 0u)
                << "a mashing defender started " << r.defActs
                << " move(s) inside a combo the window says is unbroken";
            // The period is MatchBuilder's own number rather than a recorded
            // constant: see Row::FirstTakeableFrame for the derivation.
            EXPECT_EQ(r.period, r.FirstTakeableFrame())
                << "the loop repeats every " << r.period
                << " ticks; the window opens at frame " << r.earliest
                << " and the source connects on frame " << r.srcStartup << ".";
        }
    }

    // --- THE FRAME ON EITHER SIDE -------------------------------------------
    //
    // Each boundary is stated twice: once as the two absolute answers, and once
    // as the fact that they DIFFER. The second is not redundant. It is the actual
    // claim -- "the frame either side of this behaves differently" -- and it is
    // the one that would survive a re-authored character where both absolutes
    // moved together.
    const auto at = [&rows](std::int32_t delay) -> const Row& {
        return rows[static_cast<std::size_t>(delay - kSweepLo)];
    };

    const Row& shipped = at(kAuthoredDelay);
    const Row& oneWider = at(kAuthoredDelay - 1);
    EXPECT_EQ(shipped.status, ProverStatus::Terminating);
    EXPECT_EQ(oneWider.status, ProverStatus::Infinite);
    EXPECT_NE(shipped.status, oneWider.status)
        << "delay " << kAuthoredDelay << " and delay " << (kAuthoredDelay - 1)
        << " get the same verdict, so there is no one-frame boundary here at all.";

    const Row& gameOpen  = at(kKernelBoundary);
    const Row& gameShut  = at(kKernelBoundary + 1);
    EXPECT_FALSE(gameOpen.inert);
    EXPECT_TRUE(gameShut.inert);
    EXPECT_EQ(gameOpen.verdict, KernelVerdict::Connects)
        << "delay " << kKernelBoundary << " did `"
        << kernelVerdictName(gameOpen.verdict) << "`";
    EXPECT_EQ(gameShut.verdict, KernelVerdict::Inert)
        << "delay " << (kKernelBoundary + 1) << " did `"
        << kernelVerdictName(gameShut.verdict) << "`";
    EXPECT_LT(gameOpen.period, gameShut.period)
        << "the kernel's boundary does not change what the game does: the string "
           "repeats every " << gameOpen.period << " ticks on one side and every "
        << gameShut.period << " on the other.";
    EXPECT_EQ(gameOpen.freeTicks, 0u);
    EXPECT_GT(gameShut.freeTicks, 0u);

    // MatchBuilder's own count of inert links moves by exactly one across that
    // frame, over the whole character. A projection that dropped or invented an
    // edge somewhere else would show up here rather than in a period.
    EXPECT_EQ(gameShut.inertLinks, gameOpen.inertLinks + 1)
        << "the character has " << gameOpen.inertLinks << " inert links at delay "
        << kKernelBoundary << " and " << gameShut.inertLinks << " at delay "
        << (kKernelBoundary + 1) << ". One edge changed, so the count must move by "
           "one.";

    // --- THE BAND BETWEEN THEM, which is the file's own finding --------------
    int band = 0;
    for (const Row& r : rows)
        if (r.status == ProverStatus::Infinite && r.inert) ++band;
    EXPECT_EQ(band, kProverBoundary - kKernelBoundary)
        << "there are " << band << " delays at which the decision procedure reports "
           "an infinite the kernel cannot perform. The two boundaries are "
        << (kProverBoundary - kKernelBoundary) << " frames apart, so that is how "
           "many there should be.";
    EXPECT_GT(band, 0)
        << "the analysis and the game now turn on the same frame, which would be an "
           "improvement to MatchBuilder and a rewrite of this file's header rather "
           "than a failure -- but it is not what is being asserted here.";

    // --- AND THE MIDDLE CATEGORY IS EMPTY, asserted rather than assumed ------
    //
    // No row can open its window and still let the defender out, because
    // `cancel_window_ticks` closes at 9 and the move inflicts 12 ticks of hitstun.
    // If a re-authored window ever closes later than the hitstun it inflicts, rows
    // WILL land here -- and then the boundary claim needs a third edge rather than
    // two, so this must be seen to fail rather than left implicit.
    int middle = 0;
    for (const Row& r : rows)
        if (r.verdict == KernelVerdict::OpensButDoesNotLock) ++middle;
    EXPECT_EQ(middle, 0)
        << middle << " delay(s) open the window and still let the defender out. The "
           "kernel boundary is then no longer a single frame: there is an 'opens "
           "but does not connect' band as well, and section 5's claim needs to name "
           "all three edges."
        << SweepTable(rows, kAuthoredDelay, kProverBoundary, kKernelBoundary);

    // --- The clamp nobody authored ------------------------------------------
    //
    // Delays 0, 1 and 2 are the same edge in the kernel, because
    // `cancelWindowOpen` of 5 is later than `startup + delay` for all three. Worth
    // an assertion because it is the kind of thing a designer discovers by
    // accident: below the window's open frame, the delay field does nothing.
    EXPECT_EQ(at(0).earliest, at(2).earliest)
        << "delays 0 and 2 resolve to different windows (" << at(0).earliest
        << " and " << at(2).earliest << "), so `cancelWindowOpen` is no longer "
           "clamping them and the delay field means something below the window";
    EXPECT_EQ(at(0).period, at(2).period);
    EXPECT_LT(at(2).earliest, at(3).earliest)
        << "delay 3 is the first value `cancelWindowOpen` does NOT clamp -- "
           "startup + 3 = 6 is past the window's open frame of 5 -- and it still "
           "resolves to the same frame as delay 2 (" << at(3).earliest << "). The "
           "clamp is swallowing more of the range than the frame data says.";

    RecordProperty("authored_delay", kAuthoredDelay);
    RecordProperty("prover_boundary_delay", kProverBoundary);
    RecordProperty("kernel_boundary_delay", kKernelBoundary);
    RecordProperty("band_of_unperformable_infinites", band);
}
