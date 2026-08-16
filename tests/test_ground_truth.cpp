// GROUND-TRUTH VALIDATION: the printed loop, executed.
//
// ARCHITECTURE.md section 5.5 item 4 names this as the one piece of evidence the
// research contribution cannot buy anywhere else:
//
//     "Take a character the prover calls Infinite, hand the printed loop to the
//      engine as a scripted input trace, and EXECUTE IT. Either the combo works
//      -- the analysis is validated end to end on a running game -- or it does
//      not, and the gap between the model and the game is itself a publishable
//      finding. NO OFFLINE ANALYSIS CAN PRODUCE THIS."
//
// ADR-001 section 6.1 records it as unearned: "Nobody has executed a printed
// loop. That is the contribution no offline tool can produce and it remains
// entirely unearned." It could not be earned before now for two reasons that
// have both just gone away. The kernel had no cancel system until recently, so
// there was no way to perform a chain at all; and every character in the tree
// was a transcription of a third-party MUGEN character, so a loop executed
// against one would have been a statement about somebody else's game.
//
// Both halves are now available. `fighter_a` and `fighter_a_infinite` are this
// project's OWN characters, authored against schema v2 with the engine's own
// numbers in front of the author, and the kernel can cancel. So this file does
// the experiment.
//
// ---------------------------------------------------------------------------
// WHAT IS ACTUALLY BEING CLAIMED, AND WHAT WOULD FALSIFY IT
// ---------------------------------------------------------------------------
// The claim is NOT "the prover is correct". The prover decides a question about
// a graph, and it is right about that graph by construction. The claim is that
// THE GRAPH IS THE GAME -- that a loop the decision procedure prints out of a
// character file is a thing a player can do in the running simulation, at the
// ticks the file says, doing the damage the file says.
//
// Four things would falsify it, and this file is arranged so that each shows up
// as a distinct failure rather than as one undifferentiated red:
//
//   1. THE FILE DOES NOT LOAD. Then nothing downstream means anything, and it is
//      the first finding rather than a setup error. Section 1.
//   2. THE VERDICT DISAGREES. If `fighter_a` is not TERMINATING or
//      `fighter_a_infinite` is not INFINITE, the expectation is NOT adjusted --
//      the test fails and prints the actual verdict and the actual loop witness,
//      because that means a character needs redesigning and the orchestrator has
//      to be told which one. Section 2.
//   3. THE LOOP DOES NOT EXECUTE. The prover says INFINITE and the engine cannot
//      perform it. That is the OTHER publishable outcome, not a bug to hide, so
//      section 3 reports the tick it broke on, the defender's hitstun and moveId
//      at that tick, and a per-tick table around it -- not a bare EXPECT_EQ.
//   4. THE EXPERIMENT PROVES NOTHING. "Pressing buttons does damage" is trivially
//      true and is what a payoff test accidentally measures if nobody guards it.
//      Section 4 runs the SAME derived trace against the SAFE character and
//      requires the defender to get out.
//
// ---------------------------------------------------------------------------
// WHAT IT MEASURED
// ---------------------------------------------------------------------------
// Every number below is asserted, not recorded, so none of them can rot quietly.
//
//   printed witness       stand_lp x5, then [loop] stand_lp
//   derived trace         hold LP -- ONE binding, because the loop's target is
//                         its own source, so there is no motion input, no
//                         release and no timing to get right
//   executed              26 hits in 160 ticks, one every 6, first on tick 4
//   damage                30 a turn, defender 1000 -> 220, strictly decreasing
//   defender              out of hitstun on 0 ticks inside the combo, and never
//                         once allowed to start a move while holding a button
//   SAME TRACE, fighter_a 12 hits, one every 14, defender free on 22 ticks and
//                         starting a move on 11 of them
//
// The two periods -- 6 against 14 -- are the whole difference between the files,
// and the file that differs by one cancel edge is the one whose defender never
// gets a tick back.
//
// ---------------------------------------------------------------------------
// THE FINDING, STATED UP FRONT BECAUSE IT IS THE POINT OF THE FILE
// ---------------------------------------------------------------------------
// Sections 1-4 come out clean: the printed loop executes, on the tick, forever,
// and the control escapes. Section 5 is the one that does not, and it is the
// direction that matters.
//
// `fighter_a` is TERMINATING, and its certificate is that juggle runs down.
// Its `air_mp` cancels into ITSELF, and the prover calls that edge USABLE -- it
// simply cannot be taken more than four times, because `air_mp` spends one point
// of a four-point juggle budget and `nonNegative` refuses the fifth.
//
// THE KERNEL HAS NO JUGGLE. It has no resources at all: `Fighter::meter` exists
// and nothing writes it, and there is no juggle field to write. MatchBuilder
// already says so -- `move.effect` is a KernelOmits row and
// `BuildReport::playsAsAnalysed` is false -- but "a loss table entry" and "the
// character has an infinite the tool certified away" are very different
// sentences, and section 5 turns the first into the second by executing it.
//
// This is the CONSERVATIVE direction in ProverAdapter.h's vocabulary, moved one
// layer down: the analysis is sound about the file, and the projection from the
// file to the running game is what loses the thing termination rested on. It is
// exactly the failure MatchBuilder.h's header predicts in the abstract --
// "either direction means the verdict is about a character nobody is playing" --
// and this file measures it: 18 repetitions in 200 ticks of a cancel the model
// permits four of, with the defender unable to act on any tick in between.
//
// The tests in section 5 PASS. They assert the gap, precisely, because it is a
// known and named limitation of a kernel that has not yet grown resources -- and
// a test that fails on a documented limitation is a broken test. What they must
// never do is let it go unsaid.
//
// ---------------------------------------------------------------------------
// WHY THE TRACE IS DERIVED AND NOT WRITTEN DOWN
// ---------------------------------------------------------------------------
// Every input in this file comes from `ProverResult::prefix` and
// `ProverResult::loop`. Nothing hardcodes "hold LP". The bindings are assigned
// by walking the witness and handing each distinct move its own single button,
// and the trace is a driver that presses the button of whatever move the witness
// says comes next. If the witness changes, the trace changes with it; if the
// witness named a move the character did not have, the test would say so rather
// than quietly press nothing.
//
// That matters because the alternative -- reading the loop, working out by hand
// that it means "hold LP", and writing `kInputLP` into a test -- proves that a
// human can read a verdict. The point is that the ENGINE can.
//
// This test links CseData AND CseKernel, like test_match_bridge, and for the same
// reason: the data layer builds a POD, the kernel consumes it, and only a caller
// ever links both.
#include <gtest/gtest.h>

#include "cse/data/CharacterData.h"
#include "cse/data/MatchBuilder.h"
#include "cse/data/ProverAdapter.h"
#include "cse/kernel/Simulate.h"

#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace cse::data;
using cse::kernel::GameState;
using cse::kernel::InputPair;
using cse::kernel::MatchData;

