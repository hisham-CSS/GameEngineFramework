// Projecting a loaded character onto the decision procedure.
//
// This file does three things and they are worth naming separately, because
// only the first is obvious and the other two are where the correctness is.
//
//   (1) It copies CharacterData into comboprover::Character. About sixty lines,
//       and it is the least interesting part of the file.
//   (2) It re-derives, on THIS character's own data, which of the projection's
//       losses are live and which way each one errs. Not from the ADR: the ADR
//       says the projectile contact frame is inert across the corpus, and this
//       file checks that claim against the file in front of it rather than
//       quoting it, because the corpus is three characters and a designer's
//       character is not in it.
//   (3) It owns the resource ceiling, which comboprover.hpp does not have.
//
// ---------------------------------------------------------------------------
// THE LOSS TABLE, AND WHICH DIRECTION EACH LOSS ERRS
// ---------------------------------------------------------------------------
//
// ARCHITECTURE.md section 5.2 has the table; this is that table with the
// direction argued for each row. PERMISSIVE means the tool can invent an
// infinite that is not real (safe). CONSERVATIVE means it can hide a real one
// (a soundness bug, and it is called one in the code and in the API).
//
//   startup/active/recovery/hitstun/damage/effect/guard   EXACT (a copy)
//   from/to/delay/effect/guard on a cancel                EXACT (a copy)
//   Contact::Hit, Contact::Always  -> onHit = true        EXACT for this
//       question. An `always` edge is available on hit, which is the only case
//       a combo can be in. What is lost is the ability to ask a DIFFERENT
//       question ("what do I get on block"), and this projection does not ask
//       it.
//   Contact::Block, Contact::Whiff -> onHit = false       EXACT. A blocked or
//       whiffed move did not connect, so the edge cannot continue a combo. The
//       edges are dropped and the count is reported so the designer can see
//       that part of their cancel table was not considered.
//   Cancel::certain == false, edges INCLUDED               PERMISSIVE. 200 of
//       the corpus's 247 edges are gated on a runtime condition no importer can
//       evaluate. Including them assumes every condition can be met. The
//       opposite policy -- drop them -- is CONSERVATIVE and would hide real
//       infinites, so this adapter does not offer it as an option. A switch
//       nobody can flip is a soundness bug nobody can introduce.
//   Move::hitConditionProse                               PERMISSIVE. ADR-001
//       section 4 group G, "the finding": every one of Kung Fu Girl's 17
//       normals only connects against a defender who has not already been
//       juggled, and the prover has no defender state at all. The gate is
//       assumed to pass, so the model can chain moves the game would not.
//   stance                                                PERMISSIVE. An air
//       move may follow a ground move with nothing in the way.
//   engine.cancel_window_ticks                            PERMISSIVE. The
//       window's CLOSE is dropped; the prover's delay is its open edge only.
//   several HitDefs collapsed to one record               PERMISSIVE for the
//       verdict: the combo's hit count is understated, so hitstun decay is
//       applied less often than it should be and more cancels stay usable.
//       (It also understates maxDamage, which is a reporting error, not a
//       verdict error.)
//   resource `floor` > 0                                  PERMISSIVE.
//       comboprover.hpp:264-269 hardcodes floor 0, so the model lets a resource
//       run below a floor the game enforces.
//   resource `floor` < 0                                  CONSERVATIVE, and the
//       one direction the hardcoded zero can go wrong. The model would forbid
//       states the game allows. No shipped character does this; the check below
//       fires if one ever does.
//   resource `ceiling`                                    PERMISSIVE. See the
//       lemma below. The adapter clamps where it can and reports where it
//       cannot.
//   reach / pushback / walk_speed / gap_actions           EXACT FOR THE CORNER
//       QUESTION, which is the only question this procedure answers
//       (comboprover.hpp:15-24). Not an approximation of the midscreen answer:
//       a different question, whose answer legitimately differs -- Kung Fu Man
//       is TERMINATING midscreen and INFINITE in the corner and both are right.
//       The one thing that would break this is a gap action with a RESOURCE
//       effect rather than a space effect; dropping that would be CONSERVATIVE,
//       so it is checked below rather than assumed.
//   startup of a move whose file declines to state a reach  CONSERVATIVE on its
//       OUTGOING edges. See the projectile section below; this is ADR-001's one
//       recorded conservative case and it is re-derived here, not cited.
//   decay kind `table`                                    POSSIBLY CONSERVATIVE.
//       Our table is integer permille; the prover's is float, and it computes
//       int(base * ratio). 900/1000 is not representable in binary, so a
//       hitstun of 10 can come out as 8 instead of 9 -- one frame, which
//       ADR-001 notes is the entire difference between TERMINATING and
//       INFINITE. Checked exhaustively below against every base hitstun in the
//       file, rather than hoped for.
//
// ---------------------------------------------------------------------------
// THE CEILING, AND THE LEMMA THAT SAYS WHICH WAY ITS ABSENCE ERRS
// ---------------------------------------------------------------------------
//
// comboprover::Character carries no ceilings (:148-174) and nonNegative
// (:264-269) hardcodes a floor of zero, while model.py:192 has ceilings and
// termination.py:200,245,252 clamps against them at every step. ARCHITECTURE.md
// section 5.2 calls the difference unsoundness. It is a real difference -- the
// editor and the offline run genuinely search different state spaces -- but the
// direction is worth establishing rather than assuming, because it decides
// whether an unclamped run is dangerous or merely imprecise:
//
//   LEMMA. Write u_i for the unclamped resource vector after i steps of some
//   run and c_i for the clamped one along the same edges, and d_i = u_i - c_i.
//   d_0 = 0 (both start from the clamped initial vector). For any step with
//   effect e: u_{i+1} = u_i + e and c_{i+1} = min(C, c_i + e) <= c_i + e, so
//   d_{i+1} = u_i + e - c_{i+1} >= u_i + e - (c_i + e) = d_i. So d is
//   componentwise NON-DECREASING along the run.
//
//   COROLLARY 1. u_i >= c_i everywhere, guards are `>=` only (meetsGuard,
//   :256-262) and the floor test is `>= 0`, so every step legal in the clamped
//   run is legal in the unclamped one. The unclamped system simulates the
//   clamped one.
//
//   COROLLARY 2. The self-covering test is `later >= earlier` (covers,
//   :274-280). If the clamped run covers -- c_late >= c_early -- then, because
//   d_late >= d_early, u_late = c_late + d_late >= c_early + d_early = u_early.
//   So every clamped infinite is an unclamped infinite.
//
// Therefore running without the ceiling can only ADD infinites, never remove
// them: PERMISSIVE, which is the safe direction. ARCHITECTURE.md's "otherwise
// the in-engine verdict is unsound" is right that the two implementations
// disagree and wrong that the engine can miss an infinite; ADR-001 section 5's
// reason for rejecting the `meter_room` encoding -- that a hard wall FORBIDS a
// gain instead of saturating it and so under-approximates the attacker -- is
// exactly this argument run the other way, and it is the argument that matters.
//
// One honest caveat on the corollaries: they are about the transition system,
// not about comboprover's search of it. That search marks a configuration Black
// once expanded (:527-535) and never reconsiders it under a different set of
// ancestors, which is an incompleteness of the algorithm as written. It applies
// equally with and without ceilings and this adapter does not fix it.
//
// SO WHAT THE ADAPTER ACTUALLY DOES ABOUT CEILINGS, in ADR-001 section 8 item
// 4's words, "clamps in its own loop":
//
//   * The initial vector is clamped into [floor, ceiling] before it is handed
//     over -- a loop this file owns.
//   * Whether the clamp can ever bind at all is DECIDED, not assumed: if no
//     usable edge between two reachable moves adds to a ceilinged resource,
//     no reachable configuration can exceed its ceiling and the projection is
//     exact. That is the whole answer for a spend-only character.
//   * When it can bind, and the verdict is INFINITE, the reported loop is
//     replayed through a clamped loop this file owns. An infinite that survives
//     the replay is real under the character's declared ceilings. One that does
//     not is reported as such -- the verdict stands, because a failed replay
//     does not license TERMINATING, but the panel can say the loop could not be
//     reproduced once meter was capped.
//
// An exact clamp inside the search is NOT expressible in the prover's own
// vocabulary and that is a fact about the vocabulary, not a missing idea:
// saturation needs `min`, effects are constants, and guards can only say
// `resource >= n`. Encoding it would need one edge per attainable value of the
// resource. The clamp belongs inside analyse(); until it is there, this file is
// where it lives.
#include "cse/data/ProverAdapter.h"

