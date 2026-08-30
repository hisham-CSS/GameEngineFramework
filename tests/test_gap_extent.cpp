// HOW BIG IS THE GAP? Every cycle in the character, not one of them.
//
// tests/test_ground_truth.cpp section 5 ends on a finding and a single
// measurement. The finding: `fighter_a` is TERMINATING, its ranking certificate
// says so because JUGGLE RUNS DOWN, and the kernel never spends it. Since
// ROADMAP M1.1b the kernel DOES simulate resources -- it primes them, applies
// each move's effect on contact and refuses a move whose guard is unmet -- so
// the sentence that used to sit here, "it has no resources at all", is no longer
// the reason. Two narrower things are: `ApplyEffects` CLAMPS at the authored
// floor instead of refusing, and `MatchBuilder` sets neither `juggleMax` nor
// `juggleCost`, so the budget gate in Combat.cpp has never fired for a built
// character. The measurement: ONE cycle, `air_mp` into itself, which the
// model permits four repetitions of and the kernel performed eighteen.
//
// One cycle is an existence proof. It settles that the gap is real and settles
// nothing about its size, and the size is the number a research contribution
// actually quotes. So this file measures the whole character.
//
// ---------------------------------------------------------------------------
// THE QUESTION, AND THE FOUR THINGS IT DECOMPOSES INTO
// ---------------------------------------------------------------------------
// fighter_a's usable cancel graph holds EXACTLY 121 SIMPLE CYCLES -- one of
// length 1 (air_mp into itself), eight of length 3, forty-eight of length 4 and
// sixty-four of length 5 -- and all 121 terminate for one
// reason: every one contains an edge into an air move, air moves spend juggle,
// so every cycle strictly decreases it. If that is true, then in a kernel with no
// juggle every one of those 41 is a candidate infinite.
//
//   1. HOW MANY CYCLES ARE THERE? Derived here, by enumerating them from the
//      loaded CharacterData, because a number a file states about itself is a
//      claim and not a measurement. Section 1.
//   2. HOW MANY DOES THE MODEL TERMINATE BY A RESOURCE? Read off the loaded
//      effects, per cycle, rather than off the certificate -- `hasRanking` is
//      true as soon as ONE resource is extracted and says nothing about which
//      cycles the order covers (the file's own `what_the_certificate_does_and_
//      does_not_say` is emphatic about this). Section 2.
//   3. HOW MANY CAN THE KERNEL PERFORM? Decided per EDGE against the rules in
//      cse/kernel/Combat.h, from the window MatchBuilder actually resolved --
//      not from the authored delay. Section 3.
//   4. HOW MANY DOES IT ACTUALLY EXECUTE? Driven, tick by tick, one scripted
//      trace per cycle, with the defender mashing. Section 4.
//
// ---------------------------------------------------------------------------
// WHAT IT MEASURED. THE HEADLINE IS THE LAST LINE
// ---------------------------------------------------------------------------
//   the graph            73 authored cancels, 5 dead in the model, 68 usable
//   simple cycles        143 over the AUTHORED graph, 41 over the USABLE one
//   the report's 41      CONFIRMED, including the 1 / 8 / 32 split by length
//   ended by juggle      41 of 41. Every cycle's total juggle effect is strictly
//                        negative and NO cycle touches meter, so juggle alone
//                        ends all of them -- and the kernel never spends juggle
//                        (M1.1b: tracked, clamped at the floor, gate unwired).
//   performable as the   1 of 41. The other 40 each contain exactly one edge the
//   model describes it   kernel cannot take.
//   EXECUTED ANYWAY      33 of 41, as loops the defender never gets one tick out
//                        of, returning to their own opening state every turn.
//
// So the D8 gap on this character is 33 cycles wide, not 1 and not 41.
//
// ---------------------------------------------------------------------------
// THE TWO SURPRISES, BECAUSE NEITHER WAS THE EXPECTED ANSWER
// ---------------------------------------------------------------------------
//
// FIRST: 40 OF THE 41 CYCLES CONTAIN AN EDGE THE KERNEL CANNOT TAKE, AND IT IS
// ALWAYS THE SAME KIND OF EDGE. Every cycle longer than the self-loop leaves
// `air_mp` by one of its nine landing links, and every one of those resolves to
// an EMPTY kernel window of [21, 14]. `air_mp` starts on frame 6 and the link's
// authored delay is 15, so MatchBuilder opens the window at 6 + 15 = 21; but the
// move authors `cancel_window_ticks` [8, 14], and MatchBuilder intersects, so the
// window closes seven ticks before it opens. This is not a bridge bug. The file
// means these edges as LINKS -- `air_mp`'s own derivation says "hitstun 22
// against L 15 is on-hit +7, so this move DOES link" -- and Combat.h names the
// case exactly: "an edge whose earliest is past its latest matches nothing for
// any frame, which is how an authored delay longer than the source move ends up
// inert instead of ending up wrong."
//
// SECOND, AND IT IS WHY THE ANSWER IS 33 RATHER THAN 1: THE LOOPS RUN ANYWAY.
// The kernel takes buttons HELD rather than PRESSED (Combat.cpp says so, and
// calls it "a real gap, named rather than papered over"), and StepAttack ends a
// move and runs the button scan in the SAME call -- so a trace holding the
// follow-up's button starts it on the very tick `air_mp` recovers. The cancel
// system loses the edge and the input model hands it straight back. What the
// kernel performs is the LINK the file authored, by a route the file never
// mentions, one tick later than the file's own arithmetic puts it: frame 22, the
// end of the move, against the delay's frame 21.
//
// That one lost tick is the entire difference between the 33 and the 8. `air_mp`
// is +7 on hit; a light (3f, 4f startup) still connects with ticks to spare and a
// medium (6f, 7f) does not. So the eight cycles that leak a defender turn are
// EXACTLY the eight of length 3 -- the ones that land from `air_mp` straight into
// a medium -- and all 32 of length 4, which land into a light first, do not. The
// kernel is CONSERVATIVE there, in ProverAdapter.h's vocabulary: stricter than
// the file, on eight cycles, by one tick.
//
// Both halves of that are asserted rather than told. Section 3 counts the blocked
// edges and names the arithmetic; section 4 requires the static two-route account
// to predict the executed outcome for all 41 AND the frame each of the 500-odd
// observed transitions happened on, because an explanation that cannot predict the
// run is a story.
//
// THE BUTTON-START ROUTE IS ALREADY IN AN EXISTING GREEN MEASUREMENT, unremarked.
// test_ground_truth.cpp's control replays the infinite character's `stand_lp`
// witness against fighter_a and records "12 hits, one every 14". fighter_a's
// `stand_lp` is 3 + 2 + 9 = 14 ticks long, and it has no edge back into itself:
// that period of 14 IS the move ending and the held button starting it again, at
// exactly MoveDuration. The mechanism 32 of the cycles below depend on has been
// sitting in a passing test the whole time, as a number nobody had a reason to
// read that way.
//
// ---------------------------------------------------------------------------
// A NOTE ON MEASUREMENT, BECAUSE IT CHANGED THE ANSWER BY FOUR
// ---------------------------------------------------------------------------
// test_ground_truth.cpp asks whether the defender escaped in two ways, and this
// file needs a third because both of those miss the same case.
//
//   TickLog::FreeTicks()   `defStun == 0 && !hit`, and it EXCLUDES the tick a hit
//                          lands on -- correctly, for its own question.
//   defenderActedTicks     `defMove != 0 && defFrame == 0`, read after Simulate.
//
// A defender whose stun expires on exactly the tick the next hit arrives is
// invisible to both: they are actionable at the top of the tick, StepAttack
// really does start their move, and then ResolveHits at the bottom of the same
// tick sets `moveId = 0` on them again. The sample taken afterwards shows a
// defender who never moved, in hitstun, being hit.
//
// So the criterion here is the direct one: the defender was ACTIONABLE at tick t
// if their hitstun at the end of tick t-1 was at most 1, because StepPhysics
// decrements it before anything else and `actionable()` is `hitstun == 0`. It
// needs nothing the harness does not already record and it cannot be erased by a
// later step of the same tick. On this character it finds four escapes the other
// two detectors do not, and 37 becomes 33. Section 6 measures that difference
// rather than asserting it in a comment.
//
// ---------------------------------------------------------------------------
// WHAT WOULD FALSIFY WHAT, AND WHAT IS COPIED
// ---------------------------------------------------------------------------
// The numbers below are asserted rather than recorded, so none of them can rot:
// a character edit that changes the shape of this gap fails a test that names the
// old number and the new one. What must NOT happen is an expectation being moved
// to keep the file green -- section 3's `1 of 41` in particular is a fact about a
// projection, and if it grows, the bridge changed and the ground-truth file's
// finding is about a different engine.
//
// The harness -- the units, the two positions, the button pool, the Driver, the
// per-tick sampling and the table printer -- is COPIED from test_ground_truth.cpp
// rather than shared with it. That file is a test, not a library, and a header
// extracted from it so two tests could agree would be a third thing to keep true.
// The copies are marked, and where this file needed something different (the
// actionable detector above, the state-repetition check in section 5) the
// difference is stated where it appears.
#include <gtest/gtest.h>

#include "cse/data/CharacterData.h"
#include "cse/data/MatchBuilder.h"
#include "cse/data/ProverAdapter.h"
#include "cse/kernel/Simulate.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace cse::data;
using cse::kernel::GameState;
using cse::kernel::InputPair;
using cse::kernel::MatchData;

