// TRAINING MODE, PROVEN WITHOUT A WINDOW.
//
// Games/UntitledFighter/Modes/src/UntitledFighterMode.cpp is a GameMode: it owns
// a Renderer2D call, a fixed tick and a keyboard, and none of those can be
// asserted on a CI machine with no display. What it is BUILT OUT OF can be, and
// that is what this file does -- every behaviour training mode claims, exercised
// through Games/UntitledFighter/Game/ with no GL context, no window and no
// Engine on the link line.
//
// The alternative is the one this repository has refused everywhere else: a
// feature whose claims rest on somebody having looked at it once.
//
// ---------------------------------------------------------------------------
// THE FIVE CLAIMS, AND WHY EACH IS THE ONE THAT WOULD HURT
// ---------------------------------------------------------------------------
//   1. DEMONSTRATE WORKS WHEN IT IS PRESSED MID-SESSION. Section 1. The
//      playtester does not press it on tick 0 -- they press it after watching
//      the HUD for a few seconds -- and every off-by-one in absolute tick
//      numbering shows up there and nowhere else. tests/test_game_core.cpp
//      section 7 covers the same handover from tick 0, which is the one tick at
//      which a relative trace and an absolute one cannot be told apart.
//   2. FRAME STEP AND SLOW MOTION ARE FREE. Section 2. FightSession.h claims
//      them as consequences of owning no clock rather than as features, and the
//      claim is falsifiable: the same 120 ticks, paced four different ways, must
//      reach the same bytes. If they do not, the session has a hidden clock and
//      training mode's most-used tools are wrong.
//   3. RESET IS REALLY A RESET. Section 3. A training mode restarts constantly.
//      Two fights from the same inputs must be byte-identical -- and the subtle
//      half is that Begin() deliberately does NOT reset the observers, so a host
//      that forgets ComboWatcher::Reset() carries the previous fight's combo
//      into the new one. That is asserted as a FACT here rather than left as a
//      trap for the host to fall into.
//   4. THE WATCHER NOTICES WHAT A PLAYTESTER WOULD STUMBLE INTO. Section 4, and
//      it is the point of the file. The mode ships `fighter_a` -- the character
//      the tool CERTIFIES -- and tests/test_gap_extent.cpp measured that 33 of
//      its 41 cycles run forever in a kernel that has no juggle to stop them. A
//      playtester who wanders into one of those is the most valuable event this
//      feature can produce, and it is worth nothing if the game shows a green
//      combo counter and says nothing.
//   5. THE NUMBERS ON THE SCREEN ARE THE NUMBERS THE KERNEL HONOURS. Section 5,
//      and it is the newest and the most ordinary kind of failure: the mode
//      DERIVES three quantities rather than reading them -- ticks-until-
//      actionable, frame advantage, and which frames get a red box -- and a
//      derivation is where a screen can be confidently, stably, unnoticeably
//      wrong. Sections 1-4 ask whether the seam behaves; this one asks whether
//      the arithmetic laid on top of it is the arithmetic the tick performs.
//
// ---------------------------------------------------------------------------
// WHAT SECTION 4 FOUND, STATED UP FRONT BECAUSE IT IS WHAT THE MODE MUST DO
// ---------------------------------------------------------------------------
// ComboWatcher's loud markers are `completedProverLoop`, `performedDeadCancel`
// and `deadEdgeConnected`. ON THIS CHARACTER, WITH THE ANALYSIS THE MODE
// ACTUALLY HOLDS, ALL THREE STAY DOWN -- correctly, and by design:
//
//   completedProverLoop is gated on ProverStatus::Infinite, and fighter_a is
//   TERMINATING. `cycleRun` is matched against ProverResult::loop, and a
//   TERMINATING verdict carries no loop at all -- so the cycle matcher has
//   nothing to match and reports zero however many turns the player performs.
//   The edge is one the prover KEEPS, so no dead-cancel flag fires either.
//
// tests/test_game_core.cpp's own certified-away-cycle test writes a synthetic
// one-move loop INTO a copy of the verdict before handing it to the watcher, so
// that the matcher has something to follow. That is the right test of the
// matcher and it is not the situation the mode is in: the mode holds what
// AnalyseCharacter returned, loop and all. Section 4 therefore runs the case
// with the REAL verdict, unmodified, and the result is that the watcher counts
// the string honestly and flags nothing.
//
// So the alarm has to come from a comparison, and there is exactly one available
// that is already in the mode's hands: ProverResult::maxHits, the analysis's own
// worst case, which ARCHITECTURE.md 5.3 already tells the editor panel to show.
// A string with more hits than that is, in the analysis's own words, impossible.
// Section 4 drives past it and asserts the watcher's count crosses it, which is
// docs/NORTHSTAR.md item 2 -- "the bounds dual" -- performed live by the seam
// the training mode is made of rather than by an offline search.
//
// ---------------------------------------------------------------------------
// AND A SECOND FINDING, WHICH IS ABOUT THE HUD RATHER THAN THE CHARACTER
// ---------------------------------------------------------------------------
// ComboWatcher::Current() is "the string in progress, OR THE LAST ONE THAT
// FINISHED". After a Demonstrate-then-you-try, the playtester's own attempts are
// strings of their own -- so by the time anyone looks, Current() describes the
// one-hit string the player just landed and not the fifteen-hit loop the
// demonstration performed. Section 1 samples through an observer for that reason
// and then asserts the trap, so that a mode drawing Current() at Draw time knows
// what it is drawing. A "you performed the demonstrated combo" badge has to be
// latched while it runs, or read off Previous() on the tick the string ends.
//
// ---------------------------------------------------------------------------
// WHAT IT MEASURED. Every number here is asserted rather than recorded, so none
// of them can rot quietly; they are restated for a reader who wants the shape of
// the result without running it.
// ---------------------------------------------------------------------------
//   Demonstrate at tick 37   an 85-tick trace, IDENTICAL BIT FOR BIT to the one
//                            the same request produces at tick 0, and authoring
//                            [37, 122) rather than [0, 85)
//   the handover             the pad's `stand_mp` came out on tick 127, and the
//                            demonstration's move never started again
//   120 ticks, four pacings  burst / one-per-frame / one-per-four-frames /
//                            irregular frame step: same bytes at every tick,
//                            same checksum, same 20-hit verdict
//   two fights, same inputs  byte-identical at every tick; and a restart that
//                            forgets ComboWatcher::Reset() is caught by the
//                            COMPLETED-STRING COUNT and by nothing else (see the
//                            note at that assertion -- the hit count matches)
//   fighter_a, stumbled into `air_mp` -> itself. The certificate affords 4 turns
//                            (juggle 4, spend 1); the kernel ran 27, gapTicks 0,
//                            defender 1000 -> 0. The analysis's own worst case is
//                            21 HITS AND THE STRING REACHED 27. Every one of the
//                            watcher's loud markers stayed down.
//   ticks until actionable   the two-term rule is EXACT on 5 of `stand_lp`'s 14
//                            frames and up to NINE TICKS TOO LONG on the other 9.
//                            A cancel is a third way out of a move and the rule
//                            has no term for it.
//   frame advantage, live    with the button held, the same interaction reads
//                            +1 for eleven ticks, -13 for one, and "not known"
//                            for two -- forever, at 60/14 = 4.3 Hz.
//   body separation          the interval rule tracks the kernel's own
//                            BoxesOverlap across all four bands; the ternary form
//                            is right until the bodies touch and then reports
//                            -52 px on a tick the kernel calls a clean miss, and
//                            stays wrong for 13 consecutive ticks.
//   the red box              `air_mp` is active for 4 frames and can hit on ONE.
//                            On the other three the kernel still returns a box
//                            and it still overlaps the dummy -- and the state as
//                            a view sees it CANNOT TELL THE LIVE FRAME FROM THE
//                            THREE SPENT ONES, because the guard bit is already
//                            set by the end of the tick that connected.
//
// ---------------------------------------------------------------------------
// WHAT THIS FILE DELIBERATELY DUPLICATES FROM tests/test_game_core.cpp
// ---------------------------------------------------------------------------
// charactersDir(), the button pool, the witness, bindingsFor() and the rig are
// the same shapes, on purpose and by instruction: two spellings of "load
// fighter_a and build a mirror match" is how two tests come to disagree about
// what they loaded. FOUR THINGS ARE DELIBERATELY DIFFERENT, and each has a
// reason written where it is used:
//
//   * TickTrace counts hits TWO WAYS -- the defender's health delta, which is
//     what test_game_core uses, and ComboWatcher.h signal 3's alreadyHitBits
//     rule. Section 4 runs a string past the point a 1000-health bar can express
//     another hit, and the health delta stops counting there while the fight
//     does not.
//   * Presentation is new: it is everything a host does BETWEEN ticks, and
//     section 2 has nothing to measure without it.
//   * Nothing here re-derives the ground-truth driver. test_game_core section 3
//     already asserts BuildDemonstration reproduces it tick for tick; repeating
//     that comparison would be a second copy of a reference whose whole value is
//     that there is one of it.
//   * Section 5 does not use a FightSession at all. It shares the rig -- the same
//     character, the same body, the same two start positions -- and then drives
//     cse::kernel::Simulate directly, because two of its four tests need a FORK
//     (one state advanced twice with different inputs) and a fork is a kernel
//     question rather than a session one. tests/test_cancels.cpp drives the same
//     way. Sections 1-4 are unchanged and still go through the session, which is
//     what they are about.
#include <gtest/gtest.h>

#include "cse/game/ComboWatcher.h"
#include "cse/game/FightSession.h"
#include "cse/game/WitnessCursor.h"
#include "cse/game/InputSource.h"

#include "cse/data/CharacterData.h"
#include "cse/data/MatchBuilder.h"
#include "cse/data/ProverAdapter.h"

#include "cse/kernel/Combat.h"
#include "cse/kernel/GameState.h"
#include "cse/kernel/Simulate.h"

// Included explicitly rather than inherited. gcc is stricter than MSVC about
// transitively-included headers and CI compiles both, so every name used below
// is declared by something named here: <cstring> for memcmp, <string_view> for
// MoveIndexMap::IdOf's return type, <cstddef> for size_t.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace cse::data;
using namespace cse::game;

using cse::kernel::GameState;
using cse::kernel::Input;
using cse::kernel::InputPair;
using cse::kernel::MatchData;

namespace {

// ============================================================================
// 0. UNITS, THE STAGE, AND FINDING THE CHARACTERS
// ============================================================================

// Authored in pixels and converted once, matching test_combat.cpp,
// test_ground_truth.cpp and test_game_core.cpp.
constexpr std::int32_t px(std::int32_t pixels) {
    return pixels * cse::kernel::kSubUnitsPerPixel;
}

// The body is the CALLER'S number -- CharacterData carries none -- and these are
// both shipped files' own `engine.constants`, restated in the unit this test
// reads in. See the long note in test_ground_truth.cpp.
constexpr std::int32_t kHalfWidth = px(13);
constexpr std::int32_t kHeight    = px(60);

// WHERE THE TWO FIGHTERS STAND, AND WHY IT IS NOT MatchStart's DEFAULT.
// MatchStart's own opening is ResetMatch's -100px/+100px, a 174 px body-to-body
// gap -- further than anything either character reaches, so nothing would ever
// connect and every assertion in this file would be about an empty room. Origins
// 34 px apart, bodies 8 px apart, inside the reach of every move used here.
// IN THE CORNER, because almost everything in this file executes a verdict
// computed at `stage: corner` -- the demonstrations rehearse `fighter_a_infinite`'s
// printed loop, and at corner the model drops horizontal position entirely.
// Opening midscreen was harmless while nothing moved the defender and stops
// being harmless the moment pushback is wired (ROADMAP M1.3d): the rehearsal
// separates and reaches index 3 of 6.
constexpr std::int32_t kStageEdge = 480 * cse::kernel::kSubUnitsPerPixel;
//
// THE DEFENDER'S BODY IS AGAINST THE WALL, not its origin. Since ROADMAP M1.2
// the stage clamps the BODY -- a fighter may not disappear half into the corner
// -- so an origin placed exactly on the edge is outside its own limit and the
// first tick shoves it inward. That is not a cornered opening, it is a fighter
// falling into position while the test believes nothing has happened.
constexpr std::int32_t kP1X =  kStageEdge - kHalfWidth;
constexpr std::int32_t kP0X =  kP1X - px(34);

// THE ONE EXCEPTION, and it is a different subject rather than an exemption.
// The HUD's gap chip is walk ARITHMETIC across four bands -- apart, touching,
// overlapping, coincident -- and it needs room to walk. Cornered, the fighter is
// against the clamp and the walk step measures zero, which is the test correctly
// refusing to measure a walk that did not happen.
constexpr std::int32_t kWalkP0X = -px(17);
constexpr std::int32_t kWalkP1X =  px(17);

// Fixed so a failing run reproduces verbatim. It only feeds GameState::rng,
// which nothing in a combat tick reads -- but it IS in the checksum, so section
// 2's four pacings would fail every comparison if one of them advanced the
// stream differently. That is deliberate: the checksum covers the whole state.
constexpr std::uint32_t kSeed = 0xC0FFEEu;

// ResetMatch's opening health (Simulate.cpp), named so the damage arithmetic
// reads as a subtraction from a known start rather than as a magic 1000.
constexpr std::int32_t kStartingHealth = 1000;

// The build-wide positional resource order (ADR-001 section 8 item 7). Passing
// it is what arms load assertion A03.
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

// kSafe is THE CHARACTER THE MODE ACTUALLY SHIPS. UntitledFighterMode.cpp loads
// `Characters/fighter_a.json`, which is why section 4 is about this file rather
// than about the one with the deliberate bug in it: the playtester in front of
// the mode as it stands is holding the certified character, and the 33 cycles
// are what they can stumble into.
const char* kSafe     = "fighter_a.json";
const char* kInfinite = "fighter_a_infinite.json";

// THE STAGED SHIPPING DIRECTORY, NOT THE PHASE-0 CORPUS. Same shape and same
// reasons as test_ground_truth.cpp and test_game_core.cpp:
// tests/fixtures/characters holds the MUGEN transcriptions, which are EVIDENCE
// and are deliberately never staged next to an executable; these two files are
// the project's own content and are staged to
// ${CMAKE_CURRENT_BINARY_DIR}/Exported by the shared asset-staging script.
//
// Two fallbacks, in order, and neither can make anything pass vacuously because
// every load below ASSERTs.
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

    return "Exported/Characters";
#endif
}

void loadShipped(const char* file, CharacterData& out) {
    LoadReport report{};
    ASSERT_TRUE(LoadCharacterFile(charactersDir(), file, loadOptions(), out, report))
        << file << " did not load from " << charactersDir() << ".\n"
        << "  rule : " << (report.rule.empty() ? "(no load assertion named)" : report.rule)
        << "\n  error: " << report.error
        << "\n\nA character that does not load is the first finding; everything "
           "below this point is about a file that reached the engine.";
    ASSERT_FALSE(out.moves.empty()) << file << " loaded with no moves";
}

void analyseShipped(const CharacterData& character, ProverResult& result) {
    ProverReport report{};
    ASSERT_TRUE(AnalyseCharacter(character, proverOptions(), result, report))
        << character.id << " could not be projected into the decision procedure.\n"
        << "  rule : " << report.rule << "\n  error: " << report.error;
}

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

// A mirror match: both sides get the same bindings, so which of the two fighters
// acts is decided by input bits and never by data.
bool buildMirror(const CharacterData& character,
                 const std::vector<MoveBinding>& bindings, MatchBuild& out) {
    BuildOptions options{};
    options.body     = body();
    options.bindings = bindings;
    return BuildMatchData(character, options, character, options, out);
}

// SIX SINGLE BITS, AND SINGLE ON PURPOSE. StepAttack takes the first move in slot
// order all of whose bits are held, so a binding whose mask is a superset of an
// earlier one's can never start. No mask below is a subset of any other.
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

Input inputOf(std::uint16_t bits) {
    Input in{};
    in.bits = bits;
    return in;
}

InputPair pairOf(std::uint16_t p0, std::uint16_t p1) {
    InputPair pair{};
    pair.p[0].bits = p0;
    pair.p[1].bits = p1;
    return pair;
}

// ============================================================================
// 0b. THE WITNESS
// ============================================================================

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

// A witness that is a single move repeated -- what a self-cancel produces, and
// what section 4's fighter_a case builds from the character's own cancel table.
Witness selfLoopWitness(const std::string& moveId) {
    Witness w{};
    w.sequence.push_back(moveId);
    w.loopStart = 0;
    return w;
}

// One button per DISTINCT move in the witness, in first-appearance order. A
// surplus move gets a binding of ZERO rather than being dropped, so a shortfall
// surfaces as a named refusal rather than as a trace that silently stops
// following the witness.
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

// ============================================================================
// 0c. WATCHING A SESSION FROM OUTSIDE, AND COUNTING HITS TWICE
// ============================================================================

// A tick log kept BY AN OBSERVER, which is the fourth watcher FightSession.h
// predicts. It counts connecting hits TWO INDEPENDENT WAYS, and the second one
// is the divergence from tests/test_game_core.cpp's TickLog:
//
//   HEALTH DELTA        the defender's health fell this tick. The reading every
//                       other test in this repository uses, and the one that owes
//                       nothing to any rule the watcher implements. It STOPS
//                       COUNTING at Fighter::health's clamp of zero.
//   alreadyHitBits      ComboWatcher.h signal 3, re-implemented here from the
//                       header rather than borrowed from the implementation:
//
//                         (atk.alreadyHitBits & defenderBit) != 0
//                         AND ( (previous atk bits & defenderBit) == 0
//                               OR the attacker started a move this tick )
//
//                       The disjunct is the hole a naive `was clear, now set`
//                       leaves: a move that has already hit can END and its
//                       successor START in the same StepAttack call, so the bit
//                       is set before and after and the hit is missed.
//
// WHY BOTH, RATHER THAN THE SIMPLE ONE. Section 4 deliberately runs a string
// past the analysis's own worst case, and on this character that is more damage
// than the defender has health: the health bar runs out and the fight does not.
// A test that counted only health deltas there would silently be measuring the
// clamp. So the two are asserted to AGREE for as long as the health bar can
// express a hit, and the bits reading carries the count past it -- which is
// exactly the argument ComboWatcher.h gives for the watcher not reading damage.
struct TickTrace final : public ITickObserver {
    struct Sample {
        std::uint32_t tick        = 0;
        InputPair     inputs{};
        GameState     state{};      // AFTER the tick
        std::uint32_t checksum     = 0;
        bool          resimulated  = false;
        bool          healthFell   = false;
        bool          bitsConnected = false;
    };

    explicit TickTrace(int attackerSlot = 0) : attackerSlot_(attackerSlot) {}

    std::vector<Sample> samples;

    // Contract violations, counted rather than asserted here: OnTick must not
    // throw and a gtest ASSERT inside an observer would unwind through the 60 Hz
    // path the header says must not throw.
    int nullViolations     = 0;
    int offByOneViolations = 0;
    int gapViolations      = 0;