#include "comboprover.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace cse::data {
namespace {

using comboprover::ResourceVec;

std::string toString(std::int64_t v) { return std::to_string(v); }

// Round half away from zero, the same rule CharacterData.cpp's loader uses and
// for the same reason: a rounding rule that is "whatever the standard library
// does" is a divergence waiting for a platform.
std::int32_t hundredthsOf(float damage) {
    const double scaled = static_cast<double>(damage) * 100.0;
    if (!(scaled > -2147483649.0 && scaled < 2147483649.0)) return 0;
    return static_cast<std::int32_t>(scaled >= 0.0 ? scaled + 0.5 : scaled - 0.5);
}

// A sparse resource map becomes the dense positional vector the prover indexes.
// `ok` goes false on an index the character does not declare -- which cannot
// come out of the loader, but CAN come out of a CharacterData assembled in
// memory, and silently dropping the amount is precisely how index 0 stops
// meaning what everyone believes it means.
ResourceVec dense(const std::vector<ResourceAmount>& sparse, std::size_t n, bool& ok) {
    ResourceVec out(n, 0);
    for (const ResourceAmount& a : sparse) {
        if (static_cast<std::size_t>(a.resource) >= n) { ok = false; continue; }
        out[a.resource] += a.value;
    }
    return out;
}

comboprover::Decay projectDecay(const Decay& d) {
    comboprover::Decay out;
    out.floorFrames = d.floor;
    switch (d.kind) {
        case DecayKind::None:
            out.kind = comboprover::Decay::Kind::None;
            break;
        case DecayKind::Linear:
            // Both implementations compute max(floor, base - step*n) in pure
            // integers (model.py:260, comboprover.hpp:81), so this row is exact
            // and ADR-001 section 8 item 3 closes D8's hitstun gap outright.
            out.kind = comboprover::Decay::Kind::Linear;
            out.step = d.step;
            break;
        case DecayKind::Table:
            out.kind = comboprover::Decay::Kind::Table;
            out.table.reserve(d.tablePermille.size());
            for (std::int32_t permille : d.tablePermille)
                out.table.push_back(static_cast<float>(permille) / 1000.0f);
            break;
    }
    // There is deliberately no Multiplicative case: DecayKind has no such
    // member and assertion A02 rejects a file that authors one, because the
    // prover computes it as repeated float multiply then truncate and an
    // integer kernel reproduces neither implementation.
    return out;
}

// The link condition, evaluated exactly as comboprover.hpp:336-347 evaluates it,
// so that the adapter's own dead-cancel list and the prover's cannot drift.
bool edgeUsable(const comboprover::Character& c, const comboprover::Decay& decay,
                int settling, std::size_t edgeIndex,
                std::int32_t& advantageOut, std::int32_t& startupOut) {
    const comboprover::Cancel& edge = c.cancels[edgeIndex];
    const int settledStun = decay.hitstun(c.moves[edge.from].hitstun, settling);
    advantageOut = settledStun - edge.delay;
    startupOut   = c.moves[edge.to].startup;
    return edge.onHit && startupOut <= advantageOut;
}

struct Bound {
    bool         hasCeiling = false;
    std::int32_t ceiling    = 0;
    std::int32_t floor      = 0;
};

// The clamp itself. One function, used for the initial vector and for every
// step of the replay, so there is exactly one place in this repository where a
// resource ceiling is applied.
void clampInto(ResourceVec& v, const std::vector<Bound>& bounds) {
    const std::size_t n = std::min(v.size(), bounds.size());
    for (std::size_t i = 0; i < n; ++i) {
        if (v[i] < bounds[i].floor) v[i] = bounds[i].floor;
        if (bounds[i].hasCeiling && v[i] > bounds[i].ceiling) v[i] = bounds[i].ceiling;
    }
}

bool withinFloors(const ResourceVec& v, const std::vector<Bound>& bounds) {
    const std::size_t n = std::min(v.size(), bounds.size());
    for (std::size_t i = 0; i < n; ++i)
        if (v[i] < bounds[i].floor) return false;
    return true;
}

bool coversVec(const ResourceVec& later, const ResourceVec& earlier) {
    const std::size_t n = std::min(later.size(), earlier.size());
    for (std::size_t i = 0; i < n; ++i)
        if (later[i] < earlier[i]) return false;
    return true;
}

void addLoss(ProverResult& out, const char* field, LossDirection dir,
             std::int32_t count, std::string note) {
    ProjectionLoss loss;
    loss.field     = field;
    loss.direction = dir;
    loss.count     = count;
    loss.note      = std::move(note);
    out.losses.push_back(std::move(loss));
    if (dir == LossDirection::Conservative && count > 0) out.soundnessAlarm = true;
}

} // namespace

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------

