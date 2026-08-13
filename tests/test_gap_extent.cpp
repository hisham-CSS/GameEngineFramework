// HOW BIG IS THE GAP? Every cycle in the character, not one of them.
//
// tests/test_ground_truth.cpp section 5 ends on a finding and a single
// measurement. The finding: `fighter_a` is TERMINATING, its ranking certificate
// says so because JUGGLE RUNS DOWN, and the kernel has no juggle -- it has no
// resources at all. The measurement: ONE cycle, `air_mp` into itself, which the
// model permits four repetitions of and the kernel performed eighteen.
//
// One cycle is an existence proof. It settles that the gap is real and settles
// nothing about its size, and the size is the number a research contribution
// actually quotes. So this file measures the whole character.
//
// ---------------------------------------------------------------------------
// THE QUESTION, AND THE FOUR THINGS IT DECOMPOSES INTO
// ---------------------------------------------------------------------------
// fighter_a's own `engine.termination_argument` claims its usable cancel graph
// holds EXACTLY 41 SIMPLE CYCLES -- "one of length 1 (air_mp into itself), eight
// of length 3, and thirty-two of length 4" -- and that all 41 terminate for one
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
//      trace per cycle. Section 4.
//
// ---------------------------------------------------------------------------
// WHAT IT MEASURED. THE HEADLINE IS THE LAST LINE
// ---------------------------------------------------------------------------
//   the graph            73 authored cancels, 5 dead in the model, 68 usable
//   simple cycles        143 over the AUTHORED graph, 41 over the USABLE one
//   the report's 41      CONFIRMED, including the 1 / 8 / 32 split by length
//   ended by juggle      41 of 41. Every cycle's total juggle effect is strictly
//                        negative and NO cycle touches meter, so juggle alone
//                        ends all of them -- and the kernel has no juggle.
//   performable as the   1 of 41. The other 40 each contain exactly one edge the
//   model describes it   kernel cannot take.
//   EXECUTED ANYWAY      33 of 41, as loops the defender never gets one tick out
//                        of, for as long as the trace runs.
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
// is +7 on hit; a light (3f, 4f) still connects with ticks to spare and a medium
// (6f, 7f) does not. So the eight cycles that leak a defender turn are EXACTLY
// the eight of length 3 -- the ones that land from `air_mp` straight into a
// medium -- and all 32 of length 4, which land into a light first, do not. The
// kernel is CONSERVATIVE there, in ProverAdapter.h's vocabulary: stricter than
// the file, on eight cycles, by one tick.
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
// if their hitstun at the end of tick t-1 was at most 1, because stepFighter
// decrements it before anything else and `actionable()` is `hitstun == 0`. It
// needs nothing the harness does not already record and it cannot be erased by a
// later step of the same tick. On this character it finds four escapes the other
// two detectors do not, and 37 becomes 33.
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
// per-tick sampling, the table and summary printers -- is COPIED from
// test_ground_truth.cpp rather than shared with it. That file is a test, not a
// library, and a header extracted from it so two tests could agree would be a
// third thing to keep true. The copies are marked, and where this file needed
// something different (the actionable detector above, the state-repetition check
// in section 5) the difference is stated where it appears.
#include <gtest/gtest.h>

#include "cse/data/CharacterData.h"
#include "cse/data/MatchBuilder.h"
#include "cse/data/ProverAdapter.h"
#include "cse/kernel/Simulate.h"

#include <algorithm>
#include <cstdint>
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
// 0. The harness. COPIED from test_ground_truth.cpp -- see the header
// ============================================================================

constexpr std::int32_t px(std::int32_t pixels) {
    return pixels * cse::kernel::kSubUnitsPerPixel;
}

// The body is the caller's number, supplied deliberately: CharacterData carries
// none, so leaving it out would make MatchBuilder's documented default look like
// the character's own measurement. 3328 and 15360 are fighter_a's own
// `engine.constants.default_pushbox_sub` restated in pixels.
constexpr std::int32_t kHalfWidth = px(13);
constexpr std::int32_t kHeight    = px(60);

// Origins 34 px apart, so the BODIES are 8 px apart. Every move any cycle below
// uses reaches at least 40 px, so no verdict in this file can turn on distance --
// which matters more here than in the file this is copied from, because 41 cycles
// use fourteen different moves and a marginal gap would silently reclassify one.
constexpr std::int32_t kP0X = -px(17);
constexpr std::int32_t kP1X =  px(17);

// The build-wide positional resource order (ADR-001 section 8 item 7).
const std::vector<std::string> kBuildResources = { "meter", "juggle" };