    void OnTick(const TickView& view) override {
        if (view.state == nullptr || view.data == nullptr) { ++nullViolations; return; }
        // THE OFF-BY-ONE, ASSERTED ONCE AND FOR EVERY TICK. TickView::tick is the
        // tick that RAN, so the state it hands over is already one ahead.
        if (view.state->tick != view.tick + 1u) ++offByOneViolations;

        const int defenderSlot = 1 - attackerSlot_;
        const std::uint8_t defenderBit =
            static_cast<std::uint8_t>(1u << static_cast<unsigned>(defenderSlot));

        const cse::kernel::Fighter& atk = view.state->p[attackerSlot_];
        const cse::kernel::Fighter& def = view.state->p[defenderSlot];

        Sample s{};
        s.tick        = view.tick;
        s.inputs      = view.inputs;
        s.state       = *view.state;
        s.checksum    = cse::kernel::Checksum(*view.state);
        s.resimulated = view.resimulated;
        s.healthFell  = havePrevious_ && def.health < prevDefHealth_;

        const bool startedAMove = atk.moveId != 0 && atk.moveFrame == 0;
        s.bitsConnected = (atk.alreadyHitBits & defenderBit) != 0 &&
                          ((prevAtkHitBits_ & defenderBit) == 0 || startedAMove);

        havePrevious_   = true;
        prevAtkHitBits_ = atk.alreadyHitBits;
        prevDefHealth_  = def.health;

        // A re-simulation OVERWRITES rather than appends, which is what
        // TickView::resimulated tells an observer to do. Nothing in this file
        // rolls back, so a gap would be a session bug and is counted as one.
        if (view.tick < samples.size())        samples[view.tick] = s;
        else if (view.tick == samples.size())  samples.push_back(s);
        else                                   ++gapViolations;
    }

    bool Clean() const {
        return nullViolations == 0 && offByOneViolations == 0 && gapViolations == 0;
    }

    std::size_t Size() const { return samples.size(); }
    const GameState& Final() const { return samples.back().state; }

    std::vector<std::uint32_t> HealthDeltaHitTicks() const {
        std::vector<std::uint32_t> out;
        for (const Sample& s : samples) if (s.healthFell) out.push_back(s.tick);
        return out;
    }

    std::vector<std::uint32_t> ConnectedHitTicks() const {
        std::vector<std::uint32_t> out;
        for (const Sample& s : samples) if (s.bitsConnected) out.push_back(s.tick);
        return out;
    }

    // The first tick on which the defender's health reached zero, or samples.size()
    // when it never did. Past it the health delta can no longer express a hit and
    // only the bits reading is a count.
    std::uint32_t HealthExhaustedTick() const {
        for (const Sample& s : samples)
            if (s.state.p[1 - attackerSlot_].health == 0) return s.tick;
        return static_cast<std::uint32_t>(samples.size());
    }

    // The ticks on which `slot` STARTED a move -- ComboWatcher.h signal 1,
    // spelled the way the header spells it and not as an id transition.
    std::vector<std::uint32_t> MoveStartTicks(int slot) const {
        std::vector<std::uint32_t> out;
        for (const Sample& s : samples)
            if (s.state.p[slot].moveId != 0 && s.state.p[slot].moveFrame == 0)
                out.push_back(s.tick);
        return out;
    }

private:
    int           attackerSlot_   = 0;
    bool          havePrevious_   = false;
    std::uint8_t  prevAtkHitBits_ = 0;
    std::int32_t  prevDefHealth_  = kStartingHealth;
};

// A per-tick snapshot of a ComboWatcher's report, taken by an observer
// registered AFTER the watcher.
//
// THAT REGISTRATION ORDER IS THE CONTRACT AND IT IS BEING USED ON PURPOSE:
// observers are notified in order, so this sees the watcher's state as of the
// end of the same tick. It is the only way to assert about a string while it is
// running -- and a string is the only thing ComboWatcher::Current() describes.
// A test that read Current() at the end of a long fight would be reading
// whatever string was open THEN, which is very rarely the one it meant.
struct WatcherProbe final : public ITickObserver {
    explicit WatcherProbe(const ComboWatcher* watcher) : watcher_(watcher) {}

    struct Frame {
        std::uint32_t tick                = 0;
        std::int32_t  hits                = 0;
        std::int32_t  gapTicks            = 0;
        std::uint32_t startTick           = 0;
        bool          open                = false;
        bool          completedProverLoop = false;
        std::int32_t  completedCombos     = 0;
    };
    std::vector<Frame> frames;

    void OnTick(const TickView& view) override {
        if (watcher_ == nullptr) return;
        const ComboReport& r = watcher_->Current();
        Frame f{};
        f.tick                = view.tick;
        f.hits                = r.hits;
        f.gapTicks            = r.gapTicks;
        f.startTick           = r.startTick;
        f.open                = r.open;
        f.completedProverLoop = r.completedProverLoop;
        f.completedCombos     = watcher_->CompletedCombos();
        frames.push_back(f);
    }

private:
    const ComboWatcher* watcher_ = nullptr;
};

// ============================================================================
// 0d. THE RIG
// ============================================================================

struct Rig {
    CharacterData            character;
    ProverResult             verdict;
    Witness                  witness;
    std::vector<MoveBinding> bindings;
    MatchBuild               build;
    FightSetup               setup;

    // The witness as KERNEL move ids, which is what DemonstrationRequest speaks:
    // BuildDemonstration has the MatchData and not the CharacterData, and reads
    // the button straight out of MoveDef::button.
    std::vector<std::uint16_t> kernelWitness;
    std::size_t                loopStart = 0;
};

void bringUpFrom(const CharacterData& character, const Witness& witness, Rig& rig) {
    rig.witness  = witness;
    rig.bindings = bindingsFor(witness);
    ASSERT_FALSE(rig.bindings.empty());
    ASSERT_LE(rig.bindings.size(), kButtonPoolSize)
        << "the witness names " << rig.bindings.size() << " distinct moves and "
           "there are only " << kButtonPoolSize << " single-bit buttons; two moves "
           "sharing a mask would let StepAttack's first-wins rule pick the wrong "
           "one, so the trace would stop being the witness.";

    ASSERT_TRUE(buildMirror(character, rig.bindings, rig.build))
        << "p0: " << rig.build.report[0].error
        << " / p1: " << rig.build.report[1].error;

    rig.kernelWitness.clear();
    for (const std::string& id : witness.sequence) {
        const std::uint16_t slot = rig.build.moves[0].Find(id);
        ASSERT_NE(slot, 0u) << "`" << id << "` did not survive the build into the "
                               "kernel, so the witness cannot be performed at all";
        rig.kernelWitness.push_back(slot);
    }
    rig.loopStart = witness.loopStart;

    rig.setup.start.seed         = kSeed;
    rig.setup.start.startPosX[0] = kP0X;
    rig.setup.start.startPosX[1] = kP1X;
    rig.setup.data               = &rig.build.data;
}

// fighter_a_infinite, driven by its OWN printed loop.
void bringUpInfinite(Rig& rig) {
    loadShipped(kInfinite, rig.character);
    if (::testing::Test::HasFatalFailure()) return;
    analyseShipped(rig.character, rig.verdict);
    if (::testing::Test::HasFatalFailure()) return;

    ASSERT_EQ(rig.verdict.status, ProverStatus::Infinite)
        << kInfinite << " carries one deliberate infinite and the decision "
           "procedure did not find it, so there is no printed loop for a "
           "demonstration to perform.\n"
        << DescribeVerdict(rig.character, rig.verdict);
    ASSERT_FALSE(rig.verdict.loop.empty())
        << "INFINITE with an empty loop is a word with nothing behind it";

    bringUpFrom(rig.character, witnessOf(rig.character, rig.verdict), rig);
}

// fighter_a, driven by the ONE self-cancel the decision procedure keeps.
//
// The move is DERIVED, by the same rule tests/test_ground_truth.cpp section 5 and
// tests/test_game_core.cpp use: every edge from a move back into itself, minus
// the ones the prover reported as dead. What is left is the cycle whose only
// brake is juggle -- a resource the kernel does not have. The move id is not
// written down here, so a character edit moves this test rather than silently
// making it about the wrong edge.
void bringUpSafeSelfCycle(Rig& rig, std::string& moveIdOut, CancelIndex& edgeOut) {
    loadShipped(kSafe, rig.character);
    if (::testing::Test::HasFatalFailure()) return;
    analyseShipped(rig.character, rig.verdict);
    if (::testing::Test::HasFatalFailure()) return;

    ASSERT_EQ(rig.verdict.status, ProverStatus::Terminating)
        << kSafe << " was authored to be SAFE and the decision procedure does not "
           "agree. Section 4 is about a character the tool CERTIFIES; without the "
           "certificate there is no gap to measure.\n"
        << DescribeVerdict(rig.character, rig.verdict);

    const Cancel* live = nullptr;
    for (std::size_t i = 0; i < rig.character.cancels.size(); ++i) {
        const Cancel& e = rig.character.cancels[i];
        if (e.from != e.to) continue;
        bool reportedDead = false;
        for (const ProverDeadCancel& d : rig.verdict.deadCancels)
            if (d.cancel == static_cast<CancelIndex>(i)) { reportedDead = true; break; }
        if (!reportedDead) { live = &e; edgeOut = static_cast<CancelIndex>(i); break; }
    }
    ASSERT_NE(live, nullptr)
        << kSafe << " no longer has a self-cancel the prover keeps, so there is no "
           "certified-away cycle for section 4 to be about.\n"
        << DescribeVerdict(rig.character, rig.verdict);

    moveIdOut = rig.character.moves[live->from].id;
    bringUpFrom(rig.character, selfLoopWitness(moveIdOut), rig);
}

// ============================================================================
// 0e. SAYING WHAT HAPPENED
// ============================================================================

std::string moveName(const MoveIndexMap& map, std::uint16_t slot) {
    if (slot == 0) return "idle";
    const std::string_view id = map.IdOf(slot);
    return id.empty() ? ("slot" + std::to_string(slot)) : std::string(id);
}

// A per-tick table for a failure message. Same role as the ones in
// test_ground_truth.cpp and test_game_core.cpp: if something breaks, a reader
// must be able to see WHICH TICK and WHAT THE FIGHTERS WERE DOING without
// re-running anything. The two hit columns are next to each other on purpose --
// where they disagree is where the health bar ran out.
std::string Table(const TickTrace& log, const MoveIndexMap& map,
                  std::uint32_t from, std::uint32_t count) {
    std::ostringstream s;
    s << "\n  tick  p0 in  attacker move        fr  bits  hit(hp)  hit(bits)  "
         "def stun  def hp\n";
    for (const TickTrace::Sample& sample : log.samples) {
        if (sample.tick < from) continue;
        if (sample.tick >= from + count) break;
        s << "  " << std::setw(4) << sample.tick
          << "  " << std::setw(5) << buttonName(sample.inputs.p[0].bits)
          << "  " << std::setw(18) << std::left
          << moveName(map, sample.state.p[0].moveId) << std::right
          << "  " << std::setw(2) << sample.state.p[0].moveFrame
          << "  " << std::setw(4) << static_cast<int>(sample.state.p[0].alreadyHitBits)
          << "  " << std::setw(7) << (sample.healthFell ? "HIT" : " . ")
          << "  " << std::setw(9) << (sample.bitsConnected ? "HIT" : " . ")
          << "  " << std::setw(8) << sample.state.p[1].hitstun
          << "  " << std::setw(6) << sample.state.p[1].health
          << "\n";
    }
    return s.str();
}

std::string DescribeReport(const ComboReport& r, const MoveIndexMap& map) {
    std::ostringstream s;
    s << "\n  open " << (r.open ? "yes" : "no")
      << "  hits " << r.hits << "  damage " << r.damage
      << "  gapTicks " << r.gapTicks << "  whiffs " << r.whiffedStarts
      << "\n  startTick " << r.startTick << "  lastHitTick " << r.lastHitTick
      << "  endTick " << r.endTick
      << "\n  cycleRun " << r.cycleRun << "  turns " << r.loopTurnsCompleted
      << "  onWitness " << (r.onWitness ? "yes" : "no")
      << "  witnessIndex " << r.witnessIndex
      << "\n  completedProverLoop " << (r.completedProverLoop ? "yes" : "no")
      << "  performedDeadCancel " << (r.performedDeadCancel ? "yes" : "no")
      << "  deadEdgeConnected " << (r.deadEdgeConnected ? "yes" : "no")
      << "  witnessIncomplete " << (r.witnessIncomplete ? "yes" : "no")
      << "\n  sequence(" << r.sequence.size()
      << (r.sequenceTruncated ? ", TRUNCATED" : "") << ") ";
    for (std::size_t i = 0; i < r.sequence.size() && i < 12; ++i)
        s << moveName(map, r.sequence[i]) << (i + 1 < r.sequence.size() ? ", " : "");
    if (r.sequence.size() > 12) s << "...";
    s << "\n";
    return s.str();
}

// Two reports compared field by field, so a drift is NAMED rather than reported
// as "the reports differ". Used by section 2 (four pacings, one verdict) and
// section 3 (two fights, one verdict).
void expectSameReport(const ComboReport& a, const ComboReport& b,
                      const MoveIndexMap& map, const char* what) {
    EXPECT_EQ(a.open, b.open)               << what << ": open";
    EXPECT_EQ(a.hits, b.hits)               << what << ": hit count";
    EXPECT_EQ(a.damage, b.damage)           << what << ": damage";
    EXPECT_EQ(a.gapTicks, b.gapTicks)       << what << ": gapTicks";
    EXPECT_EQ(a.startTick, b.startTick)     << what << ": startTick";
    EXPECT_EQ(a.lastHitTick, b.lastHitTick) << what << ": lastHitTick";
    EXPECT_EQ(a.whiffedStarts, b.whiffedStarts) << what << ": whiffedStarts";
    EXPECT_EQ(a.cycleRun, b.cycleRun)       << what << ": cycleRun";
    EXPECT_EQ(a.loopTurnsCompleted, b.loopTurnsCompleted)
        << what << ": loopTurnsCompleted";
    EXPECT_EQ(a.onWitness, b.onWitness)     << what << ": onWitness";
    EXPECT_EQ(a.witnessIndex, b.witnessIndex) << what << ": witnessIndex";
    EXPECT_EQ(a.completedProverLoop, b.completedProverLoop)
        << what << ": completedProverLoop";
    EXPECT_EQ(a.performedDeadCancel, b.performedDeadCancel)
        << what << ": performedDeadCancel";
    EXPECT_EQ(a.edges.size(), b.edges.size()) << what << ": edge count";

    ASSERT_EQ(a.sequence.size(), b.sequence.size())
        << what << ": the two strings have different lengths"
        << DescribeReport(a, map) << DescribeReport(b, map);
    for (std::size_t i = 0; i < a.sequence.size(); ++i)
        ASSERT_EQ(a.sequence[i], b.sequence[i])
            << what << ": connecting move " << i << " is `"
            << moveName(map, a.sequence[i]) << "` and `"
            << moveName(map, b.sequence[i]) << "`";
}

// ============================================================================
// 0f. RUNNING A FIGHT, AND REHEARSING ONE
// ============================================================================

void run(FightSession& session, std::uint32_t ticks) {
    for (std::uint32_t i = 0; i < ticks; ++i) session.Tick();
}

// Build the demonstration the tool-assisted player would perform.
//
// `firstTick` is a PARAMETER here rather than always zero, which is the whole
// point of section 1: a playtester presses Demonstrate at whatever tick they
// happen to be on.
void demonstrate(const std::vector<std::uint16_t>& kernelWitness,
                 std::size_t loopStart, const MatchData& data,
                 const GameState& from, std::uint32_t turns,
                 std::uint32_t firstTick, std::uint32_t maxTicks,
                 Demonstration& out) {
    DemonstrationRequest request{};
    request.from          = &from;
    request.data          = &data;
    request.attackerSlot  = 0;
    request.defenderInput = Input{};      // the silent training dummy
    request.moveIds       = kernelWitness;
    request.loopStart     = loopStart;
    request.turns         = turns;
    request.maxTicks      = maxTicks;
    request.firstTick     = firstTick;

    const bool complete = BuildDemonstration(request, out);
    EXPECT_EQ(complete, out.complete)
        << "BuildDemonstration's return value and Demonstration::complete "
           "disagree; the header says it returns `out.complete`.";
    ASSERT_TRUE(out.complete)
        << "the rehearsal did not finish. THAT IS A FINDING, not a flaky test: a "
           "witness the engine cannot perform is the other publishable outcome "
           "(ARCHITECTURE.md 5.5 item 4).\n"
        << "  reachedIndex  " << out.reachedIndex << " of " << kernelWitness.size()
        << "\n  turnsDone     " << out.turnsDone << " of " << turns
        << "\n  stalledAt     " << out.stalledAt
        << "\n  error         " << out.error;
    ASSERT_FALSE(out.inputs.empty());
    EXPECT_EQ(out.firstTick, firstTick)
        << "DemonstrationRequest::firstTick is carried through to "
           "Demonstration::firstTick so the caller can build the "
           "ScriptedInputSource without re-deriving it. It did not survive, so a "
           "host that trusted it would number the trace from the wrong tick.";
}

}  // namespace

// ============================================================================
// 1. DEMONSTRATE, PRESSED MID-SESSION            (CLAIM 1)
// ============================================================================
//
// The whole feature: a Demonstrate the playtester presses, which performs the
// prover's printed loop perfectly and THEN HANDS CONTROL BACK so they can try it
// themselves. tests/test_game_core.cpp section 7 proves that composition from
// tick 0. This section presses it at tick 37, because tick 0 is the one tick at
// which a trace numbered from the session and a trace numbered from zero are the
// same trace, and every off-by-one in absolute numbering hides there.

namespace {

// A deliberately awkward number: prime, not a multiple of the loop period, and
// larger than any move's duration. An off-by-one that happened to land on a
// multiple of something would be invisible at a rounder one.
constexpr std::uint32_t kPreDemoTicks = 37;

// Small enough that the defender is nowhere near Fighter::health's clamp at
// zero: fighter_a_infinite's loop deals 30 a turn and its witness has a prefix,
// so this is well under 1000. Sections that WANT to run past the health bar say
// so; this one does not.
constexpr std::uint32_t kDemoTurns   = 10;
constexpr std::uint32_t kYouTryTicks = 90;

// Sections 2 and 3 compare a fixed WINDOW of ticks and therefore need a trace at
// least that long. The loop repeats every few ticks, so the number that has to
// move when a character edit changes the period is this one -- stated in TURNS,
// which is what BuildDemonstration takes, and checked by an ASSERT at each use
// rather than left to be discovered as a comparison over neutral input.
constexpr std::uint32_t kLongDemoTurns = 20;
constexpr std::uint32_t kWindowTicks   = 120;

}  // namespace