namespace {

// ============================================================================
// 0. The harness. COPIED from test_ground_truth.cpp -- see the header
// ============================================================================

constexpr std::int32_t px(std::int32_t pixels) {
    return pixels * cse::kernel::kSubUnitsPerPixel;
}

// The body is the caller's number, supplied deliberately: CharacterData carries
// none, so leaving it out would make MatchBuilder's documented default look like
// the character's own measurement. 13 px and 60 px are fighter_a's own
// `engine.constants.default_pushbox_sub`, restated in the unit this file reads in.
constexpr std::int32_t kHalfWidth = px(13);
constexpr std::int32_t kHeight    = px(60);

// Origins 34 px apart, so the BODIES are 8 px apart. Every move any cycle below
// uses reaches at least 40 px, so no verdict in this file can turn on distance --
// which matters more here than in the file this is copied from, because 41 cycles
// use fourteen different moves and one marginal gap would silently reclassify a
// cycle as unperformable for a reason that is really about the stage.
//
// THE DEFENDER IS PUSHED, AND THE CORNER IS WHY THAT DOES NOT MOVE THE GAP.
// The attacker holds attack buttons and no direction, so StepPhysics zeroes its
// velX; the defender is in hitstun, which zeroes velX anyway. Pushback is a
// different mechanism -- it rides Fighter::pushX -- and since the bridge began
// carrying `move.pushback` it is REAL: every hit queues a displacement.
//
// What holds the gap at 8 px is the STAGE, not the absence of a mechanic. The
// defender opens with its back to the wall, so the clamp absorbs every push and
// the separation the run measures is the one the corner verdict was computed
// for. Move this sweep midscreen and the gap opens on the first hit -- which is
// exactly what happened when pushback was first wired, and why these openings
// are cornered above rather than centred.
// IN THE CORNER, because that is the stage the verdict was computed for.
// `fighter_a.json` declares `stage: corner` and its own header says the corner
// verdict is the one the ranking certificate belongs to -- at stage corner
// model.py drops horizontal position entirely. A sweep opening midscreen was
// comparing a midscreen game against a corner model, which was invisible while
// nothing moved the defender and stops being invisible the moment pushback is
// wired (ROADMAP M1.3d). The defender's back is to the wall Simulate clamps at,
// so pushback has nowhere to put them and the comparison is the one the model
// licenses.
constexpr std::int32_t kStageEdge = 480 * cse::kernel::kSubUnitsPerPixel;
//
// THE DEFENDER'S BODY IS AGAINST THE WALL, not its origin. Since ROADMAP M1.2
// the stage clamps the BODY -- a fighter may not disappear half into the corner
// -- so an origin placed exactly on the edge is outside its own limit and the
// first tick shoves it inward. That is not a cornered opening, it is a fighter
// falling into position while the test believes nothing has happened.
constexpr std::int32_t kP1X =  kStageEdge - kHalfWidth;
constexpr std::int32_t kP0X =  kP1X - px(34);

// The build-wide positional resource order (ADR-001 section 8 item 7).
const std::vector<std::string> kBuildResources = { "meter", "juggle" };

// A03's positional contract, named rather than written as 0 and 1 at each use --
// "resource 1" is meaningless and "juggle" is the claim.
constexpr ResourceIndex kMeter  = 0;
constexpr ResourceIndex kJuggle = 1;

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

// The staged shipping tree, with a source-tree fallback so the test still runs
// from a shell anywhere in the repository. Neither branch can make anything pass
// vacuously, because the load below ASSERTs.
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

const char* kSafe = "fighter_a.json";

// Six single bits, and single on purpose: StepAttack takes the first move in slot
// order all of whose bits are held, so a mask that is a superset of an earlier
// one's can never start. No mask below is a subset of any other, so nothing in
// this file can stall for that reason -- which matters here because every cycle
// is handed a fresh binding table, and a shadowed mask would look like an
// unperformable cycle rather than like a harness bug.
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

// A mirror match: both sides get the same character and the same bindings, so
// which of the two fighters acts is decided entirely by the input bits. A
// defender handed no bindings could not act even if it were actionable, and that
// would make "the defender never acted" a fact about the harness.
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

void loadShipped(const char* file, CharacterData& out) {
    LoadReport report{};
    ASSERT_TRUE(LoadCharacterFile(charactersDir(), file, loadOptions(), out, report))
        << file << " did not load from " << charactersDir() << ".\n"
        << "  rule : " << (report.rule.empty() ? "(no load assertion named)" : report.rule)
        << "\n  error: " << report.error;
    ASSERT_TRUE(report.rule.empty())
        << file << " loaded but named load assertion " << report.rule;
    ASSERT_FALSE(out.moves.empty()) << file << " loaded with no moves";
}

struct Subject {
    CharacterData character;
    ProverResult  verdict;
};

void bringUp(const char* file, Subject& out) {
    loadShipped(file, out.character);
    if (::testing::Test::HasFatalFailure()) return;
    ProverReport report{};
    ASSERT_TRUE(AnalyseCharacter(out.character, proverOptions(), out.verdict, report))
        << out.character.id << " could not be projected into the decision "
           "procedure.\n  rule : " << report.rule << "\n  error: " << report.error;
}

// ============================================================================
// 1. The cancel graph, and its simple cycles
// ============================================================================

// One simple cycle: a closed walk that repeats no move. Carried as MOVE INDICES
// plus the CANCEL INDICES that join them, because both halves are needed later --
// section 2 reads the moves' resource effects and section 3 reads the edges'
// resolved windows, and a cycle carrying only one of them would have to look the
// other up by a rule this file would then have to keep true.
//
// `moves[0]` is the lowest move index in the cycle, which is the rotation the
// enumerator produces. A cycle has as many rotations as it has moves and they are
// all the same cycle; fixing the rotation is what makes the count a count.
struct Cycle {
    std::vector<MoveIndex>   moves;
    std::vector<CancelIndex> edges;   // edges[i] joins moves[i] -> moves[(i+1) % n]

    std::string ToString(const CharacterData& c) const {
        std::ostringstream s;
        for (std::size_t i = 0; i < moves.size(); ++i) {
            if (i) s << " > ";
            s << c.moves[moves[i]].id;
        }
        s << " > " << c.moves[moves[0]].id;   // closing the loop, spelled out
        return s.str();
    }
};

// A HARD CAP ON THE SEARCH, and it is not decoration. The number of simple cycles
// in a directed graph is exponential in the worst case, and this enumerator runs
// inside a unit test over authored content. A cap that is checked turns a
// pathological character into a named failure; a cap that is not turns it into a
// test run that never ends. Combat.h makes the same argument about move tables.
// 4096 is over an order of magnitude past what this character produces.
constexpr std::size_t kMaxCycles = 4096;

// Simple-cycle enumeration, the textbook DFS: a cycle is reported exactly once,
// from its LOWEST move index, by never walking to a move below the start. That
// rule is what makes the count exact rather than a count of rotations -- without
// it a 4-cycle is found four times.
//
// The edge set is a parameter rather than "all of them", because the whole point
// of section 1 is that the answer differs between the authored graph and the
// graph the prover reasons about, and those two differ only in which edges exist.
class CycleFinder {
public:
    CycleFinder(const CharacterData& character, std::vector<bool> allowedEdges)
        : c_(character), allowed_(std::move(allowedEdges)),
          onPath_(character.moves.size(), false) {}

    // False if the cap fired, in which case `out` holds the cycles found so far.
    bool Enumerate(std::vector<Cycle>& out) {
        for (std::size_t s = 0; s < c_.moves.size(); ++s) {
            start_ = static_cast<MoveIndex>(s);
            path_.clear();
            path_.push_back(start_);
            pathEdges_.clear();
            onPath_[s] = true;
            const bool ok = Walk(start_, out);
            onPath_[s] = false;
            if (!ok) return false;
        }
        return true;
    }

private:
    bool Walk(MoveIndex u, std::vector<Cycle>& out) {
        for (const CancelIndex e : c_.cancelsFrom[u]) {
            if (!allowed_[e]) continue;
            const MoveIndex v = c_.cancels[e].to;
            if (v == start_) {
                if (out.size() >= kMaxCycles) return false;
                Cycle cycle{};
                cycle.moves = path_;
                cycle.edges = pathEdges_;
                cycle.edges.push_back(e);   // the hop that closes the loop
                out.push_back(std::move(cycle));
                continue;
            }
            // Strictly greater, so every cycle is found from its lowest member and
            // from nowhere else.
            if (v <= start_ || onPath_[v]) continue;
            path_.push_back(v);
            pathEdges_.push_back(e);
            onPath_[v] = true;
            const bool ok = Walk(v, out);
            onPath_[v] = false;
            pathEdges_.pop_back();
            path_.pop_back();
            if (!ok) return false;
        }
        return true;
    }

    const CharacterData&     c_;
    std::vector<bool>        allowed_;
    std::vector<bool>        onPath_;
    std::vector<MoveIndex>   path_;
    std::vector<CancelIndex> pathEdges_;
    MoveIndex                start_ = 0;
};

// Which edges the DECISION PROCEDURE kept. Taken from the verdict's own dead list
// rather than re-derived from the link condition: the prover's opinion is the
// thing section 1 enumerates over, and a second implementation of
// `startup <= hitstun - delay` in this file could drift from the one that
// produced the verdict. test_prover_adapter.cpp is where that condition is
// tested; here it is consumed.
//
// TWO WAYS AN EDGE LEAVES THE GRAPH, and only one of them produces a dead-cancel
// entry. comboprover.hpp:337 SKIPS an edge that is not contact-gated before it
// ever asks the link condition -- `if (!edge.onHit) continue;` -- so such an edge
// is neither usable nor dead, it is absent, and reading only `deadCancels` would
// silently put it back. The mapping is CharacterData.h's: {Hit, Always} -> true,
// {Block, Whiff} -> false. fighter_a authors no block or whiff edge, and section
// 1's `dead + usable == authored` is what says so, but reproducing the skip is
// what keeps this an enumeration over the prover's graph rather than over one
// that happens to coincide with it.
std::vector<bool> usableEdges(const Subject& s) {
    std::vector<bool> out(s.character.cancels.size(), true);
    for (std::size_t i = 0; i < s.character.cancels.size(); ++i) {
        const Contact on = s.character.cancels[i].on;
        if (on == Contact::Block || on == Contact::Whiff) out[i] = false;
    }
    for (const ProverDeadCancel& dead : s.verdict.deadCancels)
        if (static_cast<std::size_t>(dead.cancel) < out.size())
            out[dead.cancel] = false;
    return out;
}

std::vector<bool> allEdges(const Subject& s) {
    return std::vector<bool>(s.character.cancels.size(), true);
}

// Cycles by length, so a change of shape is distinguishable from a change of
// count. 41 cycles that were all of length 4 would be a different character.
std::vector<std::size_t> lengthHistogram(const std::vector<Cycle>& cycles) {
    std::vector<std::size_t> out;
    for (const Cycle& c : cycles) {
        if (out.size() <= c.moves.size()) out.resize(c.moves.size() + 1, 0);
        ++out[c.moves.size()];
    }
    return out;
}

// ============================================================================
// 2. What the MODEL says ends each cycle
// ============================================================================

// The net change in one resource over one full turn of a cycle.
//
// SUMMED OVER THE MOVES AND THE EDGES, which is comboprover's own `totalEffect`
// (:282-290), and it is the reason no edge in this file needs to carry a juggle
// cost: entering `air_mp` spends the point, so every edge whose TARGET is
// `air_mp` pays it. Summing only the edges would find zero and conclude that
// nothing stops any cycle at all -- which is the mistake this function exists to
// not make.
std::int32_t cycleResource(const CharacterData& c, const Cycle& cycle,
                           ResourceIndex resource) {
    std::int32_t total = 0;
    for (std::size_t i = 0; i < cycle.moves.size(); ++i) {
        // The TARGET of hop i, i.e. the move the cycle enters on that hop. Over a
        // full turn this is the same set as `cycle.moves` -- a cycle enters each
        // of its moves exactly once -- but it is the sentence the edge semantics
        // justify, and it stays correct if a cycle ever repeats a move.
        const Move& target = c.moves[cycle.moves[(i + 1) % cycle.moves.size()]];
        for (const ResourceAmount& a : target.effect)
            if (a.resource == resource) total += a.value;
        for (const ResourceAmount& a : c.cancels[cycle.edges[i]].effect)
            if (a.resource == resource) total += a.value;
    }
    return total;
}

// ============================================================================
// 3. What the KERNEL can do with each edge
// ============================================================================

// Why the kernel cannot take a cancel edge. One value per rule in Combat.h, so a
// failure can be reported as a REASON rather than as a count -- the difference
// between "40 cycles do not work" and "40 cycles do not work because of one
// window", which is the whole of this file's honesty requirement.
enum class Block : std::uint8_t {
    None,          // the kernel takes it
    NoButton,      // nothing can ask for the target: more distinct moves in the
                   // cycle than there are single-bit buttons to hand out. A limit
                   // of the HARNESS, so it is checked first -- a cycle nothing can
                   // ask for was not measured, whatever else is true of it
    EmptyWindow,   // MatchBuilder resolved earliestFrame > latestFrame: inert
    ContactGate,   // window non-empty, but it has closed by the time the source's
                   // contact is visible. StepAttack runs BEFORE ResolveHits, so a
                   // hit on tick N is first readable on N+1 and the fastest cancel
                   // is one tick after contact, never zero (Combat.h)
    TooSlow        // takeable, but the follow-up's first active frame lands at or
                   // after the tick the defender's hitstun runs out
};

const char* blockName(Block b) {
    switch (b) {
        case Block::None:        return "takeable";
        case Block::NoButton:    return "no button";
        case Block::EmptyWindow: return "empty window";
        case Block::ContactGate: return "contact gate";
        case Block::TooSlow:     return "too slow";
    }
    return "?";
}

// One hop of a cycle, decided against the BUILT data rather than against the
// authored delay. `earliest`/`latest` are read out of cse::kernel::CancelEdge, so
// this is the window the running game compares moveFrame against and not this
// file's opinion of what MatchBuilder should have produced.
struct Hop {
    CancelIndex  edge = 0;
    MoveIndex    from = 0;
    MoveIndex    to   = 0;
    // The same two moves as kernel slots, so the trace can be read back against
    // this hop without a lookup per tick. MoveIndexMap::KernelMoveIdOf is the
    // mapping as a function rather than as the "+1" everybody remembers.
    std::uint16_t fromSlot = 0;
    std::uint16_t toSlot   = 0;
    std::int32_t earliest = 0;
    std::int32_t latest   = 0;