namespace {

// ============================================================================
// Units, bodies and the stage
// ============================================================================

// Authored in pixels and converted once, here, matching test_combat.cpp and
// test_match_bridge.cpp.
constexpr std::int32_t px(std::int32_t pixels) {
    return pixels * cse::kernel::kSubUnitsPerPixel;
}

// THE BODY IS THE CALLER'S NUMBER AND IS SUPPLIED DELIBERATELY. CharacterData
// carries no body at all (MatchBuilder.h says why), so leaving it out would make
// MatchBuilder's documented default look like the character's own measurement.
// Both files state the same figures in their own `engine.constants` --
// `pushbox_half_width_sub` 3328 and `pushbox_height_sub` 15360 in
// fighter_a_infinite.json, `default_pushbox_sub` [-3328, -15360, 3328, 0] with
// `height_px` 60 in fighter_a.json -- and 3328 == px(13), 15360 == px(60). So
// these are the file's numbers, restated in the unit this test reads in.
constexpr std::int32_t kHalfWidth = px(13);
constexpr std::int32_t kHeight    = px(60);

// WHERE THE TWO FIGHTERS STAND, AND WHY IT IS NOT ResetMatch's OPENING.
// ResetMatch opens them 200 px apart, which is a 174 px body-to-body gap --
// further than anything either character reaches, so nothing would ever connect
// and every assertion below would be about an empty room. These two put the
// origins 34 px apart, hence the bodies (34 - 2*13) = 8 px apart.
//
// 8 px is inside the reach of every move either trace uses: `stand_lp` reaches
// 30 px in fighter_a_infinite and 42 px in fighter_a, and `air_mp` reaches 50 px.
// So no assertion in this file can pass or fail because of distance, which is
// the point -- MatchBuilder builds the box to make "reach is the maximum gap"
// literally true, and test_match_bridge already tests that boundary to the
// sub-unit. Here the margin is deliberately enormous.
//
// NOBODY MOVES DURING ANY TRACE. The attacker holds attack buttons and no
// direction, so stepFighter sets velX to 0; the defender is in hitstun, which
// zeroes velX anyway; and the kernel applies no pushback on hit at all
// (MatchBuilder's `move.pushback` row: "the kernel moves nobody on hit"). The
// gap is therefore 8 px on every tick of every run below, and no verdict here
// rests on the pushback estimate ADR-001 section 6.3 names as the weakest number
// in the corpus.
constexpr std::int32_t kP0X = -px(17);
constexpr std::int32_t kP1X =  px(17);

// The build-wide resource order, positional and identical in every file a build
// loads (ADR-001 section 8 item 7). Passing it is what arms load assertion A03.
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
// Finding the characters
// ============================================================================

// THE STAGED SHIPPING DIRECTORY, NOT THE PHASE-0 CORPUS.
//
// test_match_bridge.cpp and test_prover_adapter.cpp walk up to
// tests/fixtures/characters, which holds the three MUGEN transcriptions. Those
// are EVIDENCE and deliberately never staged next to an executable, because this
// project holds no licence to them (the README beside them records the rule).
//
// This file is about the project's own characters, which ARE shipping content:
// they live in Editor/src/Exported/Characters and tests/CMakeLists.txt stages
// that whole tree to ${CMAKE_CURRENT_BINARY_DIR}/Exported through the same
// cmake/stage_runtime_assets.cmake the Editor and the Player use. ctest runs a
// test with its working directory at the build's tests/ dir, so `Exported/
// Characters` is right there.
//
// Two fallbacks, in order, and the second one reports itself rather than hiding:
//   1. walk up looking for `Exported/Characters/fighter_a.json` -- the staged
//      copy, which is the artifact that would actually ship;
//   2. walk up looking for `Editor/src/Exported/Characters/fighter_a.json` --
//      the source tree, so the test still runs from a shell anywhere in the
//      repository and a missing staging step is a slower test rather than a
//      mysterious one.
// Neither can make anything pass vacuously, because every load below ASSERTs.
std::string charactersDir() {
#ifdef CSE_CHARACTERS_DIR
    return CSE_CHARACTERS_DIR;
#else
    namespace fs = std::filesystem;
    const char* const marker = "fighter_a.json";

    fs::path here = fs::current_path();
    for (int i = 0; i < 8; ++i) {
        const fs::path staged = here / "Exported" / "Characters";
        if (fs::exists(staged / marker)) return staged.string();
        if (!here.has_parent_path() || here.parent_path() == here) break;
        here = here.parent_path();
    }

    here = fs::current_path();
    for (int i = 0; i < 8; ++i) {
        const fs::path source = here / "Editor" / "src" / "Exported" / "Characters";
        if (fs::exists(source / marker)) return source.string();
        if (!here.has_parent_path() || here.parent_path() == here) break;
        here = here.parent_path();
    }

    // Nothing found. Return the staged-relative path so the ASSERT that follows
    // names the place it expected to look.
    return "Exported/Characters";
#endif
}

void loadShipped(const char* file, CharacterData& out) {
    LoadReport report{};
    ASSERT_TRUE(LoadCharacterFile(charactersDir(), file, loadOptions(), out, report))
        << file << " did not load from " << charactersDir() << ".\n"
        << "  rule : " << (report.rule.empty() ? "(no load assertion named)" : report.rule)
        << "\n  error: " << report.error
        << "\n\nA character that does not load is the first finding. Everything "
           "below this point is about a file that reached the engine.";
    // A file that trips A01..A08 returns false above, so reaching here means no
    // rule fired -- but naming the field explicitly is what keeps "the load
    // assertions passed" from being an inference about a return value.
    ASSERT_TRUE(report.rule.empty())
        << file << " loaded but named load assertion " << report.rule;
    ASSERT_FALSE(out.moves.empty()) << file << " loaded with no moves";
}

void analyseShipped(const CharacterData& character, ProverResult& result) {
    ProverReport report{};
    ASSERT_TRUE(AnalyseCharacter(character, proverOptions(), result, report))
        << character.id << " could not be projected into the decision procedure.\n"
        << "  rule : " << report.rule << "\n  error: " << report.error;
}

// ============================================================================
// Building a match
// ============================================================================

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

// SIX SINGLE BITS, AND SINGLE ON PURPOSE. StepAttack takes the first move in
// slot order all of whose bits are held, so a binding whose mask is a superset
// of an earlier one's can never start (MatchBuilder reports it, and
// test_match_bridge watches the report fire). No mask below is a subset of any
// other, so the shadowing rule cannot bite any trace in this file and a stall
// can never be blamed on it.
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

// Build a mirror match. Both sides get the same bindings so that one MatchData
// serves both the silent-defender and the mashing-defender runs; which of the
// two happens is decided entirely by the defender's input bits, never by its
// data. A defender who is handed no bindings could not act even if it were
// actionable, and that would make "the defender never acted" a fact about the
// harness instead of about the combo.
bool buildMirror(const CharacterData& character,
                 const std::vector<MoveBinding>& bindings, MatchBuild& out) {
    BuildOptions options{};
    options.body     = body();
    options.bindings = bindings;
    return BuildMatchData(character, options, character, options, out);
}

const BuildLoss* findLoss(const BuildReport& report, const char* field) {
    for (const BuildLoss& loss : report.losses)
        if (loss.field == field) return &loss;
    return nullptr;
}

// ============================================================================
// The driver: a scripted trace derived from a witness
// ============================================================================

// The sequence of move IDS a witness names, `prefix` then `loop`, with the index
// at which the loop begins. Ids and not indices, because a move index is
// per-character: `stand_lp` is character move 12 in fighter_a_infinite and 0 in
// fighter_a, so a witness carried as integers could not be replayed against the
// other character at all -- and replaying it against the other character is
// exactly what the control does.
struct Witness {
    std::vector<std::string> sequence;
    std::size_t              loopStart = 0;