// THE TRACE IS THE SAME TRACE, ONLY NUMBERED LATER.
//
// The rehearsal runs a private session from a COPY of the state handed in, and
// FightSession.cpp says the absolute tick number inside that state is irrelevant
// to the outcome -- "nothing in a tick reads GameState::tick except the
// increment at the bottom of Simulate". This is that sentence, tested: the same
// opening, rehearsed at tick 0 and at tick 37, must produce IDENTICAL BITS and
// differ only in `firstTick`.
//
// It is the cheapest possible probe for the failure section 1 exists to catch,
// and it fails loudly in both directions: bits that differ mean the rehearsal
// depends on the tick index, and a firstTick that did not survive means a host
// building the ScriptedInputSource from it would number the trace wrongly.
TEST(TrainingModeDemonstrate, TheTraceDoesNotDependOnTheTickItWasPressedOn) {
    Rig rig{};
    bringUpInfinite(rig);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    FightSession session;
    std::string error;
    ASSERT_TRUE(session.Begin(rig.setup, error)) << error;

    // Pressed the instant the match opens.
    const GameState atZero = session.State();
    ASSERT_EQ(session.CurrentTick(), 0u);

    Demonstration first{};
    demonstrate(rig.kernelWitness, rig.loopStart, rig.build.data, atZero,
                kDemoTurns, 0, 600, first);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    // ...and pressed after the playtester has stood there reading the HUD.
    //
    // NEUTRAL, and that is the point rather than laziness: the variable under
    // test is the ABSOLUTE TICK NUMBER, so exactly one thing may differ between
    // the two rehearsals. A playtester who threw a punch first would also have
    // moved the defender, and a difference in the traces would then have two
    // possible causes.
    run(session, kPreDemoTicks);
    ASSERT_EQ(session.CurrentTick(), kPreDemoTicks)
        << "the session did not advance one tick per Tick() call, so nothing "
           "below is about the tick it claims to be about";
    const GameState atThirtySeven = session.State();

    // THE PREMISE, ASSERTED RATHER THAN ASSUMED. If a future kernel makes a
    // neutral tick change something -- a timer, a regen, a walk-forward AI --
    // the trace comparison below would fail for a reason that has nothing to do
    // with tick numbering, and this says so first and precisely.
    {
        GameState normalised = atThirtySeven;
        normalised.tick = atZero.tick;
        normalised.rng  = atZero.rng;
        EXPECT_EQ(0, std::memcmp(&normalised, &atZero, sizeof(GameState)))
            << kPreDemoTicks << " neutral ticks changed the fighters. The two "
               "rehearsals below therefore start from different positions and "
               "any difference between their traces is not about tick numbering.";
    }
    EXPECT_NE(atThirtySeven.rng, atZero.rng)
        << "the rng stream did not advance over " << kPreDemoTicks << " ticks, so "
           "GameState::rng is not being stepped and the checksum comparisons in "
           "section 2 would pass for a reason that is not determinism";

    Demonstration later{};
    demonstrate(rig.kernelWitness, rig.loopStart, rig.build.data, atThirtySeven,
                kDemoTurns, kPreDemoTicks, 600, later);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    ASSERT_EQ(later.inputs.size(), first.inputs.size())
        << "the same combo rehearsed from the same position took "
        << later.inputs.size() << " ticks at tick " << kPreDemoTicks << " and "
        << first.inputs.size() << " ticks at tick 0. THE REHEARSAL DEPENDS ON THE "
           "ABSOLUTE TICK INDEX, which FightSession.cpp says it does not.";
    for (std::size_t i = 0; i < first.inputs.size(); ++i)
        ASSERT_EQ(later.inputs[i].bits, first.inputs[i].bits)
            << "relative tick " << i << " of the demonstration presses "
            << buttonName(later.inputs[i].bits) << " when pressed at tick "
            << kPreDemoTicks << " and " << buttonName(first.inputs[i].bits)
            << " when pressed at tick 0";

    EXPECT_EQ(first.firstTick, 0u);
    EXPECT_EQ(later.firstTick, kPreDemoTicks);
    EXPECT_EQ(later.turnsDone, first.turnsDone);
    EXPECT_EQ(later.reachedIndex, first.reachedIndex);

    // And the SOURCE built from it authors the right absolute window. This is
    // where a relative trace would give itself away: it would author [0, N) and
    // the pad would answer every tick of the demonstration.
    ScriptedInputSource source(later.inputs, later.firstTick, "DEMO");
    const std::uint32_t end =
        kPreDemoTicks + static_cast<std::uint32_t>(later.inputs.size());
    EXPECT_EQ(source.FirstTick(), kPreDemoTicks);
    EXPECT_EQ(source.AuthoredEndTick(), end);
    EXPECT_FALSE(source.Truncated());
    EXPECT_FALSE(source.At(kPreDemoTicks - 1u).authored)
        << "the demonstration authors the tick BEFORE it was pressed";
    EXPECT_TRUE(source.At(kPreDemoTicks).authored);
    EXPECT_TRUE(source.At(end - 1u).authored);
    EXPECT_FALSE(source.At(end).authored);
    EXPECT_FALSE(source.At(0).authored)
        << "the demonstration authors tick 0, which is where a trace numbered "
           "from zero rather than from the session's current tick would land";

    RecordProperty("demo_ticks", static_cast<int>(later.inputs.size()));
    RecordProperty("demo_first_tick", static_cast<int>(later.firstTick));
}

// THE LIFECYCLE, END TO END, AT A NON-ZERO START TICK.
//
// The playtester stands still for 37 ticks, presses Demonstrate, watches the
// prover's printed loop performed perfectly, and then holds a button of their
// own -- and their move comes out on the next tick with nothing to reset and
// nothing to notice.
TEST(TrainingModeDemonstrate, TheComboConnectsWhileTheScriptSpeaksAndThePadTakesOverAfter) {
    Rig rig{};
    bringUpInfinite(rig);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    // A SECOND BINDING, so that "the pad took over" is visible as a different
    // move rather than inferred from an absence. The witness binds one move; this
    // adds `stand_mp` on MP, a disjoint single bit that cannot shadow or be
    // shadowed under StepAttack's first-wins rule.
    const std::string padMoveId = "stand_mp";
    ASSERT_NE(rig.character.FindMove(padMoveId), kInvalidMove)
        << "this character has no `" << padMoveId << "`, so the pad has nothing "
           "distinguishable to press";

    std::vector<MoveBinding> bindings = rig.bindings;
    for (const MoveBinding& b : bindings)
        ASSERT_NE(b.moveId, padMoveId)
            << "the witness already binds `" << padMoveId << "`, so the pad's move "
               "cannot be told apart from the demonstration's";
    bindings.push_back(bind(padMoveId, cse::kernel::kInputMP));

    MatchBuild build{};
    ASSERT_TRUE(buildMirror(rig.character, bindings, build)) << build.report[0].error;

    FightSetup setup{};
    setup.start = rig.setup.start;
    setup.data  = &build.data;

    // The witness as kernel ids of THIS build, which has one binding more than
    // rig.build does -- so the slots are looked up again rather than reused.
    std::vector<std::uint16_t> kernelWitness;
    for (const std::string& id : rig.witness.sequence) {
        const std::uint16_t slot = build.moves[0].Find(id);
        ASSERT_NE(slot, 0u) << "`" << id << "` is not in this build";
        kernelWitness.push_back(slot);
    }
    const std::uint16_t demoSlot = build.moves[0].Find(rig.witness.sequence.back());
    const std::uint16_t padSlot  = build.moves[0].Find(padMoveId);
    ASSERT_NE(demoSlot, 0u);
    ASSERT_NE(padSlot, 0u);
    ASSERT_NE(demoSlot, padSlot);

    FightSession session;
    std::string error;
    ASSERT_TRUE(session.Begin(setup, error)) << error;

    // --- the host's pad, latched from tick 0 --------------------------------
    //
    // THE PAD IS LATCHED ON EVERY TICK, INCLUDING THE ONES THE DEMONSTRATION
    // SPEAKS FOR, and that is a host rule rather than a detail. LatchedInputSource
    // refuses any tick that is not NextTick(), so a host that stopped latching
    // while the script had control would find Latch() returning false the moment
    // the script ran out -- a hole in the input log, at the exact tick the
    // playtester was handed the pad back. The assertion inside the loop is what
    // that bug would trip.
    LatchedInputSource pad(0, "YOU");

    TickTrace log(0);
    ASSERT_TRUE(session.AddObserver(&log));

    ComboWatcher watcher(0, &build.moves[0], &rig.verdict);
    watcher.Reset();
    ASSERT_TRUE(session.AddObserver(&watcher));

    // AFTER the watcher, deliberately: observers are notified in registration
    // order, so this sees the watcher's verdict as of the end of the same tick.
    WatcherProbe probe(&watcher);
    ASSERT_TRUE(session.AddObserver(&probe));

    // --- 37 ticks of a playtester reading the HUD ---------------------------
    //
    // The pad is bound for these, so the pre-demonstration ticks are the pad's
    // and the handover has a BEFORE as well as an AFTER. test_game_core's
    // section 7 has no before: it presses Demonstrate on tick 0.
    session.SetInputSource(0, &pad);
    for (std::uint32_t t = 0; t < kPreDemoTicks; ++t) {
        ASSERT_TRUE(pad.Latch(t, inputOf(0))) << "latching tick " << t;
        session.Tick();
    }
    ASSERT_EQ(session.CurrentTick(), kPreDemoTicks);
    ASSERT_EQ(watcher.Current().hits, 0)
        << "something connected before the demonstration was even pressed";

    // --- the playtester presses Demonstrate ---------------------------------
    Demonstration demo{};
    demonstrate(kernelWitness, rig.loopStart, build.data, session.State(),
                kDemoTurns, session.CurrentTick(), 600, demo);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    const std::uint32_t demoEnd =
        demo.firstTick + static_cast<std::uint32_t>(demo.inputs.size());
    ASSERT_EQ(demo.firstTick, kPreDemoTicks);

    ScriptedInputSource demoSource(demo.inputs, demo.firstTick, "DEMO");
    FallbackInputSource attacker(&demoSource, &pad);
    session.SetInputSource(0, &attacker);

    // EXHAUSTION IS A NORMAL STATE, expressed as data and knowable BEFORE it is
    // reached, so a HUD draws a progress bar without polling.
    EXPECT_EQ(demoSource.AuthoredEndTick(), demoEnd);
    EXPECT_FALSE(demoSource.Exhausted(demoEnd - 1u));
    EXPECT_TRUE(demoSource.Exhausted(demoEnd));
    EXPECT_EQ(attacker.AuthoredEndTick(), kUnboundedTick)
        << "the composition claims it runs out, though the pad behind it never "
           "does; a host would stop asking for ticks at that number";

    // THE PLAYTESTER MASHES, and under press-activation that is the only way to
    // be one. A pad that HELD MP would be a single press for the whole session:
    // one stand_mp at the handover tick and silence after it, so the
    // demonstration's own string would simply run on and Current() would still
    // be holding it at the end -- which is the opposite of the trap this test
    // exists to pin. Alternating is a person pressing about thirty times a
    // second, and it is what makes "the playtester's string" a thing that
    // exists at all.
    const std::uint32_t total = demoEnd + kYouTryTicks;
    for (std::uint32_t t = kPreDemoTicks; t < total; ++t) {
        const std::uint16_t padBits =
            (t % 2u == 0u) ? cse::kernel::kInputMP : std::uint16_t{0};
        ASSERT_TRUE(pad.Latch(t, inputOf(padBits)))
            << "latching tick " << t << " was refused, which is a host sequencing "
               "bug: the pad must be latched on every tick, including the ones "
               "the demonstration is speaking for, or the input log has a hole in "
               "it at the moment control is handed back";
        session.Tick();
    }

    ASSERT_EQ(log.Size(), total);
    EXPECT_TRUE(log.Clean());
    EXPECT_EQ(session.CurrentTick(), total);

    // --- WHO WAS SPEAKING, TICK BY TICK, ON ABSOLUTE NUMBERS ----------------
    for (std::uint32_t t = 0; t < kPreDemoTicks; ++t) {
        ASSERT_EQ(attacker.Active(t), static_cast<const IInputSource*>(&pad))
            << "tick " << t << " is BEFORE the demonstration was pressed and the "
               "script answered it. A trace numbered from zero rather than from "
               "the session's current tick would do exactly this.";
        ASSERT_EQ(log.samples[t].inputs.p[0].bits, 0u);
    }
    for (std::uint32_t t = kPreDemoTicks; t < demoEnd; ++t) {
        ASSERT_EQ(attacker.Active(t), static_cast<const IInputSource*>(&demoSource))
            << "tick " << t << " is inside the demonstration and the pad answered it";
        ASSERT_EQ(log.samples[t].inputs.p[0].bits,
                  demo.inputs[t - demo.firstTick].bits)
            << "tick " << t << " did not receive the demonstrated bits. The trace "
               "is indexed from firstTick " << demo.firstTick << ", so an "
               "off-by-one here is the whole failure this test exists for.";
    }
    for (std::uint32_t t = demoEnd; t < total; ++t) {
        ASSERT_EQ(attacker.Active(t), static_cast<const IInputSource*>(&pad))
            << "control was not handed back at tick " << t;
        ASSERT_EQ(log.samples[t].inputs.p[0].bits,
                  (t % 2u == 0u) ? cse::kernel::kInputMP : std::uint16_t{0})
            << "tick " << t << " is past the demonstration and the pad's bits did "
                              "not arrive as latched. The alternation is the "
                              "point: the log must carry the RELEASE ticks too, "
                              "because a press is only a press against the tick "
                              "before it and a log that dropped them would "
                              "replay as a held button.";
    }

    // What a HUD asks, and it must be a pure question with a pure answer.
    EXPECT_STREQ(attacker.Name(), "FALLBACK");
    EXPECT_STREQ(attacker.Active(0)->Name(), "YOU");
    EXPECT_STREQ(attacker.Active(kPreDemoTicks)->Name(), "DEMO");
    EXPECT_STREQ(attacker.Active(demoEnd)->Name(), "YOU")
        << "a HUD asking Active(tick)->Name() would still be drawing DEMO after "
           "the demonstration ended";

    // --- THE COMBO CONNECTED WHILE THE SCRIPT SPOKE -------------------------
    const std::vector<std::uint32_t> hits = log.HealthDeltaHitTicks();
    ASSERT_GE(hits.size(), static_cast<std::size_t>(kDemoTurns))
        << "the demonstration asked for " << kDemoTurns << " turns of the printed "
           "loop and landed " << hits.size() << " hits. A witness the engine "
           "cannot perform is the OTHER publishable outcome, not a flaky test."
        << Table(log, build.moves[0], kPreDemoTicks, 40);
    EXPECT_GT(log.Final().p[1].health, 0)
        << "the defender was knocked out inside the measured window, so the hit "
           "count is against Fighter::health's clamp at zero. Lower kDemoTurns.";

    // EVERY hit landed while the script had control. A hit after `demoEnd` would
    // be the playtester's, and the counts below would be about two combos.
    ASSERT_GE(hits.front(), kPreDemoTicks)
        << "a hit landed at tick " << hits.front() << ", before Demonstrate was "
           "pressed at tick " << kPreDemoTicks;
    std::size_t hitsInsideDemo = 0;
    for (std::uint32_t t : hits)
        if (t < demoEnd) ++hitsInsideDemo;
    EXPECT_GE(hitsInsideDemo, static_cast<std::size_t>(kDemoTurns))
        << "only " << hitsInsideDemo << " of " << hits.size() << " hits landed "
           "while the demonstration was speaking";

    // The two independent readings agree here, which is what makes the bits
    // reading usable as the count in section 4 where the health bar runs out.
    EXPECT_EQ(log.ConnectedHitTicks(), hits)
        << "the health-delta reading and ComboWatcher.h signal 3's alreadyHitBits "
           "reading disagree about which ticks connected, on a fight in which the "
           "defender never reached the health clamp."
        << Table(log, build.moves[0], hits.front(), 32);

    // --- AND THE LIVE JUDGE AGREES, SAMPLED WHILE THE SCRIPT WAS SPEAKING ---
    //
    // READ OFF THE PROBE RATHER THAN OFF Current() AT THE END, AND THIS IS A
    // FINDING RATHER THAN A TEST DETAIL. ComboWatcher::Current() is "the string in
    // progress, or the last one that finished" -- and the playtester's own
    // attempts after the handover are strings of their own. By the last tick of
    // this test, Current() describes the ONE-HIT string the pad's `stand_mp` just
    // landed, not the fifteen-hit loop the demonstration performed.
    //
    // SO A TRAINING HUD MUST NOT READ Current() AFTER THE FACT AND CALL IT THE
    // DEMONSTRATION'S RESULT. It must latch the verdict while the demonstration
    // is running, or read Previous() on the tick the string ends. The assertions
    // below sample the way a HUD drawing every frame samples, and the ones after
    // them pin the trap.
    const WatcherProbe::Frame* peak = nullptr;
    for (const WatcherProbe::Frame& f : probe.frames) {
        if (f.tick >= demoEnd) break;
        if (peak == nullptr || f.hits > peak->hits) peak = &f;
    }
    ASSERT_NE(peak, nullptr) << "the probe recorded no ticks at all";

    EXPECT_EQ(peak->hits, static_cast<std::int32_t>(hitsInsideDemo))
        << "while the script was speaking the watcher's string reached "
        << peak->hits << " hits and the defender's health fell on "
        << hitsInsideDemo << " ticks before the trace ran out.\n"
           "A watcher that reports 1 is detecting a CHANGE OF moveId; this loop "
           "is one move cancelling into itself, so the id never changes."
        << Table(log, build.moves[0], kPreDemoTicks, 40);
    EXPECT_GE(peak->hits, static_cast<std::int32_t>(kDemoTurns));
    EXPECT_TRUE(peak->open);
    EXPECT_EQ(peak->gapTicks, 0)
        << "the demonstrated loop left the defender actionable on "
        << peak->gapTicks << " tick(s); the ground-truth test measures this loop "
           "as unescapable";
    EXPECT_GE(peak->startTick, kPreDemoTicks)
        << "the demonstration's string is reported as starting at tick "
        << peak->startTick << ", before Demonstrate was pressed at tick "
        << kPreDemoTicks;
    EXPECT_EQ(peak->completedCombos, 0)
        << "a string ended while the demonstration was still running, so the "
           "defender got out of a loop the ground-truth test measures as "
           "unescapable";

    // THE LOUD MARKER, on the tick a HUD would be drawing it. The player
    // performed, in the running game, the loop the decision procedure printed out
    // of the character file.
    bool sawCompletedProverLoop = false;
    for (const WatcherProbe::Frame& f : probe.frames) {
        if (f.tick >= demoEnd) break;
        if (f.completedProverLoop) { sawCompletedProverLoop = true; break; }
    }
    EXPECT_TRUE(sawCompletedProverLoop)
        << "the demonstration went round the PRINTED loop " << peak->hits
        << " times and `completedProverLoop` was never true on any tick of it. "
           "That flag is what a training HUD shouts with."
        << DescribeReport(watcher.Current(), build.moves[0]);

    // ...AND THE TRAP, PINNED. What Current() holds once the playtester has had a
    // go is the playtester's string, and it is much shorter. This is asserted so
    // that a mode reading Current() at Draw time knows what it is reading.
    EXPECT_GT(watcher.CompletedCombos(), 0)
        << "the demonstration's string never ended, although the trace ran out "
        << kYouTryTicks << " ticks ago";
    EXPECT_LT(watcher.Current().hits, peak->hits)
        << "the string Current() holds at the end of the fight is as long as the "
           "demonstration's, so either the playtester's own attempt joined it or "
           "the two strings were never told apart"
        << "\n  completed combos " << watcher.CompletedCombos()
        << DescribeReport(watcher.Current(), build.moves[0]);

    // --- AND THE FIGHT KEPT GOING ACROSS THE HANDOVER ------------------------
    //
    // Not merely the tick loop: the playtester's OWN move came out. Nothing was
    // reset, nothing had to be noticed, and the demonstration did not leave the
    // button jammed down.
    bool demoMoveDuringDemo = false, padMoveAfter = false;
    std::uint32_t firstPadMoveTick = 0;
    for (std::uint32_t t = kPreDemoTicks; t < demoEnd; ++t)
        if (log.samples[t].state.p[0].moveId == demoSlot) demoMoveDuringDemo = true;
    for (std::uint32_t t = demoEnd; t < total; ++t)
        if (log.samples[t].state.p[0].moveId == padSlot) {
            padMoveAfter     = true;
            firstPadMoveTick = t;
            break;
        }

    EXPECT_TRUE(demoMoveDuringDemo)
        << "the demonstration never performed its own move, so there is nothing "
           "for control to be handed back FROM."
        << Table(log, build.moves[0], kPreDemoTicks, 24);
    EXPECT_TRUE(padMoveAfter)
        << "the playtester held " << buttonName(cse::kernel::kInputMP) << " for "
        << kYouTryTicks << " ticks after the demonstration ended and `" << padMoveId
        << "` never came out, so control was not really handed back."
        << Table(log, build.moves[0], demoEnd, 32);

    // The demonstration's own move must NOT keep coming out afterwards -- that
    // would be the jammed button, and it is the exact failure "neutral, never
    // repeat-last" exists to rule out. Anything already in flight is allowed to
    // finish, which is why this starts a full move duration after the handover.
    const cse::kernel::MoveDef* demoMove =
        cse::kernel::MoveAt(build.data.p[0], demoSlot);
    ASSERT_NE(demoMove, nullptr);
    const std::uint32_t settled =
        demoEnd + static_cast<std::uint32_t>(cse::kernel::MoveDuration(*demoMove)) + 1u;
    for (std::uint32_t t = settled; t < total; ++t)
        ASSERT_NE(log.samples[t].state.p[0].moveId, demoSlot)
            << "the demonstration's move started again on tick " << t
            << ", well after the trace ran out."
            << Table(log, build.moves[0], demoEnd, 32);

    RecordProperty("demo_pressed_at", static_cast<int>(kPreDemoTicks));
    RecordProperty("demo_end_tick", static_cast<int>(demoEnd));
    RecordProperty("first_player_move_tick", static_cast<int>(firstPadMoveTick));
}

