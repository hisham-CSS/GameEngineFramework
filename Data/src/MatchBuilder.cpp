#include "cse/data/MatchBuilder.h"

// <algorithm> is fine HERE and would not be fine one directory over. This is
// CseData: it already has std::string, std::vector and nlohmann. The rule about
// keeping <algorithm> out of a translation unit (Simulate.cpp says so) is a rule
// about the kernel, whose entire portability argument is that nothing it
// compiles can reach a float overload. Nothing in this file runs inside a tick.
#include <algorithm>
#include <string>

namespace cse::data {

// At namespace scope rather than inside the unnamed namespace below: these names
// are used by the public definitions further down, and a using-declaration
// hidden in an unnamed namespace that happens to be visible anyway is the kind
// of thing that works until someone moves a function.
using cse::kernel::Box;
using cse::kernel::FighterData;
using cse::kernel::MoveDef;

namespace {

std::string num(std::int64_t v) { return std::to_string(v); }

// Damage: HUNDREDTHS of a damage point in CharacterData, whole points in
// MoveDef, because MoveDef::damage is in the same units as Fighter::health and
// Combat.h states the conversion happens exactly once, at load.
//
// Round half AWAY FROM ZERO, which is D2's scaleBy rule and is chosen for the
// same reason: it is symmetric about zero, so the rule cannot make a character
// behave differently depending on the sign of a number. Damage is non-negative
// in every shipped file and this conversion is exact for all three of them --
// every authored value is a whole number of points -- but a rounding rule that
// is only correct on the data you happened to look at is not a rule.
std::int32_t damagePointsFromHundredths(std::int32_t hundredths) {
    const std::int64_t p = static_cast<std::int64_t>(hundredths);
    const std::int64_t den = 100;
    const std::int64_t h = den / 2;
    const std::int64_t q = (p >= 0) ? (p + h) / den : (p - h) / den;
    return static_cast<std::int32_t>(q);
}

// The bound check Combat.h asks the loader to perform. This is BoxIsValid's
// range half, written out rather than called: calling it would put CseKernel on
// CseData's link line, and Data/CMakeLists.txt fails the configure when that
// happens. The emptiness half is deliberately NOT replicated -- a zero-width box
// is how this file expresses "this move has no reach", and it is inert rather
// than invalid.
bool coordInRange(std::int32_t v) {
    return v <= cse::kernel::kMaxBoxCoord && v >= -cse::kernel::kMaxBoxCoord;
}

void addLoss(BuildReport& report, const char* field, BuildLossDirection direction,
             std::int32_t count, std::string note) {
    BuildLoss loss{};
    loss.field     = field;
    loss.direction = direction;
    loss.count     = count;
    loss.note      = std::move(note);
    report.losses.push_back(std::move(loss));
}

// --- The loss table ----------------------------------------------------------
//
// Every field of CharacterData that MatchData cannot carry, with the count of
// objects in THIS character that it touches and the direction the difference
// runs. Entries with count 0 stay in the list: "this character has no decay" and
// "nobody checked decay" must not look the same.
void recordLosses(const CharacterData& c, BuildReport& report) {
    std::int32_t stanced = 0, withEffect = 0, withGuard = 0, withPushback = 0;
    std::int32_t withHitCondition = 0, noReach = 0, withReach = 0;
    std::int32_t withHits = 0, withMotion = 0, withCancelWindow = 0, withEscapeHatch = 0;

    for (const Move& m : c.moves) {
        if (m.stance != Stance::Any)        ++stanced;
        if (!m.effect.empty())              ++withEffect;
        if (!m.guard.empty())               ++withGuard;
        if (m.pushbackSub != 0)             ++withPushback;
        if (!m.hitConditionProse.empty())   ++withHitCondition;
        if (m.reachSub == kNoReach)         ++noReach; else ++withReach;
        if (!m.hits.empty())                ++withHits;
        if (!m.motion.empty())              ++withMotion;
        if (m.hasCancelWindow)              ++withCancelWindow;
        if (m.escapeHatchNeeded)            ++withEscapeHatch;
    }

    // THE BIG ONE, and it is first on purpose. The kernel starts a move from a
    // held button and from nothing else; there is no cancel system, so not one
    // of these edges can be taken. Everything the combo prover reasons about is
    // a path through this list, which is why
    // kernelPlaysTheAnalysedCharacter is false for every character that has any.
    addLoss(report, "cancels", BuildLossDirection::KernelOmits,
            static_cast<std::int32_t>(c.cancels.size()),
            "The kernel has no cancel system: a move can only be started from an "
            "idle fighter holding its buttons (Combat.h StepAttack). Every "
            "authored chain is unreachable, so the combos ProverAdapter's verdict "
            "is about cannot be performed. This is the single reason a shipped "
            "character does not yet play as analysed.");

    addLoss(report, "move.cancel_window", BuildLossDirection::KernelOmits, withCancelWindow,
            "The [open, close] tick window a cancel may be buffered in. Inert "
            "while `cancels` is, and listed separately so it does not silently "
            "become the next thing forgotten when cancels land.");

    addLoss(report, "character.walk_speed", BuildLossDirection::KernelOmits,
            c.walkSpeedSub != 0 ? 1 : 0,
            "FighterData has no walk speed; Simulate.cpp walks every fighter at a "
            "hardcoded 2 px/tick. This is not cosmetic -- walking is how a "
            "midscreen attacker closes the gap a move's reach is measured "
            "against, so a character with a different walk speed connects at "
            "different times than the file says. Authored value, sub-units per "
            "tick: " + num(c.walkSpeedSub) + ".");

    addLoss(report, "move.pushback", BuildLossDirection::KernelOmits, withPushback,
            "Defender displacement on hit. ADR-001 section 6.3 records that this "
            "number is ESTIMATED in every shipped character and that the "
            "midscreen verdict turns on it; the kernel moves nobody on hit, so "
            "two fighters stay exactly as far apart as they were.");

    addLoss(report, "move.stance", BuildLossDirection::KernelPermits, stanced,
            "Ground/air restriction. The kernel gates starting a move on hitstun "
            "and blockstun only, so an air-only move is startable standing and a "
            "ground-only move is startable in the air. More is possible in the "
            "game than in the file.");

    addLoss(report, "move.guard", BuildLossDirection::KernelPermits, withGuard,
            "The resource minimum a move requires -- meter, for the shipped "
            "characters. Fighter::meter exists but nothing reads it, so a super "
            "that costs a full bar is startable on an empty one.");

    addLoss(report, "move.effect", BuildLossDirection::KernelOmits, withEffect,
            "The resource delta a move applies. Fighter::meter exists but nothing "
            "writes it, so meter is never gained either. The two halves cancel "
            "into 'resources are not simulated' rather than into 'no difference'.");

    addLoss(report, "resources", BuildLossDirection::KernelOmits,
            static_cast<std::int32_t>(c.resources.size()),
            "Declared resources and their initial/floor/ceiling. The kernel has "
            "one integer called meter and no ceiling logic at all.");

    addLoss(report, "move.hit_condition", BuildLossDirection::KernelPermits, withHitCondition,
            "A predicate over the DEFENDER gating whether the move connects -- "
            "ADR-001 section 4 group G. Kept as prose by the loader because "
            "evaluating it needs the opponent namespace Phase 5 owns. The kernel "
            "connects whenever the boxes overlap, so a move that should have been "
            "refused a hit lands.");

    addLoss(report, "move.escape_hatch", BuildLossDirection::KernelPermits, withEscapeHatch,
            "Moves whose file says they need behaviour no data field can express "
            "(a proximity variant, a runtime condition). Built here as ordinary "
            "moves, so they behave like their unconditional half.");

    addLoss(report, "scaling", BuildLossDirection::KernelOmits,
            static_cast<std::int32_t>(c.scalingPermille.size()),
            "Damage scaling by combo depth. MoveDef::damage is a constant and "
            "Fighter::comboHits is never read, so the tenth hit of a combo deals "
            "what the first did.");

    addLoss(report, "decay", BuildLossDirection::KernelOmits,
            c.decay.kind == DecayKind::None ? 0 : 1,
            c.decay.kind == DecayKind::None
                ? std::string("Checked and inert: this character authors "
                              "decay.kind 'none', which is the truthful "
                              "transcription for MUGEN 1.0 (ADR-001's amendment "
                              "to D8). There is no hitstun decay to lose.")
                : std::string("Hitstun decay. ResolveHits SETS hitstun to the "
                              "move's authored value on every hit regardless of "
                              "how many have landed, so a combo does not get "
                              "harder to extend as it runs -- the direction that "
                              "makes loops easier."));

    addLoss(report, "gap_actions", BuildLossDirection::KernelOmits,
            static_cast<std::int32_t>(c.gapActions.size()),
            "Defender options between hits. The kernel gives a fighter in hitstun "
            "no options at all.");

    addLoss(report, "starters", BuildLossDirection::KernelOmits,
            static_cast<std::int32_t>(c.starters.size()),
            "Which moves may open a combo. The kernel's opener is 'the fighter is "
            "idle and holding the buttons', which is a different rule and a "
            "looser one.");

    addLoss(report, "move.engine.hits", BuildLossDirection::KernelOmits, withHits,
            "Per-HitDef records for moves that register several. MoveDef is one "
            "(startup, active, hitstun, damage) tuple and ResolveHits applies at "
            "most one hit per active window, so a multi-hit move lands once.");

    addLoss(report, "move.engine.motion", BuildLossDirection::KernelOmits, withMotion,
            "The attacker's own displacement keys. The kernel's fighters do not "
            "move during a move, so a special that travels forward connects only "
            "from where it started.");

    // Reach: two entries, because "the file declined to state one" and "the file
    // stated one measured from somewhere else" are different problems pointing in
    // opposite directions.
    addLoss(report, "move.reach (absent)", BuildLossDirection::KernelOmits, noReach,
            "Moves whose file authors `reach: null` -- projectiles, whose reach is "
            "a function of distance travelled (CharacterData.h kNoReach). Built "
            "with a ZERO-WIDTH hitbox: the move runs for its authored frames and "
            "connects with nothing, because inventing a reach for a fireball "
            "would fabricate range the file refused to claim.");

    addLoss(report, "move.reach (provenance)", BuildLossDirection::KernelPermits, withReach,
            "The hitbox is built so that the move connects at a body-to-body gap "
            "of exactly reachSub and no further, which is the semantics "
            "CharacterData.h states. The transcription derived those numbers from "
            "the sprite's attack box measured from the character's ORIGIN "
            "(engine.derivation, 'Clsn1 <px>'), and the two measurements differ "
            "by one body half-width -- so this build reaches further than the "
            "sprite did. Flagged rather than split, because the discrepancy is in "
            "the file and this bridge is not the place to decide it.");

    addLoss(report, "move.hitbox.y", BuildLossDirection::KernelPermits,
            static_cast<std::int32_t>(c.moves.size()),
            "The schema authors no vertical extent for an attack at all. Every "
            "hitbox therefore spans the whole body height, so a low kick hits a "
            "fighter at the top of a jump. The model is horizontal, which is the "
            "same axis the combo prover reasons on.");

    addLoss(report, "hurtbox", BuildLossDirection::KernelPermits, 1,
            "CharacterData carries no body: the shipped files put the pushbox in "
            "engine.constants, which the loader does not read. The box comes from "
            "BuildOptions::body instead, so it is the caller's number and not the "
            "character's.");

    report.lossesThatBite = 0;
    for (const BuildLoss& loss : report.losses)
        if (loss.count != 0) ++report.lossesThatBite;
    report.playsAsAnalysed = (report.lossesThatBite == 0);
}

} // namespace

// --- MoveIndexMap ------------------------------------------------------------

std::uint16_t MoveIndexMap::KernelMoveIdOf(MoveIndex characterMove) {
    if (characterMove == kInvalidMove) return 0;
    return static_cast<std::uint16_t>(characterMove + 1);
}

MoveIndex MoveIndexMap::CharacterMoveOf(std::uint16_t kernelMoveId) {
    if (kernelMoveId == 0) return kInvalidMove;   // idle is not a move
    return static_cast<MoveIndex>(kernelMoveId - 1);
}

std::uint16_t MoveIndexMap::Find(std::string_view moveId) const {
    const auto it = std::lower_bound(
        byId.begin(), byId.end(), moveId,
        [](const std::pair<std::string, std::uint16_t>& a, std::string_view b) {
            // Converted explicitly rather than relying on the "sufficient
            // additional overloads" the standard requires for mixed
            // string/string_view comparison. The conversion is free and the
            // overload resolution question does not have to be asked.
            return std::string_view(a.first) < b;
        });
    if (it == byId.end() || std::string_view(it->first) != moveId) return 0;
    return it->second;
}

std::string_view MoveIndexMap::IdOf(std::uint16_t kernelMoveId) const {
    if (kernelMoveId >= idByMoveId.size()) return std::string_view{};
    return idByMoveId[kernelMoveId];
}

// --- One side ----------------------------------------------------------------

bool BuildFighterData(const CharacterData& character, const BuildOptions& options,
                      FighterData& out, MoveIndexMap& moves, BuildReport& report) {
    // Value-initialised, which zeroes the padding as well as the members. That
    // matters more here than it looks: ARCHITECTURE.md 4.8 has the connect
    // handshake hash the LOADED POD ARRAYS, so a byte this function never wrote
    // is a byte two peers can disagree about.
    out    = FighterData{};
    moves  = MoveIndexMap{};
    report = BuildReport{};

    const std::string who =
        character.id.empty() ? std::string("<character with no id>") : character.id;

    if (character.moves.empty()) {
        report.error = who + ": has no moves. A fighter with an empty move table "
                             "can never attack, and building one silently is how a "
                             "load failure turns into a match nobody can win.";
        return false;
    }

    // THE CAPACITY DECISION. See the long note in MatchBuilder.h: refuse, never
    // truncate. The message names all three numbers because the person reading it
    // has to decide which moves to cut, and "too many moves" does not help them.
    if (static_cast<std::int32_t>(character.moves.size()) > kMaxBuildableMoves) {
        report.error =
            who + ": has " + num(static_cast<std::int64_t>(character.moves.size())) +
            " moves and the kernel holds " + num(kMaxBuildableMoves) +
            " (slot 0 of " + num(cse::kernel::kMaxMovesPerFighter) +
            " is the reserved idle slot). REFUSED rather than truncated: dropping "
            "the tail would produce a character the combo prover never analysed, "
            "with every cancel into a dropped move silently pointing at nothing. "
            "Raise kMaxMovesPerFighter in cse/kernel/Combat.h -- which is a wire "
            "layout change both peers must agree on -- or cut moves in the file.";
        return false;
    }

    // --- The body -----------------------------------------------------------
    std::int32_t halfWidth = options.body.halfWidthSub;
    std::int32_t height    = options.body.heightSub;

    if (halfWidth <= 0) {
        report.warnings.push_back(
            who + ": BuildOptions::body.halfWidthSub was not supplied; using the "
                  "default of " + num(kDefaultBodyHalfWidthSub) + " sub-units (" +
            num(kDefaultBodyHalfWidthSub / cse::kernel::kSubUnitsPerPixel) +
            " px). CharacterData carries no body, so this number is the caller's "
            "and not this character's.");
        halfWidth = kDefaultBodyHalfWidthSub;
    }
    if (height <= 0) {
        report.warnings.push_back(
            who + ": BuildOptions::body.heightSub was not supplied; using the "
                  "default of " + num(kDefaultBodyHeightSub) + " sub-units (" +
            num(kDefaultBodyHeightSub / cse::kernel::kSubUnitsPerPixel) + " px).");
        height = kDefaultBodyHeightSub;
    }
    if (!coordInRange(halfWidth) || !coordInRange(height)) {
        report.error = who + ": body is larger than the kernel's box bound of " +
                       num(cse::kernel::kMaxBoxCoord) +
                       " sub-units. PlaceBox would clamp it, which would silently "
                       "change the shape rather than reject it.";
        return false;
    }

    out.hurtbox = Box{ -halfWidth, 0, halfWidth, height };

    // --- Bindings -----------------------------------------------------------
    const std::size_t moveCount = character.moves.size();
    std::vector<std::uint16_t> button(moveCount, 0u);
    std::vector<bool>          bound(moveCount, false);

    for (const MoveBinding& b : options.bindings) {
        const MoveIndex idx = character.FindMove(b.moveId);
        if (idx == kInvalidMove) {
            // A warning, not an error: one binding table shared across two
            // characters with different movesets is a reasonable thing to write,
            // and refusing it would make the sane call site the awkward one.
            report.warnings.push_back(
                who + ": binding names move `" + b.moveId +
                "`, which this character does not have. Ignored.");
            continue;
        }
        if (bound[idx]) {
            report.warnings.push_back(
                who + ": move `" + b.moveId + "` is bound more than once. The FIRST "
                "binding wins; the later one is ignored. First-wins rather than "
                "last-wins so the result does not depend on where in a list "
                "somebody appended.");
            continue;
        }
        bound[idx]  = true;
        button[idx] = b.button;
    }

    // --- The moves ----------------------------------------------------------
    moves.characterId = character.id;
    moves.idByMoveId.assign(moveCount + 1, std::string{});   // slot 0 names nothing
    moves.byId.reserve(moveCount);

    for (std::size_t i = 0; i < moveCount; ++i) {
        const Move& src  = character.moves[i];
        const std::uint16_t slot = static_cast<std::uint16_t>(i + 1);
        const std::string where  = who + ".moves[" + num(static_cast<std::int64_t>(i)) +
                                   "] (`" + src.id + "`)";

        // Negative frame data is rejected here rather than leaned on downstream.
        // MoveDuration and ActiveHitbox clamp negatives, but those clamps exist to
        // make the SIMULATION total for any state including a corrupt one -- they
        // are not a place to launder authored data, and a move whose recovery is
        // -4 is a file that needs fixing, not a move that recovers instantly.
        if (src.startup < 0 || src.active < 0 || src.recovery < 0) {
            report.error = where + ": negative frame data (startup " +
                           num(src.startup) + ", active " + num(src.active) +
                           ", recovery " + num(src.recovery) + ").";
            return false;
        }
        if (src.hitstun < 0) {
            report.error = where + ": negative hitstun (" + num(src.hitstun) + ").";
            return false;
        }
        if (src.damageHundredths < 0) {
            report.error = where + ": negative damage (" + num(src.damageHundredths) +
                           " hundredths). ResolveHits clamps a negative to zero so "
                           "it cannot heal, but a file that authors one is saying "
                           "something it does not mean.";
            return false;
        }

        MoveDef m{};
        m.startup  = src.startup;
        m.active   = src.active;
        m.recovery = src.recovery;
        m.hitstun  = src.hitstun;
        m.damage   = damagePointsFromHundredths(src.damageHundredths);
        m.button   = button[i];
        m.pad_     = 0;   // explicit: these bytes are hashed by the handshake

        if (src.hitstun > 0xFFFF) {
            report.warnings.push_back(
                where + ": hitstun " + num(src.hitstun) +
                " exceeds Fighter::hitstun's uint16 range and will saturate at "
                "65535 when applied.");
        }
        if (m.startup + m.active + m.recovery == 0) {
            report.warnings.push_back(
                where + ": has a duration of zero ticks, so it ends on the tick it "
                "starts and can never have a live hitbox.");
        }

        // --- The box, which is the whole representational disagreement -------
        //
        // The file gives one scalar: the maximum GAP between the two bodies at
        // which the move connects (CharacterData.h). The kernel wants a rectangle
        // relative to the fighter's origin, facing +X, tested half-open against
        // the defender's body. The construction below is chosen so that the
        // file's sentence comes out literally true:
        //
        //     hitbox  = [ halfWidth, halfWidth + reach + 1 )
        //     hurtbox = [ -halfWidth, +halfWidth )
        //
        // With the attacker at ax and the defender at dx > ax, the boxes overlap
        // exactly when dx - ax - 2*halfWidth <= reach, and that left-hand side is
        // the gap between the two bodies. So the move connects at a gap of
        // reachSub and misses at reachSub + 1 sub-unit.
        //
        // THE + 1 IS NOT A FUDGE FACTOR. The kernel's boxes are half-open, which
        // Combat.h argues for at length: touching is not overlapping, so a hit at
        // exactly maximum range would otherwise be a miss. The authored bound is
        // INCLUSIVE. Converting an inclusive upper bound to a half-open one is
        // the same +1 it is everywhere else, and doing it here means it happens
        // once, at load, rather than as a >= somewhere in the tick.
        //
        // Both edges are exact under MirrorBox, which negates and swaps: a
        // left-facing fighter's reach is the same integer as a right-facing one's,
        // with no division anywhere to lose the last sub-unit that decides whether
        // a combo connects.
        if (src.reachSub == kNoReach) {
            // Zero width, positioned at the front of the body so the number reads
            // as "no reach" rather than as "no box". BoxesOverlap is false for any
            // empty rectangle, so the move runs its frames and connects with
            // nothing. Counted in the loss table.
            m.hitbox = Box{ halfWidth, 0, halfWidth, height };
        } else {
            if (src.reachSub < 0) {
                report.error = where + ": negative reach (" + num(src.reachSub) +
                               " sub-units). Use kNoReach to say the file declines "
                               "to state one; a negative distance says nothing.";
                return false;
            }
            const std::int64_t far =
                static_cast<std::int64_t>(halfWidth) +
                static_cast<std::int64_t>(src.reachSub) + 1;
            if (far > cse::kernel::kMaxBoxCoord) {
                report.error = where + ": reach " + num(src.reachSub) +
                               " sub-units puts the hitbox edge at " + num(far) +
                               ", past the kernel's box bound of " +
                               num(cse::kernel::kMaxBoxCoord) +
                               ". PlaceBox would clamp it, which would silently "
                               "shorten the move instead of rejecting the file.";
                return false;
            }
            m.hitbox = Box{ halfWidth, 0, static_cast<std::int32_t>(far), height };
        }

        out.moves[slot] = m;

        moves.idByMoveId[slot] = src.id;
        moves.byId.emplace_back(src.id, slot);
    }

    out.moveCount = static_cast<std::int32_t>(moveCount) + 1;   // slot 0 included
    moves.moveCount = out.moveCount;

    std::sort(moves.byId.begin(), moves.byId.end(),
              [](const std::pair<std::string, std::uint16_t>& a,
                 const std::pair<std::string, std::uint16_t>& b) {
                  return a.first < b.first;
              });

    // --- Shadowed bindings ---------------------------------------------------
    //
    // StepAttack scans the table from slot 1 upward and takes the FIRST move all
    // of whose bits are held. So a move whose buttons are a strict superset of an
    // earlier move's can never start: whenever its combination is held, the
    // earlier one's is too. {LP} at slot 1 and {Down|LP} at slot 12 is exactly
    // that, and it is the natural way somebody binds crouching normals.
    //
    // Reported rather than reordered. Reordering would change the move indices,
    // and those indices are the thing the whole file exists to keep aligned with
    // the ids ProverAdapter reports.
    for (std::size_t j = 0; j < moveCount; ++j) {
        if (button[j] == 0) continue;
        for (std::size_t i = 0; i < j; ++i) {
            if (button[i] == 0) continue;
            if ((button[j] & button[i]) != button[i]) continue;
            report.warnings.push_back(
                who + ": move `" + character.moves[j].id + "` (slot " +
                num(static_cast<std::int64_t>(j + 1)) + ") can never start. Its "
                "buttons are a superset of `" + character.moves[i].id + "`'s (slot " +
                num(static_cast<std::int64_t>(i + 1)) + "), and StepAttack takes the "
                "first move in slot order whose buttons are all held, so the "
                "earlier one always wins.");
            break;   // one report per shadowed move; the first shadower is enough
        }
    }

    recordLosses(character, report);
    return true;
}

// --- Both sides --------------------------------------------------------------

bool BuildMatchData(const CharacterData& p0, const BuildOptions& options0,
                    const CharacterData& p1, const BuildOptions& options1,
                    MatchBuild& out) {
    out = MatchBuild{};

    // Both sides are built even when the first fails, so a caller looking at a
    // rejected match sees BOTH characters' problems instead of fixing one and
    // discovering the other on the next run. The return value is the conjunction.
    const bool ok0 = BuildFighterData(p0, options0, out.data.p[0], out.moves[0], out.report[0]);
    const bool ok1 = BuildFighterData(p1, options1, out.data.p[1], out.moves[1], out.report[1]);

    if (!ok0 || !ok1) {
        // A half-built MatchData is worse than none: it looks like a match. Reset
        // the POD and keep the reports, which are the only part worth reading.
        out.data = cse::kernel::MatchData{};
        return false;
    }
    return true;
}

const char* BuildLossDirectionName(BuildLossDirection direction) {
    switch (direction) {
        case BuildLossDirection::Exact:         return "exact";
        case BuildLossDirection::KernelPermits: return "kernel permits more";
        case BuildLossDirection::KernelOmits:   return "kernel omits";
    }
    return "unknown";
}

} // namespace cse::data