    std::string ToString() const {
        std::ostringstream s;
        for (std::size_t i = 0; i < sequence.size(); ++i) {
            if (i) s << " > ";
            if (i == loopStart) s << "[loop] ";
            s << sequence[i];
        }
        return s.str();
    }
};

Witness witnessOf(const CharacterData& character, const ProverResult& result) {
    Witness w{};
    for (MoveIndex m : result.prefix)
        if (m < character.moves.size()) w.sequence.push_back(character.moves[m].id);
    w.loopStart = w.sequence.size();
    for (MoveIndex m : result.loop)
        if (m < character.moves.size()) w.sequence.push_back(character.moves[m].id);
    return w;
}

// A witness that is a single move repeated, which is what a self-cancel produces
// and what section 5 needs to build by hand.
Witness selfLoopWitness(const std::string& moveId) {
    Witness w{};
    w.sequence.push_back(moveId);
    w.loopStart = 0;
    return w;
}

// One button per DISTINCT move in the witness, in first-appearance order. A
// witness naming more distinct moves than there are single-bit buttons gets a
// binding of ZERO for the surplus rather than a shorter list, so the shortfall
// surfaces as Driver::Usable saying which move nothing can ask for -- rather
// than as a trace that silently stops following the witness.
std::vector<MoveBinding> bindingsFor(const Witness& w) {
    std::vector<MoveBinding> out;
    for (const std::string& id : w.sequence) {
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

// THE TRACE ITSELF. A cursor over the witness: press the button of the move the
// witness says comes next, and advance the cursor when the attacker actually
// ENTERS that move. Past the end of the sequence the cursor returns to
// `loopStart`, which is what makes the loop repeat.
//
// Advancing on `moveFrame == 0` rather than on a change of `moveId` is the same
// distinction test_match_bridge had to make and for the same reason: this
// witness's loop is a move cancelling into ITSELF, so the id never changes and a
// transition detector would see nothing happen at all. The frame counter is what
// resets, so it is what says a move began.
//
// If the attacker never enters the move being asked for, the cursor never
// advances and the trace holds one button forever. That is the correct failure
// mode -- it stalls visibly, and the per-tick table below shows what the attacker
// was doing instead.
class Driver {
public:
    Driver(const Witness& w, const MoveIndexMap& map,
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
                why = "this character has no move called `" + ids_[i] +
                      "`, so the witness cannot be replayed against it";
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

    const std::vector<std::uint16_t>& Slots() const { return slots_; }
    std::size_t LoopStart() const { return loopStart_; }
    std::size_t LoopLength() const { return slots_.size() - loopStart_; }

private:
    std::vector<std::uint16_t> slots_;
    std::vector<std::uint16_t> buttons_;
    std::vector<std::string>   ids_;
    std::size_t                loopStart_ = 0;
    std::size_t                cursor_    = 0;
};

// ============================================================================
// Running one, and watching from outside
// ============================================================================

// What the defender is allowed to do.
//
// Silent is the recipe fighter_a_infinite.json's own `engine.ground_truth`
// prescribes: "input: zero on every tick".
//
// MashesOnceHit is the stronger experiment and it has to start LATE, which is
// worth explaining because starting it at tick 0 gives the wrong answer for an
// uninteresting reason. Both sides hold the same MatchData; at tick 0 both are
// idle; so if both press, both start the same move, both boxes go live on the
// same tick, and ResolveHits -- which is deliberately symmetric, and argues for
// it at length -- lands BOTH. The attacker goes into hitstun too and there is no
// combo to measure. That is a correct simulation of two people mashing at each
// other and it is not the question. Starting the mash on the tick AFTER the
// combo has opened asks the question a player asks: "I have been hit; can I get
// out?"
enum class DefenderPolicy { Silent, MashesOnceHit };

// One tick, observed from outside. Every field is read off the state the
// simulation produced; nothing is inferred from what the script asked for.
struct Sample {
    std::int32_t  tick        = 0;
    std::uint16_t inputBits   = 0;
    std::uint16_t atkMove     = 0;
    std::uint16_t atkFrame    = 0;
    std::uint8_t  atkHitBits  = 0;
    std::uint16_t defStun     = 0;
    std::uint16_t defMove     = 0;
    std::int32_t  defHealth   = 0;
    std::int32_t  atkHealth   = 0;
    bool          hit         = false;   // the defender's health fell this tick
    bool          defEntered  = false;   // the defender began a move this tick
};

// NAMED TickLog RATHER THAN `Run`, WHICH IS THE OBVIOUS NAME AND DOES NOT WORK.
// A TEST body is a member function of a class deriving from ::testing::Test, and
// that class has a member function `Run`. Class-scope lookup wins over the
// enclosing namespace, so `Run trace = drive(...)` inside a test resolves to
// `Test::Run` and fails with a message about a pointer to member. Found by
// compiling, which is the only way anybody finds it.
struct TickLog {
    std::vector<Sample>        samples;
    std::vector<std::int32_t>  hitTicks;
    std::vector<std::uint16_t> hitMoves;    // the attacker's kernel moveId at each hit
    std::vector<std::int32_t>  hitHealth;   // the defender's health after each hit
    std::vector<std::int32_t>  defenderActedTicks;
    std::int32_t               firstHitTick = -1;
    std::int32_t               lastHitTick  = -1;
    GameState                  finalState{};

    std::size_t Hits() const { return hitTicks.size(); }

    // The ticks, strictly inside the combo, on which the defender's hitstun had
    // run out. THE DIRECT READING of "the defender left hitstun", taken off
    // Fighter::hitstun rather than derived from frame arithmetic.
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

TickLog drive(const MatchData& data, Driver& driver, int ticks,
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
        in.p[0].bits = driver.Bits();
        if (policy == DefenderPolicy::MashesOnceHit && run.firstHitTick >= 0 &&
            t > run.firstHitTick) {
            in.p[1].bits = defenderBits;
        }

        cse::kernel::Simulate(s, in, data);
        driver.Observe(s.p[0].moveId, s.p[0].moveFrame);

        Sample sample{};
        sample.tick       = t;
        sample.inputBits  = in.p[0].bits;
        sample.atkMove    = s.p[0].moveId;
        sample.atkFrame   = s.p[0].moveFrame;
        sample.atkHitBits = s.p[0].alreadyHitBits;
        sample.defStun    = s.p[1].hitstun;
        sample.defMove    = s.p[1].moveId;
        sample.defHealth  = s.p[1].health;
        sample.atkHealth  = s.p[0].health;
        sample.hit        = s.p[1].health < healthBefore;
        sample.defEntered = s.p[1].moveId != 0 && s.p[1].moveFrame == 0;

        if (sample.hit) {
            if (run.firstHitTick < 0) run.firstHitTick = t;
            run.lastHitTick = t;
            run.hitTicks.push_back(t);
            run.hitMoves.push_back(s.p[0].moveId);
            run.hitHealth.push_back(s.p[1].health);
        }
        if (sample.defEntered) run.defenderActedTicks.push_back(t);
        run.samples.push_back(sample);
    }
    run.finalState = s;
    return run;
}

// ============================================================================
// Saying what happened
// ============================================================================

std::string moveName(const MoveIndexMap& map, std::uint16_t slot) {
    if (slot == 0) return "idle";
    const std::string_view id = map.IdOf(slot);
    return id.empty() ? ("slot" + std::to_string(slot)) : std::string(id);
}

// A per-tick table. This is the thing the task asks for in place of a bare
// assertion failure: if the loop breaks, a reader must be able to see WHICH TICK
// and WHAT THE DEFENDER'S STATE WAS without re-running anything.
std::string Table(const TickLog& run, const MoveIndexMap& map,
                  std::int32_t from, std::int32_t count) {
    std::ostringstream s;
    s << "\n  tick  in   attacker move        fr  hitbits  hit   def stun  "
         "def move      def hp\n";
    for (const Sample& sample : run.samples) {
        if (sample.tick < from) continue;
        if (sample.tick >= from + count) break;
        s << "  " << std::setw(4) << sample.tick
          << "  " << buttonName(sample.inputBits)
          << "   " << std::setw(18) << std::left << moveName(map, sample.atkMove)
          << std::right
          << "  " << std::setw(2) << sample.atkFrame
          << "  " << std::setw(7) << static_cast<int>(sample.atkHitBits)
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
    s << "\n  hits            " << run.Hits();
    s << "\n  first hit tick  " << run.firstHitTick;
    s << "\n  last hit tick   " << run.lastHitTick;
    s << "\n  hit ticks       ";
    for (std::size_t i = 0; i < run.hitTicks.size() && i < 24; ++i)
        s << run.hitTicks[i] << (i + 1 < run.hitTicks.size() ? ", " : "");
    if (run.hitTicks.size() > 24) s << "...";
    s << "\n  hit moves       ";
    for (std::size_t i = 0; i < run.hitMoves.size() && i < 12; ++i)
        s << moveName(map, run.hitMoves[i]) << (i + 1 < run.hitMoves.size() ? ", " : "");
    if (run.hitMoves.size() > 12) s << "...";
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
      << ", defender " << run.finalState.p[1].health << "\n";
    return s.str();
}

// The first tick at which the combo stopped being a combo, described in the
// terms the task asks for. Returns an empty string when it never broke.
std::string FirstBreak(const TickLog& run, const MoveIndexMap& map) {
    const std::vector<std::int32_t> free = run.FreeTicks();
    if (free.empty()) return std::string();
    const std::int32_t at = free.front();
    const Sample* sample = nullptr;
    for (const Sample& candidate : run.samples)
        if (candidate.tick == at) { sample = &candidate; break; }
    if (sample == nullptr) return std::string();

    std::int32_t previousHit = -1;
    std::int32_t nextHit     = -1;
    for (std::int32_t tick : run.hitTicks) {
        if (tick < at) previousHit = tick;
        if (tick > at && nextHit < 0) nextHit = tick;
    }

    std::ostringstream s;
    s << "\n  THE COMBO BROKE AT TICK " << at << ".\n"
      << "    defender hitstun    " << sample->defStun << " (zero: actionable)\n"
      << "    defender moveId     " << moveName(map, sample->defMove) << "\n"
      << "    defender health     " << sample->defHealth << "\n"
      << "    attacker moveId     " << moveName(map, sample->atkMove)
      << " at frame " << sample->atkFrame << "\n"
      << "    attacker hit bits   " << static_cast<int>(sample->atkHitBits) << "\n"
      << "    attacker health     " << sample->atkHealth
      << " (below 1000 means the defender traded rather than merely escaped)\n"
      << "    input held          " << buttonName(sample->inputBits) << "\n"
      << "  The previous hit landed on tick " << previousHit
      << " and the next on " << nextHit << ".\n";
    return s.str();
}

// Damage the loop deals in one full turn, read out of the BUILT MoveDefs rather
// than out of the file, so it is the number the kernel will actually subtract.
std::int32_t loopDamagePerTurn(const MatchData& data, const Driver& driver) {
    std::int32_t total = 0;
    for (std::size_t i = driver.LoopStart(); i < driver.Slots().size(); ++i) {
        const cse::kernel::MoveDef* m =
            cse::kernel::MoveAt(data.p[0], driver.Slots()[i]);
        if (m != nullptr) total += m->damage;
    }
    return total;
}

// ============================================================================
// Fixtures, loaded per test rather than shared
// ============================================================================
//
// Built fresh in every test rather than as a global. A MatchData mutated by
// accident in one test and read in another is the kind of coupling that makes a
// failure impossible to localise, and these loads cost microseconds.

struct Subject {
    CharacterData character;
    ProverResult  verdict;
};

void bringUp(const char* file, Subject& out) {
    loadShipped(file, out.character);
    if (::testing::Test::HasFatalFailure()) return;
    analyseShipped(out.character, out.verdict);
}

const char* kSafe     = "fighter_a.json";
const char* kInfinite = "fighter_a_infinite.json";

// How many full turns of the loop section 3 requires, and how many ticks it is
// given to produce them. 160 ticks is chosen against the file: the loop is
// authored to repeat every 6 ticks for 30 damage, so 160 ticks is ~26 turns and
// ~780 damage against 1000 health. It stays clear of the KO on purpose --
// Fighter::health clamps at zero and "damage keeps accruing" cannot be measured
// against a clamped number. The test asserts the defender is still alive at the
// end, so a character whose damage grew past this budget says so rather than
// quietly measuring nothing.
constexpr int kPayoffTicks = 160;
constexpr int kMinTurns    = 20;

// Section 5 runs longer, because the edge it exercises repeats every 11 ticks
// rather than every 6 and the point is the size of the overrun.
constexpr int kGapTicks = 200;

// ResetMatch's opening health (Simulate.cpp). Named so the damage arithmetic
// below reads as a subtraction from a known starting point rather than as a
// magic 1000.
constexpr std::int32_t kStartingHealth = 1000;

}  // namespace

// ============================================================================
// 1. THE FILES LOAD
// ============================================================================

TEST(GroundTruthLoading, BothOriginalCharactersLoadWithNoAssertionFailure) {
    for (const char* file : { kSafe, kInfinite }) {
        SCOPED_TRACE(file);
        CharacterData c{};
        loadShipped(file, c);
        if (::testing::Test::HasFatalFailure()) return;

        // The id is the file's stem (CharacterData.h), so this also proves the
        // loader opened the file this test names and not one beside it.
        const std::string name(file);
        EXPECT_EQ(c.id, name.substr(0, name.size() - 5));
        // THE TWO CHARACTERS NO LONGER HAVE THE SAME MOVE COUNT, and that is the
        // point rather than an inconvenience. `fighter_a` was brought up to the
        // roster standard the author set -- six standing, six crouching and six
        // aerial normals -- which took it from 18 moves to 22.
        // `fighter_a_infinite` deliberately stayed at 18: it exists to be the
        // smallest thing that makes the prover print a loop, and growing it would
        // only make the witness harder to read.
        //
        // Asserted per file rather than dropped, because the frame numbers the
        // rest of this file reasons about were read out of these two files and a
        // silent roster change is exactly what would rot them.
        const std::size_t expectedMoves = (name == kSafe) ? 22u : 18u;
        EXPECT_EQ(c.moves.size(), expectedMoves)
            << c.id << " no longer has " << expectedMoves << " moves; the frame "
                       "numbers this file reasons about were read out of it.";
        EXPECT_GT(c.cancels.size(), 0u);
        EXPECT_FALSE(c.cancelsFrom.empty())
            << "RebuildIndices did not run, so no tick could ask what a move "
               "cancels into";
    }
}

// The load assertions, re-derived from the loaded data rather than trusted. The
// loader checks these and returns false when they fail; checking them again from
// the outside is what tells "the file satisfies A01" apart from "nobody looked".
// A01 in particular is the one that has already fabricated an infinite in this
// project (ADR-001 section 5, D8: floor 10 against Kung Fu Girl's authored
// hittime of 9).
TEST(GroundTruthLoading, TheLoadAssertionsAreRederivedRatherThanTrusted) {
    for (const char* file : { kSafe, kInfinite }) {
        SCOPED_TRACE(file);
        CharacterData c{};
        loadShipped(file, c);
        if (::testing::Test::HasFatalFailure()) return;

        // A01: decay.floor must be <= the smallest hitstun in the file. Both
        // implementations compute max(floor, base - step*n), so a floor ABOVE a
        // move's authored hitstun RAISES it and invents frame advantage.
        std::int32_t smallest = c.moves[0].hitstun;
        for (const Move& m : c.moves) smallest = m.hitstun < smallest ? m.hitstun : smallest;
        EXPECT_LE(c.decay.floor, smallest)
            << "A01: decay.floor " << c.decay.floor << " exceeds the smallest "
               "authored hitstun " << smallest << ", which RAISES that move's "
               "hitstun and fabricates frame advantage the file never authored.";
        EXPECT_GT(smallest, 0);

        // A02: `multiplicative` is not in the enum any more, so a file carrying
        // one cannot have loaded. Named so the property is visible rather than
        // implied by an enum with three members.
        EXPECT_TRUE(c.decay.kind == DecayKind::None ||
                    c.decay.kind == DecayKind::Linear ||
                    c.decay.kind == DecayKind::Table);

        // A03: the build-wide positional resource contract. comboprover keys its
        // ResourceVec by index, so a file whose index 0 is `juggle` compares
        // juggle against meter and says nothing.
        ASSERT_EQ(c.resources.size(), kBuildResources.size());
        for (std::size_t i = 0; i < kBuildResources.size(); ++i)
            EXPECT_EQ(c.resources[i].name, kBuildResources[i])
                << "A03: resource " << i << " is `" << c.resources[i].name
                << "` and the build's order says `" << kBuildResources[i] << "`.";
    }
}

// ============================================================================
// 2. THE VERDICTS
// ============================================================================

// THE EXPECTATION IS NOT ADJUSTED IF THIS FAILS. A disagreement here means one
// of the two characters is not the character it was authored to be, and the
// right response is to redesign it -- so the failure prints the whole verdict,
// including the loop witness, rather than an enum mismatch.
TEST(GroundTruthVerdict, TheSafeCharacterTerminatesAndTheBuggedOneDoesNot) {
    Subject safe{}, bugged{};
    bringUp(kSafe, safe);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    bringUp(kInfinite, bugged);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    EXPECT_EQ(safe.verdict.status, ProverStatus::Terminating)
        << "fighter_a.json was authored to be SAFE and the decision procedure "
           "does not agree. THE EXPECTATION MUST NOT BE RELAXED: this character "
           "needs redesigning.\n"
        << DescribeVerdict(safe.character, safe.verdict);
    EXPECT_TRUE(safe.verdict.loop.empty())
        << "a terminating verdict came with a loop witness, which is a "
           "contradiction in the result rather than a fact about the character";

    EXPECT_EQ(bugged.verdict.status, ProverStatus::Infinite)
        << "fighter_a_infinite.json carries one deliberate infinite -- its own "
           "`engine.the_bug` says cancels[0] is `stand_lp -> stand_lp, delay 2, "
           "on hit` -- and the decision procedure did not find it. Either the "
           "bug was removed from the file or the procedure stopped finding this "
           "shape, and both are findings.\n"
        << DescribeVerdict(bugged.character, bugged.verdict);

    // Neither may be Unknown. ADR-001 gate 2 measured 63/79/10 configurations
    // against a limit of 200000 and never produced one; a character that does
    // would be news.
    EXPECT_FALSE(safe.verdict.capped);
    EXPECT_FALSE(bugged.verdict.capped);
    EXPECT_NE(safe.verdict.status, ProverStatus::Unknown);
    EXPECT_NE(bugged.verdict.status, ProverStatus::Unknown);

    // THE STAGE CAVEAT IS GONE, AND THAT IS NEW. All three Phase-0 fixtures
    // declare `midscreen` and are answered in the corner, so ADR-001 section 5.3
    // requires every one of their verdicts to be marked conditional -- and
    // test_prover_adapter.cpp asserts the warning fires on all three. These two
    // files declare `corner`, which is the stage the in-engine procedure
    // actually answers, so for the first time in this project the verdict needs
    // no asterisk and the midscreen pushback estimate ADR-001 section 6.3 calls
    // "the weakest number in the corpus" is not underneath it.
    EXPECT_EQ(safe.verdict.stage, ProverStage::Corner);
    EXPECT_EQ(safe.verdict.fileStage, "corner");
    EXPECT_EQ(bugged.verdict.fileStage, "corner");

    for (const CharacterData* c : { &safe.character, &bugged.character }) {
        ProverResult again{};
        ProverReport report{};
        ASSERT_TRUE(AnalyseCharacter(*c, proverOptions(), again, report));
        for (const std::string& warning : report.warnings)
            EXPECT_EQ(warning.find("answers the corner"), std::string::npos)
                << c->id << " was warned that its declared stage disagrees with "
                   "the one answered, though it declares `" << c->stage << "`: "
                << warning;
    }
}

TEST(GroundTruthVerdict, TheWitnessNamesTheEdgeTheFileSaysIsTheBug) {
    Subject bugged{};
    bringUp(kInfinite, bugged);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    ASSERT_EQ(bugged.verdict.status, ProverStatus::Infinite)
        << DescribeVerdict(bugged.character, bugged.verdict);

    ASSERT_FALSE(bugged.verdict.loop.empty())
        << "INFINITE with an empty loop is a word with nothing behind it";
    for (MoveIndex m : bugged.verdict.loop)
        ASSERT_LT(m, bugged.character.moves.size());
    for (MoveIndex m : bugged.verdict.prefix)
        ASSERT_LT(m, bugged.character.moves.size());

    const Witness w = witnessOf(bugged.character, bugged.verdict);
    // The file claims, in `engine.the_bug`, that cancels[0] is the only edge in
    // the graph whose `engine.tier` does not strictly increase and therefore the
    // only cycle. If the procedure prints a different loop, the file's claim is
    // wrong and that is worth a loud failure rather than a shrug.
    for (MoveIndex m : bugged.verdict.loop)
        EXPECT_EQ(bugged.character.moves[m].id, "stand_lp")
            << "the printed loop is `" << w.ToString()
            << "`, and the file's own `engine.the_bug` says the only cycle in "
               "the graph is stand_lp into itself.\n"
            << DescribeVerdict(bugged.character, bugged.verdict);

    // And the edge itself exists, on hit, from stand_lp to stand_lp -- checked
    // in the loaded cancel table rather than in the JSON, because the loaded
    // table is what both the prover and the kernel are built from.
    const MoveIndex lp = bugged.character.FindMove("stand_lp");
    ASSERT_NE(lp, kInvalidMove);
    int selfEdges = 0;
    for (const Cancel& e : bugged.character.cancels)
        if (e.from == lp && e.to == lp) {
            ++selfEdges;
            EXPECT_EQ(e.delay, 2);
            EXPECT_TRUE(e.on == Contact::Hit || e.on == Contact::Always);
        }
    EXPECT_EQ(selfEdges, 1)
        << "the file is supposed to carry exactly ONE deliberate bug";
}

// ============================================================================
// 3. THE PAYOFF -- the printed loop, executed
// ============================================================================

TEST(GroundTruthPayoff, ThePrintedLoopExecutesInTheKernel) {
    Subject bugged{};
    bringUp(kInfinite, bugged);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    ASSERT_EQ(bugged.verdict.status, ProverStatus::Infinite)
        << DescribeVerdict(bugged.character, bugged.verdict);
    ASSERT_FALSE(bugged.verdict.loop.empty());

    // --- Turn the witness into inputs ---------------------------------------
    const Witness w = witnessOf(bugged.character, bugged.verdict);
    const std::vector<MoveBinding> bindings = bindingsFor(w);
    ASSERT_FALSE(bindings.empty());
    ASSERT_LE(bindings.size(), kButtonPoolSize)
        << "the witness names " << bindings.size() << " distinct moves and there "
           "are only " << kButtonPoolSize << " single-bit buttons to hand out. "
           "Two moves sharing a mask would let StepAttack's first-wins rule pick "
           "the wrong one, so the trace would stop being the witness.";

    MatchBuild build{};
    ASSERT_TRUE(buildMirror(bugged.character, bindings, build))
        << "p0: " << build.report[0].error << " / p1: " << build.report[1].error;

    Driver driver(w, build.moves[0], bindings);
    std::string why;
    ASSERT_TRUE(driver.Usable(why)) << "the witness cannot be driven: " << why;

    // The minimal binding is deliberate and MatchBuilder is expected to say so:
    // every cancel edge whose target is one of the 17 unbound moves can never be
    // taken. That warning is a property of the BINDING TABLE, not of the
    // character, and asserting it here is the guard that the trace really is
    // minimal rather than accidentally holding six buttons.
    bool namedTheUnreachableEdges = false;
    for (const std::string& warning : build.report[0].warnings)
        if (warning.find("no button bound") != std::string::npos)
            namedTheUnreachableEdges = true;
    EXPECT_TRUE(namedTheUnreachableEdges)
        << "the build did not report that most cancel edges point at unbound "
           "moves, so either the binding table is not minimal or the warning "
           "stopped being produced";

    // --- Execute ------------------------------------------------------------
    TickLog run = drive(build.data, driver, kPayoffTicks, DefenderPolicy::Silent, 0);

    const std::size_t loopLength = driver.LoopLength();
    ASSERT_GT(loopLength, 0u);
    const std::size_t wanted = loopLength * static_cast<std::size_t>(kMinTurns);

    // THE HEADLINE. If this fails, the prover said Infinite and the engine could
    // not perform it, which is the OTHER publishable outcome. Say where it broke.
    ASSERT_GE(run.Hits(), wanted)
        << "THE PRINTED LOOP DID NOT EXECUTE.\n"
        << "  witness: " << w.ToString() << "\n"
        << "  asked for " << kMinTurns << " turns (" << wanted
        << " hits) in " << kPayoffTicks << " ticks and got " << run.Hits() << ".\n"
        << "This is a MEASURED GAP BETWEEN THE MODEL AND THE GAME "
           "(ARCHITECTURE.md section 5.5 item 4), not a flaky test."
        << FirstBreak(run, build.moves[0])
        << Summary(run, build.moves[0])
        << Table(run, build.moves[0], 0, 48);

    // --- Every hit came from the move the loop says, in order ----------------
    for (std::size_t i = 0; i < wanted; ++i) {
        const std::uint16_t expected =
            driver.Slots()[driver.LoopStart() + (i % loopLength)];
        ASSERT_EQ(run.hitMoves[i], expected)
            << "hit " << i << " (tick " << run.hitTicks[i] << ") came from `"
            << moveName(build.moves[0], run.hitMoves[i]) << "` and the loop says `"
            << moveName(build.moves[0], expected) << "`."
            << Summary(run, build.moves[0]);
    }

    // --- The defender never left hitstun between them ------------------------
    //
    // Read straight off Fighter::hitstun on every tick inside the combo, not
    // derived from frame arithmetic. The tick each hit lands on is excluded, and
    // has to be: hitstun is decremented at the top of stepFighter and re-set by
    // ResolveHits at the bottom, so on the very first hit's tick the defender was
    // legitimately free -- that is what being hit out of neutral means.
    const std::vector<std::int32_t> free = run.FreeTicks();
    EXPECT_TRUE(free.empty())
        << "the defender was out of hitstun on " << free.size()
        << " tick(s) inside the combo, so this is a long string with gaps in it "
           "rather than an unescapable loop."
        << FirstBreak(run, build.moves[0])
        << Summary(run, build.moves[0])
        << Table(run, build.moves[0], run.firstHitTick, 32);

    // --- It repeats on a fixed period ---------------------------------------
    //
    // The interval from one turn of the loop to the next. A loop whose period
    // grew would still land hits and would not be an infinite; a constant period
    // is what "forever" looks like from outside.
    ASSERT_GE(run.hitTicks.size(), loopLength + 1);
    const std::int32_t period =
        run.hitTicks[loopLength] - run.hitTicks[0];
    for (std::size_t i = 0; i + loopLength < wanted; ++i)
        EXPECT_EQ(run.hitTicks[i + loopLength] - run.hitTicks[i], period)
            << "turn " << i << " of the loop took a different number of ticks "
               "than the first, so the loop is not periodic."
            << Summary(run, build.moves[0]);

    // --- The damage keeps accruing -------------------------------------------
    //
    // This is what makes it an infinite rather than a two-hit combo. Asserted
    // against the damage the BUILT MoveDefs carry, so the number is the one the
    // kernel subtracts rather than one this test believes.
    const std::int32_t perTurn = loopDamagePerTurn(build.data, driver);
    ASSERT_GT(perTurn, 0) << "the loop deals no damage at all";
    for (std::size_t i = 1; i < run.Hits(); ++i)
        ASSERT_LT(run.hitHealth[i], run.hitHealth[i - 1])
            << "hit " << i << " at tick " << run.hitTicks[i]
            << " did not reduce the defender's health";
    EXPECT_GT(run.finalState.p[1].health, 0)
        << "the defender was knocked out inside the measured window, so the last "
           "hits were measured against Fighter::health's clamp at zero rather "
           "than against real damage. Lower kPayoffTicks.";

    const std::size_t turns = run.Hits() / loopLength;
    EXPECT_GE(turns, static_cast<std::size_t>(kMinTurns));
    const std::int32_t expected =
        kStartingHealth - static_cast<std::int32_t>(turns) * perTurn;
    EXPECT_EQ(run.hitHealth[turns * loopLength - 1], expected)
        << "after " << turns << " turns at " << perTurn
        << " damage each the defender should be at " << expected << "."
        << Summary(run, build.moves[0]);

    // The attacker was never touched, so none of the above is a trade being
    // read as a combo.
    EXPECT_EQ(run.finalState.p[0].health, kStartingHealth);

    // And the record, kept on success too, because this run IS the ground-truth
    // evidence ARCHITECTURE.md section 5.5 item 4 says no offline analysis can
    // produce. RecordProperty for a harness, stdout for a human.
    RecordProperty("witness", w.ToString());
    RecordProperty("loop_period_ticks", period);
    RecordProperty("hits", static_cast<int>(run.Hits()));
    RecordProperty("damage_per_turn", perTurn);
    RecordProperty("defender_free_ticks", static_cast<int>(free.size()));

    std::cout
        << "\n[ GROUND TRUTH ] the printed loop executed on `"
        << bugged.character.id << "`\n"
        << "  witness         " << w.ToString() << "\n"
        << "  derived trace   hold " << buttonName(bindings[0].button)
        << " (one binding; the loop's target is its own source)\n"
        << "  executed        " << run.Hits() << " hits in " << kPayoffTicks
        << " ticks, one every " << period << ", first on tick "
        << run.firstHitTick << "\n"
        << "  damage          " << perTurn << " per turn, defender "
        << kStartingHealth << " -> " << run.finalState.p[1].health << "\n"
        << "  defender        out of hitstun on " << free.size()
        << " tick(s) inside the combo\n\n";
}

// The strongest available statement of "the defender cannot get out": give them
// the same character, the same bindings and a held button from the tick after
// the combo opens, and require that the kernel never once lets them start a
// move. Unlike reading Fighter::hitstun, this asks the simulation itself.
TEST(GroundTruthPayoff, TheDefenderCannotMashOutOfIt) {
    Subject bugged{};
    bringUp(kInfinite, bugged);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    ASSERT_EQ(bugged.verdict.status, ProverStatus::Infinite);
    ASSERT_FALSE(bugged.verdict.loop.empty());

    const Witness w = witnessOf(bugged.character, bugged.verdict);
    const std::vector<MoveBinding> bindings = bindingsFor(w);
    MatchBuild build{};
    ASSERT_TRUE(buildMirror(bugged.character, bindings, build))
        << build.report[0].error;

    Driver silentDriver(w, build.moves[0], bindings);
    Driver mashDriver(w, build.moves[0], bindings);
    std::string why;
    ASSERT_TRUE(silentDriver.Usable(why)) << why;

    const TickLog silent =
        drive(build.data, silentDriver, kPayoffTicks, DefenderPolicy::Silent, 0);
    const TickLog mashed = drive(build.data, mashDriver, kPayoffTicks,
                             DefenderPolicy::MashesOnceHit, bindings[0].button);

    ASSERT_GT(silent.Hits(), 0u);

    EXPECT_TRUE(mashed.defenderActedTicks.empty())
        << "the defender started a move on " << mashed.defenderActedTicks.size()
        << " tick(s) while holding a button through the combo, so it is escapable."
        << FirstBreak(mashed, build.moves[0])
        << Summary(mashed, build.moves[0])
        << Table(mashed, build.moves[0], mashed.firstHitTick, 32);

    // Mashing changed nothing at all: the attacker's hit ticks are identical to
    // the silent run's. That is the difference between "the defender lost the
    // exchange" and "the defender never had an exchange to lose".
    EXPECT_EQ(mashed.hitTicks, silent.hitTicks)
        << "holding a button changed the combo, so the defender did get a turn "
           "somewhere even though no move started."
        << Summary(mashed, build.moves[0]);
    EXPECT_EQ(mashed.finalState.p[0].health, kStartingHealth)
        << "the attacker took damage from a defender who was supposedly never "
           "actionable";
}

// The loop executes inside a rollback-deterministic kernel, which is the claim
// the rest of the engine exists to support. Cheap, and it guards against the
// trace depending on anything that is not in (state, inputs, data).
TEST(GroundTruthPayoff, TheExecutedLoopIsBitIdenticalOnAReplay) {
    Subject bugged{};
    bringUp(kInfinite, bugged);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    ASSERT_FALSE(bugged.verdict.loop.empty());

    const Witness w = witnessOf(bugged.character, bugged.verdict);
    const std::vector<MoveBinding> bindings = bindingsFor(w);
    MatchBuild build{};
    ASSERT_TRUE(buildMirror(bugged.character, bindings, build))
        << build.report[0].error;

    Driver first(w, build.moves[0], bindings);
    Driver again(w, build.moves[0], bindings);
    const TickLog a = drive(build.data, first, kPayoffTicks, DefenderPolicy::Silent, 0);
    const TickLog b = drive(build.data, again, kPayoffTicks, DefenderPolicy::Silent, 0);

    ASSERT_GT(a.Hits(), 0u) << "a run in which nothing happened is trivially "
                               "reproducible and proves nothing";
    EXPECT_EQ(0, std::memcmp(&a.finalState, &b.finalState, sizeof(GameState)))
        << "two runs of the same derived trace diverged";
    EXPECT_EQ(cse::kernel::Checksum(a.finalState), cse::kernel::Checksum(b.finalState));
    EXPECT_EQ(a.hitTicks, b.hitTicks);
}

// ============================================================================
// 4. THE CONTROL -- the same trace against the safe character
// ============================================================================

// Without this, section 3 proves only that holding a button does damage.
//
// The trace here is not re-derived from fighter_a's own verdict -- fighter_a
// TERMINATES and therefore prints no loop at all. It is the INFINITE character's
// witness, replayed move-for-move against the safe one, which is possible
// because both files author a move called `stand_lp`. That correspondence is
// asserted rather than assumed: a witness naming a move fighter_a lacks would
// make this control vacuous, and Driver::Usable is what says so.
TEST(GroundTruthControl, TheSameTraceAgainstTheSafeCharacterLetsTheDefenderOut) {
    Subject bugged{}, safe{};
    bringUp(kInfinite, bugged);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    bringUp(kSafe, safe);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    ASSERT_FALSE(bugged.verdict.loop.empty());
    ASSERT_EQ(safe.verdict.status, ProverStatus::Terminating)
        << DescribeVerdict(safe.character, safe.verdict);

    const Witness w = witnessOf(bugged.character, bugged.verdict);
    const std::vector<MoveBinding> bindings = bindingsFor(w);

    MatchBuild build{};
    ASSERT_TRUE(buildMirror(safe.character, bindings, build))
        << build.report[0].error;

    Driver driver(w, build.moves[0], bindings);
    std::string why;
    ASSERT_TRUE(driver.Usable(why))
        << "the infinite character's witness cannot be replayed against "
           "fighter_a, so this control proves nothing: " << why;

    TickLog run = drive(build.data, driver, kPayoffTicks, DefenderPolicy::Silent, 0);

    // The trace still CONNECTS -- otherwise "the defender escaped" would just
    // mean "nothing ever hit them", which is the easiest green in the world to
    // get by accident.
    ASSERT_GT(run.Hits(), 0u)
        << "the same trace never connected at all against fighter_a, so the "
           "control is measuring distance or bindings rather than frame data."
        << Summary(run, build.moves[0])
        << Table(run, build.moves[0], 0, 24);

    // THE CONTROL'S CLAIM. The defender gets out, repeatedly, between hits.
    const std::vector<std::int32_t> free = run.FreeTicks();
    EXPECT_FALSE(free.empty())
        << "the defender never left hitstun against the character that was "
           "authored to be SAFE, so fighter_a has an executable infinite the "
           "prover did not report. THAT IS A FINDING, not a test to relax."
        << Summary(run, build.moves[0])
        << Table(run, build.moves[0], run.firstHitTick, 32);

    // And it is not one stray tick: the defender is free between essentially
    // every pair of hits, which is what a chain that ends looks like.
    EXPECT_GE(free.size(), run.Hits() - 1)
        << "the defender got out fewer times than there were gaps between hits."
        << Summary(run, build.moves[0]);

    // The same trace, side by side. The safe character's `stand_lp` has no edge
    // back into itself, so the only repetition available is the whole move
    // running out and the held button starting it again -- which is slower than
    // the hitstun it inflicts, by construction. Law 1 of fighter_a's design, as
    // its author put it: every ground move is at most +2 on hit and the fastest
    // is 3 frames of startup, so no ground move links into any ground move.
    MatchBuild buggedBuild{};
    ASSERT_TRUE(buildMirror(bugged.character, bindings, buggedBuild))
        << buggedBuild.report[0].error;
    Driver buggedDriver(w, buggedBuild.moves[0], bindings);
    const TickLog buggedRun = drive(buggedBuild.data, buggedDriver, kPayoffTicks,
                                DefenderPolicy::Silent, 0);

    ASSERT_GE(run.hitTicks.size(), 2u);
    ASSERT_GE(buggedRun.hitTicks.size(), 2u);
    const std::int32_t safePeriod   = run.hitTicks[1] - run.hitTicks[0];
    const std::int32_t buggedPeriod = buggedRun.hitTicks[1] - buggedRun.hitTicks[0];
    EXPECT_GT(safePeriod, buggedPeriod)
        << "the same inputs repeat at " << safePeriod << " ticks against "
           "fighter_a and " << buggedPeriod << " against fighter_a_infinite. "
           "The whole difference between the two files is one cancel edge, so "
           "the safe one must be strictly slower.";

    RecordProperty("safe_period_ticks", safePeriod);
    RecordProperty("bugged_period_ticks", buggedPeriod);
    RecordProperty("safe_escape_ticks", static_cast<int>(free.size()));
}

// And the escape is real in the strongest sense: the defender, holding a button
// from the tick after the first hit, actually starts a move.
TEST(GroundTruthControl, TheSafeCharactersDefenderReallyGetsToAct) {
    Subject bugged{}, safe{};
    bringUp(kInfinite, bugged);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    bringUp(kSafe, safe);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    ASSERT_FALSE(bugged.verdict.loop.empty());

    const Witness w = witnessOf(bugged.character, bugged.verdict);
    const std::vector<MoveBinding> bindings = bindingsFor(w);
    MatchBuild build{};
    ASSERT_TRUE(buildMirror(safe.character, bindings, build)) << build.report[0].error;

    Driver driver(w, build.moves[0], bindings);
    const TickLog run = drive(build.data, driver, kPayoffTicks,
                          DefenderPolicy::MashesOnceHit, bindings[0].button);

    ASSERT_GT(run.Hits(), 0u);
    EXPECT_FALSE(run.defenderActedTicks.empty())
        << "the defender held a button through the whole combo and the kernel "
           "never let them start a move, against the character authored to be "
           "escapable."
        << Summary(run, build.moves[0])
        << Table(run, build.moves[0], run.firstHitTick, 32);

    // The first escape lands after the first hit and before the second, which is
    // the gap the frame data predicts rather than merely somewhere in the run.
    ASSERT_GE(run.hitTicks.size(), 2u);
    ASSERT_FALSE(run.defenderActedTicks.empty());
    EXPECT_GT(run.defenderActedTicks.front(), run.hitTicks[0]);
    EXPECT_LE(run.defenderActedTicks.front(), run.hitTicks[1])
        << "the defender's first move came after the second hit, so the gap this "
           "control is about is not where the frame data says it is."
        << Summary(run, build.moves[0]);

    // ONE ESCAPE PER GAP, which is the shape of a blockstring rather than of a
    // loop: the attacker's pressure re-establishes after every one of them (the
    // defender's counter never lands, because the next hit arrives first), and
    // that is a real fighting game rather than an infinite. Stated as an equality
    // rather than as "more than zero" so that a character which merely leaked one
    // escape somewhere would not pass this control.
    EXPECT_EQ(run.defenderActedTicks.size(), run.hitTicks.size() - 1)
        << "the defender acted " << run.defenderActedTicks.size() << " times "
           "across " << (run.hitTicks.size() - 1) << " gaps between hits."
        << Summary(run, build.moves[0]);

    RecordProperty("safe_defender_acts", static_cast<int>(run.defenderActedTicks.size()));
    RecordProperty("safe_hits", static_cast<int>(run.Hits()));
}

// ============================================================================
// 5. THE GAP -- a cancel the prover proved could not repeat, repeating
// ============================================================================
//
// READ THE HEADER OF THIS FILE BEFORE READING THESE TWO TESTS. They PASS, and
// what they assert is a defect in the projection from the file to the kernel,
// measured rather than argued. This is the outcome ARCHITECTURE.md section 5.5
// item 4 calls "the gap between the model and the game is itself a publishable
// finding", arrived at from the safe character rather than the bugged one.

// First half: establish, from the data, that fighter_a contains exactly one
// self-cancel the prover keeps, and that the only thing stopping it looping is a
// resource.
TEST(GroundTruthGap, TheSafeCharactersOnlyLiveSelfCancelIsStoppedByAResource) {
    Subject safe{};
    bringUp(kSafe, safe);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    ASSERT_EQ(safe.verdict.status, ProverStatus::Terminating)
        << DescribeVerdict(safe.character, safe.verdict);

    // Every edge from a move back into itself, split by whether the decision
    // procedure kept it. A dead edge cannot loop in the model and, as it happens,
    // cannot loop in the kernel either -- the two `short by 1` probes fighter_a
    // authors resolve to an EMPTY kernel window, because their authored
    // cancel_window closes before their delay opens.
    std::vector<CancelIndex> live, dead;
    for (std::size_t i = 0; i < safe.character.cancels.size(); ++i) {
        const Cancel& e = safe.character.cancels[i];
        if (e.from != e.to) continue;
        bool reportedDead = false;
        for (const ProverDeadCancel& d : safe.verdict.deadCancels)
            if (d.cancel == static_cast<CancelIndex>(i)) { reportedDead = true; break; }
        (reportedDead ? dead : live).push_back(static_cast<CancelIndex>(i));
    }

    ASSERT_EQ(live.size(), 1u)
        << "fighter_a has " << live.size() << " self-cancels the prover keeps; "
           "this test was written against exactly one (`air_mp`), and the count "
           "changing is a change to the character rather than to the test.\n"
        << DescribeVerdict(safe.character, safe.verdict);
    EXPECT_GT(dead.size(), 0u)
        << "the file is supposed to author self-cancels that are printed DEAD, so "
           "the prover is seen rejecting something rather than only accepting";

    const Cancel& edge = safe.character.cancels[live[0]];
    const Move&   src  = safe.character.moves[edge.from];
    EXPECT_EQ(src.id, "air_mp");

    // The edge is comfortably live on frame arithmetic alone: it leaves far more
    // hitstun than the follow-up needs. So frame data is NOT what stops it.
    EXPECT_LE(src.startup, src.hitstun - edge.delay)
        << "`" << src.id << "` -> itself does not close on frames, so the "
           "resource argument below is about an edge that was never usable";

    // What DOES stop it: `air_mp` spends juggle, juggle has a floor of 0, and the
    // budget is finite. This is the whole termination argument for this cycle.
    const ResourceIndex juggle = 1;   // A03: the build's positional order
    ASSERT_LT(static_cast<std::size_t>(juggle), safe.character.resources.size());
    ASSERT_EQ(safe.character.resources[juggle].name, "juggle");

    std::int32_t spend = 0;
    for (const ResourceAmount& a : src.effect)
        if (a.resource == juggle) spend = a.value;
    EXPECT_LT(spend, 0)
        << "`" << src.id << "` does not spend juggle, so nothing in the model "
           "stops this self-cancel and the verdict should not have been "
           "TERMINATING at all.\n"
        << DescribeVerdict(safe.character, safe.verdict);

    const std::int32_t budget = safe.character.resources[juggle].initial;
    EXPECT_GT(budget, 0);
    // How many repetitions the model permits, and the number section 5's second
    // half is measured against.
    const std::int32_t permitted = budget / (-spend);
    EXPECT_GT(permitted, 0);
    RecordProperty("model_permits_repetitions", permitted);

    // And the certificate names juggle, so this is not an incidental field --
    // it is the reason `describe()` gives for the character terminating.
    //
    // UNCONDITIONAL, AND THAT IS THE POINT. This was written as
    // `if (safe.verdict.hasRanking) { ... }`, which is the shape of a check that
    // can never fail: with no certificate the body does not run, the test stays
    // green, and the header comment's claim -- "its certificate is that juggle
    // runs down" -- would be resting on nothing anybody executed. A guarded
    // assertion is indistinguishable from a passing one in the output, which is
    // exactly the vacuous-test class this repository has been bitten by before
    // (the non-ACP filename test that compiled to mojibake and asserted nothing).
    //
    // The certificate is ALSO the rarest thing about this character. ADR-001
    // gate 3 measured `hasRanking` false for all three fixtures, because
    // `spendOnly` is cleared by any reachable cancel with a positive resource
    // effect and every real fighting game builds meter on hit. So if this ever
    // goes false, `fighter_a` has quietly stopped being the thing it was authored
    // to be, and that must be a red test rather than a skipped branch.
    ASSERT_TRUE(safe.verdict.hasRanking)
        << "`" << kSafe << "` no longer carries a ranking certificate. It was "
           "authored to carry one -- the first character in this repository to do "
           "so -- and section 5 of this file describes the gap in terms of what "
           "that certificate promises.\n"
        << DescribeVerdict(safe.character, safe.verdict);

    bool namesJuggle = false;
    for (ResourceIndex r : safe.verdict.rankingOrder)
        if (r == juggle) namesJuggle = true;
    EXPECT_TRUE(namesJuggle)
        << "the ranking certificate does not mention juggle, though juggle is "
           "what stops the only live cycle in the character";
}

// Second half: execute it. THIS IS THE FINDING.
TEST(GroundTruthGap, TheKernelPerformsItForeverBecauseItHasNoResources) {
    Subject safe{};
    bringUp(kSafe, safe);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    ASSERT_EQ(safe.verdict.status, ProverStatus::Terminating);

    // Find the same edge the test above characterised, by the same rule.
    const Cancel* live = nullptr;
    for (std::size_t i = 0; i < safe.character.cancels.size(); ++i) {
        const Cancel& e = safe.character.cancels[i];
        if (e.from != e.to) continue;
        bool reportedDead = false;
        for (const ProverDeadCancel& d : safe.verdict.deadCancels)
            if (d.cancel == static_cast<CancelIndex>(i)) { reportedDead = true; break; }
        if (!reportedDead) { live = &e; break; }
    }
    ASSERT_NE(live, nullptr);
    const std::string moveId = safe.character.moves[live->from].id;

    const Witness w = selfLoopWitness(moveId);
    const std::vector<MoveBinding> bindings = bindingsFor(w);
    MatchBuild build{};
    ASSERT_TRUE(buildMirror(safe.character, bindings, build)) << build.report[0].error;

    // The edge survived into the kernel with a window it can actually match. An
    // empty window would make it inert and there would be no gap to measure --
    // which is exactly what happens to the two DEAD self-cancels, and is why the
    // test above insists this one is the live one.
    const std::uint16_t slot = build.moves[0].Find(moveId);
    ASSERT_NE(slot, 0u);
    const cse::kernel::CancelEdge* kernelEdge = nullptr;
    for (std::int32_t i = 0; i < build.data.p[0].cancelCount; ++i) {
        const cse::kernel::CancelEdge& e = build.data.p[0].cancels[i];
        if (e.from == slot && e.to == slot) { kernelEdge = &e; break; }
    }
    ASSERT_NE(kernelEdge, nullptr) << "the self-cancel did not cross into the kernel";
    ASSERT_LE(kernelEdge->earliestFrame, kernelEdge->latestFrame)
        << "the kernel window is empty, so this edge is inert and there is no gap";

    Driver driver(w, build.moves[0], bindings);
    std::string why;
    ASSERT_TRUE(driver.Usable(why)) << why;

    // The defender mashes the moment the combo opens, and gets nowhere.
    const TickLog run = drive(build.data, driver, kGapTicks,
                          DefenderPolicy::MashesOnceHit, bindings[0].button);

    // The model's budget, recomputed here so the two halves cannot drift.
    const ResourceIndex juggle = 1;   // A03: the build's positional order
    ASSERT_LT(static_cast<std::size_t>(juggle), safe.character.resources.size());
    ASSERT_EQ(safe.character.resources[juggle].name, "juggle");
    std::int32_t spend = 0;
    for (const ResourceAmount& a : safe.character.moves[live->from].effect)
        if (a.resource == juggle) spend = a.value;
    ASSERT_LT(spend, 0);
    const std::int32_t permitted =
        safe.character.resources[juggle].initial / (-spend);

    // THE MEASUREMENT.
    EXPECT_GT(static_cast<std::int32_t>(run.Hits()), permitted)
        << "the kernel performed `" << moveId << "` -> itself "
        << run.Hits() << " times; the model permits " << permitted << ".";
    EXPECT_TRUE(run.FreeTicks().empty())
        << "the defender got out, so this is not the infinite it looks like."
        << Summary(run, build.moves[0]);
    EXPECT_TRUE(run.defenderActedTicks.empty())
        << "the defender acted while mashing, so the loop is escapable after all."
        << Summary(run, build.moves[0]);

    // And it is not slowing down. A repetition count is not an infinite; a fixed
    // period with no escape is.
    ASSERT_GE(run.hitTicks.size(), 3u);
    const std::int32_t period = run.hitTicks[1] - run.hitTicks[0];
    for (std::size_t i = 1; i + 1 < run.hitTicks.size(); ++i)
        EXPECT_EQ(run.hitTicks[i + 1] - run.hitTicks[i], period)
            << "the repetition is not periodic" << Summary(run, build.moves[0]);

    // WHY, in the loss table's own words. Both of these are already recorded by
    // MatchBuilder as things the kernel does not do; what this test adds is that
    // one of them is load-bearing for a TERMINATING verdict.
    const BuildLoss* effects = findLoss(build.report[0], "move.effect");
    ASSERT_NE(effects, nullptr);
    EXPECT_GT(effects->count, 0)
        << "the kernel is supposed to be dropping this character's resource "
           "effects, and the loss table says it drops none";
    EXPECT_EQ(effects->direction, BuildLossDirection::KernelOmits)
        << "`move.effect` is recorded as "
        << BuildLossDirectionName(effects->direction)
        << " and the argument in this test needs it to be KernelOmits";

    const BuildLoss* stance = findLoss(build.report[0], "move.stance");
    ASSERT_NE(stance, nullptr);
    EXPECT_GT(stance->count, 0)
        << "`" << moveId << "` is an AIR move and the kernel has no stance rule, "
           "which is a second, independent reason this loop is performable from "
           "the ground";
    EXPECT_EQ(stance->direction, BuildLossDirection::KernelPermits)
        << "`move.stance` is recorded as "
        << BuildLossDirectionName(stance->direction);

    // The flag that was always going to be the one to watch. It is false, it is
    // COMPUTED rather than remembered, and this test is what it costs.
    EXPECT_FALSE(build.report[0].playsAsAnalysed)
        << "the bridge is claiming the kernel plays the character ProverAdapter "
           "analysed, and this test just performed a combo that character cannot "
           "do.";
    EXPECT_GT(build.report[0].lossesThatBite, 0);

    RecordProperty("kernel_repetitions", static_cast<int>(run.Hits()));
    RecordProperty("model_permits_repetitions", permitted);
    RecordProperty("gap_loop_move", moveId);
    RecordProperty("gap_period_ticks", period);

    // PRINTED ON SUCCESS, because a passing test says nothing and this number is
    // the reason the file exists. RecordProperty puts the same figures in the
    // XML for a harness; this is for the human reading the console.
    std::cout
        << "\n[ GROUND TRUTH ] a measured model/game gap on `" << safe.character.id
        << "`\n"
        << "  the decision procedure says TERMINATING, and its certificate is "
           "that juggle runs down.\n"
        << "  `" << moveId << "` cancels into itself; the model permits "
        << permitted << " repetition(s) before\n"
        << "  juggle hits its floor. The kernel performed " << run.Hits()
        << " in " << kGapTicks << " ticks, every "
        << period << " ticks, with the defender\n"
        << "  unable to act on any tick in between. The kernel has no resources: "
           "`move.effect`\n"
        << "  is a " << BuildLossDirectionName(effects->direction) << " loss over "
        << effects->count << " move(s) and `move.stance` a "
        << BuildLossDirectionName(stance->direction) << " loss over "
        << stance->count << " move(s),\n"
        << "  and BuildReport::playsAsAnalysed is "
        << (build.report[0].playsAsAnalysed ? "true" : "false")
        << ". ARCHITECTURE.md 5.5 item 4.\n\n";
}