// ============================================================================
// 2. FRAME STEP AND SLOW MOTION ARE THE HOST'S BUSINESS       (CLAIM 2)
// ============================================================================
//
// FightSession.h claims frame step, slow motion and fast rehearsal as
// CONSEQUENCES of owning no clock rather than as features:
//
//     normal play   the host ticks once per 1/60 s of real time
//     frame step    the host ticks once when a key is pressed
//     slow motion   the host ticks once every fourth display frame
//
// That claim is falsifiable and this is the test. The same 120 ticks, paced four
// different ways, must reach a BIT-IDENTICAL GameState and the same checksum --
// and, because a training mode's HUD is what the playtester is actually reading,
// the live judge must reach the same verdict too.
//
// WHAT MAKES IT MORE THAN A TAUTOLOGY. A session with no clock cannot notice
// pacing unless something happens between the ticks, so the paced runs do
// everything a host does on a display frame: read State(), take the checksum,
// snapshot, ask the input source about ticks it is not running (a progress bar),
// ask the watcher to describe itself, and do floating-point arithmetic on all of
// it. Presentation may use float freely; the rule is that nothing it computes
// may feed back into a tick, and that is exactly what a differing checksum here
// would prove had happened.

namespace {

// Everything a host does BETWEEN ticks, minus the drawing. Deliberately
// allocating (Describe), deliberately floating-point (the sink), and
// deliberately asking questions out of tick order (the progress bar).
struct Presentation {
    double sink       = 0.0;
    int    describes  = 0;
    int    frames     = 0;

    void Frame(const FightSession& session, const IInputSource& source,
               const ComboWatcher& watcher) {
        ++frames;

        // A HUD reading the state. Floats, freely: this is presentation.
        const GameState& s = session.State();
        sink += static_cast<double>(s.p[0].posX) * 0.5;
        sink += static_cast<double>(s.p[1].health) / 3.0;
        sink += static_cast<double>(session.Checksum() & 0xFFFFu) / 7.0;
        sink += static_cast<double>(session.HighWaterTick());

        // A rollback host snapshots every frame whether it ticks or not, and a
        // replay scrubber asks the source about ticks it is nowhere near. Both
        // are the questions that would perturb a source holding a cursor.
        GameState copy{};
        session.Snapshot(copy);
        sink += static_cast<double>(copy.tick);
        (void)source.At(session.CurrentTick());
        (void)source.At(session.CurrentTick() + 97u);
        (void)source.At(0u);
        (void)source.Exhausted(session.CurrentTick());

        // ComboWatcher.h says Describe allocates and is for on demand rather than
        // per tick, so this is on a divisor rather than every frame -- but it IS
        // called from a paced run and not from the burst, which is the point.
        if ((frames % 8) == 0) { (void)watcher.Describe(); ++describes; }
    }
};

// One session, one watcher, one trace, run at a given pacing.
struct PacedRun {
    FightSession session;
    TickTrace    log{0};
    ComboWatcher watcher;
    Presentation present;

    PacedRun(const MoveIndexMap* moves, const ProverResult* analysis)
        : watcher(0, moves, analysis) {}

    void Begin(const FightSetup& setup, const IInputSource& source) {
        std::string error;
        ASSERT_TRUE(session.Begin(setup, error)) << error;
        watcher.Reset();
        ASSERT_TRUE(session.AddObserver(&watcher));
        ASSERT_TRUE(session.AddObserver(&log));
        session.SetInputSource(0, &source);
    }
};

}  // namespace

TEST(TrainingModePacing, TheSameTicksPacedFourWaysReachTheSameBytes) {
    Rig rig{};
    bringUpInfinite(rig);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    // TWO SECONDS RATHER THAN ONE. 60 is the smallest interesting number -- a
    // session ticked for one second -- and kWindowTicks is chosen so the window
    // contains several full turns of the loop, which gives the watcher a verdict
    // to compare rather than an opening hit.
    GameState from{};
    cse::kernel::ResetMatch(from, kSeed);
    from.p[0].posX = kP0X;
    from.p[1].posX = kP1X;

    Demonstration demo{};
    demonstrate(rig.kernelWitness, rig.loopStart, rig.build.data, from,
                kLongDemoTurns, 0, 600, demo);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    ASSERT_GE(demo.inputs.size(), static_cast<std::size_t>(kWindowTicks))
        << "the demonstration is " << demo.inputs.size() << " ticks and the paced "
           "window is " << kWindowTicks << ", so most of the comparison below "
           "would be over the neutral input a source produces when it runs out. "
           "Raise kLongDemoTurns.";

    // ONE SOURCE, SHARED BY ALL FOUR SESSIONS. It is a `const IInputSource*`
    // everywhere, and At() is documented pure -- so sharing it is not a shortcut,
    // it is the strongest available statement of that purity: four differently
    // paced sessions interleave their questions and every one gets the same
    // answer.
    ScriptedInputSource source(demo.inputs, 0, "DEMO");

    PacedRun burst(&rig.build.moves[0], &rig.verdict);       // as fast as it can
    PacedRun realtime(&rig.build.moves[0], &rig.verdict);    // one tick per frame
    PacedRun slowmo(&rig.build.moves[0], &rig.verdict);      // one tick per 4 frames
    PacedRun stepped(&rig.build.moves[0], &rig.verdict);     // irregular, on demand

    burst.Begin(rig.setup, source);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    realtime.Begin(rig.setup, source);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    slowmo.Begin(rig.setup, source);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    stepped.Begin(rig.setup, source);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    // --- 1. THE BURST. Nothing between the ticks at all ---------------------
    for (std::uint32_t t = 0; t < kWindowTicks; ++t) burst.session.Tick();

    // --- 2. REAL TIME. One display frame of work per tick -------------------
    for (std::uint32_t t = 0; t < kWindowTicks; ++t) {
        realtime.present.Frame(realtime.session, source, realtime.watcher);
        realtime.session.Tick();
    }

    // --- 3. SLOW MOTION. Three frames of pure presentation between ticks ----
    for (std::uint32_t t = 0; t < kWindowTicks; ++t) {
        for (int f = 0; f < 4; ++f)
            slowmo.present.Frame(slowmo.session, source, slowmo.watcher);
        slowmo.session.Tick();
    }

    // --- 4. FRAME STEP. Irregular gaps, including long ones -----------------
    //
    // The playtester holds the step key, taps it, walks away and comes back.
    // Deterministic rather than random: a fixed pattern reproduces verbatim, and
    // there is nothing a random one could catch that an irregular one cannot.
    for (std::uint32_t t = 0; t < kWindowTicks; ++t) {
        const int idle = static_cast<int>(t % 7u) * 2;
        for (int f = 0; f < idle; ++f)
            stepped.present.Frame(stepped.session, source, stepped.watcher);
        stepped.session.Tick();
    }

    // --- THE COMPARISON -----------------------------------------------------
    //
    // Per tick first, so a divergence is localised to the tick it began on rather
    // than only visible as a different number at the end.
    PacedRun* const runs[] = { &realtime, &slowmo, &stepped };
    const char* const names[] = { "one tick per display frame",
                                  "slow motion (one tick per four frames)",
                                  "frame step (irregular gaps)" };

    for (std::size_t r = 0; r < 3; ++r) {
        PacedRun& other = *runs[r];
        SCOPED_TRACE(names[r]);

        ASSERT_EQ(other.log.Size(), burst.log.Size())
            << "the paced run produced " << other.log.Size() << " ticks and the "
               "burst produced " << burst.log.Size() << ". A session that owned an "
               "accumulator would drop or duplicate ticks here -- which is exactly "
               "why FightSession.h disqualifies Application's FixedTimestep, "
               "whose cap-then-zero behaviour drops the backlog.";
        EXPECT_TRUE(other.log.Clean());
        EXPECT_GT(other.present.frames, 0)
            << "the paced run did no presentation work at all, so it is the burst "
               "with extra steps and this comparison proves nothing";

        for (std::size_t t = 0; t < burst.log.Size(); ++t) {
            ASSERT_EQ(other.log.samples[t].checksum, burst.log.samples[t].checksum)
                << "TICK " << t << " DIFFERS BETWEEN TWO PACINGS. FightSession is "
                   "supposed to own no clock, no frame rate and no accumulator, so "
                   "the only thing that changed between these two runs is what the "
                   "host did BETWEEN the ticks. If this fails, frame step and slow "
                   "motion are not free and every claim built on them is wrong."
                << Table(burst.log, rig.build.moves[0],
                         t > 3 ? static_cast<std::uint32_t>(t) - 3u : 0u, 8);
            ASSERT_EQ(other.log.samples[t].inputs.p[0].bits,
                      burst.log.samples[t].inputs.p[0].bits)
                << "tick " << t << " was fed different bits at a different pacing, "
                   "so the shared input source answered the same question twice "
                   "with two answers";
            ASSERT_EQ(0, std::memcmp(&other.log.samples[t].state,
                                     &burst.log.samples[t].state, sizeof(GameState)))
                << "tick " << t << " reached a different state at a different "
                   "pacing, although its checksum matched -- which would mean the "
                   "checksum does not cover the whole state";
        }

        EXPECT_EQ(0, std::memcmp(&other.session.State(), &burst.session.State(),
                                 sizeof(GameState)));
        EXPECT_EQ(other.session.Checksum(), burst.session.Checksum());
        EXPECT_EQ(other.session.CurrentTick(), burst.session.CurrentTick());
        EXPECT_EQ(other.session.HighWaterTick(), burst.session.HighWaterTick());

        // THE HUD REACHES THE SAME VERDICT TOO. This is what a training mode
        // actually shows, and a combo counter that read differently in slow motion
        // would be the most visible possible version of this bug.
        expectSameReport(other.watcher.Current(), burst.watcher.Current(),
                         rig.build.moves[0], names[r]);
        EXPECT_EQ(other.watcher.CompletedCombos(), burst.watcher.CompletedCombos())
            << names[r];
        EXPECT_FALSE(other.watcher.Stale())
            << "a paced run marked its judgement stale, though nothing was "
               "re-simulated";
    }

    // NO TICK WAS DROPPED AND NONE WAS RUN TWICE. The most direct statement of
    // "this object owns no accumulator": N calls to Tick() advance the index by
    // exactly N, whatever the host was doing in between.
    EXPECT_EQ(burst.session.CurrentTick(), kWindowTicks);
    EXPECT_EQ(burst.session.HighWaterTick(), kWindowTicks);
    for (const TickTrace::Sample& s : burst.log.samples)
        ASSERT_FALSE(s.resimulated)
            << "tick " << s.tick << " reported itself re-simulated in a run that "
               "never restored anything";

    // The presentation really happened, and it really was different per run.
    EXPECT_GT(slowmo.present.frames, realtime.present.frames);
    EXPECT_GT(slowmo.present.describes, 0);
    EXPECT_NE(stepped.present.frames, realtime.present.frames);
    EXPECT_NE(realtime.present.sink, 0.0)
        << "the presentation computed nothing, so a compiler that removed it "
           "entirely would leave this test comparing three identical loops";

    ASSERT_GT(burst.watcher.Current().hits, 0)
        << "nothing connected in " << kWindowTicks << " ticks, so the verdict "
           "comparison above was between two empty reports";

    RecordProperty("paced_ticks", static_cast<int>(kWindowTicks));
    RecordProperty("paced_hits", burst.watcher.Current().hits);
}

// ============================================================================
// 3. RESET IS REALLY A RESET                                  (CLAIM 3)
// ============================================================================
//
// A training mode restarts constantly -- that is what training mode IS. Begin()
// on a running session is a documented RESTART, and the documented part that
// bites is what it deliberately does NOT touch: "OBSERVERS ARE KEPT ... every
// observer in this module has its own Begin/Reset for its own state."

TEST(TrainingModeReset, TwoFightsFromTheSameInputsAreByteIdentical) {
    Rig rig{};
    bringUpInfinite(rig);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    GameState from{};
    cse::kernel::ResetMatch(from, kSeed);
    from.p[0].posX = kP0X;
    from.p[1].posX = kP1X;

    Demonstration demo{};
    demonstrate(rig.kernelWitness, rig.loopStart, rig.build.data, from,
                kLongDemoTurns, 0, 600, demo);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    ASSERT_GE(demo.inputs.size(), static_cast<std::size_t>(kWindowTicks))
        << "the demonstration is shorter than the round this test runs, so the "
           "two fights would be compared over neutral input. Raise kLongDemoTurns.";

    // THE SOURCE IS NOT REBOUND BETWEEN THE FIGHTS, AND THAT IS THE POINT. It is
    // a pure function of an absolute tick index, so the restarted session asks
    // At(0), At(1), ... again and gets the same bits -- a restart reproduces for
    // free, with nothing to rewind. A source that held a cursor would hand the
    // second fight the tail of the first one's trace.
    ScriptedInputSource source(demo.inputs, 0, "DEMO");

    FightSession session;
    std::string error;
    ASSERT_TRUE(session.Begin(rig.setup, error)) << error;
    session.SetInputSource(0, &source);

    TickTrace    first(0), second(0);
    ComboWatcher watcher(0, &rig.build.moves[0], &rig.verdict);
    watcher.Reset();
    ASSERT_TRUE(session.AddObserver(&watcher));
    ASSERT_TRUE(session.AddObserver(&first));

    run(session, kWindowTicks);
    ASSERT_EQ(first.Size(), kWindowTicks);
    ASSERT_TRUE(first.Clean());

    const GameState     endOfFirst      = session.State();
    const std::uint32_t checksumOfFirst = session.Checksum();
    const ComboReport   reportOfFirst   = watcher.Current();
    const std::int32_t  completedOfFirst = watcher.CompletedCombos();
    ASSERT_GT(reportOfFirst.hits, 0)
        << "the first fight landed nothing, so there is no state for the second "
           "one to be compared against";

    // --- THE RESTART ---------------------------------------------------------
    ASSERT_TRUE(session.Begin(rig.setup, error)) << error;

    // The tick index does not drift, and the re-simulation high-water mark does
    // not survive. If HighWaterTick lived on, every tick of the second fight
    // would report itself `resimulated` -- and ComboWatcher would mark itself
    // STALE on the first one and never judge the new fight at all.
    EXPECT_EQ(session.CurrentTick(), 0u)
        << "the tick index survived a restart, so the second fight is numbered "
           "from where the first one stopped";
    EXPECT_EQ(session.HighWaterTick(), 0u)
        << "the re-simulation high-water mark survived a restart. Every tick of "
           "the new fight will report itself resimulated, the combo judge will go "
           "stale on the first one, and training mode will show nothing after the "
           "first reset.";
    // ResetMatch memsets and MatchStart::startPosX is re-applied, so the opening
    // position is the setup's again rather than wherever the first fight left the
    // fighters standing. Read off the fields rather than memcmp'd against the
    // first fight's opening, because that comparison is what the whole test does
    // below and asserting it here would only assert it twice.
    EXPECT_EQ(session.State().p[0].health, kStartingHealth);
    EXPECT_EQ(session.State().p[1].health, kStartingHealth);
    EXPECT_EQ(session.State().p[0].posX, kP0X);
    EXPECT_EQ(session.State().p[1].posX, kP1X);
    EXPECT_EQ(session.State().p[0].moveId, 0u)
        << "a fighter was still mid-move after a Begin that is documented to "
           "memset the state";

    // OBSERVERS ARE KEPT, deliberately. A host that re-registered them here would
    // get `false` back from AddObserver -- a duplicate registration is refused --
    // and a host that did not notice would carry on with the right number of
    // observers by luck.
    EXPECT_EQ(session.ObserverCount(), 2)
        << "the observers did not survive the restart, so a training host would "
           "have to rebind its recorder and its combo judge on every reset -- and "
           "would eventually drop one";
    EXPECT_FALSE(session.AddObserver(&watcher))
        << "the watcher was registered twice, which double-counts every hit";

    // Each observer resets ITSELF. That is the contract Begin leans on.
    watcher.Reset();
    ASSERT_TRUE(session.RemoveObserver(&first));
    ASSERT_TRUE(session.AddObserver(&second));

    run(session, kWindowTicks);
    ASSERT_EQ(second.Size(), kWindowTicks);
    ASSERT_TRUE(second.Clean());

    // --- BYTE FOR BYTE, TICK BY TICK ----------------------------------------
    for (std::uint32_t t = 0; t < kWindowTicks; ++t) {
        ASSERT_EQ(second.samples[t].inputs.p[0].bits, first.samples[t].inputs.p[0].bits)
            << "tick " << t << " of the second fight was fed different bits than "
               "the first, although the same source answered both";
        ASSERT_EQ(second.samples[t].checksum, first.samples[t].checksum)
            << "THE SECOND FIGHT DIVERGED AT TICK " << t << ". Something survived "
               "Begin() that should not have."
            << Table(first, rig.build.moves[0], t > 3 ? t - 3u : 0u, 8)
            << Table(second, rig.build.moves[0], t > 3 ? t - 3u : 0u, 8);
        ASSERT_EQ(0, std::memcmp(&second.samples[t].state, &first.samples[t].state,
                                 sizeof(GameState)))
            << "tick " << t << " differs in a field the checksum does not cover";
        ASSERT_FALSE(second.samples[t].resimulated)
            << "tick " << t << " of the second fight reported itself re-simulated";
    }

    EXPECT_EQ(0, std::memcmp(&session.State(), &endOfFirst, sizeof(GameState)));
    EXPECT_EQ(session.Checksum(), checksumOfFirst);
    EXPECT_EQ(session.CurrentTick(), kWindowTicks);
    EXPECT_EQ(session.HighWaterTick(), kWindowTicks);

    // AND THE LIVE JUDGE REACHED THE SAME VERDICT, which is the half a state
    // comparison cannot see: the watcher is not simulation state and Begin does
    // not touch it, so its agreeing is a statement about Reset() rather than
    // about the kernel.
    expectSameReport(watcher.Current(), reportOfFirst, rig.build.moves[0],
                     "the second fight's verdict");

    // THE COMPLETED-STRING COUNT IS THE ONE THAT CATCHES A FORGOTTEN Reset(), and
    // it needs saying because nothing else here does.
    //
    // A watcher carried into a new fight WITHOUT Reset() does not report an
    // obviously doubled hit count, which is the failure one would expect and go
    // looking for. Its stale string survives the restart, ends on the new fight's
    // FIRST ACTIONABLE TICK -- the defender is fresh, so tick 1 -- and is counted
    // there; Current() then starts over and reproduces the first fight's numbers
    // exactly. Every field compared above therefore MATCHES, and the only trace
    // left of the mistake is that a string ended which never began.
    //
    // Found by deleting the Reset() above and re-running: without this line the
    // test passed.
    EXPECT_EQ(watcher.CompletedCombos(), completedOfFirst)
        << "the second fight ended " << watcher.CompletedCombos() << " string(s) "
           "and the first ended " << completedOfFirst << ". The commonest cause is "
           "a host that restarted the session and did not call "
           "ComboWatcher::Reset(): the previous fight's string is still open, and "
           "it ends on the new fight's first actionable tick and is counted there.";

    RecordProperty("round_ticks", static_cast<int>(kWindowTicks));
    RecordProperty("round_hits", reportOfFirst.hits);
}