const char* ProverStatusName(ProverStatus status) {
    switch (status) {
        case ProverStatus::Terminating: return "TERMINATING";
        case ProverStatus::Infinite:    return "INFINITE COMBO";
        case ProverStatus::Unknown:     return "UNRESOLVED";
    }
    return "UNRESOLVED";
}

const char* ProverOpeningName(ProverOpening opening) {
    switch (opening) {
        case ProverOpening::Neutral: return "neutral";
        case ProverOpening::Counter: return "counter";
        case ProverOpening::Air:     return "air";
    }
    return "neutral";
}

const char* RankingAbsenceName(RankingAbsence absence) {
    switch (absence) {
        case RankingAbsence::Present:                 return "present";
        case RankingAbsence::NoResources:             return "the character declares no resources";
        case RankingAbsence::CharacterGainsAResource: return "the character gains a resource on hit";
        case RankingAbsence::NothingLoops:            return "nothing loops at all";
        case RankingAbsence::NoDescendingOrder:       return "no resource runs down across every edge";
    }
    return "present";
}

const char* LossDirectionName(LossDirection direction) {
    switch (direction) {
        case LossDirection::Exact:        return "exact";
        case LossDirection::Permissive:   return "permissive (can invent an infinite)";
        case LossDirection::Conservative: return "CONSERVATIVE (can hide an infinite)";
    }
    return "exact";
}

// ---------------------------------------------------------------------------
// The analysis
// ---------------------------------------------------------------------------

namespace {

// THE ONE PLACE AN OPENING TOUCHES THE PROJECTION (ADR-015 option 3). The
// model reads one hitstun per move; the opening decides which authored number
// that is, and everything downstream -- the settling index, the usable-edge
// graph, the dead-cancel lists, the verdict -- moves with it.
std::int32_t hitstunForOpening(const Move& m, ProverOpening opening) {
    switch (opening) {
        case ProverOpening::Neutral:
            return m.hitstun;
        case ProverOpening::Counter:
            // The transform exists, the field does not: M1.3(c) adds the
            // authored `counter_hit` bonus on this line, and until then a
            // counter opening restates neutral -- the identity
            // ProverOpenings.ACounterOpeningWithNothingAuthoredIsIdenticalToNeutral
            // pins, so semantics nobody authored cannot leak in early.
            return m.hitstun;
        case ProverOpening::Air:
            // The file's own air number, falling back to ground where none is
            // authored (zero is the loaded default; a move with no air number
            // stuns airborne defenders exactly as it stuns grounded ones,
            // which is ADR-011's off-by-default at the model layer).
            return m.airHitstunTicks != 0 ? m.airHitstunTicks : m.hitstun;
    }
    return m.hitstun;
}

} // namespace