    Block        block = Block::None;   // why the CANCEL cannot be taken, if it cannot

    // THE SECOND ROUTE, and section 4 is why it is modelled at all. When the
    // cancel is inert the fighter can still reach the follow-up by letting the
    // source run out: StepAttack ends the move and runs the button scan in the
    // same call, so a held button starts the target on frame `MoveDuration`. That
    // is one tick later than a delay resolving to the source's last frame, and on
    // this character that tick decides eight cycles.
    std::int32_t startFrame = 0;        // the frame the follow-up actually begins
    bool         viaCancel  = false;    // ... by cancel (true) or by button (false)
    bool         connects   = false;    // ... and it lands before the defender is free
};

// The kernel's resolved window for a FILE cancel index, or null when the edge did
// not cross into the kernel at all. MoveIndexMap::fileCancelByEdge is the inverse
// of MatchBuilder's drop, which is why this needs no arithmetic.
const cse::kernel::CancelEdge* kernelEdgeFor(const MatchBuild& build, CancelIndex file) {
    for (std::size_t i = 0; i < build.moves[0].fileCancelByEdge.size(); ++i)
        if (build.moves[0].fileCancelByEdge[i] == file)
            return &build.data.p[0].cancels[i];
    return nullptr;
}

// Decide one hop. `bound` is whether the target got a button in this cycle's
// binding table.
Hop classify(const CharacterData& c, const cse::kernel::CancelEdge& ke,
             CancelIndex file, bool bound) {
    const Cancel& e   = c.cancels[file];
    const Move&   src = c.moves[e.from];
    const Move&   tgt = c.moves[e.to];

    Hop hop{};
    hop.edge     = file;
    hop.from     = e.from;
    hop.to       = e.to;
    hop.fromSlot = MoveIndexMap::KernelMoveIdOf(e.from);
    hop.toSlot   = MoveIndexMap::KernelMoveIdOf(e.to);
    hop.earliest = ke.earliestFrame;
    hop.latest   = ke.latestFrame;

    // The frame the source's hit lands on, in the source's own numbering, and the
    // frame the defender is free again. Both are the kernel's own arithmetic:
    // ResolveHits SETS hitstun on the frame the boxes overlap -- which is
    // `startup`, the first active frame -- and StepPhysics decrements it at the
    // top of every tick after, so it reaches zero at startup + hitstun.
    const std::int32_t contactFrame = src.startup;
    const std::int32_t defenderFree = contactFrame + src.hitstun;

    // The first frame the cancel could fire on. An `onHit` edge must wait one tick
    // past contact for alreadyHitBits to be visible; a whiff edge need not. This
    // character authors none of the latter, but the distinction is written out
    // rather than assumed away, because a file that grew one would otherwise be
    // classified against a rule it does not obey.
    const std::int32_t firstCancel =
        ke.onHit != 0 ? std::max<std::int32_t>(ke.earliestFrame, contactFrame + 1)
                      : ke.earliestFrame;

    if (!bound) {
        hop.block = Block::NoButton;
    } else if (ke.earliestFrame > ke.latestFrame) {
        hop.block = Block::EmptyWindow;
    } else if (firstCancel > ke.latestFrame) {
        hop.block = Block::ContactGate;
    }

    if (hop.block == Block::None) {
        hop.viaCancel  = true;
        hop.startFrame = firstCancel;
    } else if (hop.block == Block::NoButton) {
        // No fallback to model: the button scan needs the same button the cancel
        // does, so an unbound target is unreachable by BOTH routes.
        hop.viaCancel  = false;
        hop.startFrame = -1;
    } else {
        hop.viaCancel  = false;
        hop.startFrame = src.startup + src.active + src.recovery;   // MoveDuration
    }

    // "Connects before the defender leaves hitstun". STRICTLY before: a follow-up
    // whose first active frame IS the frame the stun expires arrives on a tick the
    // defender was already actionable on, and section 4's detector counts that as
    // an escape -- so the two halves of this file must agree on the boundary here.
    hop.connects = hop.startFrame >= 0 && hop.startFrame + tgt.startup < defenderFree;
    if (hop.block == Block::None && !hop.connects) hop.block = Block::TooSlow;
    return hop;
}

// ============================================================================
// 4. Driving one cycle. Driver and drive() are COPIED from test_ground_truth.cpp
// ============================================================================

// One button per DISTINCT move in the cycle, in first-appearance order. A cycle
// naming more distinct moves than there are single-bit buttons gets a binding of
// ZERO for the surplus rather than a shorter list, so the shortfall surfaces as
// Block::NoButton -- a named reason -- rather than as a trace that quietly stops
// following the cycle.
std::vector<MoveBinding> bindingsFor(const CharacterData& c, const Cycle& cycle) {
    std::vector<MoveBinding> out;
    for (const MoveIndex m : cycle.moves) {
        const std::string& id = c.moves[m].id;
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

// A cursor over the cycle: press the button of the move that comes next, and
// advance when the attacker actually ENTERS it. Advancing on `moveFrame == 0`
// rather than on a change of moveId is what lets a self-cancel be followed at
// all -- the id never changes there, and the frame counter is what resets.
//
// It does not care HOW the attacker got into the move. That is not laziness: it
// is the property section 4 turns on, because 40 of these 41 cycles are entered
// by a route the cancel table does not contain, and a driver that watched for a
// cancel specifically would have reported every one of them as a stall.
class Driver {
public:
    Driver(const CharacterData& c, const Cycle& cycle, const MoveIndexMap& map,
           const std::vector<MoveBinding>& bindings) {
        for (const MoveIndex m : cycle.moves) {
            const std::string& id = c.moves[m].id;
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

    // A FOURTH COPY of this cursor -- with FightSession.cpp, test_ground_truth.cpp
    // and test_game_core.cpp -- and the fourth place the same two input rules had
    // to be written. Recorded as work in ROADMAP M1.6 rather than as a comment.
    //
    // Zero on a release tick, because a held bit is one press however long it
    // lasts and a witness that cancels a move into itself asks for the same bit
    // twice running. And a re-press while WAITING, because a driver that stalls
    // on a move which never comes otherwise stops feeding the kernel anything at
    // all -- "the cycle managed one turn" would then mean "the driver went
    // quiet", not "the game refused".
    std::uint16_t Bits() const {
        if (release_ || buttons_.empty()) return 0;
        return buttons_[cursor_];
    }

    // THE MOVE STARTING IS CHECKED BEFORE THE RELEASE TICK IS SPENT, and that
    // ordering is the whole of it. Buffering exists so a press that arrived
    // early is consumed the tick the fighter can act -- which is very often a
    // tick this driver is spending SILENT, because it pressed two ticks ago and
    // is now releasing so the next press has an edge to be. A driver that
    // returned early on a release tick therefore missed precisely the
    // transitions buffering creates: the cursor never advanced, so it went on
    // asking for the move already running, and the move restarted from frame 0
    // one duration later. That read as "the loop decayed" when the loop was
    // fine and the observer was blind.
    void Observe(std::uint16_t attackerMove, std::uint16_t attackerFrame) {
        if (slots_.empty()) return;
        if (attackerMove == slots_[cursor_] && attackerFrame == 0) {
            waiting_ = 0;
            const std::uint16_t justUsed = buttons_[cursor_];
            cursor_ = (cursor_ + 1 < slots_.size()) ? cursor_ + 1 : 0;
            release_ = (buttons_[cursor_] == justUsed);
            return;
        }
        if (release_) { release_ = false; return; }
        ++waiting_;
        if (waiting_ >= kRepressAfter) { release_ = true; waiting_ = 0; }
    }

private:
    std::vector<std::uint16_t> slots_;
    std::vector<std::uint16_t> buttons_;
    std::vector<std::string>   ids_;
    std::size_t                cursor_ = 0;
    bool                       release_ = false;
    int                        waiting_ = 0;
    static constexpr int       kRepressAfter = 2;
};

// Silent is the recipe the character files prescribe. MashesOnceHit starts LATE
// on purpose: at tick 0 both fighters are idle and hold the same bindings, so
// both would start the same move and ResolveHits -- deliberately symmetric --
// would land both. That is a correct simulation of two people mashing at each
// other and it is not the question. Starting after the combo has opened asks the
// question a player asks: "I have been hit; can I get out?"
enum class DefenderPolicy { Silent, MashesOnceHit };

struct Sample {
    std::int32_t  tick       = 0;
    std::uint16_t inputBits  = 0;
    std::uint16_t atkMove    = 0;
    std::uint16_t atkFrame   = 0;
    std::uint8_t  atkHitBits = 0;
    std::uint16_t defStun    = 0;
    std::uint16_t defMove    = 0;
    std::int32_t  defHealth  = 0;
    std::int32_t  atkHealth  = 0;
    bool          hit        = false;
    bool          defEntered = false;
};

// Named TickLog rather than Run: ::testing::Test has a member function `Run`, and
// class-scope lookup wins over the enclosing namespace inside a TEST body.
struct TickLog {
    std::vector<Sample>       samples;
    std::vector<std::int32_t> hitTicks;
    std::vector<std::int32_t> defenderActedTicks;
    std::int32_t              firstHitTick = -1;
    std::int32_t              lastHitTick  = -1;
    GameState                 finalState{};

    std::size_t Hits() const { return hitTicks.size(); }

    // COPIED, and kept even though nothing here decides on it, so that section 6
    // can quote what the ground-truth file's detector would have said about these
    // same 41 traces.
    std::vector<std::int32_t> FreeTicks() const {
        std::vector<std::int32_t> out;
        if (firstHitTick < 0) return out;
        for (const Sample& s : samples) {
            if (s.tick < firstHitTick || s.tick > lastHitTick) continue;
            if (s.defStun == 0 && !s.hit) out.push_back(s.tick);
        }
        return out;
    }

    // NOT COPIED. THE DETECTOR THIS FILE DECIDES ON -- see the header.
    //
    // The defender was ACTIONABLE at tick t if their hitstun at the end of tick
    // t-1 was at most 1: StepPhysics decrements it at the top of the tick before
    // anything else looks at it, and `actionable()` is `hitstun == 0`. Read this
    // way it cannot be erased by ResolveHits clearing their moveId later in the
    // same tick, which is exactly what hides the case both copied detectors miss.
    //
    // The first hit's own tick is excluded and has to be: the defender was
    // legitimately free then, which is what being hit out of neutral means.
    std::vector<std::int32_t> ActionableTicks() const {
        std::vector<std::int32_t> out;
        if (firstHitTick < 0) return out;
        for (std::int32_t t = firstHitTick + 1; t <= lastHitTick; ++t) {
            const std::size_t previous = static_cast<std::size_t>(t - 1);
            if (previous < samples.size() && samples[previous].defStun <= 1)
                out.push_back(t);
        }
        return out;
    }
};

TickLog drive(const MatchData& data, Driver& driver, int ticks,
              DefenderPolicy policy, std::uint16_t defenderBits) {
    TickLog log{};
    GameState s{};
    // The seed only feeds GameState::rng, which nothing in a combat tick reads.
    // Fixed here so a failing run is reproducible verbatim.
    cse::kernel::ResetMatch(s, 0xC0FFEEu);
    s.p[0].posX = kP0X;
    s.p[1].posX = kP1X;

    log.samples.reserve(static_cast<std::size_t>(ticks));
    for (int t = 0; t < ticks; ++t) {
        const std::int32_t healthBefore = s.p[1].health;

        InputPair in{};
        in.p[0].bits = driver.Bits();
        if (policy == DefenderPolicy::MashesOnceHit && log.firstHitTick >= 0 &&
            t > log.firstHitTick) {
            // Mashing is repeated PRESSES; holding is one. See the same change
            // in tests/test_ground_truth.cpp.
            in.p[1].bits = (t % 2 == 0) ? defenderBits : 0u;
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
            if (log.firstHitTick < 0) log.firstHitTick = t;
            log.lastHitTick = t;
            log.hitTicks.push_back(t);
        }
        if (sample.defEntered) log.defenderActedTicks.push_back(t);
        log.samples.push_back(sample);
    }
    log.finalState = s;
    return log;
}

std::string moveName(const MoveIndexMap& map, std::uint16_t slot) {
    if (slot == 0) return "idle";
    const std::string_view id = map.IdOf(slot);
    return id.empty() ? ("slot" + std::to_string(slot)) : std::string(id);
}

// COPIED. The per-tick table a reader needs when a cycle stops behaving: WHICH
// TICK, and WHAT THE DEFENDER WAS DOING, without re-running anything. With 41
// cycles under measurement, a bare count in a failure message would name a
// disagreement and give nobody a way to find it.
std::string Table(const TickLog& log, const MoveIndexMap& map,
                  std::int32_t from, std::int32_t count) {
    std::ostringstream s;
    s << "\n  tick  in   attacker move        fr  hitbits  hit   def stun  "
         "def move      def hp\n";
    for (const Sample& sample : log.samples) {
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

// ============================================================================
// The sweep: every cycle, measured the same way
// ============================================================================

// How long each cycle is driven, and why it is this number rather than a bigger
// one. A four-move turn of this character deals up to 223 damage against 1000
// health, so five turns knocks the defender out -- and Fighter::health clamps at
// zero, which would make "the defender's health fell this tick" stop reporting
// hits and silently truncate every count below. 160 ticks gives every one of the
// 41 cycles at least three full turns with the defender still standing, and the
// sweep ASSERTS both halves of that, so a damage change becomes a named failure
// rather than a quiet undercount. Section 5 is what turns three turns into
// "forever"; the budget does not have to.
constexpr int kSweepTicks = 160;
constexpr int kMinTurns   = 3;

// ONE BUDGET STOPPED WORKING WHEN THE CYCLES GOT LONGER, and the two failure
// modes pull in opposite directions, which is why this is a function of the
// cycle rather than a bigger constant.
//
// A turn of a length-N cycle takes about N times as long as one move, so a
// length-5 cycle managed only TWO turns inside the 160 ticks that gave a
// length-4 cycle three. Raising the constant for everybody is the obvious fix
// and it is the wrong one: the short cycles would then run long enough to KO the
// defender, `Fighter::health` clamps at zero, and "the defender's health fell
// this tick" would stop reporting hits -- silently undercounting the very thing
// this sweep measures. The comment above already names that hazard.
//
// So the budget scales with length and is FLOORED at the old number. Every cycle
// of length 1, 3 or 4 is driven for exactly the 160 ticks it was driven for
// before, so no previously-measured number can move for this reason; only the
// length-5 cycles the six-aerial roster introduced get more.
constexpr int kTicksPerMoveInCycle = 40;   // 160 / 4, the old budget's own ratio

inline int sweepTicksFor(std::size_t cycleLength) {
    const int scaled = kTicksPerMoveInCycle * static_cast<int>(cycleLength);
    return scaled > kSweepTicks ? scaled : kSweepTicks;
}

// ResetMatch's opening health (Simulate.cpp).
constexpr std::int32_t kStartingHealth = 1000;

struct CycleResult {
    Cycle            cycle;
    std::vector<Hop> hops;
    std::string      label;

    // What the MODEL says (section 2)
    std::int32_t juggle = 0;
    std::int32_t meter  = 0;

    // What the KERNEL can take (section 3)
    bool         everyEdgeTakeable = false;
    std::size_t  blockedEdges      = 0;
    Block        firstBlock        = Block::None;
    bool         predictedLoop     = false;   // every hop connects, by either route

    // What it DID (section 4)
    bool         driven  = false;
    std::size_t  hits    = 0;
    std::size_t  turns   = 0;
    std::int32_t period  = -1;
    bool         periodic     = false;
    bool         stateRepeats = false;
    bool         mashChangedNothing = false;
    std::size_t  freeTicks       = 0;   // the copied detector, silent run
    std::size_t  actedTicks      = 0;   // the other copied detector, defender mashing
    std::size_t  actionableTicks = 0;   // THE ONE THIS FILE DECIDES ON
    std::int32_t defenderHealth  = 0;
    std::int32_t attackerHealth  = 0;   // under the MASH, so it is a real claim

    // Section 3's account, checked against the trace one transition at a time:
    // how many move-to-move intervals were compared with the frame the hop says
    // the follow-up begins on, and how many disagreed.
    std::size_t  timingChecks     = 0;
    std::size_t  timingMismatches = 0;

    // Kept for failure messages only: a count nobody can look into is a count
    // nobody can argue with.
    TickLog      silent;
    MoveIndexMap map;

    bool Unescapable() const { return actionableTicks == 0; }
};

// Whether one full turn of the loop leaves the simulation in the state it started
// in, ignoring the defender's health.
//
// THIS IS WHAT LETS THE FILE SAY "FOREVER" RATHER THAN "FOR 160 TICKS". Counting
// turns can only ever report a number; if the tick-by-tick state over one period
// is identical to the next period's -- same move, same frame, same hit record,
// same stun, and the same input arriving -- then the same inputs meet the same
// state and the trace continues without end by induction. Health is excluded ON
// PURPOSE and is the only field excluded: it is the one thing that must change,
// and it is what makes the loop a combo rather than a stalemate.
bool oneTurnRepeats(const TickLog& log, std::size_t cycleLength) {
    if (log.hitTicks.size() < 2 * cycleLength + 1) return false;
    const std::int32_t first  = log.hitTicks[0];
    const std::int32_t second = log.hitTicks[cycleLength];
    const std::int32_t period = second - first;
    if (period <= 0) return false;
    if (static_cast<std::size_t>(second + period) > log.samples.size()) return false;

    for (std::int32_t k = 0; k < period; ++k) {
        const Sample& a = log.samples[static_cast<std::size_t>(first + k)];
        const Sample& b = log.samples[static_cast<std::size_t>(second + k)];
        if (a.inputBits != b.inputBits || a.atkMove != b.atkMove ||
            a.atkFrame != b.atkFrame || a.atkHitBits != b.atkHitBits ||
            a.defStun != b.defStun || a.defMove != b.defMove || a.hit != b.hit)
            return false;
    }
    return true;
}

// Everything sections 1 to 6 need, computed once per test. Built fresh rather
// than shared as a global: a MatchData mutated by accident in one test and read
// in another is the coupling that makes a failure impossible to localise, and 41
// builds with 82 traces cost milliseconds.
struct Sweep {
    std::vector<Cycle>       authored;
    std::vector<Cycle>       usable;
    std::vector<CycleResult> results;
    bool                     capHit = false;
};

std::string describe(const CharacterData& c, const CycleResult& r) {
    std::ostringstream s;
    s << "\n  cycle           " << r.label
      << "\n  model           juggle " << r.juggle << ", meter " << r.meter
      << "\n  hops";
    for (const Hop& h : r.hops) {
        s << "\n    " << std::setw(18) << std::left << c.moves[h.from].id
          << " -> " << std::setw(18) << std::left << c.moves[h.to].id << std::right
          << " window [" << h.earliest << ", " << h.latest << "]  "
          << std::setw(13) << std::left << blockName(h.block) << std::right
          << " starts on frame " << h.startFrame
          << " by " << (h.viaCancel ? "cancel" : "button")
          << (h.connects ? ", connects" : ", DOES NOT CONNECT");
    }
    s << "\n  predicted       " << (r.predictedLoop ? "a loop" : "an escape")
      << ", and " << r.timingChecks << " observed transition(s) were compared "
         "with those frames: " << r.timingMismatches << " disagreed"
      << "\n  executed        " << r.hits << " hits, " << r.turns
      << " turns, period " << r.period
      << (r.periodic ? " (periodic)" : " (NOT periodic)")
      << (r.stateRepeats ? ", state repeats" : ", state does NOT repeat")
      << "\n  defender        actionable on " << r.actionableTicks
      << " tick(s); free on " << r.freeTicks << "; started a move on "
      << r.actedTicks
      << "\n  health          attacker " << r.attackerHealth
      << ", defender " << r.defenderHealth << "\n";
    if (r.driven) s << Table(r.silent, r.map, r.silent.firstHitTick, 44);
    return s.str();
}

void runSweep(const Subject& s, Sweep& out) {
    const CharacterData& c = s.character;

    CycleFinder all(c, allEdges(s));
    out.capHit = !all.Enumerate(out.authored);
    if (out.capHit) return;

    CycleFinder live(c, usableEdges(s));
    out.capHit = !live.Enumerate(out.usable);
    if (out.capHit) return;

    for (const Cycle& cycle : out.usable) {
        CycleResult r{};
        r.cycle  = cycle;
        r.label  = cycle.ToString(c);
        r.juggle = cycleResource(c, cycle, kJuggle);
        r.meter  = cycleResource(c, cycle, kMeter);

        const std::vector<MoveBinding> bindings = bindingsFor(c, cycle);
        MatchBuild build{};
        if (!buildMirror(c, bindings, build)) {
            ADD_FAILURE() << "the build failed for " << r.label << ": "
                          << build.report[0].error;
            out.results.push_back(r);
            continue;
        }
        r.map = build.moves[0];

        // --- section 3: can the kernel take each hop? ------------------------
        r.everyEdgeTakeable = true;
        r.predictedLoop     = true;
        for (const CancelIndex file : cycle.edges) {
            const cse::kernel::CancelEdge* ke = kernelEdgeFor(build, file);
            if (ke == nullptr) {
                ADD_FAILURE() << "cancel " << file << " of " << r.label
                              << " did not cross into the kernel at all, which "
                                 "MatchBuilder only does for a dangling endpoint";
                r.everyEdgeTakeable = false;
                r.predictedLoop     = false;
                continue;
            }
            bool bound = false;
            for (const MoveBinding& b : bindings)
                if (b.moveId == c.moves[c.cancels[file].to].id && b.button != 0)
                    bound = true;

            const Hop hop = classify(c, *ke, file, bound);
            if (hop.block != Block::None) {
                if (r.everyEdgeTakeable) r.firstBlock = hop.block;
                ++r.blockedEdges;
                r.everyEdgeTakeable = false;
            }
            if (!hop.connects) r.predictedLoop = false;
            r.hops.push_back(hop);
        }

        // --- section 4: execute it -------------------------------------------
        // AN AUTHORED BUFFER WINDOW, and it is what keeps the timing account
        // exact. Edge detection alone means a re-pressing driver can only land
        // its press within a tick or two of the fighter becoming actionable, so
        // every transition drifts and section 3's frame-by-frame account stops
        // matching. A buffered press is consumed the EXACT tick the fighter can
        // act -- which is what buffering is for in every fighting game that has
        // it, and why ROADMAP M1.1d makes the window a character field rather
        // than a constant.
        //
        // Set here rather than in the character file because this sweep drives
        // 121 synthesised cycles, not a shipped character.
        build.data.p[0].inputBufferFrames = 2;
        build.data.p[1].inputBufferFrames = 2;

        Driver silentDriver(c, cycle, build.moves[0], bindings);
        std::string why;
        if (!silentDriver.Usable(why)) {
            // Not a failure: a cycle nothing can ask for is a MEASUREMENT, and
            // Block::NoButton above is where it is counted.
            out.results.push_back(r);
            continue;
        }
        Driver mashDriver(c, cycle, build.moves[0], bindings);

        const int budget = sweepTicksFor(cycle.moves.size());
        r.silent = drive(build.data, silentDriver, budget,
                         DefenderPolicy::Silent, 0);
        // The defender mashes the FIRST move of the cycle, which is bound by
        // construction. Handing them an unbound button would make "the defender
        // never acted" a fact about the binding table rather than about the combo.
        const TickLog mashed = drive(build.data, mashDriver, budget,
                                     DefenderPolicy::MashesOnceHit, bindings[0].button);

        const std::size_t length = cycle.moves.size();
        r.driven             = true;
        r.hits               = r.silent.Hits();
        r.turns              = r.hits / length;
        r.freeTicks          = r.silent.FreeTicks().size();
        r.actedTicks         = mashed.defenderActedTicks.size();
        r.actionableTicks    = r.silent.ActionableTicks().size();
        r.defenderHealth     = r.silent.finalState.p[1].health;
        r.attackerHealth     = mashed.finalState.p[0].health;
        r.stateRepeats       = oneTurnRepeats(r.silent, length);
        r.mashChangedNothing = (mashed.hitTicks == r.silent.hitTicks);

        if (r.silent.hitTicks.size() > length) {
            r.period   = r.silent.hitTicks[length] - r.silent.hitTicks[0];
            r.periodic = true;
            for (std::size_t i = 0; i + length < r.silent.hitTicks.size(); ++i)
                if (r.silent.hitTicks[i + length] - r.silent.hitTicks[i] != r.period)
                    r.periodic = false;
        }

        // --- section 3's account, read back off the trace one hop at a time ---
        //
        // The outcome matching is not enough on its own: a cycle could loop for a
        // reason other than the one this file gives and the counts would still
        // agree. So every move-to-move transition the trace performed is compared
        // with the frame the hop SAYS the follow-up begins on -- `firstCancel` for
        // a takeable edge, `MoveDuration` for the button-start route. A trace that
        // reached the same follow-up by some third path would disagree here.
        //
        // A move ENTRY is `moveFrame == 0` with a move in progress, the same
        // signal the Driver advances its cursor on.
        std::vector<std::pair<std::int32_t, std::uint16_t>> entries;
        for (const Sample& sample : r.silent.samples)
            if (sample.atkMove != 0 && sample.atkFrame == 0)
                entries.emplace_back(sample.tick, sample.atkMove);
        for (std::size_t i = 0; i + 1 < entries.size(); ++i) {
            for (const Hop& h : r.hops) {
                if (h.fromSlot != entries[i].second) continue;
                if (h.toSlot   != entries[i + 1].second) continue;
                ++r.timingChecks;
                if (entries[i + 1].first - entries[i].first != h.startFrame)
                    ++r.timingMismatches;
                break;
            }
        }

        out.results.push_back(r);
    }
}

// --- The measurement, in one place so no number below is written twice --------
//
// Every one of these is DERIVED by the sweep and asserted against, never used to
// compute anything. A character edit that changes the gap fails a test that
// prints both the old number and the new one.
constexpr std::size_t kAuthoredCycles = 615;  // over all 94 authored edges
constexpr std::size_t kUsableCycles   = 121;  // over the 88 the prover keeps
constexpr std::size_t kSelfLoops      = 1;    // ... of length 1
constexpr std::size_t kLengthThree    = 8;
constexpr std::size_t kLengthFour     = 48;
constexpr std::size_t kLengthFive     = 64;

constexpr std::size_t kDeadCancels    = 6;
constexpr std::size_t kUsableCancels  = 88;

constexpr std::size_t kEndedByJuggle  = 121;  // of 121
constexpr std::size_t kFullyTakeable  = 1;    // of 121
constexpr std::size_t kEmptyWindowed  = 120;  // of 121

constexpr std::size_t kUnescapable    = 97;   // of 121, on the actionable detector
constexpr std::size_t kEscapable      = 24;
constexpr std::size_t kByCancelAlone  = 1;    // of the 97
constexpr std::size_t kByHeldButton   = 96;

// What the copied detectors would have said, kept so the difference is pinned
// rather than described.
constexpr std::size_t kUnescapableByFreeTicks = 109;

// air_mp's landing links: startup 6 + delay 15 = 21, against a cancel window that
// closes at 14. Named so a failure can quote the arithmetic and not just the
// numbers it produced.
constexpr std::int32_t kLandingLinkEarliest = 21;
constexpr std::int32_t kLandingLinkLatest   = 14;

// How `air_mp` leaves: nine links back to the ground (eight normals and the
// uppercut) and two that stay in the air (itself and `air_hk`, both delay 5 and
// both takeable). Counted separately because the second pair is the reason the
// self-loop is the one cycle the cancel system can perform end to end.
constexpr std::size_t kLandingLinks     = 9;
constexpr std::size_t kAirToAirCancels  = 2;

// The relations between the numbers above, checked at compile time rather than
// asserted at runtime -- a runtime EXPECT comparing two constants in this file
// would test nothing but the arithmetic on this page. Both are load-bearing: the
// blocked-hop count must be one per cycle that is not the self-loop, and the
// escapable count must leave the self-loop and the length-4 cycles alone.
static_assert(kEmptyWindowed == kUsableCycles - kSelfLoops,
              "every cycle but the self-loop is blocked at exactly one edge");
static_assert(kUsableCycles == kSelfLoops + kLengthThree + kLengthFour + kLengthFive,
              "the length histogram must account for every cycle");
static_assert(kUnescapable + kEscapable == kUsableCycles,
              "every cycle is either escapable or not");
static_assert(kUnescapable == kByCancelAlone + kByHeldButton,
              "every loop runs by one of the two routes");

// THE RELATION THAT USED TO BE HERE WAS A COINCIDENCE OF A TWO-AERIAL CHARACTER,
// and saying so is worth more than replacing it with a new arithmetic identity.
//
// It read `kEscapable == kLengthThree` -- "the cycles that leak are exactly the
// ones of length 3" -- and that was true only because, with `air_mp` and
// `air_hk` the sole aerials, every length-3 cycle happened to be one whose
// landing link enters a MEDIUM and no length-4 cycle was. What actually decides
// whether a cycle leaks is that the held-button restart route is one tick later
// than the file's authored delay, so `air_mp` at +7 still connects into a light
// (3f/4f) and does not into a medium (6f/7f).
//
// The six-aerial character breaks the coincidence without touching the rule:
// the leaking cycles are now the 8 of length 3 PLUS 16 of length 4. Restating
// that as `kEscapable == kLengthThree + 16` would encode a second coincidence
// and teach the next reader nothing, so the property is asserted where it
// belongs instead -- section 6 measures the route per cycle and this file's own
// `[ GAP EXTENT ]` block prints the split.
static_assert(kEscapable < kUsableCycles,
              "if every cycle leaks there is no gap left to measure");

}  // namespace

// ============================================================================
// 1. THE GRAPH -- the number, derived rather than trusted
// ============================================================================

TEST(GapExtentGraph, TheCycleCountIsDerivedAndMatchesTheAuthoringReport) {
    Subject safe{};
    bringUp(kSafe, safe);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    ASSERT_EQ(safe.verdict.status, ProverStatus::Terminating)
        << "this whole file is about a character the tool CERTIFIES, and it does "
           "not certify this one.\n"
        << DescribeVerdict(safe.character, safe.verdict);

    Sweep sweep{};
    runSweep(safe, sweep);
    ASSERT_FALSE(sweep.capHit)
        << "the cycle enumeration hit its cap of " << kMaxCycles
        << ". The character's graph grew a combinatorial explosion, and every "
           "count in this file is a floor rather than a measurement.";

    // The prover's own split, checked here because everything downstream is an
    // enumeration over the graph it defines. 68/5 is what the file's own
    // `meter_is_not_load_bearing.the_counterfactual_that_checks_it` requires.
    //
    // The third line is not arithmetic for its own sake: usable and dead account
    // for every authored edge only when NONE was skipped for not being
    // contact-gated (comboprover.hpp:337, and see usableEdges above). A file that
    // grew a `on: block` edge would break this equality first, before it silently
    // changed a cycle count.
    EXPECT_EQ(safe.verdict.deadCancels.size(), kDeadCancels);
    EXPECT_EQ(static_cast<std::size_t>(safe.verdict.usableCancels), kUsableCancels);
    EXPECT_EQ(safe.character.cancels.size(), kDeadCancels + kUsableCancels);

    // THE NUMBER. fighter_a's `engine.termination_argument` says "the usable
    // cancel graph contains exactly 41 simple cycles: one of length 1 (air_mp into
    // itself), eight of length 3 ... and thirty-two of length 4". Nothing here
    // reads that claim; this is an enumeration of the loaded data, and the
    // agreement is the result rather than the input.
    ASSERT_EQ(sweep.usable.size(), kUsableCycles)
        << "fighter_a's authoring report claims " << kUsableCycles
        << " simple cycles in its usable cancel graph and enumeration finds "
        << sweep.usable.size()
        << ". THE REPORT IS THE THING THAT IS WRONG unless the character changed: "
           "this count is derived from CharacterData::cancels and the prover's own "
           "dead list, and nothing in it is quoted from the file.";

    const std::vector<std::size_t> byLength = lengthHistogram(sweep.usable);
    ASSERT_GT(byLength.size(), 4u);
    EXPECT_EQ(byLength[1], kSelfLoops)   << "self-loops";
    EXPECT_EQ(byLength[3], kLengthThree) << "cycles of length 3";
    EXPECT_EQ(byLength[4], kLengthFour)  << "cycles of length 4";
    EXPECT_EQ(byLength[5], kLengthFive)  << "cycles of length 5";
    for (std::size_t n = 0; n < byLength.size(); ++n)
        if (n != 1 && n != 3 && n != 4 && n != 5)
            EXPECT_EQ(byLength[n], 0u)
                << "a cycle of length " << n << " appeared, and the "
                   "1 / 8 / 48 / 64 structure accounts for none";

    RecordProperty("usable_cycles", static_cast<int>(sweep.usable.size()));
    RecordProperty("authored_cycles", static_cast<int>(sweep.authored.size()));
}

// The qualifier in "the USABLE cancel graph" is load-bearing, and this is what it
// is worth. The five edges the prover discards are not decoration -- three of them
// close cycles of their own -- and putting them back takes the count from 41 to
// 143.
//
// This is not a criticism of the report, which says "usable" and means it. It is
// the reason nobody may quote 41 as "the number of cycles in fighter_a", and the
// reason every count in this file is taken over the graph the VERDICT was
// computed on rather than over the file.
TEST(GapExtentGraph, TheAuthoredGraphHoldsFarMoreCyclesThanTheUsableOne) {
    Subject safe{};
    bringUp(kSafe, safe);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    Sweep sweep{};
    runSweep(safe, sweep);
    ASSERT_FALSE(sweep.capHit);

    EXPECT_EQ(sweep.authored.size(), kAuthoredCycles)
        << "the authored graph's cycle count moved. Either an edge was added or "
           "one of the five dead ones came back to life.";
    EXPECT_GT(sweep.authored.size(), sweep.usable.size());

    // And the dead edges are dead for the reason the prover reports: the follow-up
    // needs more startup than the source leaves. Re-derived from ProverDeadCancel's
    // own two numbers, so this checks the report is internally consistent rather
    // than re-implementing the link condition a second time in this repository.
    for (const ProverDeadCancel& dead : safe.verdict.deadCancels) {
        EXPECT_GT(dead.shortfall(), 0)
            << "cancel " << dead.cancel << " is listed dead with a shortfall of "
            << dead.shortfall() << ", which is not short of anything";
        EXPECT_EQ(dead.shortfall(), dead.startup - dead.advantage);
        ASSERT_LT(static_cast<std::size_t>(dead.from), safe.character.moves.size());
        ASSERT_LT(static_cast<std::size_t>(dead.to), safe.character.moves.size());
    }
}

// ============================================================================
// 2. THE MODEL -- what ends each of the 41
// ============================================================================

// The premise of the whole file: every one of these cycles is stopped by juggle
// and by nothing else. If one of them terminated for a structural reason instead,
// the kernel having no juggle would say nothing about it, and it would have to
// come out of the headline count.
TEST(GapExtentModel, EveryCycleIsEndedByJuggleAndNoCycleTouchesMeter) {
    Subject safe{};
    bringUp(kSafe, safe);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    Sweep sweep{};
    runSweep(safe, sweep);
    ASSERT_FALSE(sweep.capHit);
    ASSERT_EQ(sweep.results.size(), kUsableCycles);

    // A03: the positional resource contract the two indices above depend on.
    ASSERT_EQ(safe.character.resources.size(), kBuildResources.size());
    ASSERT_EQ(safe.character.resources[kMeter].name,  "meter");
    ASSERT_EQ(safe.character.resources[kJuggle].name, "juggle");
    EXPECT_GT(safe.character.resources[kJuggle].initial, 0);
    EXPECT_EQ(safe.character.resources[kJuggle].floor, 0)
        << "juggle's floor is what `nonNegative` refuses to cross, and the whole "
           "termination argument is that the budget runs INTO it";

    std::size_t endedByJuggle = 0;
    std::size_t touchMeter    = 0;
    for (const CycleResult& r : sweep.results) {
        if (r.juggle < 0) ++endedByJuggle;
        if (r.meter != 0) ++touchMeter;
        EXPECT_LT(r.juggle, 0)
            << "this cycle does not spend juggle, so nothing in the model stops it "
               "and the verdict should not have been TERMINATING."
            << describe(safe.character, r);
    }

    EXPECT_EQ(endedByJuggle, kEndedByJuggle)
        << endedByJuggle << " of " << sweep.results.size()
        << " cycles strictly decrease juggle. The file's structural proof claims "
           "ALL of them do, because every cycle contains an edge into an air move.";

    // Meter appears on no cycle at all, which is the file's own
    // `meter_is_not_load_bearing` argument: super_beam is the only move that
    // spends it and it has zero outgoing cancels. It matters here because the
    // certificate's order is [meter, juggle] -- if meter DID gate a cycle, "the
    // kernel has no resources" would have two consequences and this file measures
    // one of them.
    EXPECT_EQ(touchMeter, 0u)
        << touchMeter << " cycle(s) change meter, so the certificate's first "
           "resource is load-bearing after all and the sections below measure only "
           "half of the gap.";

    // The certificate names juggle. Not the argument -- the argument is the sum
    // above -- but if it did not, the character would be terminating for a reason
    // its own describe() does not print.
    ASSERT_TRUE(safe.verdict.hasRanking)
        << "fighter_a is the first character in this repository to carry a ranking "
           "certificate, and it no longer does: "
        << RankingAbsenceName(safe.verdict.rankingAbsence);
    bool namesJuggle = false;
    for (const ResourceIndex r : safe.verdict.rankingOrder)
        if (r == kJuggle) namesJuggle = true;
    EXPECT_TRUE(namesJuggle);

    RecordProperty("cycles_ended_by_juggle", static_cast<int>(endedByJuggle));
}

// ============================================================================
// 3. THE KERNEL -- how many of the 41 it can perform as the model describes them
// ============================================================================

// THE HONEST HALF. It would have been convenient for this file if all 41 cycles
// were performable and the answer were "41 of 41 run forever". They are not, and
// the reason is one specific edge shape appearing 40 times.
TEST(GapExtentKernel, ExactlyOneCycleIsPerformableThroughTheCancelSystem) {
    Subject safe{};
    bringUp(kSafe, safe);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    Sweep sweep{};
    runSweep(safe, sweep);
    ASSERT_FALSE(sweep.capHit);
    ASSERT_EQ(sweep.results.size(), kUsableCycles);

    std::size_t fullyTakeable = 0;
    std::size_t byEmptyWindow = 0, byContactGate = 0, byNoButton = 0, byTooSlow = 0;
    for (const CycleResult& r : sweep.results) {
        if (r.everyEdgeTakeable) { ++fullyTakeable; continue; }
        switch (r.firstBlock) {
            case Block::EmptyWindow: ++byEmptyWindow; break;
            case Block::ContactGate: ++byContactGate; break;
            case Block::NoButton:    ++byNoButton;    break;
            case Block::TooSlow:     ++byTooSlow;     break;
            case Block::None:        break;
        }
    }

    EXPECT_EQ(fullyTakeable, kFullyTakeable)
        << fullyTakeable << " of " << sweep.results.size()
        << " cycles have every edge takeable by the kernel's cancel rule, and this "
           "test was written against " << kFullyTakeable
        << ". If this GREW, the bridge changed and the gap changed with it.";

    // GROUPED BY REASON, which is the point of the section. One reason accounts
    // for all 40, and the other three are checked and found inert -- knowing a
    // check ran and found nothing is worth more than its absence, which is
    // MatchBuilder's own argument for listing a loss with a count of 0.
    EXPECT_EQ(byEmptyWindow, kEmptyWindowed);
    EXPECT_EQ(byContactGate, 0u)
        << byContactGate << " cycle(s) are blocked because the cancel window has "
           "closed by the time contact is visible. That is a DIFFERENT defect from "
           "the empty window -- StepAttack running before ResolveHits, rather than "
           "the delay outliving the window -- and it is new.";
    EXPECT_EQ(byNoButton, 0u)
        << byNoButton << " cycle(s) name more distinct moves than there are "
           "single-bit buttons (" << kButtonPoolSize << "). That is a limit of the "
           "HARNESS rather than of the character, and it means those cycles were "
           "not measured at all.";
    EXPECT_EQ(byTooSlow, 0u)
        << byTooSlow << " cycle(s) have a takeable edge whose follow-up cannot "
           "reach the defender before hitstun runs out.";

    // ... and the 40 are blocked by ONE edge each, always the same shape: a
    // landing link out of an AIR move, resolved to a window that closes before it
    // opens. Asserting the arithmetic rather than the count is what makes this a
    // finding a reader can check against Combat.h and MatchBuilder.cpp.
    std::size_t blockedHops = 0;
    for (const CycleResult& r : sweep.results) {
        if (r.everyEdgeTakeable) continue;
        EXPECT_EQ(r.blockedEdges, 1u)
            << "a cycle is blocked at " << r.blockedEdges << " edges, so the "
               "single-cause account below is wrong for it."
            << describe(safe.character, r);
        for (const Hop& h : r.hops) {
            if (h.block == Block::None) continue;
            EXPECT_EQ(h.block, Block::EmptyWindow);
            EXPECT_EQ(safe.character.moves[h.from].stance, Stance::Air)
                << "the blocked edge leaves `" << safe.character.moves[h.from].id
                << "`, which is not an air move, so the shared explanation does "
                   "not hold for it";
            EXPECT_EQ(h.earliest, kLandingLinkEarliest);
            EXPECT_EQ(h.latest, kLandingLinkLatest);
            ++blockedHops;
        }
    }
    EXPECT_EQ(blockedHops, kEmptyWindowed);

    // The window is empty because the two halves of MatchBuilder's resolution
    // disagree, and this is that arithmetic spelled out against the LOADED file
    // rather than against the built edge -- so the two derivations have to meet.
    //
    // THE SPLIT IS BY THE TARGET'S STANCE, not by the delay, and the difference
    // matters: `air_mp` leaves by ELEVEN edges and only nine of them are landing
    // links. Two are air-to-air (into itself and into `air_hk`) with a delay of 5,
    // and those resolve inside the window and are perfectly takeable -- the
    // self-cancel is the one cycle section 4 counts as performable through the
    // cancel system. Splitting on "the delay is 15" instead would have been the
    // same partition today and a circular argument for it.
    const MoveIndex airMp = safe.character.FindMove("air_mp");
    ASSERT_NE(airMp, kInvalidMove);
    const Move& air = safe.character.moves[airMp];
    ASSERT_TRUE(air.hasCancelWindow)
        << "air_mp authors no cancel window, so nothing would have closed the "
           "landing links and this whole section is about something else";
    EXPECT_EQ(air.cancelWindowClose, kLandingLinkLatest);

    std::size_t landingLinks = 0, airToAir = 0;
    for (const CancelIndex e : safe.character.cancelsFrom[airMp]) {
        const Cancel& edge   = safe.character.cancels[e];
        const Move&   target = safe.character.moves[edge.to];
        if (target.stance == Stance::Air) {
            ++airToAir;
            EXPECT_LE(air.startup + edge.delay, air.cancelWindowClose)
                << "`air_mp` -> `" << target.id << "` stays in the air and its "
                   "window is closed too, so the split this section draws between "
                   "air-to-air cancels and landing links has stopped holding";
            continue;
        }
        ++landingLinks;
        EXPECT_EQ(air.startup + edge.delay, kLandingLinkEarliest)
            << "`air_mp` -> `" << target.id << "` resolves its delay of "
            << edge.delay << " against a startup of " << air.startup
            << ", and this file's account assumed " << kLandingLinkEarliest;
        EXPECT_GT(air.startup + edge.delay, air.cancelWindowClose)
            << "this landing link is takeable after all, so it is not one of the "
               "40 and the count above is measuring something else";
    }
    EXPECT_EQ(landingLinks, kLandingLinks)
        << "`air_mp` has " << landingLinks << " links back to the ground and the "
           "file's derivation names nine: eight ground normals and the uppercut.";
    EXPECT_EQ(airToAir, kAirToAirCancels);
    // NINE links are inert and only EIGHT of them appear in a cycle: the ninth
    // lands in `special_uppercut`, whose only outgoing edge is into `super_beam`,
    // which has none at all, so it is on no cycle to block. That is why the
    // blocked-hop count above is 40 -- one per cycle of length 3 or 4 -- and not a
    // count of inert edges. The two numbers measure different things and only one
    // of them is the gap.

    RecordProperty("cycles_fully_takeable", static_cast<int>(fullyTakeable));
    RecordProperty("cycles_blocked_by_empty_window", static_cast<int>(byEmptyWindow));
}

// ============================================================================
// 4. THE MEASUREMENT -- what the kernel actually does with all 41
// ============================================================================

// THIS IS THE FILE'S ANSWER.
TEST(GapExtentKernel, NinetySevenOfThe121RunForever) {
    Subject safe{};
    bringUp(kSafe, safe);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    ASSERT_EQ(safe.verdict.status, ProverStatus::Terminating);

    Sweep sweep{};
    runSweep(safe, sweep);
    ASSERT_FALSE(sweep.capHit);
    ASSERT_EQ(sweep.results.size(), kUsableCycles);

    // --- every cycle got a fair run -----------------------------------------
    //
    // Before any count below means anything: each cycle must have had enough ticks
    // to repeat, and the defender must still be standing, because Fighter::health
    // clamps at zero and a KO would stop `hit` being reported at all.
    for (const CycleResult& r : sweep.results) {
        ASSERT_TRUE(r.driven)
            << "this cycle was never driven, so it is absent from every count "
               "below rather than counted as unperformable."
            << describe(safe.character, r);
        EXPECT_GE(r.turns, static_cast<std::size_t>(kMinTurns))
            << "this cycle managed " << r.turns << " turns in " << sweepTicksFor(r.cycle.moves.size())
            << " ticks, which is too few to call it periodic."
            << describe(safe.character, r);
        EXPECT_TRUE(r.periodic)
            << "the hits are not evenly spaced, so this is a decaying string "
               "rather than a loop." << describe(safe.character, r);
        EXPECT_GT(r.defenderHealth, 0)
            << "the defender was knocked out inside the measured window, so the "
               "hit counter stopped at the health clamp and every count in this "
               "test is an undercount. LOWER kSweepTicks."
            << describe(safe.character, r);
        // The defender held a button from the tick after the combo opened and it
        // changed nothing: not the attacker's health, and not one of the ticks the
        // attacker's hits landed on. That is the difference between "the defender
        // lost the exchange" and "the defender never had an exchange to lose".
        EXPECT_EQ(r.attackerHealth, kStartingHealth)
            << "the attacker took damage from a mashing defender, so this run is a "
               "trade being read as a combo." << describe(safe.character, r);
        EXPECT_TRUE(r.mashChangedNothing)
            << "holding a button moved the attacker's hits, so the defender did "
               "get a turn somewhere." << describe(safe.character, r);
    }

    // --- the static account predicts the run, cycle for cycle ----------------
    //
    // Section 3 says which route the kernel takes for each hop and whether it
    // arrives in time. If that account is right it must predict the executed
    // outcome for all 41 -- and if it does not, the explanation in this file's
    // header is a story rather than the mechanism.
    for (const CycleResult& r : sweep.results)
        EXPECT_EQ(r.predictedLoop, r.Unescapable())
            << "the two-route account predicted "
            << (r.predictedLoop ? "a loop" : "an escape") << " and the kernel "
            << (r.Unescapable() ? "looped" : "let the defender out") << "."
            << describe(safe.character, r);

    // ... and it predicts the TICKS, not only the outcome. A cycle could loop for
    // a reason other than the one this file gives and the counts above would still
    // agree; this compares every move-to-move transition the trace performed with
    // the frame the hop says the follow-up begins on. It is what turns "the kernel
    // performs the link by the button-start route" from an inference into a
    // reading -- the 32 button-route cycles enter their follow-up on `air_mp`'s
    // frame 22, its full duration, and never inside the [21, 14] window that does
    // not exist.
    std::size_t totalChecks = 0, totalMismatches = 0;
    for (const CycleResult& r : sweep.results) {
        totalChecks     += r.timingChecks;
        totalMismatches += r.timingMismatches;
        EXPECT_GE(r.timingChecks, r.cycle.moves.size())
            << "fewer transitions were checked than the cycle has hops, so at "
               "least one hop of this cycle was never observed happening."
            << describe(safe.character, r);
        EXPECT_EQ(r.timingMismatches, 0u)
            << r.timingMismatches << " of this cycle's " << r.timingChecks
            << " observed transitions happened on a different frame than section "
               "3 says. The account is wrong about the mechanism even if it "
               "happens to be right about the outcome."
            << describe(safe.character, r);
    }
    EXPECT_GT(totalChecks, 0u);
    EXPECT_EQ(totalMismatches, 0u);
    RecordProperty("route_timings_checked", static_cast<int>(totalChecks));

    // --- the count ----------------------------------------------------------
    std::size_t unescapable = 0, escapable = 0;
    std::size_t viaCancel = 0, viaButton = 0;
    for (const CycleResult& r : sweep.results) {
        if (r.Unescapable()) {
            ++unescapable;
            (r.everyEdgeTakeable ? viaCancel : viaButton) += 1;
        } else {
            ++escapable;
        }
    }

    EXPECT_EQ(unescapable, kUnescapable)
        << unescapable << " of " << sweep.results.size()
        << " cycles ran with the defender never once actionable, and this test was "
           "written against " << kUnescapable << ".";
    EXPECT_EQ(escapable, kEscapable);
    EXPECT_EQ(unescapable + escapable, sweep.results.size());

    // Of the 33, ONE is performable the way the model describes it and 32 are
    // performed by the held-button route instead. Both are the same gap and they
    // are not the same defect, so they are counted apart: closing the cancel
    // projection tomorrow would leave 32 of these standing.
    EXPECT_EQ(viaCancel, kByCancelAlone);
    EXPECT_EQ(viaButton, kByHeldButton);

    // --- WHY the ones that leak, leak ----------------------------------------
    //
    // THIS USED TO ASSERT A LENGTH AND THE LENGTH WAS A COINCIDENCE. It read
    // `EXPECT_EQ(r.cycle.moves.size(), 3u)` -- "the cycles that leak are exactly
    // the ones of length 3" -- which was true of a character with two aerials and
    // is false of one with six. The six-aerial roster leaks cycles of length 3
    // AND of length 4, so the old assertion would now fail while the underlying
    // rule had not changed at all.
    //
    // The rule was always about WHAT THE CYCLE LANDS INTO, not how long it is.
    // Every cycle but the self-loop leaves the air by a landing link out of
    // `air_mp`, and the held-button restart route arrives ONE TICK LATER than the
    // link the file authored. `air_mp` is +7 on hit, so what is left after that
    // tick fits a light's startup and does not fit a medium's. Asserting the
    // startup is asserting the mechanism; asserting the length was asserting a
    // correlate that happened to hold.
    //
    // The startup comes from the character's own frame data rather than from the
    // sweep, so this is not the simulation checking itself.
    const MoveIndex airMpIdx = safe.character.FindMove("air_mp");
    ASSERT_NE(airMpIdx, kInvalidMove);
    const std::int32_t airMpStartup = safe.character.moves[airMpIdx].startup;

    for (const CycleResult& r : sweep.results) {
        if (r.Unescapable()) continue;

        // The move this cycle enters immediately after `air_mp`.
        const std::size_t n = r.cycle.moves.size();
        std::size_t at = n;
        for (std::size_t i = 0; i < n; ++i)
            if (r.cycle.moves[i] == airMpIdx) { at = i; break; }
        ASSERT_LT(at, n)
            << "a leaking cycle does not pass through `air_mp`, so the landing-link "
               "account this section gives does not describe it at all."
            << describe(safe.character, r);

        const MoveIndex landed = r.cycle.moves[(at + 1) % n];
        const std::int32_t landedStartup = safe.character.moves[landed].startup;
        EXPECT_GT(landedStartup, airMpStartup - 1)
            << "this cycle leaks a defender turn but lands into `"
            << safe.character.moves[landed].id << "`, whose startup of "
            << landedStartup << " should have fitted inside what is left of "
               "`air_mp`'s advantage after the one-tick restart delay -- so the "
               "one-tick account this section gives does not explain the leak."
            << describe(safe.character, r);
        // A marginal miss rather than a broken chain: the loop still lands every
        // hit on schedule, it simply hands one or two ticks back per turn.
        EXPECT_GE(r.turns, static_cast<std::size_t>(kMinTurns));
        EXPECT_LE(r.actionableTicks, r.turns * 2)
            << "the defender is getting more than two ticks per turn back, which "
               "is a gap rather than the one-tick miss described here."
            << describe(safe.character, r);
    }
    // ... and the converse, stated by the same rule rather than by its old
    // length proxy: a cycle that lands into something FAST enough to fit inside
    // what is left of `air_mp`'s advantage does not leak.
    //
    // The self-loop is excluded because it never performs a landing link at all
    // -- it is the one cycle the cancel system takes end to end, which is a
    // different mechanism and is measured in section 3.
    for (const CycleResult& r : sweep.results) {
        const std::size_t n = r.cycle.moves.size();
        if (n <= 1) continue;

        std::size_t at = n;
        for (std::size_t i = 0; i < n; ++i)
            if (r.cycle.moves[i] == airMpIdx) { at = i; break; }
        if (at == n) continue;   // counted elsewhere; section 3 owns it

        const MoveIndex landed = r.cycle.moves[(at + 1) % n];
        if (safe.character.moves[landed].startup >= airMpStartup) continue;

        EXPECT_TRUE(r.Unescapable())
            << "this cycle lands into `" << safe.character.moves[landed].id
            << "`, whose startup of " << safe.character.moves[landed].startup
            << " fits inside what is left of `air_mp`'s advantage, and it still "
               "leaked a defender turn -- so the one-tick account is incomplete "
               "in the direction that matters."
            << describe(safe.character, r);
    }

    RecordProperty("cycles_unescapable", static_cast<int>(unescapable));
    RecordProperty("cycles_escapable", static_cast<int>(escapable));
    RecordProperty("unescapable_via_cancel", static_cast<int>(viaCancel));
    RecordProperty("unescapable_via_held_button", static_cast<int>(viaButton));

    // --- the record, printed on success, because this IS the number ----------
    std::size_t endedByJuggle = 0, fullyTakeable = 0;
    for (const CycleResult& r : sweep.results) {
        if (r.juggle < 0)        ++endedByJuggle;
        if (r.everyEdgeTakeable) ++fullyTakeable;
    }

    MatchBuild probe{};
    ASSERT_TRUE(buildMirror(safe.character, {}, probe)) << probe.report[0].error;
    const BuildLoss* effects = findLoss(probe.report[0], "move.effect");
    const BuildLoss* stance  = findLoss(probe.report[0], "move.stance");
    ASSERT_NE(effects, nullptr);
    ASSERT_NE(stance, nullptr);
    // `exact` since ROADMAP M1.1b: the kernel carries these effects and applies
    // them. What still separates it from the model is that ApplyEffects CLAMPS
    // at the authored floor, so a juggle cost that cannot be paid is forgiven
    // rather than ending the combo -- the count below is unchanged for exactly
    // that reason.
    EXPECT_EQ(effects->direction, BuildLossDirection::Exact);
    EXPECT_GT(effects->count, 0)
        << "this character authors no resource effects at all, so the gap this "
           "file measures has nothing to bite on";
    EXPECT_EQ(stance->direction, BuildLossDirection::KernelPermits);
    EXPECT_GT(stance->count, 0)
        << "`air_mp` is an AIR move and the kernel is supposed to have no stance "
           "rule, which is the second, independent reason every one of these loops "
           "is performable from the ground";
    EXPECT_FALSE(probe.report[0].playsAsAnalysed)
        << "the bridge claims the kernel plays the character ProverAdapter "
           "analysed, and this test just performed " << unescapable
        << " combos that character cannot do.";

    std::cout
        << "\n[ GAP EXTENT ] every cycle of `" << safe.character.id << "`, measured\n"
        << "  the graph       " << safe.character.cancels.size()
        << " authored cancels, " << safe.verdict.deadCancels.size()
        << " dead in the model, " << safe.verdict.usableCancels << " usable\n"
        << "  simple cycles   " << sweep.authored.size()
        << " over the AUTHORED graph, " << sweep.usable.size()
        << " over the USABLE one:\n"
        << "                  " << kSelfLoops << " self-loop, " << kLengthThree
        << " of length 3, " << kLengthFour << " of length 4, " << kLengthFive
        << " of length 5 --\n"
        << "                  derived here rather than believed.\n"
        << "  ended by juggle " << endedByJuggle << " of " << sweep.usable.size()
        << ". Every cycle spends juggle strictly and none touches\n"
        << "                  meter, and `move.effect` is a "
        << BuildLossDirectionName(effects->direction) << " loss over "
        << effects->count << " move(s):\n"
        << "                  the kernel never spends juggle. `move.stance` is a "
        << BuildLossDirectionName(stance->direction) << "\n"
        << "                  loss over " << stance->count
        << " move(s), so the air moves are startable standing.\n"
        << "  performable as  " << fullyTakeable << " of " << sweep.usable.size()
        << ". The other " << (sweep.usable.size() - fullyTakeable)
        << " each contain ONE edge the\n"
        << "  the model says  kernel cannot take: a landing link out of `air_mp`, "
           "resolved to the\n"
        << "                  empty window [" << kLandingLinkEarliest << ", "
        << kLandingLinkLatest << "] because the authored delay lands past the\n"
        << "                  cancel window's close.\n"
        << "  EXECUTED        " << unescapable << " of " << sweep.usable.size()
        << " run with the defender never once actionable, and\n"
        << "                  every one of them returns to its own opening state "
           "each turn.\n"
        << "                  " << viaCancel << " through the cancel system, "
        << viaButton << " through the held-button restart\n"
        << "                  that performs the inert link anyway. The other "
        << escapable << " hand one tick\n"
        << "                  back per turn, and they are exactly the ones that land\n"
        << "                  from `air_mp` into a move too slow to fit what is left\n"
        << "                  of its advantage -- a medium, not a light. That is the\n"
        << "                  rule; the cycle LENGTH that used to stand in for it was\n"
        << "                  a coincidence of a two-aerial character.\n"
        << "  SO THE GAP IS   " << unescapable
        << " cycles wide on a character the tool certifies as\n"
        << "                  TERMINATING, and the ground-truth file measured 1 of "
           "them.\n"
        << "                  ARCHITECTURE.md D8; BuildReport::playsAsAnalysed is "
        << (probe.report[0].playsAsAnalysed ? "true" : "false") << ".\n\n";
}

// ============================================================================
// 5. FOREVER, NOT MERELY LONG
// ============================================================================

// A turn count is a number and this file's headline is a quantifier. The bridge
// between them is state repetition: if the whole tick-by-tick state over one
// period is identical to the next period's, then the same inputs meet the same
// state and the trace continues without end by induction. The defender's health
// is the one field excluded, and it is excluded because it is the one thing that
// must change for this to be a combo rather than a stalemate.
TEST(GapExtentKernel, EveryLoopReturnsToItsOwnOpeningStateExactly) {
    Subject safe{};
    bringUp(kSafe, safe);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    Sweep sweep{};
    runSweep(safe, sweep);
    ASSERT_FALSE(sweep.capHit);
    ASSERT_EQ(sweep.results.size(), kUsableCycles);

    std::size_t repeats = 0;
    for (const CycleResult& r : sweep.results) {
        if (r.stateRepeats) ++repeats;
        EXPECT_TRUE(r.stateRepeats)
            << "one turn of this cycle does not return the simulation to the state "
               "it started in, so it is a long combo rather than a loop and the "
               "word `forever` is not available for it."
            << describe(safe.character, r);
    }
    EXPECT_EQ(repeats, kUsableCycles)
        << repeats << " of " << sweep.results.size()
        << " cycles are exactly periodic in state.";

    // The escapable eight repeat too, and that is worth saying rather than
    // glossing: they are eternal loops WITH a defender turn inside each period,
    // not combos that peter out. The kernel hands the defender a tick per turn and
    // the attacker takes it straight back, every turn, for as long as the trace
    // runs.
    for (const CycleResult& r : sweep.results) {
        if (r.Unescapable()) continue;
        EXPECT_TRUE(r.stateRepeats);
        EXPECT_GT(r.actionableTicks, 0u);
    }

    RecordProperty("cycles_state_periodic", static_cast<int>(repeats));
}

// ============================================================================
// 6. THE DETECTOR -- why this file counts 33 where the ground-truth one counts 37
// ============================================================================

// Kept as a test rather than as a paragraph, because "our detector is stricter"
// is a claim about four specific cycles, and either it is measurable or it is an
// excuse. Both detectors are run over the same 41 traces and the difference is
// asserted.
//
// The four are the cycles that land from `air_mp` into `stand_mp` or `crouch_mp`
// -- 6f startup against the 5f the button-start route leaves. Their next hit
// arrives on EXACTLY the tick the defender's stun expires, so:
//
//   FreeTicks()   skips it, because the tick has a hit on it;
//   defEntered    misses it, because the defender really does start a move and
//                 ResolveHits sets their moveId back to 0 later in the same tick.
//
// Both then report a clean loop. The defender was actionable and got hit for it,
// which is a one-frame gap rather than no gap -- and in a kernel that had
// blocking, it is the frame the combo would end on.
TEST(GapExtentMethod, TheGroundTruthDetectorsMissTwelveEscapesThisOneFinds) {
    Subject safe{};
    bringUp(kSafe, safe);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    Sweep sweep{};
    runSweep(safe, sweep);
    ASSERT_FALSE(sweep.capHit);
    ASSERT_EQ(sweep.results.size(), kUsableCycles);

    std::size_t byFreeTicks = 0, byActionable = 0, disagreements = 0;
    for (const CycleResult& r : sweep.results) {
        const bool freeSaysLoop = (r.freeTicks == 0 && r.actedTicks == 0);
        if (freeSaysLoop)    ++byFreeTicks;
        if (r.Unescapable()) ++byActionable;
        if (freeSaysLoop != r.Unescapable()) {
            ++disagreements;
            // The disagreement only ever runs one way. A tick this file calls an
            // escape and the copied detectors call a loop is a real escape; the
            // reverse would mean the actionable rule was inventing them, and the
            // argument in the header would be backwards.
            EXPECT_TRUE(freeSaysLoop)
                << "the copied detectors found an escape the actionable rule did "
                   "not, so the actionable rule is the LOOSE one."
                << describe(safe.character, r);
            EXPECT_GT(r.actionableTicks, 0u);
        }
    }

    EXPECT_EQ(byFreeTicks, kUnescapableByFreeTicks)
        << "test_ground_truth.cpp's two detectors would call " << byFreeTicks
        << " of these cycles unescapable.";
    EXPECT_EQ(byActionable, kUnescapable);
    EXPECT_EQ(disagreements, kUnescapableByFreeTicks - kUnescapable)
        << "the two detectors disagree on " << disagreements
        << " cycles and this file was written against "
        << (kUnescapableByFreeTicks - kUnescapable) << ".";

    RecordProperty("unescapable_by_free_ticks", static_cast<int>(byFreeTicks));
    RecordProperty("unescapable_by_actionable", static_cast<int>(byActionable));
    RecordProperty("detector_disagreements", static_cast<int>(disagreements));
}