// THE SUBTLE ONE. Begin() does NOT reset the ComboWatcher, on purpose -- and a
// host that forgets to is not told. It shows the previous fight's combo, on the
// new fight, with a startTick that names a tick the new fight has not reached.
//
// This is asserted as a FACT rather than left as a trap, because there is a
// tempting wrong fix in both directions: making Begin() reset its observers would
// need the session to know what a ComboWatcher is (FightSession.h refuses that at
// length), and leaving it undocumented makes a training mode's most common
// action -- reset -- its least trustworthy display.
TEST(TrainingModeReset, BeginDoesNotResetTheWatcherAndAHostThatForgetsShowsTheOldCombo) {
    Rig rig{};
    bringUpInfinite(rig);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    const std::uint16_t held = rig.bindings[0].button;
    ASSERT_NE(held, 0u);

    FightSession session;
    std::string error;
    ASSERT_TRUE(session.Begin(rig.setup, error)) << error;

    ComboWatcher watcher(0, &rig.build.moves[0], &rig.verdict);
    watcher.Reset();
    ASSERT_TRUE(session.AddObserver(&watcher));

    TickTrace log(0);
    ASSERT_TRUE(session.AddObserver(&log));

    constexpr std::uint32_t kFightTicks = 60;
    for (std::uint32_t t = 0; t < kFightTicks; ++t) session.Tick(pairOf(held, 0));

    const std::int32_t  hitsBefore   = watcher.Current().hits;
    const std::uint32_t startBefore  = watcher.Current().startTick;
    ASSERT_GT(hitsBefore, 0)
        << "nothing connected, so there is no stale combo for the restart to carry";
    ASSERT_TRUE(watcher.Current().open)
        << "the string closed on its own, so what survives the restart below is "
           "an ENDED combo rather than an open one and the trap is a different "
           "shape than this test describes";

    // --- the host restarts and forgets --------------------------------------
    ASSERT_TRUE(session.Begin(rig.setup, error)) << error;

    // BEFORE A SINGLE TICK OF THE NEW FIGHT. No simulation is involved in this
    // assertion at all, which is what makes it exact: the watcher is simply still
    // holding what it held.
    EXPECT_EQ(watcher.Current().hits, hitsBefore)
        << "Begin() cleared the combo judge. That would be convenient and it is "
           "not what FightSession.h says -- observers are KEPT and each resets "
           "itself -- so either the header or the implementation has moved.";
    EXPECT_TRUE(watcher.Current().open);
    EXPECT_EQ(watcher.Current().startTick, startBefore);
    EXPECT_FALSE(watcher.Stale())
        << "the restart marked the judgement stale by itself, which would make "
           "the trap self-healing";

    // ...and the numbers it is showing are nonsense in the new fight's own terms:
    // the carried-over string claims its last hit landed at a tick this fight has
    // not run. A HUD drawing Current() here shows a hit count, a damage total and
    // a TRUE COMBO badge for a fight that no longer exists.
    EXPECT_GT(watcher.Current().lastHitTick, session.CurrentTick())
        << "the carried-over string's lastHitTick is at or behind the restarted "
           "session's current tick, so the stale report looks plausible rather "
           "than obviously wrong -- which is worse for a playtester, not better";

    // --- and the fix is one call, which is all the host has to remember ------
    watcher.Reset();
    EXPECT_EQ(watcher.Current().hits, 0);
    EXPECT_FALSE(watcher.Current().open);
    EXPECT_EQ(watcher.CompletedCombos(), 0);
    EXPECT_TRUE(watcher.Current().sequence.empty());
    EXPECT_TRUE(watcher.Current().edges.empty());
    EXPECT_FALSE(watcher.Stale());

    // A LATCHED PAD IS THE OTHER HALF OF THE SAME HOST DUTY, and it fails louder:
    // LatchedInputSource::Latch refuses any tick that is not NextTick(), so a host
    // that restarts the session and keeps latching from where it left off is fine,
    // while one that restarts the tick numbering without calling pad.Reset() is
    // refused on the very first tick of the new fight. `false` there means the
    // input log has a hole in it and the match must stop.
    LatchedInputSource pad(0, "YOU");
    for (std::uint32_t t = 0; t < kFightTicks; ++t)
        ASSERT_TRUE(pad.Latch(t, inputOf(held)));
    EXPECT_EQ(pad.NextTick(), kFightTicks);
    EXPECT_FALSE(pad.Latch(0, inputOf(held)))
        << "the pad accepted tick 0 again after a restart, so the past is "
           "rewritable and `same tick in, same bytes out` is no longer true";
    pad.Reset(0);
    EXPECT_EQ(pad.NextTick(), 0u);
    EXPECT_FALSE(pad.At(0).authored)
        << "Reset kept the previous match's history, so the new fight's tick 0 is "
           "answered with the old fight's bits";
    EXPECT_TRUE(pad.Latch(0, inputOf(held)));

    // The second fight, judged properly, is a fight of its own.
    for (std::uint32_t t = 0; t < kFightTicks; ++t) session.Tick(pairOf(held, 0));
    EXPECT_EQ(watcher.Current().hits, hitsBefore)
        << "the correctly reset watcher did not reproduce the first fight's count "
           "from the same inputs";
    EXPECT_EQ(watcher.Current().startTick, startBefore);
}

// ============================================================================
// 4. WHAT A PLAYTESTER WOULD STUMBLE INTO                     (CLAIM 4)
// ============================================================================
//
// UntitledFighterMode.cpp loads `Characters/fighter_a.json`. That character is
// TERMINATING, its certificate is that juggle runs down, and THE KERNEL HAS NO
// JUGGLE: `move.effect` is a BuildLoss with direction KernelOmits and
// BuildReport::playsAsAnalysed is false. tests/test_gap_extent.cpp enumerated the
// consequence -- 33 of its 41 usable cycles run forever in the running game, with
// the defender never getting a tick back.
//
// So the playtester in front of the mode is holding a character with 33 loops the
// analysis retired. Stumbling into one is the most valuable event this feature
// can produce and it is worth nothing if the game says nothing.
//
// This section drives one of them, with the analysis the mode ACTUALLY HOLDS, and
// asserts what the game can and cannot notice.

TEST(TrainingModeVerdict, ACertifiedAwayCycleNowStopsInsideTheAnalysisWorstCase) {
    Rig         rig{};
    std::string moveId;
    CancelIndex edgeIndex = 0;
    bringUpSafeSelfCycle(rig, moveId, edgeIndex);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    ASSERT_FALSE(moveId.empty());

    const MoveIndexMap& map  = rig.build.moves[0];
    const std::uint16_t slot = map.Find(moveId);
    ASSERT_NE(slot, 0u);

    // --- WHAT THE ANALYSIS SAYS, READ OUT OF THE VERDICT ---------------------
    //
    // Every number below is the verdict's own. Nothing is quoted from a document
    // and nothing is written down here, so a character edit moves the test rather
    // than making it about a character that no longer exists.
    ASSERT_TRUE(rig.verdict.hasRanking)
        << kSafe << " is TERMINATING with no ranking certificate, so `the "
           "certificate retires this cycle` is not a sentence about it.\n"
        << DescribeVerdict(rig.character, rig.verdict);
    ASSERT_EQ(rig.verdict.rankingAbsence, RankingAbsence::Present);
    ASSERT_GT(rig.verdict.maxHits, 0)
        << "the analysis reports a worst case of " << rig.verdict.maxHits
        << " hits. Without a positive bound there is nothing for the running game "
           "to exceed, and this test would pass vacuously.";

    // HOW MANY TIMES THE MODEL PERMITS THE CYCLE, DERIVED FROM THE FILE. The
    // move and the edge each carry a resource delta; the budget is the resource's
    // own `initial`. Whichever resource the pair spends, the number of turns the
    // model can afford is initial / spend -- and `nonNegative` refuses the next
    // one. This is the certificate's content, in the one cycle's terms.
    std::int32_t permittedTurns = -1;
    std::int32_t spentInitial   = 0;
    std::int32_t spentPerTurn   = 0;
    std::string  spentResource;
    {
        const Cancel& edge = rig.character.cancels[edgeIndex];
        const MoveIndex characterMove = MoveIndexMap::CharacterMoveOf(slot);
        ASSERT_NE(characterMove, kInvalidMove);
        ASSERT_LT(static_cast<std::size_t>(characterMove), rig.character.moves.size());
        const Move& move = rig.character.moves[characterMove];

        for (std::size_t r = 0; r < rig.character.resources.size(); ++r) {
            std::int32_t spend = 0;
            for (const ResourceAmount& a : move.effect)
                if (a.resource == static_cast<ResourceIndex>(r)) spend += a.value;
            for (const ResourceAmount& a : edge.effect)
                if (a.resource == static_cast<ResourceIndex>(r)) spend += a.value;
            if (spend >= 0) continue;   // gained or untouched: not a brake

            const std::int32_t turns = rig.character.resources[r].initial / (-spend);
            if (permittedTurns < 0 || turns < permittedTurns) {
                permittedTurns = turns;
                spentInitial   = rig.character.resources[r].initial;
                spentPerTurn   = -spend;
                spentResource  = rig.character.resources[r].name;
            }
        }
    }
    ASSERT_GE(permittedTurns, 0)
        << "`" << moveId << "` -> itself spends no resource at all, so the cycle "
           "is not the one the ranking certificate retires and this test is about "
           "the wrong edge.\n"
        << DescribeVerdict(rig.character, rig.verdict);

    // AND THE KERNEL DOES NOT HAVE IT. Asserted rather than assumed, because if a
    // future kernel grew resources this whole test becomes a statement about a
    // gap that has closed -- and it must say so rather than keep passing.
    {
        const BuildLoss* effects = nullptr;
        for (const BuildLoss& loss : rig.build.report[0].losses)
            if (loss.field == "move.effect") { effects = &loss; break; }
        ASSERT_NE(effects, nullptr)
            << "the build no longer reports what happens to `move.effect`, so "
               "nothing here can claim the kernel omits it";
        // Resources LANDED in ROADMAP M1.1b and this row is `exact` now. The gap
        // this test measures has NOT closed, and the distinction is the finding:
        // the kernel applies the delta and then clamps it at the authored floor,
        // so a cost the defender cannot pay is forgiven instead of ending the
        // combo. `playsAsAnalysed` below is still false, and it is false for a
        // narrower reason than when this test was written.
        EXPECT_EQ(effects->direction, BuildLossDirection::Exact)
            << "`move.effect` is recorded as "
            << BuildLossDirectionName(effects->direction)
            << ", and this test now needs it to be Exact: " << effects->note;
        EXPECT_GT(effects->count, 0);
        EXPECT_FALSE(rig.build.report[0].playsAsAnalysed)
            << "the build claims the kernel plays this character exactly as the "
               "analysis describes it, which would make a certified-away cycle "
               "impossible to perform";
    }

    // --- THE PLAYTESTER STUMBLES INTO IT -------------------------------------
    //
    // TURNS DERIVED FROM THE ANALYSIS'S OWN BOUND, not from a number that happens
    // to fit today: the string has to be longer than the worst case the analysis
    // reports, or crossing that bound is not what is being measured. The margin
    // is small because every extra turn is damage and the point is made the tick
    // the count passes maxHits.
    const std::uint32_t turns =
        static_cast<std::uint32_t>(rig.verdict.maxHits) + 6u;

    // The budget is generous ON PURPOSE and is derived too. A fixed 600 would
    // silently become "as many turns as fit in 600 ticks" the day the analysis's
    // bound or the cycle's period moved, and the failure would read as a stall.
    const std::uint32_t budget = (turns + 8u) * 64u;

    GameState from{};
    cse::kernel::ResetMatch(from, kSeed);
    from.p[0].posX = kP0X;
    from.p[1].posX = kP1X;

    Demonstration demo{};
    demonstrate(rig.kernelWitness, rig.loopStart, rig.build.data, from, turns, 0,
                budget, demo);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    // THE WATCHER GETS THE REAL VERDICT, UNMODIFIED. This is the difference from
    // tests/test_game_core.cpp's certified-away-cycle test, which writes a
    // synthetic one-move loop into a copy of the verdict so that the cycle matcher
    // has something to follow. That is the right test OF THE MATCHER. It is not
    // the situation the mode is in: the mode holds what AnalyseCharacter returned,
    // and a TERMINATING verdict carries no loop at all.
    FightSession session;
    std::string error;
    ASSERT_TRUE(session.Begin(rig.setup, error)) << error;

    ComboWatcher watcher(0, &map, &rig.verdict);
    watcher.Reset();
    ASSERT_TRUE(session.AddObserver(&watcher));

    TickTrace log(0);
    ASSERT_TRUE(session.AddObserver(&log));

    ScriptedInputSource source(demo.inputs, 0, "DEMO");
    session.SetInputSource(0, &source);
    run(session, static_cast<std::uint32_t>(demo.inputs.size()) + 12u);
    ASSERT_TRUE(log.Clean());

    const ComboReport& report = watcher.Current();

    // --- THE TWO INDEPENDENT COUNTS, AND WHY THERE ARE TWO -------------------
    //
    // This string is deliberately longer than a 1000-point health bar can express
    // at this character's damage, so the health-delta reading runs out partway
    // through and the alreadyHitBits reading does not. They must agree up to the
    // tick the health reaches zero; past it, only the second is a count.
    const std::vector<std::uint32_t> byHealth = log.HealthDeltaHitTicks();
    const std::vector<std::uint32_t> byBits   = log.ConnectedHitTicks();
    const std::uint32_t exhausted = log.HealthExhaustedTick();

    ASSERT_FALSE(byBits.empty())
        << "the certified-away cycle did not run at all, so there is nothing to "
           "notice. tests/test_ground_truth.cpp section 5 executes this same edge."
        << Table(log, map, 0, 40);

    for (std::size_t i = 0; i < byHealth.size(); ++i) {
        ASSERT_LT(i, byBits.size())
            << "the health-delta reading found a hit at tick " << byHealth[i]
            << " that the alreadyHitBits reading missed";
        ASSERT_EQ(byHealth[i], byBits[i])
            << "the two independent hit detectors disagree at hit " << i
            << ", before the health bar ran out at tick " << exhausted << "."
            << Table(log, map, byHealth[i] > 4 ? byHealth[i] - 4u : 0u, 10);
    }

    // THE RUN IS STRINGS NOW, NOT ONE COMBO. Since ROADMAP M1.3e the cycle is
    // performed the way a player performs it -- jump, cancel down the arc,
    // land, jump again -- so the watcher correctly RESETS at every landing
    // (the defender leaves hitstun there) and `report` describes the LAST
    // string, not the run. The per-string shape is recovered from the hit
    // ticks themselves: hits inside a string arrive at the cancel period, and
    // a landing is a longer gap.
    for (std::uint16_t m : report.sequence)
        ASSERT_EQ(m, slot) << "a connecting move other than `" << moveId
                           << "` appeared in a one-move cycle";
    ASSERT_GE(byBits.size(), 2u);
    const std::uint32_t period = byBits[1] - byBits[0];
    std::vector<std::int32_t> stringLens;
    std::int32_t cur = 1;
    for (std::size_t i = 1; i < byBits.size(); ++i) {
        if (byBits[i] - byBits[i - 1] <= period) ++cur;
        else { stringLens.push_back(cur); cur = 1; }
    }
    stringLens.push_back(cur);
    std::int32_t maxString = 0;
    for (std::int32_t n : stringLens) maxString = maxString > n ? maxString : n;

    // --- THE HEADLINE, INVERTED BY M1.3e: NOTHING OUTRUNS THE ANALYSIS ------
    //
    // This test used to measure a string LONGER than ProverResult::maxHits --
    // the certified-away cycle running forever off the ground route the
    // missing stance wire left open. That route is closed. What the kernel
    // produces now is strings the certificate can look at without flinching:
    // the longest is exactly the certificate's own permitted turns -- an
    // AGREEMENT OF COUNT, not of reason, because juggle is still unwired
    // (M1.1f) and what ends the kernel's string is the ballistic arc running
    // out of air. If maxString ever exceeds permittedTurns again, a route
    // around the arc has opened and the infinite is back; if it falls short,
    // the arc got shorter than the budget and the agreement was luck.
    EXPECT_EQ(maxString, permittedTurns)
        << "the longest single string is " << maxString << " hit(s) against the "
           "certificate's " << permittedTurns << " -- the two bounds no longer "
           "coincide."
        << Table(log, map, 0, 40);
    EXPECT_LE(maxString, rig.verdict.maxHits)
        << "a single string of " << maxString << " outran the analysis's stated "
           "worst case of " << rig.verdict.maxHits
        << " hits -- something the analysis says cannot happen.";
    EXPECT_LE(report.hits, rig.verdict.maxHits);

    // AND THE CYCLE STILL TURNS -- across strings, with the defender free at
    // every landing. The total exceeding the per-combo budget is NOT a
    // violation of the certificate: `nonNegative` is a statement about ONE
    // combo, and the model's own juggle restores when the defender recovers,
    // exactly as the kernel's does. More than one string is what proves the
    // demonstration performed its turns ACROSS jumps rather than stopping at
    // the first landing.
    EXPECT_GE(stringLens.size(), 2u)
        << "the whole run was one string, so the demonstration stopped at the "
           "first landing" << Table(log, map, 0, 40);
    EXPECT_GT(static_cast<std::int32_t>(byBits.size()), permittedTurns);

    // Every repetition connected: no whiffs padding the count, and one move
    // start per connecting hit.
    const std::vector<std::uint32_t> starts = log.MoveStartTicks(0);
    EXPECT_EQ(starts.size(), byBits.size())
        << "`" << moveId << "` started " << starts.size() << " time(s) and "
           "connected " << byBits.size() << " time(s)"
        << Table(log, map, 0, 32);
    EXPECT_EQ(report.whiffedStarts, 0);

    // The LAST string is still a true combo on the direct Fighter::hitstun
    // reading -- within a string the defender never gets a tick back.
    EXPECT_EQ(report.gapTicks, 0)
        << "the defender was actionable on " << report.gapTicks << " tick(s) "
           "INSIDE a string; the free ticks belong between strings."
        << DescribeReport(report, map)
        << Table(log, map, byBits.front(), 32);
    EXPECT_TRUE(report.TrueCombo());

    // --- AND THE PART THE MODE IS BUILT AROUND -------------------------------
    //
    // NONE OF ComboWatcher'S LOUD MARKERS IS UP, and every one of them is down
    // for a correct reason:
    //
    //   cycleRun / loopTurnsCompleted  matched against ProverResult::loop, and a
    //                                  TERMINATING verdict carries no loop.
    //   completedProverLoop            gated on ProverStatus::Infinite.
    //   performedDeadCancel            the edge is one the prover KEEPS.
    //
    // Before M1.3e a quiet HUD here was a LIE -- the playtester was performing
    // something the analysis called impossible and no flag said so. Now the
    // quiet is earned: the game stops where the certificate says. The mode
    // still has to draw the COMPARISON rather than only the flags, because the
    // comparison is the instrument that caught the wire missing -- and the day
    // maxString and permittedTurns part company again, it is what says so.
    EXPECT_EQ(report.cycleRun, 0)
        << "the cycle matcher followed a loop on a TERMINATING verdict, which "
           "carries none. If ProverResult::loop is now populated for a terminating "
           "character, the assertions in this block are the ones to revisit.";
    EXPECT_EQ(report.loopTurnsCompleted, 0);
    EXPECT_FALSE(report.completedProverLoop)
        << "the watcher claims the player completed a PROVER LOOP on a character "
           "the decision procedure calls TERMINATING, which printed no loop at "
           "all. `completedProverLoop` is gated on ProverStatus::Infinite."
        << DescribeReport(report, map);
    EXPECT_FALSE(report.performedDeadCancel)
        << "the cycle this test derived as the prover's LIVE self-cancel is "
           "reported as dead, so the derivation picked the wrong edge"
        << DescribeReport(report, map);
    EXPECT_FALSE(report.deadEdgeConnected);

    // THE OTHER QUALIFICATION THE MODE ALREADY HAS IN HAND, and it is a different
    // axis from the one above: `soundnessAlarm` is the analysis saying its own
    // TERMINATING verdict rests on a projection that could HIDE a real infinite.
    // Whatever it happens to be, the mode showing a certified character must not
    // be silent about it, so it is recorded rather than asserted one way.
    RecordProperty("soundness_alarm", rig.verdict.soundnessAlarm ? 1 : 0);
    RecordProperty("prover_max_hits", rig.verdict.maxHits);
    RecordProperty("longest_string_hits", maxString);
    RecordProperty("permitted_turns", permittedTurns);
    RecordProperty("total_hits_across_strings", static_cast<int>(byBits.size()));
    RecordProperty("cycle_move", moveId);

    std::cout
        << "\n[ TRAINING MODE ] a certified-away cycle, judged live on `"
        << rig.character.id << "` -- the character the mode ships\n"
        << "  the analysis says   TERMINATING, ranking certificate present, worst "
           "case " << rig.verdict.maxHits << " hits\n"
        << "  the certificate     `" << moveId << "` -> itself spends "
        << spentPerTurn << " " << spentResource << " of " << spentInitial
        << ", so the model affords " << permittedTurns << " turn(s)\n"
        << "  the kernel ran it   " << stringLens.size() << " string(s), longest "
        << maxString << " hit(s), " << byBits.size() << " hits in all; defender "
        << kStartingHealth << " -> " << log.Final().p[1].health << "\n"
        << "  health bar ran out  "
        << (exhausted < static_cast<std::uint32_t>(log.Size())
                ? "at tick " + std::to_string(exhausted)
                : std::string("no"))
        << " (past it only the alreadyHitBits reading is a count)\n"
        << "  THE COUNT AGREES AND THE REASON DOES NOT: the arc ends the "
           "kernel's string where the model's\n"
        << "  budget ends its own. Juggle is still unwired (M1.1f); making the "
           "graph agree for the right\n"
        << "  reason is M1.4a. The watcher's quiet is earned now -- before "
           "M1.3e it was a lie.\n\n";
}