// The single-opening analysis -- the whole pipeline from validation through
// the loss table, exactly as it ran when there was only one opening to ask
// about. Static because the public AnalyseCharacter below runs it once per
// opening; the ONLY line that reads `opening` is the projection's hitstun.
static bool analyseForOpening(const CharacterData&  character,
                              const ProverOptions&  options,
                              ProverOpening         opening,
                              ProverResult&         out,
                              ProverReport&         report) {
    out    = ProverResult{};
    report = ProverReport{};

    out.opening   = opening;
    out.fileStage = character.stage;
    out.stage     = ProverStage::Corner;

    if (character.moves.empty()) {
        report.error = character.id + ": the character declares no moves, so there is "
                                      "nothing to decide";
        return false;
    }
    if (character.moves.size() > 0x7FFFu || character.cancels.size() > 0xFFFFu) {
        report.error = character.id + ": move or cancel count exceeds the handle width";
        return false;
    }

    const std::size_t resCount   = character.resources.size();
    const std::size_t moveCount  = character.moves.size();

    // --- A03, at the seam ----------------------------------------------------
    //
    // The order of the resource list is a build-wide contract, not a per-file
    // choice, because comboprover keys its ResourceVec by INDEX (:56, :128-129,
    // :152) and nothing anywhere names a resource. The loader checks this too;
    // it is checked again here because a CharacterData built in memory never met
    // the loader, and the failure it protects against does not announce itself.
    if (options.expectedResources.empty()) {
        report.warnings.push_back(character.id +
            ": A03 was not checked at the prover seam: the caller supplied no expected "
            "resource order, so nothing verifies that index 0 means the same resource here "
            "as in every other character this build analyses");
    } else {
        bool same = options.expectedResources.size() == resCount;
        for (std::size_t i = 0; same && i < resCount; ++i)
            same = options.expectedResources[i] == character.resources[i].name;
        if (!same) {
            std::string got, want;
            for (std::size_t i = 0; i < resCount; ++i)
                got += (i ? ", " : "") + character.resources[i].name;
            for (std::size_t i = 0; i < options.expectedResources.size(); ++i)
                want += (i ? ", " : "") + options.expectedResources[i];
            report.rule  = "A03";
            report.error = character.id + ": A03: resource order is [" + got +
                           "] but this build declares [" + want + "]. Resources are keyed "
                           "POSITIONALLY, so a mismatch silently compares one resource "
                           "against another and the verdict is meaningless rather than wrong";
            return false;
        }
    }

    // --- (1) the copy --------------------------------------------------------

    comboprover::Character c;
    c.name = character.name.empty() ? character.id : character.name;

    std::vector<Bound> bounds(resCount);
    c.resourceNames.reserve(resCount);
    c.initial.reserve(resCount);
    for (std::size_t i = 0; i < resCount; ++i) {
        const ResourceDef& r = character.resources[i];
        c.resourceNames.push_back(r.name);
        bounds[i].hasCeiling = r.hasCeiling;
        bounds[i].ceiling    = r.ceiling;
        bounds[i].floor      = r.floor;
        c.initial.push_back(r.initial);
    }
    // The adapter's own clamp, applied to the one vector it can reach.
    clampInto(c.initial, bounds);

    c.decay = projectDecay(character.decay);
    c.scaling.reserve(character.scalingPermille.size());
    for (std::int32_t permille : character.scalingPermille)
        c.scaling.push_back(static_cast<float>(permille) / 1000.0f);

    bool indicesOk = true;
    c.moves.reserve(moveCount);
    for (const Move& m : character.moves) {
        comboprover::Move pm;
        pm.id       = m.id;
        pm.startup  = m.startup;
        pm.active   = m.active;
        pm.recovery = m.recovery;
        pm.hitstun  = hitstunForOpening(m, opening);
        pm.damage   = static_cast<float>(m.damageHundredths) / 100.0f;
        pm.effect   = dense(m.effect, resCount, indicesOk);
        pm.guard    = dense(m.guard,  resCount, indicesOk);
        c.moves.push_back(std::move(pm));
    }

    std::int32_t droppedByContact = 0;
    std::int32_t uncertainEdges   = 0;
    c.cancels.reserve(character.cancels.size());
    for (const Cancel& e : character.cancels) {
        if (e.from >= moveCount || e.to >= moveCount) {
            report.error = character.id + ": a cancel names a move index outside the move "
                                          "table; the character was not built by the loader "
                                          "or RebuildIndices was not called";
            return false;
        }
        comboprover::Cancel pe;
        pe.from   = static_cast<int>(e.from);
        pe.to     = static_cast<int>(e.to);
        pe.delay  = e.delay;
        // {Hit, Always} -> true, {Block, Whiff} -> false. A blocked or whiffed
        // move did not connect, so its edges cannot continue a combo.
        pe.onHit  = (e.on == Contact::Hit || e.on == Contact::Always);
        pe.effect = dense(e.effect, resCount, indicesOk);
        pe.guard  = dense(e.guard,  resCount, indicesOk);
        if (!pe.onHit)  ++droppedByContact;
        if (!e.certain) ++uncertainEdges;
        c.cancels.push_back(std::move(pe));
    }

    if (!indicesOk) {
        report.error = character.id + ": an effect or guard names a resource index the "
                                      "character does not declare. The amount would be "
                                      "silently dropped and the verdict would be answering "
                                      "a different character";
        return false;
    }

    // An empty starter list is not an omission: comboprover.hpp:350-354 reads it
    // as "any move may open a combo", which is the permissive reading and the
    // right default for a character mid-authoring.
    c.starters.reserve(character.starters.size());
    for (MoveIndex s : character.starters) {
        if (s >= moveCount) {
            report.error = character.id + ": a starter names a move index outside the move table";
            return false;
        }
        c.starters.push_back(static_cast<int>(s));
    }

    // Belt and braces on the contract that motivates all of this. If these ever
    // disagree the adapter has reordered the vector it was handed.
    for (std::size_t i = 0; i < resCount; ++i) {
        if (c.resourceNames[i] != character.resources[i].name) {
            report.error = character.id + ": internal: the projected resource order does not "
                                          "match the character's own";
            return false;
        }
    }

    // --- (2) which losses are live, on THIS character ------------------------

    // The settled hitstun and the usable-edge set, computed the way the prover
    // computes them so the two lists cannot drift apart.
    std::vector<int> bases;
    bases.reserve(moveCount);
    for (const comboprover::Move& m : c.moves) bases.push_back(m.hitstun);
    const int settling = c.decay.settlingIndex(bases);

    const comboprover::Decay noDecay;   // kind None, floor 0

    std::vector<std::vector<std::size_t>> usableFrom(moveCount);
    for (std::size_t i = 0; i < c.cancels.size(); ++i) {
        std::int32_t advantage = 0, startup = 0;
        if (edgeUsable(c, c.decay, settling, i, advantage, startup))
            usableFrom[c.cancels[i].from].push_back(i);
    }

    // The projectile contact frame. ADR-001 section 4 group F: a fireball's
    // frame of contact is a function of the distance it travels, so `startup`
    // means only "released on frame 15". The file records this by declining to
    // author a reach at all -- kNoReach -- which is why Kung Fu Man's two
    // projectiles and only those two are found here.
    //
    // kNoReach is a PROXY. `engine.projectile` is deliberately not loaded
    // (CharacterData.h's closing note), so the signal this check has is the
    // absent reach, and it flags any move whose reach the file left out for any
    // reason. That over-reports, which for an alarm about a CONSERVATIVE loss is
    // the safe direction: a designer is told to look at a move that turns out to
    // be fine. It also means a hand-built CharacterData that never sets
    // reachSub is, as far as this check can tell, made entirely of fireballs.
    //
    // The two halves point in opposite directions and only one of them matters:
    //
    //   INCOMING (X -> fireball). The link test asks whether the fireball's
    //   startup fits inside X's remaining hitstun. Real contact is later than
    //   release, so a startup that means "release" understates how long the
    //   follow-up takes: the model allows links the game does not. PERMISSIVE.
    //
    //   OUTGOING (fireball -> Y). A cancel's delay is counted from the source
    //   CONNECTING (comboprover.hpp:136-138). Transcribed from release, the
    //   delay is larger than the truth by the travel time, so the attacker is
    //   credited with LESS advantage than they have and real cancels are
    //   reported dead. CONSERVATIVE -- it can hide an infinite.
    //
    // ADR-001 records that the conservative half is inert across the corpus
    // because those moves have no outgoing cancels. That is a fact about three
    // files, not about the schema, so it is re-derived here on whatever
    // character is in front of us: the count below IS the number of outgoing
    // usable edges from a reach-less move, and if a designer ever authors one,
    // soundnessAlarm goes up and the panel says so.
    std::int32_t contactFrameOutgoing = 0;
    std::int32_t contactFrameIncoming = 0;
    std::int32_t contactFrameMoves    = 0;
    for (std::size_t m = 0; m < moveCount; ++m) {
        if (character.moves[m].reachSub != kNoReach) continue;
        ++contactFrameMoves;
        contactFrameOutgoing += static_cast<std::int32_t>(usableFrom[m].size());
    }
    for (std::size_t i = 0; i < c.cancels.size(); ++i) {
        std::int32_t advantage = 0, startup = 0;
        if (!edgeUsable(c, c.decay, settling, i, advantage, startup)) continue;
        if (character.moves[c.cancels[i].to].reachSub == kNoReach) ++contactFrameIncoming;
    }

    // Table decay, checked rather than trusted. Our table is integer permille;
    // the prover's is float and it computes int(base * ratio), so a ratio like
    // 900/1000 -- not representable in binary -- can truncate one frame low.
    // ADR-001 section 5, D8: one frame of hitstun is the whole difference
    // between TERMINATING and INFINITE.
    std::int32_t decayUnfaithfulLow  = 0;
    std::int32_t decayUnfaithfulHigh = 0;
    if (character.decay.kind == DecayKind::Table) {
        for (std::size_t hitIndex = 0; hitIndex < character.decay.tablePermille.size(); ++hitIndex) {
            const std::int64_t permille = character.decay.tablePermille[hitIndex];
            for (const Move& m : character.moves) {
                // The bases THIS opening's verdict used, not the neutral ones:
                // an air run whose faithfulness check quoted ground hitstun
                // would be checking numbers its own search never touched.
                const std::int32_t base = hitstunForOpening(m, opening);
                const std::int64_t exact =
                    std::max<std::int64_t>(character.decay.floor,
                                           (static_cast<std::int64_t>(base) * permille) / 1000);
                const std::int64_t viaFloat =
                    c.decay.hitstun(base, static_cast<int>(hitIndex));
                if (viaFloat < exact) ++decayUnfaithfulLow;
                if (viaFloat > exact) ++decayUnfaithfulHigh;
            }
        }
    }

    // Gap actions are dropped, which is exact for the corner question only
    // because every gap action in the corpus spends SPACE and space is not part
    // of the corner model. A gap action that moved a RESOURCE would be a way of
    // paying for a follow-up that this projection cannot see, and dropping it
    // would be conservative.
    std::int32_t gapResourceEffects = 0;
    for (const GapAction& g : character.gapActions) {
        if (!g.enabled) continue;
        for (const ResourceAmount& a : g.effect)
            if (a.value != 0) ++gapResourceEffects;
    }

    std::int32_t floorsAbove = 0, floorsBelow = 0;
    for (const ResourceDef& r : character.resources) {
        if (r.floor > 0) ++floorsAbove;
        if (r.floor < 0) ++floorsBelow;
    }

    std::int32_t stanced = 0, windowed = 0, defenderGated = 0, multiHit = 0;
    for (const Move& m : character.moves) {
        if (m.stance != Stance::Any)       ++stanced;
        if (m.hasCancelWindow)             ++windowed;
        if (!m.hitConditionProse.empty())  ++defenderGated;
        if (m.hits.size() > 1)             ++multiHit;
    }

    // --- (3) run it ----------------------------------------------------------

    const comboprover::Result r =
        comboprover::analyse(c, options.limit <= 0 ? 200000 : options.limit);

    switch (r.status) {
        case comboprover::Status::Terminating: out.status = ProverStatus::Terminating; break;
        case comboprover::Status::Infinite:    out.status = ProverStatus::Infinite;    break;
        case comboprover::Status::Unknown:     out.status = ProverStatus::Unknown;     break;
    }
    out.reason        = r.reason;
    out.settlingIndex = r.settlingIndex;
    out.usableCancels = r.usableCancels;
    out.explored      = r.explored;
    out.capped        = r.capped;
    out.spendOnly     = r.spendOnly;
    out.hasRanking    = r.hasRanking;
    out.maxHits       = r.maxHits;
    out.maxFrames     = r.maxFrames;
    out.maxDamageHundredths = hundredthsOf(r.maxDamage);

    for (int resource : r.rankingOrder)
        out.rankingOrder.push_back(static_cast<ResourceIndex>(resource));
    for (int move : r.unreachableMoves)
        out.unreachableMoves.push_back(static_cast<MoveIndex>(move));
    for (int move : r.prefix) out.prefix.push_back(static_cast<MoveIndex>(move));
    for (int move : r.loop)   out.loop.push_back(static_cast<MoveIndex>(move));

    for (const comboprover::DeadCancel& dead : r.deadCancels) {
        ProverDeadCancel d;
        d.cancel    = static_cast<CancelIndex>(dead.cancelIndex);
        d.from      = character.cancels[dead.cancelIndex].from;
        d.to        = character.cancels[dead.cancelIndex].to;
        d.advantage = dead.advantage;
        d.startup   = dead.startup;
        out.deadCancels.push_back(d);
    }

    // The pre-decay list. A dead-cancel list computed at the settled hitstun
    // blames the designer for edges the decay curve killed, and ADR-001 measured
    // that at 128 of Kung Fu Girl's 134 under the project's own draft rule.
    {
        const int settlingNone = noDecay.settlingIndex(bases);
        for (std::size_t i = 0; i < c.cancels.size(); ++i) {
            std::int32_t advantage = 0, startup = 0;
            if (edgeUsable(c, noDecay, settlingNone, i, advantage, startup)) continue;
            if (!c.cancels[i].onHit) continue;   // dropped by contact, not by frames
            ProverDeadCancel d;
            d.cancel    = static_cast<CancelIndex>(i);
            d.from      = character.cancels[i].from;
            d.to        = character.cancels[i].to;
            d.advantage = advantage;
            d.startup   = startup;
            out.deadCancelsPreDecay.push_back(d);
        }
    }

    // Why there is no ranking certificate. Four distinguishable causes, and
    // ADR-001 section 3 gate 3 is the reason they are not collapsed: two of the
    // three Phase-0 characters end up here, for two different reasons, and
    // NothingLoops is the strongest termination result the tool can produce.
    if (out.hasRanking) {
        out.rankingAbsence = RankingAbsence::Present;
    } else if (resCount == 0) {
        out.rankingAbsence = RankingAbsence::NoResources;
    } else if (!r.spendOnly) {
        out.rankingAbsence = RankingAbsence::CharacterGainsAResource;
    } else {
        std::vector<bool> reachable(moveCount, true);
        for (MoveIndex m : out.unreachableMoves)
            if (m < moveCount) reachable[m] = false;
        bool anyLiveEdge = false;
        for (std::size_t m = 0; m < moveCount && !anyLiveEdge; ++m) {
            if (!reachable[m]) continue;
            for (std::size_t edge : usableFrom[m])
                if (reachable[c.cancels[edge].to]) { anyLiveEdge = true; break; }
        }
        out.rankingAbsence = anyLiveEdge ? RankingAbsence::NoDescendingOrder
                                         : RankingAbsence::NothingLoops;
    }

    // --- the ceiling, in the adapter's own loop ------------------------------

    // Can the clamp bind at all? If no usable edge between two reachable moves
    // adds to a ceilinged resource, no reachable configuration can rise above
    // its initial value, which the clamp above already put at or below the
    // ceiling. The projection is then EXACT and nothing further is needed.
    std::int32_t ceilingsThatCanBind = 0;
    {
        std::vector<bool> reachable(moveCount, true);
        for (MoveIndex m : out.unreachableMoves)
            if (m < moveCount) reachable[m] = false;
        std::vector<bool> gains(resCount, false);
        for (std::size_t m = 0; m < moveCount; ++m) {
            if (!reachable[m]) continue;
            for (std::size_t edge : usableFrom[m]) {
                if (!reachable[c.cancels[edge].to]) continue;
                for (std::size_t i = 0; i < resCount; ++i) {
                    const int total = c.cancels[edge].effect[i] + c.moves[c.cancels[edge].to].effect[i];
                    if (total > 0) gains[i] = true;
                }
            }
        }
        for (std::size_t i = 0; i < resCount; ++i)
            if (bounds[i].hasCeiling && gains[i]) ++ceilingsThatCanBind;
    }

    // Replay the reported witness with the ceilings applied. This is the loop
    // ADR-001 section 8 item 4 asks the adapter to own, and it is the only place
    // in this pipeline where a resource cap is honoured.
    //
    // The witness arrives as `prefix` then `loop`, both move indices, and the
    // OPENING MOVE IS MISSING whenever the loop is not entered on the first hit:
    // comboprover.hpp:511-517 starts the prefix at path index 1, so path[0]'s
    // move appears in neither list. It happens on both infinite characters in
    // the corpus, so the opener is recovered rather than waited for. It is a
    // starter with a usable edge into the first reported move, and when the
    // prefix is empty it must additionally BE the move the loop returns to,
    // because an empty prefix is what `match == 0` means. Every candidate is
    // followed and one surviving replay is enough: the question is whether SOME
    // clamped run loops, not whether this exact one does.
    if (out.status == ProverStatus::Infinite && !out.loop.empty()) {
        std::vector<MoveIndex> sequence = out.prefix;
        sequence.insert(sequence.end(), out.loop.begin(), out.loop.end());

        // The move the cycle closes on. Anything else means the witness is not
        // the shape this replay understands, and saying so beats guessing.
        const MoveIndex boundaryMove = out.prefix.empty() ? out.loop.back() : out.prefix.back();
        const bool wellFormed = out.loop.back() == boundaryMove;

        const std::size_t frontierCap =
            options.ceilingReplayFrontier > 0
                ? static_cast<std::size_t>(options.ceilingReplayFrontier) : 64u;

        std::vector<std::pair<MoveIndex, ResourceVec>> frontier;
        if (wellFormed) {
            std::vector<int> openers = c.starters;
            if (openers.empty())
                for (std::size_t i = 0; i < moveCount; ++i) openers.push_back(static_cast<int>(i));
            for (int s : openers) {
                if (out.prefix.empty() && static_cast<MoveIndex>(s) != boundaryMove) continue;
                bool joins = false;
                for (std::size_t edge : usableFrom[s])
                    if (c.cancels[edge].to == static_cast<int>(sequence.front())) { joins = true; break; }
                if (!joins) continue;
                if (!comboprover::detail::meetsGuard(c.initial, c.moves[s].guard)) continue;
                ResourceVec state = c.initial;
                for (std::size_t i = 0; i < resCount; ++i) state[i] += c.moves[s].effect[i];
                clampInto(state, bounds);
                if (!withinFloors(state, bounds)) continue;
                frontier.emplace_back(static_cast<MoveIndex>(s), std::move(state));
            }
        }
        out.loopEntryKnown = !frontier.empty();

        // One move along the witness, following every authored edge that joins
        // the two moves -- several can, with different costs. The prover's own
        // guard and effect helpers are reused deliberately: an independent
        // reimplementation of `totalGuard` here would be a second definition of
        // what a cancel costs, free to drift from the one the verdict used.
        auto step = [&](std::vector<std::pair<MoveIndex, ResourceVec>>& f, MoveIndex next) {
            std::vector<std::pair<MoveIndex, ResourceVec>> stepped;
            for (const auto& entry : f) {
                for (std::size_t edge : usableFrom[entry.first]) {
                    if (c.cancels[edge].to != static_cast<int>(next)) continue;
                    const ResourceVec guard = comboprover::detail::totalGuard(c, c.cancels[edge]);
                    if (!comboprover::detail::meetsGuard(entry.second, guard)) continue;
                    const ResourceVec effect = comboprover::detail::totalEffect(c, c.cancels[edge]);
                    ResourceVec after = entry.second;
                    for (std::size_t i = 0; i < resCount; ++i) after[i] += effect[i];
                    clampInto(after, bounds);
                    if (!withinFloors(after, bounds)) continue;
                    bool seen = false;
                    for (const auto& already : stepped)
                        if (already.second == after) { seen = true; break; }
                    if (!seen && stepped.size() < frontierCap)
                        stepped.emplace_back(next, std::move(after));
                }
            }
            f.swap(stepped);
        };

        if (!frontier.empty()) {
            out.ceilingReplayRan = true;
            for (MoveIndex m : out.prefix) {
                step(frontier, m);
                if (frontier.empty()) break;
            }
            std::vector<ResourceVec> boundaries;
            for (const auto& entry : frontier) boundaries.push_back(entry.second);

            const std::int32_t rounds =
                options.ceilingReplayRounds > 0 ? options.ceilingReplayRounds : 64;
            for (std::int32_t round = 0; round < rounds && !frontier.empty() &&
                                         !out.loopHoldsUnderCeilings; ++round) {
                for (MoveIndex m : out.loop) {
                    step(frontier, m);
                    if (frontier.empty()) break;
                }
                for (const auto& entry : frontier) {
                    for (const ResourceVec& earlier : boundaries)
                        if (coversVec(entry.second, earlier)) { out.loopHoldsUnderCeilings = true; break; }
                    if (out.loopHoldsUnderCeilings) break;
                }
                if (!out.loopHoldsUnderCeilings)
                    for (const auto& entry : frontier)
                        if (boundaries.size() < frontierCap) boundaries.push_back(entry.second);
            }
        }
    }

    // --- the loss table, filled in for this character ------------------------

    addLoss(out, "stage", LossDirection::Exact, 1,
            std::string("the verdict answers a CORNER-pinned defender; the file declares stage `") +
            (character.stage.empty() ? std::string("(unset)") : character.stage) +
            "`. Distance, pushback, walk speed and gap actions are not modelled, which is "
            "the scope of comboprover.hpp:15-24 rather than an approximation of it -- "
            "Kung Fu Man is TERMINATING midscreen and INFINITE in the corner and both "
            "answers are correct");
    if (!character.stage.empty() && character.stage != "corner")
        report.warnings.push_back(character.id + ": the file declares stage `" + character.stage +
            "` but the in-engine decision procedure answers the corner. Say so on the panel.");

    addLoss(out, "cancel.certain", LossDirection::Permissive, uncertainEdges,
            "edges gated on a runtime condition no importer can evaluate are INCLUDED, so "
            "the verdict assumes every such condition can be met. Dropping them instead "
            "would be conservative and could hide a real infinite, which is why this "
            "adapter does not offer that as an option");
    addLoss(out, "move.hit_condition", LossDirection::Permissive, defenderGated,
            "moves whose HitDef is gated on a predicate over the DEFENDER (ADR-001 section 4 "
            "group G). comboprover has no defender state, so the gate is assumed to pass and "
            "the model can chain moves the game would not");
    addLoss(out, "cancel.on", LossDirection::Exact, droppedByContact,
            "edges available only on block or on whiff are dropped. A move that did not "
            "connect cannot continue a combo, so this is exact for the question being asked "
            "and lost only for a question this projection does not ask");
    addLoss(out, "move.stance", LossDirection::Permissive, stanced,
            "air/ground restrictions are dropped, so an air move may follow a ground move "
            "with nothing in the way");
    addLoss(out, "engine.cancel_window_ticks", LossDirection::Permissive, windowed,
            "the window's close is dropped; the prover models the open edge as a delay and "
            "has no way to say the window shuts");
    addLoss(out, "engine.hits[]", LossDirection::Permissive, multiHit,
            "a move registering several HitDefs counts as one hit, so hitstun decay is "
            "applied less often than the game applies it and more cancels stay usable "
            "(it also understates maxDamage, which is a reporting error, not a verdict one)");
    addLoss(out, "resource.floor > 0", LossDirection::Permissive, floorsAbove,
            "comboprover.hpp:264-269 hardcodes a floor of zero, so a resource may run below "
            "a floor the game enforces");
    addLoss(out, "resource.floor < 0", LossDirection::Conservative, floorsBelow,
            "SOUNDNESS BUG IF NONZERO. comboprover.hpp:264-269 hardcodes a floor of zero, so "
            "a resource the game lets go negative is forbidden from doing so here, and a "
            "combo that spends into the red is invisible to the search");
    addLoss(out, "resource.ceiling", LossDirection::Permissive, ceilingsThatCanBind,
            "comboprover.hpp carries no ceilings. The initial vector is clamped by this "
            "adapter and the count above is how many ceilinged resources a reachable, usable "
            "edge can still push past their cap. By the simulation lemma at the top of "
            "ProverAdapter.cpp this over-approximates the attacker, so it can invent an "
            "infinite and cannot hide one; an INFINITE verdict is replayed under the real "
            "ceilings and loopHoldsUnderCeilings reports whether it survived");
    addLoss(out, "gap_action resource effects", LossDirection::Conservative, gapResourceEffects,
            "SOUNDNESS BUG IF NONZERO. Gap actions are dropped because in the corner model "
            "they can only spend space, which is not modelled. A gap action that moves a "
            "RESOURCE is a way of paying for a follow-up that this projection cannot see");
    addLoss(out, "projectile contact frame, outgoing", LossDirection::Conservative,
            contactFrameOutgoing,
            "SOUNDNESS BUG IF NONZERO, and this is ADR-001's one recorded conservative case, "
            "re-derived here rather than cited. " + toString(contactFrameMoves) +
            " move(s) in this character decline to state a reach, which is how the schema "
            "records that contact happens when the projectile arrives rather than when it is "
            "released. A cancel's delay is counted from the source CONNECTING, so an outgoing "
            "delay transcribed from the release frame is larger than the truth and real "
            "cancels get reported dead. The count is the number of usable outgoing edges from "
            "such a move: zero means the loss cannot affect this character's verdict");
    addLoss(out, "projectile contact frame, incoming", LossDirection::Permissive,
            contactFrameIncoming,
            "the same moves as targets. Their startup means `released on that frame`, not "
            "`connects on that frame`, so a link into one looks faster than it is");
    addLoss(out, "decay.table float multiply", LossDirection::Conservative, decayUnfaithfulLow,
            "SOUNDNESS BUG IF NONZERO. Our table is integer permille and the prover's is "
            "float; int(base * 0.9f) can land a frame below (base*900)/1000. Checked against "
            "every base hitstun in this file at every table index");
    addLoss(out, "decay.table float multiply", LossDirection::Permissive, decayUnfaithfulHigh,
            "the same check, in the direction that hands the attacker a frame they do not have");

    return true;
}

