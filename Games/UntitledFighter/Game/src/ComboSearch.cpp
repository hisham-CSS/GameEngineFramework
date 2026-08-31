#include "cse/game/ComboSearch.h"

#include "cse/game/WitnessCursor.h"
#include "cse/kernel/Simulate.h"

// <set> and not <unordered_set>: nothing here iterates the visited set, but a
// deterministic container costs nothing and removes the question. No <chrono>,
// no <random> -- the header's determinism claim is a claim about this include
// list too.
#include <set>

namespace cse::game {
namespace {

using cse::kernel::Fighter;
using cse::kernel::GameState;
using cse::kernel::InputPair;
using cse::kernel::MatchData;

// The node key: the state's checksum with the fields an eternal loop is
// ALLOWED to change masked out.
//
//   tick        monotonic by construction; masking it is what makes a repeat
//               visible at all.
//   rng         advanced every tick and READ BY NOTHING in a combat tick
//               (Simulate.cpp says so where it advances it). If a mechanic
//               ever consumes it, this mask becomes unsound and the search
//               must learn the fact -- the comment is the tripwire.
//   healths     the one thing a combo exists to change; test_gap_extent's
//               state-repetition rule excludes exactly this, measured.
//   round fields  derived from health and the timer.
//
// EVERYTHING ELSE STAYS IN THE KEY -- positions, velocities, pushback,
// scaling, comboHits, the input latches. That is what makes a repeated key an
// INDUCTION and not a heuristic: identical bytes meet identical inputs and
// continue identically. The cost is honest too: a loop whose scaling or combo
// counter is still decaying does not repeat until those saturate, so an
// infinite may take a few hundred turns to prove -- which is ticks, and ticks
// are what the budget is for.
std::uint32_t nodeKey(const GameState& s) {
    GameState masked = s;
    masked.tick       = 0;
    masked.rng        = 0;
    masked.roundTimer = 0;
    masked.roundState = 0;
    masked.roundNumber = 0;
    masked.roundsWon[0] = masked.roundsWon[1] = 0;
    for (int i = 0; i < cse::kernel::kMaxFighters; ++i) masked.p[i].health = 0;
    return cse::kernel::Checksum(masked);
}

// The defender was ACTIONABLE at the top of this tick iff every stun clock at
// the end of the previous tick was at most 1 -- StepPhysics decrements before
// anything else reads them. The direct rule test_gap_extent measured 37 into
// 33 with; a later step of the same tick cannot erase it.
bool defenderFreeNow(const Fighter& defBefore) {
    return defBefore.hitstun <= 1 && defBefore.blockstun <= 1 &&
           defBefore.knockdown <= 1;
}

struct MacroOutcome {
    GameState     state{};        // at the connect/completion tick
    std::uint32_t ticks   = 0;    // simulated ticks this macro consumed
    bool          connected = false;   // M landed with the string still alive
    bool          moved     = false;   // a movement macro ran its full count
    bool          stringEnded = false; // the defender got a turn first
};

// Perform "ask for move M next" from `from`, the way a player performs it:
// M's button and stance hold via a one-entry WitnessCursor, the silent dummy
// on the other side. Succeeds when M CONNECTS (the defender's body is hit by
// it) with the defender never actionable since `stringLive` began; ends the
// string the tick the defender gets a turn first; gives up after
// maxMacroTicks, which for a string in progress is the same ending.
MacroOutcome performMacro(const ComboSearchRequest& req, const GameState& from,
                          std::uint16_t slot, bool stringLive) {
    MacroOutcome out;
    const int atk = req.attackerSlot;
    const int def = 1 - atk;

    const cse::kernel::FighterData& fighter = req.data->p[atk];
    const WitnessCursor cursor =
        WitnessCursor::FromSlots({slot}, 0, fighter);
    WitnessCursor::State cur{};

    const bool isMovement = WitnessCursor::IsMacro(slot);
    bool moveWasRunning = false;

    GameState s = from;
    for (std::uint32_t t = 0; t < req.maxMacroTicks; ++t) {
        const Fighter defBefore = s.p[def];
        const std::uint8_t hitBitsBefore = s.p[atk].alreadyHitBits;
        const std::int32_t healthBefore  = defBefore.health;

        if (stringLive && defenderFreeNow(defBefore)) {
            out.stringEnded = true;
            return out;
        }

        InputPair in{};
        in.p[atk].bits = cursor.Bits(cur);
        cse::kernel::Simulate(s, in, *req.data);
        ++out.ticks;
        const WitnessCursor::StepResult sr =
            cursor.Step(cur, s.p[atk].moveId, s.p[atk].moveFrame);
        cur = sr.next;

        // A movement macro succeeds by COMPLETING its count with the string
        // rules intact (ADR-013 decision 6); it scores no hit.
        if (isMovement) {
            if (sr.advanced) {
                out.state = s;
                out.moved = true;
                return out;
            }
            continue;
        }

        // CONNECTED: the defender's health fell, or the multi-hit guard gained
        // their bit (chip through a guard cannot happen here -- the dummy
        // never blocks -- but the bit is the direct reading and survives a
        // zero-damage move).
        const bool landed =
            s.p[def].health < healthBefore ||
            (s.p[atk].alreadyHitBits & ~hitBitsBefore) != 0;
        if (landed && s.p[atk].moveId == slot) {
            out.state     = s;
            out.connected = true;
            return out;
        }

        // A WHIFF IS OVER WHEN THE MOVE IS: the ask was performed, ran its
        // whole life, and connected on nothing -- waiting out the rest of
        // maxMacroTicks buys no new fact and, at midscreen where most asks
        // whiff, multiplies the search's tick bill by the slack. THE INSTANCE
        // IS THE CURSOR'S `advanced`, not a bare moveId comparison: when the
        // ask names the move ALREADY RUNNING from the parent state (a
        // self-chain), that old instance ending is not this ask failing --
        // the re-press lands from idle a tick later, and that restart is
        // exactly how the authored infinite chains.
        if (sr.advanced) moveWasRunning = true;
        if (moveWasRunning && s.p[atk].moveId != slot) break;
    }
    // Never landed inside the bound. For a live string that is the defender's
    // turn arriving eventually; either way this branch is closed.
    out.stringEnded = stringLive;
    return out;
}

struct Node {
    GameState                  state{};
    std::vector<std::uint32_t> pathKeys;   // ancestor keys, root first
    std::vector<std::uint16_t> moves;      // macro-actions performed so far
    // Hits are counted apart from `moves` since ADR-013 decision 6: the
    // witness records movement macros, the hit count does not.
    std::int32_t               hits = 0;
    std::int32_t               movementUsed = 0;         // before the first hit
    std::int32_t               stringMovementUsed = 0;   // inside the string
};

// Movement caps (ADR-013 decision 6). The caps are what keep
// TERMINATING-by-exhaustion affordable: walks multiply reachable positions,
// and an unbounded walk vocabulary makes the corner roster's state space
// larger than any budget a test should wait on -- measured twice, fighter_a
// stopped exhausting inside 20M ticks uncapped AND under a single flat cap of
// eight, because a cap spent mid-string multiplies every stun phase of every
// string depth. Split by phase instead: the APPROACH may walk eight entries
// (five 8-tick walks cross ResetMatch's gap, with slack to fine-position),
// and a LIVE STRING may walk two -- which is what the genre's microwalk IS;
// three walked links in one string is not a mechanic anyone authors. A
// verdict is therefore "within the movement vocabulary", which the fixed
// menu already made true.
constexpr std::int32_t kMaxApproachMovement = 8;
constexpr std::int32_t kMaxStringMovement   = 2;

} // namespace

ComboSearchResult RunComboSearch(const ComboSearchRequest& req) {
    ComboSearchResult r;
    if (req.data == nullptr) {
        r.note = "no MatchData; nothing to search";
        return r;
    }
    const cse::kernel::FighterData& fighter = req.data->p[req.attackerSlot];

    // Candidate macro-actions: every pressable move, slots ascending -- the
    // same fixed order every scan in the kernel uses, and the whole of what
    // makes two runs of this search byte-identical.
    std::vector<std::uint16_t> candidates;
    for (std::int32_t i = 1;
         i < fighter.moveCount && i < cse::kernel::kMaxMovesPerFighter; ++i)
        if (fighter.moves[i].button != 0)
            candidates.push_back(static_cast<std::uint16_t>(i));
    if (candidates.empty()) {
        r.verdict = ComboVerdict::Terminating;
        r.note    = "no pressable moves: every string has length zero";
        return r;
    }

    // Then the movement menu (ADR-013 decision 6), after the moves so the
    // expansion order stays "moves first" -- fixed and small, because every
    // entry multiplies the branching factor. Absolute directions: the
    // useless one dies by dedup (walking into a wall reproduces its key).
    const std::uint16_t movementMenu[] = {
        static_cast<std::uint16_t>(WitnessCursor::kMacroWalkLeft  | 1u),
        static_cast<std::uint16_t>(WitnessCursor::kMacroWalkLeft  | 2u),
        static_cast<std::uint16_t>(WitnessCursor::kMacroWalkLeft  | 4u),
        static_cast<std::uint16_t>(WitnessCursor::kMacroWalkLeft  | 8u),
        static_cast<std::uint16_t>(WitnessCursor::kMacroWalkRight | 1u),
        static_cast<std::uint16_t>(WitnessCursor::kMacroWalkRight | 2u),
        static_cast<std::uint16_t>(WitnessCursor::kMacroWalkRight | 4u),
        static_cast<std::uint16_t>(WitnessCursor::kMacroWalkRight | 8u),
        static_cast<std::uint16_t>(WitnessCursor::kMacroWait      | 1u),
        static_cast<std::uint16_t>(WitnessCursor::kMacroWait      | 2u),
        static_cast<std::uint16_t>(WitnessCursor::kMacroWait      | 4u),
    };
    for (const std::uint16_t m : movementMenu) candidates.push_back(m);

    // Depth-first with an explicit stack, expanded LAST-candidate-first so the
    // pop order walks slot 1 first. Global dedup on expanded keys is sound for
    // existence: a cycle in state space is walked round to a path-repeat by
    // the first path that enters it.
    std::set<std::uint32_t> visited;
    std::vector<Node> stack;
    {
        Node root;
        root.state = req.from;
        root.pathKeys.push_back(nodeKey(root.state));
        stack.push_back(std::move(root));
    }
    bool budgetHit = false;

    while (!stack.empty()) {
        Node node = std::move(stack.back());
        stack.pop_back();

        if (r.ticksUsed >= req.maxTicks || r.nodesExpanded >= req.maxNodes) {
            budgetHit = true;
            break;
        }
        ++r.nodesExpanded;

        for (std::size_t ci = candidates.size(); ci-- > 0;) {
            const std::uint16_t slot = candidates[ci];
            if (r.ticksUsed >= req.maxTicks) { budgetHit = true; break; }
            const bool isMovement = WitnessCursor::IsMacro(slot);

            // The string is live once a HIT has landed -- movement macros in
            // the path do not open one (ADR-013 decision 6).
            const bool stringLive = node.hits > 0;
            if (isMovement &&
                (stringLive ? node.stringMovementUsed >= kMaxStringMovement
                            : node.movementUsed >= kMaxApproachMovement))
                continue;

            // THE APPROACH CLOSES DISTANCE. Pre-string, a walk away from the
            // opponent opens a different QUESTION (a retreat opening), not a
            // longer answer to this one -- and it is what let a corner search
            // wander to midscreen and pay for both. Reading which side the
            // opponent is on is a position FACT, not a reach model; the
            // corner opening prunes itself here, because walking toward a
            // cornered defender moves nobody and dedup drops the state.
            if (isMovement && !stringLive) {
                const std::uint16_t kind =
                    static_cast<std::uint16_t>(slot & 0xFF00);
                if (kind != WitnessCursor::kMacroWait) {
                    const int def = 1 - req.attackerSlot;
                    const bool defToRight =
                        node.state.p[def].posX >
                        node.state.p[req.attackerSlot].posX;
                    const bool walksRight =
                        kind == WitnessCursor::kMacroWalkRight;
                    if (walksRight != defToRight) continue;
                }
            }
            MacroOutcome mo = performMacro(req, node.state, slot, stringLive);
            r.ticksUsed += mo.ticks;
            if (!mo.connected && !mo.moved) continue;   // branch ended

            const std::int32_t hits = node.hits + (mo.connected ? 1 : 0);
            if (hits > r.maxHits) {
                r.maxHits       = hits;
                r.longestString = node.moves;
                r.longestString.push_back(slot);
            }

            const std::uint32_t key = nodeKey(mo.state);

            // THE VERDICT -- and only inside a LIVE string. The key already
            // on this path means the same bytes (healths aside) met the same
            // rules once before and produced this very tick again: induction,
            // not resemblance. Pre-string movement happens with the defender
            // FREE, so a repeat there (a fighter walking in place at neutral)
            // proves nothing and must not: those nodes restart the chain
            // below instead of extending it.
            if (stringLive || mo.connected) {
                for (std::size_t k = 0; k < node.pathKeys.size(); ++k) {
                    if (node.pathKeys[k] != key) continue;
                    r.verdict   = ComboVerdict::Infinite;
                    r.witness   = node.moves;
                    r.witness.push_back(slot);
                    // pathKeys[k] was the state after k macro-actions, so the
                    // cycle is the macros from index k onward and the prefix
                    // is everything before it.
                    r.loopStart = k;
                    r.note = "state " + std::to_string(key) +
                             " returned after " + std::to_string(hits) +
                             " hit(s) with the defender never actionable";
                    return r;
                }
            }

            if (visited.count(key) != 0) continue;
            visited.insert(key);

            Node child;
            child.state = mo.state;
            if (hits > 0) {
                child.pathKeys = node.pathKeys;
                child.pathKeys.push_back(key);
            } else {
                // Approach territory: the induction chain begins at the
                // first state a string could begin from.
                child.pathKeys.push_back(key);
            }
            child.moves = node.moves;
            child.moves.push_back(slot);
            child.hits  = hits;
            child.movementUsed = node.movementUsed +
                                 ((isMovement && !stringLive) ? 1 : 0);
            child.stringMovementUsed = node.stringMovementUsed +
                                       ((isMovement && stringLive) ? 1 : 0);
            stack.push_back(std::move(child));
        }
        if (budgetHit) break;
    }

    if (budgetHit || !stack.empty()) {
        r.verdict = ComboVerdict::Unresolved;
        r.note = "budget exhausted after " + std::to_string(r.ticksUsed) +
                 " tick(s) and " + std::to_string(r.nodesExpanded) +
                 " node(s); longest string so far " + std::to_string(r.maxHits) +
                 " hit(s). Not a verdict.";
        return r;
    }

    r.verdict = ComboVerdict::Terminating;
    r.note = "every string ends with the defender free; longest measured " +
             std::to_string(r.maxHits) + " hit(s) over " +
             std::to_string(r.nodesExpanded) + " node(s) and " +
             std::to_string(r.ticksUsed) + " tick(s)";
    return r;
}

} // namespace cse::game