// ============================================================================
// 5. THE NUMBERS THE MODE DERIVES RATHER THAN READS           (CLAIM 5)
// ============================================================================
//
// Sections 1-4 are about the SEAM training mode is built from -- the session,
// the sources, the judge. This one is about the four quantities the mode computes
// on top of it and puts in front of a playtester: how long until this fighter can
// act, who is plus, how far apart the bodies are, and which frames are dangerous.
//
// ---------------------------------------------------------------------------
// WHY A DERIVATION IS THE DANGEROUS KIND OF NUMBER
// ---------------------------------------------------------------------------
// FightHud.h's own opening states the rule: "EVERYTHING DRAWN HERE IS READ FROM
// THE SIMULATION OR FROM THE ANALYSIS. Not recomputed beside them", and it names
// the cost of breaking it -- "a playtester told `+2` by a HUD that computed it
// with its own arithmetic cannot [find out]: they will simply conclude the game
// is wrong about a number the game never said." The header then allows exactly
// two derivations and documents the kernel rule behind each.
//
// The trouble with a derivation is not that it might be noisy. It is that it is
// STABLE: a rule missing a term returns the same wrong number every time, on
// every machine, in every replay, and nothing about the screen looks broken.
// Sections 1-4 would all pass with every number below wrong.
//
// ---------------------------------------------------------------------------
// WHY THESE TESTS DO NOT LINK THE HUD, AND WHAT THEY ARE THEREFORE ABOUT
// ---------------------------------------------------------------------------
// Games/UntitledFighter/Modes/ links Renderer2D, a font atlas and a GL context,
// and tests/ may not acquire one -- the same rule that put every other claim in
// this file behind a headless seam. So each derivation is RESTATED below in the
// shape its source spells it, and WHAT IS UNDER TEST IS THE KERNEL'S ANSWER:
// the number the tick actually honours, MEASURED by driving cse::kernel::Simulate
// and watching what the fighters do.
//
// That boundary is worth being honest about. A restatement cannot catch a typo in
// the original -- if FightHud.cpp gains a `+ 1` tomorrow, nothing here fails. It
// catches the other thing, which is the thing a typo cannot be blamed for: A RULE
// THAT IS MISSING A TERM. Both derivations below are missing one, and the
// measurements name the missing term rather than merely disagreeing.
//
// ---------------------------------------------------------------------------
// AND WHY THE DRIVING IS RAW Simulate RATHER THAN A FightSession
// ---------------------------------------------------------------------------
// The rig is the sections above's -- loadShipped, buildMirror, the same body and
// the same two start positions -- but the ticking is not. Two of the four tests
// need a FORK: one state advanced twice with different inputs, to ask "what would
// have happened if the playtester had pressed the follow-up HERE". A fork is a
// kernel question, and FightSession can only express it by pretending a rollback
// happened (Snapshot, tick, Restore), which would put TickView::resimulated into
// a measurement that has nothing to do with re-simulation. tests/test_cancels.cpp
// drives the same way and for the same reason.

namespace {

// --- The character, bound BY NAME --------------------------------------------

// bringUpFrom() derives bindings from a witness and hands out buttons
// positionally, which is right when the witness is the subject. These four tests
// are about NAMED moves -- the source of a cancel, its target, and the two whose
// boxes get drawn -- so the bindings are written out and which key starts which
// move is part of the setup rather than an accident of pool order.
//
// No ProverResult: nothing in this section asks the analysis anything. It is
// about the kernel and the bridge only, which is also why these four tests would
// still mean what they say on a character the prover could not decide.
struct Bench {
    CharacterData character;
    MatchBuild    build;
};

void bringUpBench(const std::vector<MoveBinding>& bindings, Bench& out) {
    loadShipped(kSafe, out.character);
    if (::testing::Test::HasFatalFailure()) return;
    ASSERT_TRUE(buildMirror(out.character, bindings, out.build))
        << "p0: " << out.build.report[0].error
        << " / p1: " << out.build.report[1].error;
}

// The opening the rest of the file uses, as a plain GameState.
GameState opening() {
    GameState s{};
    cse::kernel::ResetMatch(s, kSeed);
    s.p[0].posX = kP0X;
    s.p[1].posX = kP1X;
    return s;
}

// The gap chip's own opening: see kWalkP0X for why this file has two.
GameState walkingOpening() {
    GameState s = opening();
    s.p[0].posX = kWalkP0X;
    s.p[1].posX = kWalkP1X;
    return s;
}

void step(GameState& s, const MatchData& data, std::uint16_t p0Bits,
          std::uint16_t p1Bits = 0u) {
    cse::kernel::Simulate(s, pairOf(p0Bits, p1Bits), data);
}

// The edge from one kernel slot to another, or null. Same shape as
// tests/test_cancels.cpp's, and for the same reason: FindCancel takes the FIRST
// match, and every pair asked for below is unique in this character.
const cse::kernel::CancelEdge* findEdge(const cse::kernel::FighterData& d,
                                        std::uint16_t from, std::uint16_t to) {
    for (std::int32_t i = 0; i < d.cancelCount; ++i)
        if (d.cancels[i].from == from && d.cancels[i].to == to) return &d.cancels[i];
    return nullptr;
}

const BuildLoss* findLoss(const BuildReport& report, const char* field) {
    for (const BuildLoss& loss : report.losses)
        if (loss.field == field) return &loss;
    return nullptr;
}

// --- DERIVATION ONE, RESTATED: ticks until this fighter can act --------------
//
// FightHud.cpp's TicksUntilActionable, term for term. Its header states the two
// terms and states each one correctly:
//
//   "a fighter observed with stun h at the end of tick t is free on tick t + h"
//   "a fighter observed in a move at frame f is free on tick t + (duration - f)"
//
// Both sentences are true of Simulate.cpp. Together they are not the rule, and
// the first test below is what the missing third one costs.
std::int32_t twoTermTicksUntilActionable(const cse::kernel::FighterData& data,
                                         const cse::kernel::Fighter& fighter) {
    std::int32_t ticks = static_cast<std::int32_t>(fighter.hitstun);
    const std::int32_t block = static_cast<std::int32_t>(fighter.blockstun);
    if (block > ticks) ticks = block;

    const cse::kernel::MoveDef* const move =
        cse::kernel::MoveAt(data, fighter.moveId);
    if (move != nullptr) {
        const std::int32_t left = cse::kernel::MoveDuration(*move) -
                                  static_cast<std::int32_t>(fighter.moveFrame);
        if (left > ticks) ticks = left;
    }
    return ticks < 1 ? 1 : ticks;
}

// FightHud.cpp's FrameAdvantage, on top of it. `known` is false unless the
// defender is in stun, which is the header's own gate and is half of why the
// second test's number flickers rather than merely changing.
struct TwoTermAdvantage {
    bool         known = false;
    std::int32_t ticks = 0;
};

TwoTermAdvantage twoTermAdvantage(const MatchData& data, const GameState& state,
                                  int attackerSlot) {
    TwoTermAdvantage out{};
    const int defenderSlot = 1 - attackerSlot;
    const cse::kernel::Fighter& defender = state.p[defenderSlot];
    if (defender.hitstun == 0 && defender.blockstun == 0) return out;

    out.known = true;
    out.ticks = twoTermTicksUntilActionable(data.p[defenderSlot], defender) -
                twoTermTicksUntilActionable(data.p[attackerSlot],
                                            state.p[attackerSlot]);
    return out;
}

// --- THE SAME QUESTION, ASKED OF THE KERNEL ----------------------------------

struct Freedom {
    bool          started       = false;
    std::uint32_t ticks         = 0;   // ticks after the forked state
    std::uint16_t startedMove   = 0;
    std::uint16_t leftAtFrame   = 0;   // the frame the SOURCE move was left on
    bool          byCancel      = false;
};

// A MEASUREMENT, NOT A SECOND FORMULA. Copy the state, hold the follow-up's
// button, and run until the attacker is in a move other than `source` on frame
// 0 -- which is StepAttack's own definition of a move having begun, and the one
// BuildDemonstration and ComboWatcher both insist on (a move that cancels into
// ITSELF never changes its id, so a transition detector sees nothing happen).
Freedom measureFreedom(const MatchData& data, const GameState& from,
                       std::uint16_t sourceMove, std::uint16_t bits,
                       std::uint32_t budget) {
    Freedom out{};
    GameState s = from;

    // PULSED, AND BUFFERED, because StepAttack reads the PRESS. Holding `bits`
    // for the whole fork is ONE press however long the fork runs, so a fork that
    // held them answered "the attacker never acted" for every frame where the
    // first tick was not already actionable -- a fact about this function, not
    // about the kernel. The question being asked is "how soon CAN the attacker
    // act", so the fork must have a fresh press available on every tick the
    // attacker might take one.
    //
    // Pulsing alone would only supply an edge every other tick and the answer
    // could land a frame late; the two-tick buffer covers the tick between, so
    // the press is consumed the exact frame the kernel opens. Buffer and pulse
    // are chosen together and neither is sufficient on its own.
    MatchData armed = data;
    armed.p[0].inputBufferFrames = 2;
    armed.p[1].inputBufferFrames = 2;

    for (std::uint32_t k = 1; k <= budget; ++k) {
        const std::uint16_t beforeId    = s.p[0].moveId;
        const std::uint16_t beforeFrame = s.p[0].moveFrame;
        step(s, armed, (k % 2u == 1u) ? bits : std::uint16_t{0});

        const cse::kernel::Fighter& atk = s.p[0];
        if (atk.moveId == 0 || atk.moveId == sourceMove || atk.moveFrame != 0)
            continue;

        out.started     = true;
        out.ticks       = k;
        out.startedMove = atk.moveId;
        out.leftAtFrame = static_cast<std::uint16_t>(beforeFrame + 1u);

        // A CANCEL AND A RESTART ARE BOTH "the attacker started something", and
        // they are not the same event -- the two-term rule is exactly right about
        // the second one. The line between them is tests/test_cancels.cpp's: the
        // source still had frames left, so it was interrupted rather than spent.
        const cse::kernel::MoveDef* const src =
            cse::kernel::MoveAt(data.p[0], beforeId);
        out.byCancel = src != nullptr &&
                       static_cast<std::int32_t>(beforeFrame) + 1 <
                           cse::kernel::MoveDuration(*src);
        return out;
    }
    return out;
}

// --- DERIVATION TWO, BOTH WAYS: how far apart the bodies are -----------------

// The standard separation of two intervals. Positive is the space between them,
// zero is touching, negative is how deep they overlap. SYMMETRIC IN ITS
// ARGUMENTS, which is the property the other spelling lacks.
std::int32_t separation(const cse::kernel::Box& a, const cse::kernel::Box& b) {
    const std::int32_t aThenB = b.x0 - a.x1;   // a on the left
    const std::int32_t bThenA = a.x0 - b.x1;   // b on the left
    return aThenB > bThenA ? aThenB : bThenA;
}

// The gap chip's own expression, quoted from FightHud.cpp so that the number it
// prints can be PRODUCED here rather than described. The `else` is not the mirror
// of the `then`: it is the separation the two bodies would have if they were the
// other way round, so it answers a different question the moment the condition
// that chose it stops holding.
std::int32_t ternaryGap(const cse::kernel::Box& b0, const cse::kernel::Box& b1) {
    return b0.x1 < b1.x0 ? b1.x0 - b0.x1 : b0.x0 - b1.x1;
}

// Sub-units are the kernel's unit and pixels are the chip's, so a failure carries
// both rather than asking a reader to divide by 256.
std::string subAndPx(std::int32_t sub) {
    std::ostringstream s;
    s << sub << " sub (" << (sub / cse::kernel::kSubUnitsPerPixel) << " px)";
    return s.str();
}

}  // namespace