// A03's positional contract, used by section 2. Named rather than written as 1
// at each use, because "resource 1" is meaningless and "juggle" is the claim.
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
// this file can stall for that reason -- which matters here because a cycle is
// handed a fresh binding table and a wrong one would look like an unperformable
// cycle rather than like a harness bug.
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
// resolved windows, and a cycle that carried only one of them would have to look
// the other up by a rule this file would then have to keep true.
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
// 4096 is two orders of magnitude over what this character produces.
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
            path_.assign(1, start_);
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
// thing section 1 is enumerating over, and a second implementation of
// `startup <= hitstun - delay` in this file could drift from the one that
// produced the verdict. test_prover_adapter.cpp is where that condition is
// tested; here it is consumed.
std::vector<bool> usableEdges(const Subject& s) {
    std::vector<bool> out(s.character.cancels.size(), true);
    for (const ProverDeadCancel& dead : s.verdict.deadCancels)
        if (dead.cancel < out.size()) out[dead.cancel] = false;
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
// (:282-290) and is the reason no edge in this file needs to carry a juggle cost:
// entering `air_mp` spends the point, so every edge whose TARGET is `air_mp` pays
// it. Summing only the edges would find zero and conclude that nothing stops any
// cycle at all -- which is the mistake this function exists to not make.
std::int32_t cycleResource(const CharacterData& c, const Cycle& cycle,
                           ResourceIndex resource) {
    std::int32_t total = 0;
    for (std::size_t i = 0; i < cycle.moves.size(); ++i) {
        // The TARGET of hop i, i.e. the move the cycle enters on that hop. Summing
        // over targets rather than over `cycle.moves` is the same set of moves --
        // a cycle enters each of its moves exactly once per turn -- but it is the
        // sentence the edge semantics justify.
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
// between "32 cycles do not work" and "32 cycles do not work because of one
// window", which is the whole of this file's honesty requirement.
enum class Block : std::uint8_t {
    None,          // the kernel takes it
    EmptyWindow,   // MatchBuilder resolved earliestFrame > latestFrame: inert
    ContactGate,   // window non-empty, but it has closed by the time the source's
                   // contact is visible. StepAttack runs BEFORE ResolveHits, so a
                   // hit on tick N is first readable on N+1 and the fastest cancel
                   // is one tick after contact, never zero (Combat.h).
    NoButton,      // nothing can ask for the target: more distinct moves in the
                   // cycle than there are single-bit buttons to hand out
    TooSlow        // takeable, but the follow-up's first active frame lands after
                   // the defender's hitstun has run out
};

const char* blockName(Block b) {
    switch (b) {
        case Block::None:        return "takeable";
        case Block::EmptyWindow: return "empty window";
        case Block::ContactGate: return "contact gate";
        case Block::NoButton:    return "no button";
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
    std::int32_t earliest = 0;
    std::int32_t latest   = 0;

    Block        block = Block::None;   // why the CANCEL cannot be taken, if it cannot
    std::int32_t cancelFrame = -1;      // first source frame the cancel can fire on

    // THE SECOND ROUTE, and section 4 is why it is modelled at all. When the
    // cancel is inert the fighter can still reach the follow-up by letting the
    // source run out: StepAttack ends the move and runs the button scan in the
    // same call, so a held button starts the target on frame `MoveDuration`. That
    // is one tick later than an edge whose delay resolves to the last frame, and
    // on this character that tick is decisive.
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
    hop.earliest = ke.earliestFrame;
    hop.latest   = ke.latestFrame;

    // The tick the source's hit lands on, in the source's own frame numbering, and
    // the tick the defender is free again. Both are the kernel's arithmetic:
    // ResolveHits SETS hitstun on the frame the box overlaps -- which is `startup`,
    // the first active frame -- and stepFighter decrements it at the top of every
    // tick after, so it reaches zero at startup + hitstun.
    const std::int32_t contactFrame = src.startup;
    const std::int32_t defenderFree = contactFrame + src.hitstun;

    // The first frame the cancel could fire on. `onHit` edges must wait one tick
    // past contact for alreadyHitBits to be visible; a whiff edge need not, and
    // this character authors none, but the distinction is written out rather than
    // assumed away because a file that grew one would otherwise be misclassified.
    const std::int32_t firstCancel =
        ke.onHit != 0 ? std::max(ke.earliestFrame, contactFrame + 1) : ke.earliestFrame;

    if (ke.earliestFrame > ke.latestFrame) {
        hop.block = Block::EmptyWindow;
    } else if (firstCancel > ke.latestFrame) {
        hop.block = Block::ContactGate;
    } else if (!bound) {
        hop.block = Block::NoButton;
    } else {
        hop.cancelFrame = firstCancel;
    }

    if (hop.block == Block::None) {
        hop.viaCancel  = true;
        hop.startFrame = firstCancel;
    } else if (hop.block == Block::NoButton) {
        // Nothing can ask for the target by either route, so there is no fallback
        // to model: the button scan needs the same button the cancel does.
        hop.viaCancel  = false;
        hop.startFrame = -1;
    } else {
        hop.viaCancel  = false;
        hop.startFrame = src.startup + src.active + src.recovery;   // MoveDuration
    }

    // "Connects before the defender leaves hitstun". STRICTLY before: a follow-up
    // whose first active frame is the frame the stun expires arrives on a tick the
    // defender was already actionable on, and section 4's detector counts that as
    // an escape -- so the two halves of this file must agree on it here.
    hop.connects = hop.startFrame >= 0 &&
                   hop.startFrame + tgt.startup < defenderFree;
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
// cancel specifically would have reported them as stalls.
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

    std::uint16_t Bits() const { return buttons_.empty() ? 0 : buttons_[cursor_]; }

    void Observe(std::uint16_t attackerMove, std::uint16_t attackerFrame) {
        if (slots_.empty()) return;
        if (attackerMove != slots_[cursor_] || attackerFrame != 0) return;
        cursor_ = (cursor_ + 1 < slots_.size()) ? cursor_ + 1 : 0;
    }

    const std::vector<std::uint16_t>& Slots() const { return slots_; }
    std::size_t Length() const { return slots_.size(); }

private:
    std::vector<std::uint16_t> slots_;
    std::vector<std::uint16_t> buttons_;
    std::vector<std::string>   ids_;
    std::size_t                cursor_ = 0;
};

// Silent is the recipe the character files prescribe. MashesOnceHit starts LATE
// on purpose: at tick 0 both fighters are idle and hold the same bindings, so
// both would start the same move and ResolveHits -- deliberately symmetric --
// would land both. That is a correct simulation of two people mashing and it is
// not the question. Starting after the combo opens asks the player's question.
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

    // COPIED, and kept even though this file does not decide anything on it, so
    // that section 6 can quote what the ground-truth file's detector would have
    // said about the same run.
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
    // t-1 was at most 1: stepFighter decrements it at the top of the tick before
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

// COPIED. The per-tick table a reader needs when a cycle stops behaving, so that
// WHICH TICK and WHAT THE DEFENDER WAS DOING are readable without re-running.
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
// zero, which would make `health fell this tick` stop reporting hits and silently
// truncate every count below. 160 ticks gives every one of the 41 cycles at least
// three full turns with the defender still standing, and the sweep ASSERTS both
// halves of that so a damage change is a named failure rather than a quiet
// undercount. It is also, by coincidence worth noting, the same budget
// test_ground_truth.cpp chose for a loop 40 ticks shorter.
constexpr int kSweepTicks = 160;
constexpr int kMinTurns   = 3;

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
    std::size_t  hits    = 0;
    std::size_t  turns   = 0;
    std::int32_t period  = -1;
    bool         periodic = false;
    bool         stateRepeats = false;
    std::size_t  freeTicks       = 0;   // the copied detector
    std::size_t  actedTicks      = 0;   // the other copied detector, defender mashing
    std::size_t  actionableTicks = 0;   // THE ONE THIS FILE DECIDES ON
    std::int32_t defenderHealth  = 0;
    std::int32_t attackerHealth  = 0;

    bool Unescapable() const { return actionableTicks == 0; }
};

// Whether one full turn of the loop leaves the simulation in the state it started
// in, ignoring the defender's health.
//
// THIS IS WHAT LETS THE FILE SAY "FOREVER" RATHER THAN "FOR 160 TICKS". Counting
// turns can only ever report a number; if the tick-by-tick state over one period
// is identical to the next period's, the trace is eventually periodic and the
// induction is immediate -- the same inputs meet the same state and produce the
// same tick, without end. Health is excluded ON PURPOSE and is the only field
// excluded: it is the one thing that legitimately changes, and it is what makes
// the loop a combo rather than a stalemate.
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
        if (a.atkMove != b.atkMove || a.atkFrame != b.atkFrame ||
            a.atkHitBits != b.atkHitBits || a.defStun != b.defStun ||
            a.defMove != b.defMove || a.hit != b.hit || a.inputBits != b.inputBits)
            return false;
    }
    return true;
}

// Everything section 1 through 5 needs, computed once.
struct Sweep {
    std::vector<Cycle>       authored;
    std::vector<Cycle>       usable;
    std::vector<CycleResult> results;
    bool                     capHit = false;

    std::size_t Count(bool (*predicate)(const CycleResult&)) const {
        std::size_t n = 0;
        for (const CycleResult& r : results) if (predicate(r)) ++n;
        return n;
    }
};

std::string describe(const CharacterData& c, const CycleResult& r) {
    std::ostringstream s;
    s << "\n  cycle           " << r.label
      << "\n  model           juggle " << r.juggle << ", meter " << r.meter
      << "\n  hops";
    for (const Hop& h : r.hops) {
        s << "\n    " << std::setw(18) << std::left << c.moves[h.from].id
          << " -> " << std::setw(18) << std::left << c.moves[h.to].id << std::right
          << " window [" << h.earliest << ", " << h.latest << "]"
          << "  " << blockName(h.block)
          << "  starts on frame " << h.startFrame
          << " by " << (h.viaCancel ? "cancel" : "button")
          << (h.connects ? ", connects" : ", DOES NOT CONNECT");
    }
    s << "\n  executed        " << r.hits << " hits, " << r.turns
      << " turns, period " << r.period
      << (r.periodic ? " (periodic)" : " (NOT periodic)")
      << (r.stateRepeats ? ", state repeats" : ", state does NOT repeat")
      << "\n  defender        actionable on " << r.actionableTicks
      << " tick(s); free on " << r.freeTicks << "; started a move on "
      << r.actedTicks << "\n  health          attacker " << r.attackerHealth
      << ", defender " << r.defenderHealth << "\n";
    return s.str();
}

// Run the whole measurement. Built fresh rather than shared as a global: a
// MatchData mutated by accident in one test and read in another is the coupling
// that makes a failure impossible to localise, and 41 builds cost microseconds.
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
            continue;
        }

        // --- section 3: can the kernel take each hop? ------------------------
        r.everyEdgeTakeable = true;
        r.predictedLoop     = true;
        for (std::size_t i = 0; i < cycle.edges.size(); ++i) {
            const CancelIndex file = cycle.edges[i];
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
                ++r.blockedEdges;
                if (r.everyEdgeTakeable) r.firstBlock = hop.block;
                r.everyEdgeTakeable = false;
            }
            if (!hop.connects) r.predictedLoop = false;
            r.hops.push_back(hop);
        }

        // --- section 4: execute it -------------------------------------------
        Driver silentDriver(c, cycle, build.moves[0], bindings);
        std::string why;
        if (!silentDriver.Usable(why)) {
            // Not a failure: a cycle nothing can ask for is a MEASUREMENT, and
            // Block::NoButton above is where it is counted.
            out.results.push_back(r);
            continue;
        }
        Driver mashDriver(c, cycle, build.moves[0], bindings);

        const TickLog silent =
            drive(build.data, silentDriver, kSweepTicks, DefenderPolicy::Silent, 0);
        // The defender mashes the FIRST move of the cycle, which is bound by
        // construction. Handing them an unbound button would make "the defender
        // never acted" a fact about the binding table.
        const TickLog mashed = drive(build.data, mashDriver, kSweepTicks,
                                     DefenderPolicy::MashesOnceHit, bindings[0].button);

        const std::size_t length = cycle.moves.size();
        r.hits            = silent.Hits();
        r.turns           = r.hits / length;
        r.freeTicks       = silent.FreeTicks().size();
        r.actedTicks      = mashed.defenderActedTicks.size();
        r.actionableTicks = silent.ActionableTicks().size();
        r.defenderHealth  = silent.finalState.p[1].health;
        r.attackerHealth  = silent.finalState.p[0].health;
        r.stateRepeats    = oneTurnRepeats(silent, length);

        if (silent.hitTicks.size() > length) {
            r.period   = silent.hitTicks[length] - silent.hitTicks[0];
            r.periodic = true;
            for (std::size_t i = 0; i + length < silent.hitTicks.size(); ++i)
                if (silent.hitTicks[i + length] - silent.hitTicks[i] != r.period)
                    r.periodic = false;
        }
        out.results.push_back(r);
    }
}