// The public entry: ONE FULL ANALYSIS PER OPENING (ADR-015, accepted
// 2026-09-01, option 3). Neutral runs first, straight into the caller's own
// slots -- the top-level result IS the neutral opening, so every call site
// written against the single-verdict surface keeps reading the bytes it
// always read -- and its report is THE report: the other two runs repeat the
// same structural checks over the same character and would only duplicate
// the warnings.
bool AnalyseCharacter(const CharacterData&  character,
                      const ProverOptions&  options,
                      ProverResult&         out,
                      ProverReport&         report) {
    if (!analyseForOpening(character, options, ProverOpening::Neutral, out, report))
        return false;

    // Copied BEFORE anything lands in out.openings, so the neutral element
    // cannot alias the vector it is being pushed into.
    ProverResult neutralCopy = out;

    out.openings.reserve(kProverOpeningCount);
    out.openings.push_back(std::move(neutralCopy));

    for (ProverOpening opening : { ProverOpening::Counter, ProverOpening::Air }) {
        ProverResult sub{};
        ProverReport scratch{};   // same character, same checks, same words
        if (!analyseForOpening(character, options, opening, sub, scratch)) {
            // Structurally unreachable today -- the failure paths validate
            // indices and counts the neutral run already passed -- but a
            // future transform could change that, and a half-answered result
            // must be refused loudly rather than returned with a hole in it.
            out    = ProverResult{};
            report = scratch;
            return false;
        }
        out.openings.push_back(std::move(sub));
    }
    return true;
}