// THE THIRD WAY OUT OF A MOVE.
//
// A fighter stops being stuck for one of three reasons, and Combat.cpp's
// StepAttack performs them in this order, in one call:
//
//   1. the stun ran out                     (the `actionable` gate)
//   2. the move reached its duration        (the lifecycle, then the button scan)
//   3. THE MOVE WAS CANCELLED               (FindCancel, between the two)
//
// The HUD's rule has terms for 1 and 2. Cancelling is the entire subject of this
// game -- it is what the combo prover reasons about, what MatchBuilder spends
// four loss entries projecting, and what the character file's 73 edges are -- and
// the number that answers "when can I move again" does not mention it.
//
// The test drives the kernel and measures the answer from EVERY frame of the
// source move, so the result is a shape rather than one number: the rule is
// EXACTLY RIGHT on the frames after the window shuts, and up to nine ticks too
// long on the frames before. That shape is why it survived review -- it is
// correct whenever there is no cancel to take.
TEST(TrainingModeReadout, ACancelIsAThirdWayOutOfAMoveAndTheTwoTermRuleMissesIt) {
    // stand_lp on LP, stand_mp on MP, AND NOTHING ELSE BOUND. That is what makes
    // the measurement unambiguous rather than merely convenient: stand_lp authors
    // six outgoing edges in this file (four chains and two specials) and
    // FindCancel skips every edge whose target has no button, so exactly one of
    // the six can fire and this test knows which one it measured.
    Bench bench{};
    bringUpBench({ bind("stand_lp", cse::kernel::kInputLP),
                   bind("stand_mp", cse::kernel::kInputMP) }, bench);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    const MoveIndexMap&             map  = bench.build.moves[0];
    const cse::kernel::FighterData& data = bench.build.data.p[0];

    const std::uint16_t lp = map.Find("stand_lp");
    const std::uint16_t mp = map.Find("stand_mp");
    ASSERT_NE(lp, 0u) << kSafe << " no longer has `stand_lp`";
    ASSERT_NE(mp, 0u) << kSafe << " no longer has `stand_mp`";

    const cse::kernel::MoveDef* const source = cse::kernel::MoveAt(data, lp);
    ASSERT_NE(source, nullptr);
    const std::int32_t duration = cse::kernel::MoveDuration(*source);

    // THE EDGE, AS THE BRIDGE RESOLVED IT. The file authors
    // `stand_lp -> stand_mp` at delay 2 and gives stand_lp a cancel window of
    // [5, 9]; MatchBuilder.cpp resolves the delay against the source's FIRST
    // active frame, so earliest = startup 3 + delay 2 = 5, and intersects the
    // authored window, which pulls the close in from duration - 1 = 13 to 9.
    const cse::kernel::CancelEdge* const edge = findEdge(data, lp, mp);
    ASSERT_NE(edge, nullptr)
        << "`stand_lp -> stand_mp` did not survive the build, so there is no "
           "cancel for the rule to be missing and this test is about nothing.";
    EXPECT_EQ(edge->earliestFrame, 5);
    EXPECT_EQ(edge->latestFrame, 9);
    EXPECT_EQ(edge->onHit, 1u) << "the file authors this edge `on: hit`";
    EXPECT_EQ(duration, 14) << "stand_lp is 3 + 2 + 9 in this file";

    // THE FASTEST THE KERNEL CAN TAKE A CONTACT-GATED EDGE. StepAttack runs
    // BEFORE ResolveHits, so a hit landing on tick N is not visible to a cancel
    // test until N+1 (Combat.h says so at length and Simulate.cpp repeats it).
    // The window here opens two ticks after that, so the bound is not what is
    // being measured -- but modelling the kernel without it would be modelling a
    // different kernel, and the day an edge is authored at delay 0 this term is
    // the one that keeps the expectation honest.
    const std::int32_t firstPossible =
        edge->earliestFrame > source->startup + 1 ? edge->earliestFrame
                                                  : source->startup + 1;

    // --- the trunk: ONE PRESS OF LP, and stand_lp lives its whole life -------
    //
    // One tick and not a hold. A held button restarts the move in the same
    // StepAttack call that ended it -- the next test is about exactly that -- and
    // here it would lay a second stand_lp on top of the frames being swept.
    std::vector<GameState> trunk;
    {
        GameState s = opening();
        for (std::int32_t t = 0; t < duration + 2; ++t) {
            step(s, bench.build.data,
                 static_cast<std::uint16_t>(t == 0 ? cse::kernel::kInputLP : 0));
            trunk.push_back(s);
        }
    }

    // THE PREMISE. The on-hit gate means an edge that never connects never opens,
    // so if stand_lp misses at this distance every fork below falls through to
    // the move's own duration and the two rules agree for a reason that has
    // nothing to do with the finding.
    const std::size_t contactTick = static_cast<std::size_t>(source->startup);
    ASSERT_LT(contactTick, trunk.size());
    ASSERT_NE(trunk[contactTick].p[0].alreadyHitBits, 0u)
        << "`stand_lp` did not connect on its first active frame (" << contactTick
        << ") at the opening distance, so the contact gate on the cancel edge is "
           "never satisfied and this test would measure the absence of a cancel "
           "rather than the presence of one.";

    // --- the sweep ----------------------------------------------------------
    std::ostringstream table;
    table << "\n  frame  measured  route        left at  two-term  overstated\n";

    std::int32_t worstOverstatement = 0;
    std::int32_t disagreements      = 0;
    std::int32_t agreements         = 0;
    std::int32_t framesSwept        = 0;

    for (std::size_t t = 0; t < trunk.size(); ++t) {
        const GameState& at = trunk[t];
        if (at.p[0].moveId != lp) continue;   // the move has ended; nothing to ask
        ++framesSwept;

        const std::int32_t frame = static_cast<std::int32_t>(at.p[0].moveFrame);

        // WHAT THE KERNEL DOES: fork here, hold the follow-up, and watch.
        const Freedom got =
            measureFreedom(bench.build.data, at, lp, cse::kernel::kInputMP,
                           static_cast<std::uint32_t>(duration) * 4u);
        ASSERT_TRUE(got.started)
            << "from frame " << frame << " of `stand_lp`, holding MP for "
            << duration * 4 << " ticks started nothing at all. The attacker is "
            << "then never actionable by any route, which is not a state this "
               "kernel has.";
        ASSERT_EQ(got.startedMove, mp)
            << "from frame " << frame << " the attacker started `"
            << moveName(map, got.startedMove)
            << "`, which is neither the source nor the bound follow-up";

        // WHAT THE KERNEL'S OWN NUMBERS SAY IT SHOULD DO. Written out rather than
        // read back off the run, so this is a model being checked and not a
        // recording being reprinted.
        const std::int32_t expectedLeaveFrame =
            frame + 1 <= edge->latestFrame
                ? (frame + 1 > firstPossible ? frame + 1 : firstPossible)
                : duration;
        EXPECT_EQ(static_cast<std::int32_t>(got.leftAtFrame), expectedLeaveFrame)
            << "from frame " << frame << " the attacker left `stand_lp` on frame "
            << got.leftAtFrame << ". The edge's window is ["
            << edge->earliestFrame << ", " << edge->latestFrame
            << "] and the move is " << duration << " ticks long.";
        EXPECT_EQ(static_cast<std::int32_t>(got.ticks), expectedLeaveFrame - frame);
        EXPECT_EQ(got.byCancel, expectedLeaveFrame < duration)
            << "frame " << frame << ": the route out was "
            << (got.byCancel ? "a cancel" : "the move running out")
            << " and the window says otherwise";

        const std::int32_t rule = twoTermTicksUntilActionable(data, at.p[0]);
        const std::int32_t over = rule - static_cast<std::int32_t>(got.ticks);
        if (over > worstOverstatement) worstOverstatement = over;
        if (over != 0) ++disagreements; else ++agreements;

        // THE RULE IS NEVER SHORT. Worth asserting separately from the count:
        // a HUD that told a playtester they were free EARLIER than they are is a
        // different and worse failure than one that tells them later, and nothing
        // else here would distinguish the two.
        EXPECT_GE(over, 0)
            << "at frame " << frame << " the two-term rule says the attacker is "
               "free in " << rule << " tick(s) and the kernel freed them in "
            << got.ticks << ". A readout that is EARLY is worse than one that is "
               "late: it invites a playtester to press a button the game will "
               "ignore and to conclude the input was dropped.";

        table << "  " << std::setw(5) << frame
              << "  " << std::setw(8) << got.ticks
              << "  " << std::setw(11) << std::left
              << (got.byCancel ? "cancel" : "move ended") << std::right
              << "  " << std::setw(7) << got.leftAtFrame
              << "  " << std::setw(8) << rule
              << "  " << std::setw(10) << over << "\n";
    }

    ASSERT_EQ(framesSwept, duration)
        << "the sweep saw " << framesSwept << " frames of a " << duration
        << "-tick move" << table.str();

    // --- THE HEADLINE -------------------------------------------------------
    //
    // IF THESE AGREE, THE FINDING IS WRONG AND THAT IS THE RESULT. Said as a hard
    // failure with the whole table attached, rather than as a test that quietly
    // passes either way: "the reviewer was mistaken" is a publishable outcome and
    // an unnoticed one is not.
    ASSERT_GT(disagreements, 0)
        << "THE TWO-TERM RULE AGREED WITH THE KERNEL ON ALL " << framesSwept
        << " FRAMES OF `stand_lp`. That is a finding in the other direction and "
           "it should be read before anything is changed -- the likeliest causes "
           "are that the cancel never fired (check the contact assertion above), "
           "that `stand_mp` is not bound, or that the edge's window has moved."
        << table.str();

    // The worst case, derived rather than typed: from frame 0 the rule says the
    // whole move (`duration`) and the cancel puts the follow-up out on the
    // window's opening frame.
    EXPECT_EQ(worstOverstatement, duration - edge->earliestFrame)
        << "the rule's worst overstatement was " << worstOverstatement
        << " tick(s)." << table.str();
    EXPECT_EQ(worstOverstatement, 9)
        << "on this character's `stand_lp -> stand_mp` the row is NINE TICKS TOO "
           "LONG at its worst. If this moved, the file's delay, the move's "
           "startup or its cancel window did." << table.str();

    // THE SHAPE, AND IT IS THE POINT. The rule is exact on exactly the frames
    // from which no cancel is reachable -- after the window's close there are
    // `duration - latestFrame` of them -- and wrong on every frame before it.
    // That is why a reader checking the derivation against Simulate.cpp comes
    // away satisfied: every term in it is right, and the set it is right ON is
    // the set with no third route out.
    EXPECT_EQ(agreements, duration - edge->latestFrame)
        << "the rule was exact on " << agreements << " frame(s)" << table.str();
    EXPECT_EQ(disagreements, edge->latestFrame)
        << "the rule was wrong on " << disagreements << " frame(s)" << table.str();

    RecordProperty("actionable_worst_overstatement", worstOverstatement);
    RecordProperty("actionable_frames_wrong", disagreements);
    RecordProperty("actionable_frames_right", agreements);

    std::cout
        << "\n[ TRAINING MODE ] when is a fighter actually actionable -- `"
        << bench.character.id << "`, stand_lp -> stand_mp\n"
        << "  the two-term rule   max(stun, MoveDuration - moveFrame), clamped at 1\n"
        << "  the missing term    FindCancel, which StepAttack runs BETWEEN the "
           "lifecycle and the button scan\n"
        << "  the edge            window [" << edge->earliestFrame << ", "
        << edge->latestFrame << "] on hit, out of a " << duration << "-tick move\n"
        << "  measured            exact on " << agreements << " of " << framesSwept
        << " frames, up to " << worstOverstatement << " TICKS TOO LONG on the "
           "other " << disagreements << "\n"
        << table.str() << "\n";
}

// AND THE SAME NUMBER, RECOMPUTED EVERY FRAME, DOES NOT HOLD STILL.
//
// This is the other half of the frame-advantage readout and it is independent of
// the missing term above: even with the arithmetic granted, a value computed from
// LIVE STATE describes the tick it was computed on and not the interaction the
// playtester is trying to learn. Any periodic drive makes that unavoidable rather
// than unlikely, because every quantity derived from (moveFrame, hitstun) is then
// periodic with the move's duration.
//
// WHAT DRIVES IT HERE IS A RE-PRESS, not a held key, and that is the one thing
// this test had to be rewritten for. StepAttack's button scan reads the PRESS
// -- the rising edge -- so a key left down starts `stand_lp` exactly once and
// the fighter idles from then on. The playtester who sees the strobe is the one
// mashing, so the drive below releases for the last tick of each repetition and
// presses again on the tick the move ends. The period is therefore identical to
// the one a held key used to produce, and every number below is unchanged; what
// changed is whose behaviour it is a fact about.
//
// The measurement below is the reason a training HUD has to LATCH this number on
// the contact tick rather than recompute it: not because recomputation is
// expensive, but because there is no single tick whose reading is the answer.
TEST(TrainingModeReadout, ARepeatedPressRestartsTheMoveSoALiveAdvantageStrobesForever) {
    // ONLY stand_lp IS BOUND, so no cancel is reachable at all and the period
    // below is unambiguously the move's own duration rather than a chain's.
    Bench bench{};
    bringUpBench({ bind("stand_lp", cse::kernel::kInputLP) }, bench);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    const MoveIndexMap&             map  = bench.build.moves[0];
    const cse::kernel::FighterData& data = bench.build.data.p[0];
    const std::uint16_t             lp   = map.Find("stand_lp");
    ASSERT_NE(lp, 0u);

    const cse::kernel::MoveDef* const move = cse::kernel::MoveAt(data, lp);
    ASSERT_NE(move, nullptr);
    const std::int32_t duration = cse::kernel::MoveDuration(*move);
    const std::int32_t startup  = move->startup;
    const std::int32_t stun     = move->hitstun;
    ASSERT_GT(duration, 0);
    ASSERT_GT(stun, 0);

    // Three periods: one to warm up (nothing has connected yet, so the readout is
    // legitimately unknown for the first few ticks) and two to measure.
    const std::int32_t total = duration * 3;

    std::vector<TwoTermAdvantage> readings;
    std::vector<std::uint32_t>    hitTicks;
    {
        GameState s = opening();
        std::int32_t previousHealth = kStartingHealth;
        for (std::int32_t t = 0; t < total; ++t) {
            // RELEASED FOR THE LAST TICK OF EACH REPETITION so the next tick is a
            // PRESS. Releasing cannot disturb the repetition it ends -- a move
            // already running ignores the button entirely -- and it is what buys
            // the rising edge that starts the next one exactly on time.
            const bool release = (t % duration) == duration - 1;
            step(s, bench.build.data,
                 release ? std::uint16_t{0} : cse::kernel::kInputLP);

            // THE PERIOD, ASSERTED TICK BY TICK RATHER THAN INFERRED AT THE END.
            // A re-press never lets the fighter reach idle: the move ends and the
            // button scan finds this tick's press inside the same StepAttack
            // call, so the frame counter is exactly the tick index modulo the
            // duration.
            ASSERT_EQ(s.p[0].moveId, lp)
                << "tick " << t << ": the attacker is not in `stand_lp` although "
                   "LP has been pressed once per repetition since tick 0";
            ASSERT_EQ(static_cast<std::int32_t>(s.p[0].moveFrame), t % duration)
                << "tick " << t << ": `stand_lp` is on frame " << s.p[0].moveFrame
                << " and a move restarted by a re-press is on frame "
                << (t % duration) << ". If this fails the move is NOT restarting "
                   "immediately, and the strobe below is not the shape it says.";

            if (s.p[1].health < previousHealth)
                hitTicks.push_back(static_cast<std::uint32_t>(t));
            previousHealth = s.p[1].health;

            readings.push_back(twoTermAdvantage(bench.build.data, s, 0));
        }
    }

    // One hit per repetition, on the move's first active frame every time.
    ASSERT_EQ(hitTicks.size(), static_cast<std::size_t>(3))
        << "`stand_lp` connected " << hitTicks.size() << " time(s) in " << total
        << " ticks; a " << duration << "-tick move pressed once per repetition "
           "should land three.";
    for (std::size_t i = 0; i < hitTicks.size(); ++i)
        EXPECT_EQ(static_cast<std::int32_t>(hitTicks[i]),
                  startup + static_cast<std::int32_t>(i) * duration)
            << "repetition " << i << " connected off its own startup frame";

    // --- the readings, over the two settled periods -------------------------
    std::int32_t plusCount = 0, minusCount = 0, unknownCount = 0;
    std::int32_t plusValue = 0, minusValue = 0;
    std::ostringstream strip;

    for (std::int32_t t = duration; t < total; ++t) {
        const TwoTermAdvantage& a = readings[static_cast<std::size_t>(t)];
        if (t < duration * 2) {
            strip << (a.known ? (a.ticks > 0 ? "+" + std::to_string(a.ticks)
                                             : std::to_string(a.ticks))
                              : std::string("-"))
                  << " ";
        }
        if (!a.known) { ++unknownCount; continue; }

        // EVERY READING OF A KIND IS THE SAME READING, asserted rather than
        // assumed: if the settled value drifted from tick to tick the counts
        // below would still come out right and the strobe would be a different
        // and much stranger shape than this test describes.
        if (a.ticks >= 0) {
            if (plusCount == 0) plusValue = a.ticks;
            ASSERT_EQ(a.ticks, plusValue)
                << "tick " << t << ": the settled reading is not settled";
            ++plusCount;
        } else {
            if (minusCount == 0) minusValue = a.ticks;
            ASSERT_EQ(a.ticks, minusValue)
                << "tick " << t << ": two different negative readings in a "
                   "periodic drive";
            ++minusCount;
        }
    }

    // THE ADVANTAGE THE KERNEL ACTUALLY GIVES, derived from the move's own
    // numbers. The defender is freed `hitstun` ticks after contact; the attacker
    // is freed `active + recovery` ticks after it, because contact is on the
    // first active frame and StepAttack's scan runs in the call that ends the
    // move. FightHud.h predicts exactly this and predicts that it is ONE LOWER
    // than the +2 the character file's own engine.frame_advantage quotes -- "the
    // difference is the finding, not the bug" -- so this line is that claim
    // measured rather than asserted in a comment.
    EXPECT_EQ(plusValue, stun - (move->active + move->recovery));
    EXPECT_EQ(plusValue, 1)
        << "the settled reading for `stand_lp` is " << plusValue
        << ". The file's engine.frame_advantage says +2 and the kernel honours "
           "+1; if this is now 2 the kernel's stun timing changed.";

    // AND THE OTHER TWO READINGS OF THE SAME INTERACTION.
    //
    //   the settled +1     every tick from contact until the move runs out:
    //                      `duration - startup` of them.
    //   one deep negative  the restart tick. The attacker is back on frame 0 of a
    //                      fresh move (`duration` ticks to go) while the defender
    //                      still has the last tick of the previous stun, so the
    //                      row swings from +1 to 1 - duration in ONE TICK.
    //   not known          the ticks between the stun running out and the next
    //                      repetition connecting: `duration - hitstun` of them.
    EXPECT_EQ(plusCount, (duration - startup) * 2)
        << "the settled reading appeared " << plusCount << " times over two "
           "periods; it should hold from contact until the move ends.";
    EXPECT_EQ(minusCount, 2)
        << "the restart tick should produce exactly one reading per period";
    EXPECT_EQ(minusValue, 1 - duration)
        << "the restart tick read " << minusValue << "; the attacker is on frame "
           "0 of a " << duration << "-tick move and the defender has one tick of "
           "the previous hit's stun left.";
    EXPECT_EQ(unknownCount, (duration - stun) * 2)
        << "the readout should go blank for " << (duration - stun)
        << " tick(s) per period, between the stun expiring and the next hit";

    // THE CLAIM ITSELF, stated so it cannot be satisfied by the numbers merely
    // being different: three distinct readings of ONE interaction, cycling
    // forever, at a rate a playtester perceives as a flicker rather than as a
    // change. 60 / 14 is 4.3 Hz.
    EXPECT_GE(plusCount, 1);
    EXPECT_GE(minusCount, 1);
    EXPECT_GE(unknownCount, 1);
    EXPECT_EQ(plusCount + minusCount + unknownCount, duration * 2)
        << "the three counts do not cover the two periods, so one of them is "
           "measuring something else";

    RecordProperty("advantage_period_ticks", duration);
    RecordProperty("advantage_settled", plusValue);
    RecordProperty("advantage_on_restart", minusValue);
    RecordProperty("advantage_unknown_per_period", duration - stun);

    std::cout
        << "\n[ TRAINING MODE ] the same interaction, recomputed every frame, on `"
        << bench.character.id << "` with LP held\n"
        << "  the move            `stand_lp`, " << duration
        << " ticks, restarted by the held button the tick it ends\n"
        << "  one period reads    " << strip.str() << "\n"
        << "  so the row shows    " << (duration - startup) << " tick(s) of "
        << plusValue << ", one tick of " << minusValue << ", "
        << (duration - stun) << " tick(s) of nothing -- "
        << (cse::kernel::kTicksPerSecond) << "/" << duration
        << " times a second, forever\n"
        << "  WHICH IS WHY        a number that is meant to describe an "
           "INTERACTION has to be latched on the contact tick. ComboWatcher "
           "already identifies that tick and already keeps the one byte of "
           "history it takes (prevAtkHitBits_).\n\n";
}