// --- The measurement, in one place so no number below is written twice --------
//
// Every one of these is DERIVED by the sweep and asserted against, never used to
// compute anything. A character edit that changes the gap fails a test that
// prints both numbers.
constexpr std::size_t kAuthoredCycles = 143;  // over all 73 authored edges
constexpr std::size_t kUsableCycles   = 41;   // over the 68 the prover keeps
constexpr std::size_t kSelfLoops      = 1;    // ... of length 1
constexpr std::size_t kLengthThree    = 8;
constexpr std::size_t kLengthFour     = 32;

constexpr std::size_t kDeadCancels    = 5;
constexpr std::size_t kUsableCancels  = 68;

constexpr std::size_t kEndedByJuggle  = 41;   // of 41
constexpr std::size_t kFullyTakeable  = 1;    // of 41
constexpr std::size_t kEmptyWindowed  = 40;   // of 41

constexpr std::size_t kUnescapable    = 33;   // of 41, on the actionable detector
constexpr std::size_t kEscapable      = 8;
constexpr std::size_t kByCancelAlone  = 1;    // of the 33
constexpr std::size_t kByHeldButton   = 32;

// What the copied detectors would have said, kept so the difference is pinned.
constexpr std::size_t kUnescapableByFreeTicks = 37;

// air_mp's landing links: startup 6 + delay 15 = 21, against a cancel window that
// closes at 14. Named so a failure can quote the arithmetic rather than the
// numbers alone.
constexpr std::int32_t kLandingLinkEarliest = 21;
constexpr std::int32_t kLandingLinkLatest   = 14;

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
        << ". The character's graph grew a combinatorial explosion, and the number "
           "below is a floor rather than a count.";

    // The prover's own split, checked here because everything downstream is an
    // enumeration over the graph it defines. 68/5 is what the file's
    // `meter_is_not_load_bearing.the_counterfactual_that_checks_it` requires.
    EXPECT_EQ(safe.verdict.deadCancels.size(), kDeadCancels);
    EXPECT_EQ(static_cast<std::size_t>(safe.verdict.usableCancels), kUsableCancels);
    EXPECT_EQ(safe.character.cancels.size(), kDeadCancels + kUsableCancels);

    // THE NUMBER. fighter_a's `engine.termination_argument` says "the usable
    // cancel graph contains exactly 41 simple cycles: one of length 1 (air_mp into
    // itself), eight of length 3 ... and thirty-two of length 4". Nothing here
    // reads that claim; this is an enumeration of the loaded data, and the
    // agreement is the result.
    ASSERT_EQ(sweep.usable.size(), kUsableCycles)
        << "fighter_a's authoring report claims " << kUsableCycles
        << " simple cycles in its usable cancel graph and enumeration finds "
        << sweep.usable.size()
        << ". THE REPORT IS THE THING THAT IS WRONG unless the character changed: "
           "this count is derived from CharacterData::cancels and the prover's own "
           "dead list, and nothing in it is quoted from the file.";

    const std::vector<std::size_t> byLength = lengthHistogram(sweep.usable);
    ASSERT_GT(byLength.size(), 4u);
    EXPECT_EQ(byLength[1], kSelfLoops)     << "self-loops";
    EXPECT_EQ(byLength[3], kLengthThree)   << "cycles of length 3";
    EXPECT_EQ(byLength[4], kLengthFour)    << "cycles of length 4";
    for (std::size_t n = 0; n < byLength.size(); ++n)
        if (n != 1 && n != 3 && n != 4)
            EXPECT_EQ(byLength[n], 0u)
                << "a cycle of length " << n << " appeared, and the report's "
                   "1 / 8 / 32 structure accounts for none";

    RecordProperty("usable_cycles", static_cast<int>(sweep.usable.size()));
    RecordProperty("authored_cycles", static_cast<int>(sweep.authored.size()));
}