// ---------------------------------------------------------------------------
// Reporting
// ---------------------------------------------------------------------------

std::string DescribeVerdict(const CharacterData& character, const ProverResult& result) {
    auto moveId = [&character](MoveIndex m) -> std::string {
        return m < character.moves.size() ? character.moves[m].id : std::string("<bad move index>");
    };

    std::string out = character.name.empty() ? character.id : character.name;
    out += "\n  verdict    ";
    out += ProverStatusName(result.status);
    out += "\n  stage      corner-pinned defender";
    if (!result.fileStage.empty() && result.fileStage != "corner")
        out += " (the file describes `" + result.fileStage + "`)";
    out += "\n  because    " + result.reason;
    out += "\n  model      " + toString(static_cast<std::int64_t>(character.moves.size())) +
           " moves, " + toString(result.usableCancels) + " usable cancels, " +
           (result.spendOnly ? "spend-only" : "general") + " fragment, " +
           toString(result.explored) + " configurations explored";

    if (result.status == ProverStatus::Infinite) {
        out += "\n  loop       ";
        for (std::size_t i = 0; i < result.prefix.size(); ++i)
            out += moveId(result.prefix[i]) + " > ";
        for (std::size_t i = 0; i < result.loop.size(); ++i) {
            if (i) out += " > ";
            out += moveId(result.loop[i]);
        }
        out += "\n  ceilings   ";
        if (!result.loopEntryKnown || !result.ceilingReplayRan)
            out += "not replayed: no starter affordable from the clamped initial vector "
                   "joins the reported witness, so the opening move could not be recovered";
        else if (result.loopHoldsUnderCeilings)
            out += "the loop still returns to a position no worse than it started with every "
                   "resource capped, so the infinite is real under the declared ceilings";
        else
            out += "the loop could NOT be reproduced once resources were capped; the verdict "
                   "stands but this witness may rest on meter the game would not have given";
    }

    if (result.status == ProverStatus::Terminating) {
        if (result.hasRanking) {
            out += "\n  ends because these run down, in order: ";
            for (std::size_t i = 0; i < result.rankingOrder.size(); ++i) {
                if (i) out += ", ";
                const ResourceIndex ri = result.rankingOrder[i];
                out += ri < character.resources.size() ? character.resources[ri].name
                                                       : std::string("<bad resource index>");
            }
        } else {
            out += "\n  no ranking certificate: ";
            out += RankingAbsenceName(result.rankingAbsence);
        }
        out += "\n  worst case " + toString(result.maxHits) + " hits, " +
               toString(result.maxFrames) + " frames, " +
               toString(result.maxDamageHundredths / 100) + " damage";
    }

    out += "\n  dead cancels " + toString(static_cast<std::int64_t>(result.deadCancels.size())) +
           " at the settled hitstun, " +
           toString(static_cast<std::int64_t>(result.deadCancelsPreDecay.size())) +
           " before decay";
    for (const ProverDeadCancel& dead : result.deadCancels) {
        out += "\n    " + moveId(dead.from) + " -> " + moveId(dead.to) + " leaves " +
               toString(dead.advantage) + " but needs " + toString(dead.startup) +
               ", short by " + toString(dead.shortfall());
    }
    for (MoveIndex move : result.unreachableMoves)
        out += "\n  unreachable: no combo can produce " + moveId(move);

    if (result.soundnessAlarm) {
        out += "\n  SOUNDNESS ALARM: this character contains data whose projection can HIDE a "
               "real infinite. A TERMINATING verdict here is not a clean bill of health.";
    }
    for (const ProjectionLoss& loss : result.losses) {
        if (loss.count == 0) continue;
        out += "\n  lost: " + loss.field + " x" + toString(loss.count) + " -- " +
               LossDirectionName(loss.direction);
    }

    // One verdict per opening (ADR-015 option 3), APPENDED so every sentence
    // above keeps its exact spelling -- tests and bug reports quote this text.
    // Empty on a result that predates openings or never ran, and that absence
    // is worth a reader noticing, so nothing is printed in that case.
    if (!result.openings.empty()) {
        out += "\n  by opening (every line above answers the neutral one):";
        for (const ProverResult& o : result.openings) {
            switch (o.opening) {
                case ProverOpening::Neutral: out += "\n    under a NEUTRAL opening: ";  break;
                case ProverOpening::Counter: out += "\n    under a COUNTER opening: "; break;
                case ProverOpening::Air:     out += "\n    under an AIR opening: ";    break;
            }
            out += ProverStatusName(o.status);
            if (o.status == ProverStatus::Terminating)
                out += " -- worst case " + toString(o.maxHits) + " hits";
            if (o.opening == ProverOpening::Counter)
                out += " (no counter_hit is authorable yet; identical to "
                       "neutral by construction)";
            if (o.opening == ProverOpening::Air)
                out += " (the file's authored air_hitstun numbers; the loss "
                       "ledger says what the kernel carries)";
        }
    }

    out += "\n";
    return out;
}

} // namespace cse::data