// ============================================================================
// 6. HOW FAR APART THE BODIES ARE                             (CLAIM 5, cont.)
// ============================================================================
//
// The gap chip is the one number on the screen a playtester checks against their
// own eyes, and it is the number they will use to decide whether a move that
// missed was a spacing mistake or a bug. It is read off cse::kernel::Hurtbox, so
// the boxes are the ones ResolveHits tests with -- and then the two are subtracted
// by a rule that is only the separation of two intervals while one interval is
// entirely to the left of the other.
//
// The test walks a fighter through all four bands -- apart, touching, overlapping,
// coincident -- with the SAME input a playtester holds, and checks the arithmetic
// against two things neither formula can be tuned to agree with: the origins the
// kernel actually produced, and cse::kernel::BoxesOverlap, which is the
// simulation's own answer to "are these two bodies in the same place".
TEST(TrainingModeReadout, WalkingClosesTheGapAndOnlyTheIntervalRuleSurvivesContact) {
    // NOTHING IS BOUND. No button can start a move, so every number below is a
    // fact about two bodies and a walk speed and nothing else can perturb it.
    Bench bench{};
    bringUpBench({}, bench);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    // A WALK SPEED THIS TEST CHOOSES, and the choice is arithmetic rather than
    // taste. The subject here is the gap chip's FORMULA across four bands, and
    // to catch the one tick that separates the two candidate rules the walk has
    // to land EXACTLY on the touching tick and again on the coincident tick.
    // The bodies open 34 px apart and are 26 px wide together, so the step must
    // divide both 8 and 34. Two does. The character's authored three divides
    // neither -- 26 is not a multiple of 3 -- and the test would then measure
    // whichever side of the boundary the rounding happened to fall on.
    //
    // Before M1.1b this was true by accident: the kernel walked everything at 2
    // px/tick and this test inherited that without saying so. The premise is now
    // written down and owned, which is the only thing that changed about it.
    // ROADMAP M1.1b argues the alternative -- restating the test in terms of
    // CROSSING zero rather than landing on it -- and rejects it, because "the
    // one tick this test is really about" is exactly what that would lose.
    bench.build.data.p[0].walkSpeedSub = 2 * cse::kernel::kSubUnitsPerPixel;
    bench.build.data.p[1].walkSpeedSub = 2 * cse::kernel::kSubUnitsPerPixel;

    // AND NO PUSHBOX, which is not a dodge -- it is the difference between the
    // two boxes this file keeps confusing for each other.
    //
    // ROADMAP M1.2 stopped fighters standing in each other, so two PUSHBOXES
    // never overlap any more. The gap chip does not measure pushboxes: it
    // measures HURTBOXES, which still overlap freely, because a sweep's hurtbox
    // reaches far past the body it keeps. Two of the four bands this test walks
    // through -- overlapping and coincident -- are ordinary hurtbox states and
    // unreachable pushbox ones, so a bench that carried a pushbox would separate
    // the fighters before the walk ever got there and measure two bands instead
    // of four.
    //
    // Degenerate is how FighterData spells "no pushbox".
    bench.build.data.p[0].pushbox = cse::kernel::Box{};
    bench.build.data.p[1].pushbox = cse::kernel::Box{};

    // THE BRIDGE'S HALF OF THIS, ASSERTED BEFORE THE KERNEL'S. The kernel now
    // carries the authored walk speed whole, so this row reads `exact` -- and
    // that is what makes the override above a deliberate act by this test rather
    // than the engine's own behaviour. A reader who sees 2 px/tick below must be
    // able to find out in one hop that the file says three.
    const BuildLoss* const walkLoss =
        findLoss(bench.build.report[0], "character.walk_speed");
    ASSERT_NE(walkLoss, nullptr)
        << "the build no longer reports what happens to `character.walk_speed`";
    EXPECT_EQ(walkLoss->direction, BuildLossDirection::Exact)
        << "`character.walk_speed` is no longer carried exactly into the kernel. "
           "If the kernel has stopped reading the field, the override above is "
           "silently doing nothing and every tick number below is a coincidence.";
    EXPECT_GT(walkLoss->count, 0)
        << "the loss is reported with a count of zero, which would mean this "
           "character authors no walk speed and the override above is not an "
           "override at all";

    const cse::kernel::Box body0 =
        cse::kernel::Hurtbox(bench.build.data.p[0], walkingOpening().p[0]);
    const cse::kernel::Box body1 =
        cse::kernel::Hurtbox(bench.build.data.p[1], walkingOpening().p[1]);
    ASSERT_EQ(body0.x1 - body0.x0, kHalfWidth * 2);
    ASSERT_EQ(body1.x1 - body1.x0, kHalfWidth * 2)
        << "the two bodies are different widths, so |dx| - 2*halfWidth is no "
           "longer an independent derivation of the separation and the third "
           "opinion below has become the first one again";
    ASSERT_TRUE(body0.y0 < body1.y1 && body1.y0 < body0.y1)
        << "the two bodies do not overlap vertically, so BoxesOverlap can never "
           "be true and the sign check below proves nothing";

    // --- the playtester holds D ---------------------------------------------
    struct Row {
        std::int32_t tick    = 0;
        std::int32_t dx      = 0;
        std::int32_t truth   = 0;   // |dx| - 2*halfWidth, from the origins
        std::int32_t interval = 0;  // the rule this test says is right
        std::int32_t ternary  = 0;  // the rule the chip uses today
        bool         overlap  = false;
    };
    std::vector<Row> rows;

    std::int32_t walkStep    = 0;
    std::int32_t touchTick   = -1;
    std::int32_t coincidentTick = -1;
    std::int32_t apartTicks  = 0, overlapTicks = 0;

    {
        GameState s = walkingOpening();
        std::int32_t previousP0 = s.p[0].posX;
        for (std::int32_t t = 0; t <= 22; ++t) {
            if (t > 0) step(s, bench.build.data, cse::kernel::kInputRight);

            // THE RATE, MEASURED ONCE AND THEN HELD TO. The kernel's walk speed
            // is a file-local constant in Simulate.cpp and is not exported, so
            // the only honest way to state it is to watch a fighter walk.
            if (t == 1) {
                walkStep = s.p[0].posX - previousP0;
                EXPECT_EQ(walkStep, 2 * cse::kernel::kSubUnitsPerPixel)
                    << "one tick of `right` moved the fighter "
                    << subAndPx(walkStep)
                    << ", so the override at the top of this test did not take "
                       "and the tick numbers below no longer land on the band "
                       "boundaries this test exists to measure.";
                EXPECT_NE(walkStep, bench.character.walkSpeedSub)
                    << "the override now matches the character's authored speed "
                       "of " << subAndPx(bench.character.walkSpeedSub)
                    << ", so it is no longer isolating this test from the file. "
                       "That is not a failure of the engine -- it means the file "
                       "was re-authored to 2 px/tick and the override should be "
                       "deleted along with the paragraph explaining it.";
            } else if (t > 1) {
                ASSERT_EQ(s.p[0].posX - previousP0, walkStep)
                    << "tick " << t << ": the walk rate changed mid-walk";
            }
            previousP0 = s.p[0].posX;

            ASSERT_EQ(s.p[1].posX, kWalkP1X)
                << "tick " << t << ": the defender moved. It is given no input "
                   "and the kernel has no pushboxes, so nothing may push it.";
            ASSERT_EQ(s.p[0].moveId, 0u)
                << "tick " << t << ": a move started, although nothing is bound";

            const cse::kernel::Box b0 =
                cse::kernel::Hurtbox(bench.build.data.p[0], s.p[0]);
            const cse::kernel::Box b1 =
                cse::kernel::Hurtbox(bench.build.data.p[1], s.p[1]);

            Row r{};
            r.tick     = t;
            r.dx       = s.p[1].posX - s.p[0].posX;
            r.truth    = (r.dx < 0 ? -r.dx : r.dx) - kHalfWidth * 2;
            r.interval = separation(b0, b1);
            r.ternary  = ternaryGap(b0, b1);
            r.overlap  = cse::kernel::BoxesOverlap(b0, b1);

            // A THIRD OPINION, AND IT IS THE POINT OF DRIVING RATHER THAN
            // WRITING POSITIONS. The truth is computed from the origins the
            // kernel produced and the body width the bridge built, by an
            // expression that shares no term with either formula under test.
            ASSERT_EQ(r.interval, r.truth)
                << "tick " << t << ": the interval rule says "
                << subAndPx(r.interval) << " and the origins say "
                << subAndPx(r.truth);

            // AND THE SIMULATION'S OWN OPINION ON THE SIGN. Combat.h's boxes are
            // half-open precisely so that touching is not overlapping, so a
            // separation of exactly zero must be a MISS. This is the assertion
            // that makes "the chip is wrong" mean something a playtester cares
            // about rather than something an arithmetician does.
            ASSERT_EQ(r.interval < 0, r.overlap)
                << "tick " << t << ": the separation is " << subAndPx(r.interval)
                << " and cse::kernel::BoxesOverlap says the bodies "
                << (r.overlap ? "DO" : "do NOT") << " overlap";

            if (r.interval > 0) ++apartTicks;
            if (r.interval < 0) ++overlapTicks;
            if (r.interval == 0 && touchTick < 0) touchTick = t;
            if (r.dx == 0 && coincidentTick < 0)  coincidentTick = t;

            rows.push_back(r);
        }
    }

    // --- ALL FOUR BANDS WERE VISITED ----------------------------------------
    ASSERT_GT(apartTicks, 0)   << "the walk never had the bodies apart";
    ASSERT_GT(overlapTicks, 0) << "the walk never had the bodies overlapping; the "
                                  "kernel has no pushboxes, so nothing should "
                                  "have stopped them";
    ASSERT_GE(touchTick, 0)
        << "the bodies never touched exactly. At " << subAndPx(walkStep)
        << " a tick from an opening separation of " << subAndPx(rows[0].interval)
        << " the walk steps over zero rather than landing on it, and the one tick "
           "this test is really about does not exist.";
    ASSERT_GE(coincidentTick, 0) << "the origins never coincided";
    ASSERT_NE(walkStep, 0) << "the fighter never moved, so the two derivations "
                              "below would divide by zero";

    // Both derived from the opening rather than typed: the separation closes at
    // the walk rate, so touching is `separation / step` ticks away and coincident
    // is `dx / step`.
    EXPECT_EQ(touchTick, rows[0].interval / walkStep);
    EXPECT_EQ(coincidentTick, rows[0].dx / walkStep);
    EXPECT_EQ(rows[static_cast<std::size_t>(coincidentTick)].interval,
              -kHalfWidth * 2)
        << "with the origins in the same place the bodies overlap by their whole "
           "width, and the separation is minus that";

    // --- THE TWO FORMULAS -----------------------------------------------------
    std::ostringstream table;
    table << "\n  tick   dx      separation    the chip's ternary   overlap\n";
    std::int32_t wrongTicks = 0, firstWrong = -1;
    for (const Row& r : rows) {
        if (r.ternary != r.interval) {
            ++wrongTicks;
            if (firstWrong < 0) firstWrong = r.tick;
        }
        table << "  " << std::setw(4) << r.tick
              << "  " << std::setw(6) << r.dx
              << "  " << std::setw(12) << subAndPx(r.interval)
              << "  " << std::setw(18) << subAndPx(r.ternary)
              << "  " << std::setw(7) << (r.overlap ? "yes" : "no") << "\n";
    }

    // THEY AGREE WHILE THE BODIES ARE APART, which is exactly why this survived:
    // a training session that never closes the distance never sees it.
    for (const Row& r : rows) {
        if (r.interval <= 0) break;
        ASSERT_EQ(r.ternary, r.interval)
            << "tick " << r.tick << ": the two disagree while the bodies are "
               "still apart, which is not the shape of this finding" << table.str();
    }

    // AND THE FIRST TICK THEY DISAGREE ON IS THE TICK THE BODIES TOUCH.
    ASSERT_GT(wrongTicks, 0)
        << "the ternary form and the interval rule agreed on every tick of the "
           "walk, including the overlap band. That is a finding in the other "
           "direction: either the bodies never touched or the expression has "
           "already been corrected." << table.str();
    EXPECT_EQ(firstWrong, touchTick)
        << "the two forms first disagreed at tick " << firstWrong
        << " and the bodies touched at tick " << touchTick << table.str();

    const Row& touching = rows[static_cast<std::size_t>(touchTick)];
    EXPECT_EQ(touching.interval, 0);
    EXPECT_EQ(touching.ternary, -kHalfWidth * 4)
        << "on the tick the bodies touch the chip prints "
        << subAndPx(touching.ternary)
        << ". The `else` branch computes b0.x0 - b1.x1, which for two equal "
           "bodies exactly touching is minus TWO body widths." << table.str();
    EXPECT_EQ(touching.ternary - touching.interval, -kHalfWidth * 4);
    EXPECT_EQ(touching.ternary / cse::kernel::kSubUnitsPerPixel, -52);

    // THE SENTENCE THAT MATTERS TO A PLAYTESTER. On this tick the chip says the
    // two fighters are 52 pixels INSIDE each other and the kernel says they are
    // not touching at all -- and the very next thing that playtester does is
    // report a bug about a move that should have connected.
    EXPECT_FALSE(touching.overlap)
        << "the bodies overlap on the tick the separation is zero, so Combat.h's "
           "half-open convention has changed";
    EXPECT_LT(touching.ternary, 0)
        << "the chip reads " << subAndPx(touching.ternary)
        << " -- a claim that the bodies are interpenetrating -- on a tick "
           "cse::kernel::BoxesOverlap calls a clean miss";

    // AND IT STAYS WRONG. Not one tick: every tick from contact until the
    // attacker's ORIGIN passes the defender's, which is when the branch it took
    // becomes the right one again by accident.
    EXPECT_EQ(wrongTicks, coincidentTick - touchTick)
        << "the ternary form was wrong on " << wrongTicks << " tick(s)"
        << table.str();
    EXPECT_EQ(wrongTicks, 13)
        << "at 2 px/tick from a body-to-body gap of "
        << subAndPx(rows[0].interval) << " the wrong branch is taken for 13 "
           "consecutive ticks. If this moved, the body width or the opening "
           "positions did." << table.str();

    RecordProperty("gap_touch_tick", touchTick);
    RecordProperty("gap_wrong_ticks", wrongTicks);
    RecordProperty("gap_worst_error_sub", touching.ternary - touching.interval);

    std::cout
        << "\n[ TRAINING MODE ] the gap chip, walked through all four bands\n"
        << "  walk rate           " << subAndPx(walkStep)
        << " per tick, hardcoded in Simulate.cpp; the file authors "
        << subAndPx(bench.character.walkSpeedSub) << "\n"
        << "  bands visited       apart " << apartTicks << " tick(s), touching at "
        << touchTick << ", overlapping " << overlapTicks
        << " tick(s), coincident at " << coincidentTick << "\n"
        << "  the ternary form    correct while apart, then wrong for "
        << wrongTicks << " consecutive ticks, worst at the moment of contact: "
        << subAndPx(touching.ternary) << " where the truth is "
        << subAndPx(touching.interval) << "\n"
        << "  AND THE SIGN LIES   it claims interpenetration on a tick "
           "cse::kernel::BoxesOverlap calls a clean miss\n"
        << table.str() << "\n";
}

// ============================================================================
// 7. THE RED BOX                                              (CLAIM 5, cont.)
// ============================================================================
//
// FightView.h makes the strongest claim on the screen: "a box drawn on this
// screen is the box that hit, or the box that did not, and `it looked like it
// should have connected` stops being a thing a playtester can be right about for
// the wrong reason."
//
// cse::kernel::ActiveHitbox is the wrong oracle for that sentence, and the reason
// is one line above it in ResolveHits: THE MULTI-HIT GUARD IS TESTED FIRST. An
// active window that has already connected on this defender is skipped before its
// box is ever built, so on the remaining active frames the kernel will hand out a
// rectangle that cannot produce a hit -- and at these distances it is still lying
// across the dummy's body while it does.
//
// The test measures the split for two moves of different active lengths. It then
// pins the thing that decides HOW the picture can be fixed, which is not obvious
// and is easy to get backwards.
TEST(TrainingModeReadout, ASpentActiveWindowStillOffersABoxAndTheStateCannotSayWhich) {
    Bench bench{};
    bringUpBench({ bind("stand_lp", cse::kernel::kInputLP),
                   bind("air_mp",   cse::kernel::kInputHP) }, bench);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    const MoveIndexMap& map = bench.build.moves[0];

    struct Subject { const char* id; std::uint16_t button; };
    const Subject subjects[2] = {
        { "stand_lp", cse::kernel::kInputLP },   // active 2
        { "air_mp",   cse::kernel::kInputHP },   // active 4 -- the reviewer's case
    };

    // The defender's bit in the attacker's alreadyHitBits, spelled the way
    // Combat.cpp's bitForSlot spells it.
    const std::uint8_t defenderBit = static_cast<std::uint8_t>(1u << 1);

    for (const Subject& subject : subjects) {
        SCOPED_TRACE(subject.id);

        const std::uint16_t slot = map.Find(subject.id);
        ASSERT_NE(slot, 0u) << kSafe << " no longer has `" << subject.id << "`";
        const cse::kernel::MoveDef* const move =
            cse::kernel::MoveAt(bench.build.data.p[0], slot);
        ASSERT_NE(move, nullptr);
        ASSERT_GT(move->active, 1)
            << "`" << subject.id << "` is active for one frame, so there is no "
               "second frame for the guard to have spent and this subject cannot "
               "show the split";

        // The press ESTABLISHES the move's stance (ROADMAP M1.3e): an aerial is
        // asked for with the takeoff Up provides, on the same tick, and rides
        // the arc through its active window -- which is exactly how the
        // reviewer's case is performed in play.
        const std::uint16_t hold =
            cse::game::WitnessCursor::StanceHold(bench.build.data.p[0], slot);

        std::int32_t boxFrames = 0, hitFrames = 0, deadFrames = 0;
        std::int32_t deadOverlapping = 0;
        std::int32_t bitSetOnHitFrame = -1;
        std::ostringstream table;
        table << "\n  tick  frame  box  overlaps body  hit  alreadyHitBits\n";

        GameState s = opening();
        std::int32_t previousHealth = kStartingHealth;
        for (std::int32_t t = 0; t < cse::kernel::MoveDuration(*move) + 2; ++t) {
            step(s, bench.build.data,
                 static_cast<std::uint16_t>(
                     t == 0 ? (subject.button | hold) : 0));

            cse::kernel::Box hitbox{};
            const bool hasBox =
                cse::kernel::ActiveHitbox(bench.build.data.p[0], s.p[0], hitbox);
            if (!hasBox) { previousHealth = s.p[1].health; continue; }

            const cse::kernel::Box defenderBody =
                cse::kernel::Hurtbox(bench.build.data.p[1], s.p[1]);
            const bool overlapping =
                cse::kernel::BoxesOverlap(hitbox, defenderBody);
            const bool landed = s.p[1].health < previousHealth;
            previousHealth = s.p[1].health;

            ++boxFrames;
            if (landed) {
                ++hitFrames;
                bitSetOnHitFrame =
                    (s.p[0].alreadyHitBits & defenderBit) != 0 ? 1 : 0;
            } else {
                ++deadFrames;
                if (overlapping) ++deadOverlapping;

                // The guard, and it is the WHOLE reason this frame is dead: the
                // box is live, it is across the defender's body, and ResolveHits
                // never looked at it.
                EXPECT_TRUE(overlapping)
                    << "a frame that could not hit also could not have reached, "
                       "so it is an ordinary miss rather than the finding";
                EXPECT_NE(s.p[0].alreadyHitBits & defenderBit, 0)
                    << "tick " << t << ": the active window did not hit and has "
                       "NOT already connected, so something other than the "
                       "multi-hit guard stopped it";
            }

            table << "  " << std::setw(4) << t
                  << "  " << std::setw(5) << s.p[0].moveFrame
                  << "  " << std::setw(3) << "yes"
                  << "  " << std::setw(13) << (overlapping ? "yes" : "no")
                  << "  " << std::setw(3) << (landed ? "YES" : ".")
                  << "  " << std::setw(14)
                  << static_cast<int>(s.p[0].alreadyHitBits) << "\n";
        }

        // The kernel offered a box on every active frame -- which is right, and
        // is what makes the picture wrong.
        EXPECT_EQ(boxFrames, move->active)
            << "`" << subject.id << "` is authored active " << move->active
            << " and ActiveHitbox answered on " << boxFrames << " frame(s)"
            << table.str();
        EXPECT_EQ(hitFrames, 1)
            << "the multi-hit guard allows exactly one hit per active window"
            << table.str();
        EXPECT_EQ(deadFrames, move->active - 1)
            << "`" << subject.id << "` drew " << deadFrames
            << " red frame(s) that could not hit" << table.str();
        EXPECT_EQ(deadOverlapping, deadFrames)
            << "every dead frame here is drawn LYING ACROSS THE DUMMY'S BODY, "
               "which is the exact picture FightView.h promises a playtester will "
               "never be shown" << table.str();

        // --- AND NOW THE PART THAT DECIDES THE FIX ---------------------------
        //
        // The obvious repair is to draw the box only while
        // `(alreadyHitBits & defenderBit) == 0`. IT ERASES THE BOX THAT HIT.
        //
        // ResolveHits sets that bit at the BOTTOM of the tick the hit landed, and
        // a view draws the state AFTER the tick -- so on the connecting frame the
        // bit is already set, and the condition cannot tell "this window connected
        // just now" from "this window connected three frames ago". The state at
        // draw time does not carry the distinction at all; it takes one byte of
        // HISTORY, which is exactly the rule ComboWatcher.h states as signal 3 and
        // keeps `prevAtkHitBits_` for, and which TickTrace re-implements at the
        // top of this file.
        EXPECT_EQ(bitSetOnHitFrame, 1)
            << "on the frame `" << subject.id << "` connected, the attacker's "
               "alreadyHitBits did NOT yet carry the defender's bit as observed "
               "after the tick. If that is now true, the one-condition fix works "
               "and this warning can go -- but check WHEN the bit is written "
               "before believing it." << table.str();
    }
}