// The qualifier in "the USABLE cancel graph" is load-bearing, and this is what it
// is worth. The five edges the prover discards are not decoration: three of them
// close cycles of their own, and dropping them takes the count from 143 to 41.
//
// This is not a criticism of the report -- the report says "usable" and means it.
// It is the reason a reader must not quote 41 as "the number of cycles in
// fighter_a", and the reason section 3 below enumerates over the same graph the
// verdict was computed on rather than over the file.
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

    // And the dead edges really are dead for the reason the prover reports --
    // the follow-up needs more startup than the source leaves. Re-derived from
    // ProverDeadCancel's own two numbers rather than recomputed from the file, so
    // this checks the report is internally consistent rather than re-implementing
    // the link condition.
    for (const ProverDeadCancel& dead : safe.verdict.deadCancels) {
        EXPECT_GT(dead.shortfall(), 0)
            << "cancel " << dead.cancel << " is listed dead with a shortfall of "
            << dead.shortfall() << ", which is not short of anything";
        ASSERT_LT(dead.from, safe.character.moves.size());
        ASSERT_LT(dead.to, safe.character.moves.size());
    }
}

// ============================================================================
// 2. THE MODEL -- what ends each of the 41
// ============================================================================

// The premise of the whole file: every one of these cycles is stopped by juggle
// and by nothing else. If some cycle terminated for a structural reason instead,
// the kernel having no juggle would say nothing about it.
TEST(GapExtentModel, EveryCycleIsEndedByJuggleAndNoCycleTouchesMeter) {
    Subject safe{};
    bringUp(kSafe, safe);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    Sweep sweep{};
    runSweep(safe, sweep);
    ASSERT_FALSE(sweep.capHit);
    ASSERT_EQ(sweep.results.size(), kUsableCycles);

    // A03: the positional resource contract these two indices depend on.
    ASSERT_EQ(safe.character.resources.size(), kBuildResources.size());
    ASSERT_EQ(safe.character.resources[kMeter].name,  "meter");
    ASSERT_EQ(safe.character.resources[kJuggle].name, "juggle");
    EXPECT_GT(safe.character.resources[kJuggle].initial, 0);
    EXPECT_EQ(safe.character.resources[kJuggle].floor, 0)
        << "juggle's floor is what `nonNegative` refuses to cross, and the "
           "termination argument is that the budget runs INTO it";

    std::size_t endedByJuggle = 0;
    std::size_t touchMeter    = 0;
    for (const CycleResult& r : sweep.results) {
        if (r.juggle < 0) ++endedByJuggle;
        if (r.meter != 0) ++touchMeter;
        EXPECT_LT(r.juggle, 0)
            << "this cycle does not spend juggle, so nothing in the model stops "
               "it and the verdict should not have been TERMINATING."
            << describe(safe.character, r);
    }

    EXPECT_EQ(endedByJuggle, kEndedByJuggle)
        << endedByJuggle << " of " << sweep.results.size()
        << " cycles strictly decrease juggle. The file's structural proof claims "
           "ALL of them do, because every cycle contains an edge into an air move.";

    // Meter appears on no cycle at all, which is the file's own
    // `meter_is_not_load_bearing` argument -- super_beam is the only move that
    // spends it and it has zero outgoing cancels. It matters here because the
    // ranking certificate's order is [meter, juggle]: if meter DID gate a cycle,
    // "the kernel has no resources" would have two consequences to measure and
    // this file measures one.
    EXPECT_EQ(touchMeter, 0u)
        << touchMeter << " cycle(s) change meter, so the certificate's first "
           "resource is load-bearing after all and section 4 is measuring only "
           "half of the gap.";

    // The certificate names juggle. Not the argument -- the argument is the sum
    // above -- but if it did not, the character would be terminating for a reason
    // its own describe() does not print.
    ASSERT_TRUE(safe.verdict.hasRanking)
        << "fighter_a is the first character in this repository to carry a "
           "ranking certificate and it no longer does: "
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

    // GROUPED BY REASON, which is the point. One reason accounts for all 40.
    EXPECT_EQ(byEmptyWindow, kEmptyWindowed);
    EXPECT_EQ(byContactGate, 0u)
        << byContactGate << " cycle(s) are blocked because the cancel window has "
           "closed by the time contact is visible. That is a DIFFERENT defect from "
           "the empty window -- StepAttack running before ResolveHits, rather than "
           "the delay outliving the window -- and it is new.";
    EXPECT_EQ(byNoButton, 0u)
        << byNoButton << " cycle(s) name more distinct moves than there are "
           "single-bit buttons (" << kButtonPoolSize << "). That is a limit of the "
           "HARNESS, not of the character, and it means those cycles were never "
           "measured at all.";
    EXPECT_EQ(byTooSlow, 0u)
        << byTooSlow << " cycle(s) have a takeable edge whose follow-up cannot "
           "reach the defender before hitstun runs out.";

    // ... and the 40 are blocked by ONE edge each, always the same shape: a
    // landing link out of an AIR move, resolved to a window that closes before it
    // opens. Asserting the arithmetic rather than the count is what makes this a
    // finding a reader can check against Combat.h and MatchBuilder.cpp.
    std::size_t landingLinks = 0;
    for (const CycleResult& r : sweep.results) {
        if (r.everyEdgeTakeable) continue;
        EXPECT_EQ(r.blockedEdges, 1u)
            << "a cycle is blocked at " << r.blockedEdges << " edges, so the "
               "single-cause story below is wrong for it."
            << describe(safe.character, r);
        for (const Hop& h : r.hops) {
            if (h.block == Block::None) continue;
            EXPECT_EQ(h.block, Block::EmptyWindow);
            EXPECT_EQ(safe.character.moves[h.from].stance, Stance::Air)
                << "the blocked edge leaves `" << safe.character.moves[h.from].id
                << "`, which is not an air move, so the shared explanation does "
                   "not hold";
            EXPECT_EQ(h.earliest, kLandingLinkEarliest);
            EXPECT_EQ(h.latest, kLandingLinkLatest);
            ++landingLinks;
        }
    }
    EXPECT_EQ(landingLinks, kEmptyWindowed);

    // The window is empty because the two halves of MatchBuilder's resolution
    // disagree, and this is that arithmetic spelled out against the loaded file
    // rather than against the built edge -- so the two derivations have to agree.
    const MoveIndex airMp = safe.character.FindMove("air_mp");
    ASSERT_NE(airMp, kInvalidMove);
    const Move& air = safe.character.moves[airMp];
    ASSERT_TRUE(air.hasCancelWindow)
        << "air_mp authors no cancel window, so nothing would have closed the "
           "landing links and this whole section is about something else";
    EXPECT_EQ(air.cancelWindowClose, kLandingLinkLatest);
    // A LANDING LINK IS AN EDGE FROM AN AIR MOVE TO A GROUND ONE. That is what
    // the name means -- the attacker comes down and continues on the floor -- and
    // it is the predicate this loop has to use.
    //
    // It was written as `if (edge.to == airMp) continue;  // the self-cancel,
    // delay 5`, which excluded ONE of air_mp's two delay-5 edges. `air_mp` has
    // eleven outgoing edges: two at delay 5 into AIR moves (itself and `air_hk`
    // -- the juggle edges, which resolve to [11, 14] and are perfectly takeable)
    // and nine at delay 15 into GROUND moves (the landing links, which resolve to
    // [21, 14] and are inert). Skipping by identity caught the self-cancel and
    // let `air_hk` through, and the assertion below correctly refused it.
    //
    // The count is unaffected: `air_hk` is terminal, so no cycle can contain that
    // edge, and every edge this file found blocking a cycle really is a delay-15
    // landing link. The defect was in the explanation, not the measurement -- but
    // an explanation that does not hold is exactly what this section exists to
    // rule out, so it is tightened rather than excused.
    for (const CancelIndex e : safe.character.cancelsFrom[airMp]) {
        const Cancel& edge = safe.character.cancels[e];
        if (safe.character.moves[edge.to].stance == Stance::Air) continue;
        EXPECT_EQ(air.startup + edge.delay, kLandingLinkEarliest)
            << "`air_mp` -> `" << safe.character.moves[edge.to].id
            << "` resolves its delay of " << edge.delay << " against a startup of "
            << air.startup << ", and the window this file reasons about assumed "
            << kLandingLinkEarliest;
        EXPECT_GT(air.startup + edge.delay, air.cancelWindowClose)
            << "this landing link would be takeable, so it is not one of the 40";
    }

    RecordProperty("cycles_fully_takeable", static_cast<int>(fullyTakeable));
    RecordProperty("cycles_blocked_by_empty_window", static_cast<int>(byEmptyWindow));
}

// ============================================================================
// 4. THE MEASUREMENT -- what the kernel actually does with all 41
// ============================================================================

// THIS IS THE FILE'S ANSWER.
TEST(GapExtentKernel, ThirtyThreeOfTheFortyOneRunForever) {
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
    // Before any of the counts below mean anything: each cycle must have had
    // enough ticks to repeat, and the defender must still be alive, because
    // Fighter::health clamps at zero and a KO would stop `hit` being reported.
    for (const CycleResult& r : sweep.results) {
        EXPECT_GE(r.turns, static_cast<std::size_t>(kMinTurns))
            << "this cycle managed " << r.turns << " turns in " << kSweepTicks
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
        EXPECT_EQ(r.attackerHealth, kStartingHealth)
            << "the attacker took damage, so this run is a trade being read as a "
               "combo." << describe(safe.character, r);
    }

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

    // Of the 33, one is performable the way the model describes it and 32 are
    // performed by the held-button route instead. Both are the same gap and they
    // are not the same defect, so they are counted apart.
    EXPECT_EQ(viaCancel, kByCancelAlone);
    EXPECT_EQ(viaButton, kByHeldButton);

    // --- WHY the eight that leak, leak ---------------------------------------
    //
    // They are exactly the cycles of length 3, and that is not a coincidence: a
    // length-3 cycle lands from `air_mp` into a MEDIUM, and the button-start route
    // arrives one tick later than the link the file authored. A medium's startup
    // does not fit in what is left of +7; a light's does.
    for (const CycleResult& r : sweep.results) {
        if (r.Unescapable()) continue;
        EXPECT_EQ(r.cycle.moves.size(), 3u)
            << "a cycle of length " << r.cycle.moves.size()
            << " leaks a defender turn, and the one-tick explanation this file "
               "gives covers only the eight of length 3."
            << describe(safe.character, r);
        // And it is a marginal miss rather than a broken chain: the loop still
        // lands every hit, it simply gives the defender a tick back each turn.
        EXPECT_GE(r.turns, static_cast<std::size_t>(kMinTurns));
        EXPECT_LE(r.actionableTicks, r.turns * 2)
            << "the defender is getting more than two ticks per turn back, which "
               "is a gap rather than the one-tick miss described here."
            << describe(safe.character, r);
    }
    // ... and every cycle of length 4, plus the self-loop, is on the other side.
    for (const CycleResult& r : sweep.results) {
        if (r.cycle.moves.size() == 3u) continue;
        EXPECT_TRUE(r.Unescapable())
            << "a cycle that does not land into a medium still leaked a turn."
            << describe(safe.character, r);
    }

    RecordProperty("cycles_unescapable", static_cast<int>(unescapable));
    RecordProperty("cycles_escapable", static_cast<int>(escapable));
    RecordProperty("unescapable_via_cancel", static_cast<int>(viaCancel));
    RecordProperty("unescapable_via_held_button", static_cast<int>(viaButton));

    // --- the record, printed on success, because this is the number ----------
    const BuildLoss* effects = nullptr;
    const BuildLoss* stance  = nullptr;
    {
        MatchBuild probe{};
        ASSERT_TRUE(buildMirror(safe.character, {}, probe));
        effects = findLoss(probe.report[0], "move.effect");
        stance  = findLoss(probe.report[0], "move.stance");
        ASSERT_NE(effects, nullptr);
        ASSERT_NE(stance, nullptr);
        EXPECT_EQ(effects->direction, BuildLossDirection::KernelOmits);
        EXPECT_GT(effects->count, 0)
            << "the kernel is supposed to be dropping this character's resource "
               "effects and the loss table says it drops none";
        EXPECT_FALSE(probe.report[0].playsAsAnalysed)
            << "the bridge claims the kernel plays the character ProverAdapter "
               "analysed, and this test just performed " << kUnescapable
            << " combos that character cannot do.";
    }

    std::cout
        << "\n[ GAP EXTENT ] every cycle of `" << safe.character.id << "`, measured\n"
        << "  the graph       " << safe.character.cancels.size()
        << " authored cancels, " << safe.verdict.deadCancels.size()
        << " dead in the model, " << safe.verdict.usableCancels << " usable\n"
        << "  simple cycles   " << sweep.authored.size()
        << " over the AUTHORED graph, " << sweep.usable.size()
        << " over the USABLE one\n"
        << "                  (1 self-loop, 8 of length 3, 32 of length 4 -- the "
           "authoring report's\n"
        << "                  number, derived here rather than believed)\n"
        << "  ended by juggle " << kEndedByJuggle << " of " << sweep.usable.size()
        << ". Every cycle spends juggle strictly, no cycle touches meter,\n"
        << "                  and `move.effect` is a "
        << BuildLossDirectionName(effects->direction) << " loss over "
        << effects->count << " move(s):\n"
        << "                  the kernel has no juggle to spend.\n"
        << "  performable as  " << kFullyTakeable << " of " << sweep.usable.size()
        << ". The other " << kEmptyWindowed << " each contain ONE edge the kernel\n"
        << "  the model says  cannot take -- a landing link out of `air_mp`, "
           "resolved to the\n"
        << "                  empty window [" << kLandingLinkEarliest << ", "
        << kLandingLinkLatest << "] because the authored delay lands past the\n"
        << "                  cancel window's close.\n"
        << "  EXECUTED        " << unescapable << " of " << sweep.usable.size()
        << " run with the defender never once actionable, for as\n"
        << "                  long as the trace runs. " << viaCancel
        << " through the cancel system and " << viaButton << " through\n"
        << "                  the held-button restart that performs the inert link "
           "anyway.\n"
        << "                  The other " << escapable
        << " give one tick back per turn and are exactly the\n"
        << "                  eight that land from `air_mp` into a medium.\n"
        << "  so the gap is   " << unescapable
        << " cycles wide on a character the tool certifies as\n"
        << "                  TERMINATING. ARCHITECTURE.md D8, "
           "BuildReport::playsAsAnalysed false.\n\n";
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
TEST(GapExtentKernel, EveryLoopReturnsToItsOwnStartingStateExactly) {
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
    // not combos that peter out. The kernel gives the defender one tick per turn
    // and the attacker takes the turn back every time.
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
// is a claim about four specific cycles and either it is measurable or it is an
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
// which is a one-frame gap rather than no gap -- and against a kernel that had
// blocking, it is the frame the combo would end on.
TEST(GapExtentMethod, TheGroundTruthDetectorsMissFourEscapesThisOneFinds) {
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
        if (freeSaysLoop)  ++byFreeTicks;
        if (r.Unescapable()) ++byActionable;
        if (freeSaysLoop != r.Unescapable()) {
            ++disagreements;
            // The disagreement only ever runs one way. A tick this file calls an
            // escape and the copied detectors call a loop is a real escape; the
            // reverse would mean the actionable rule was inventing them.
            EXPECT_TRUE(freeSaysLoop)
                << "the copied detectors found an escape the actionable rule did "
                   "not, which means the actionable rule is the loose one and the "
                   "argument in this file's header is backwards."
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
