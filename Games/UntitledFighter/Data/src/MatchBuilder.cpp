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
using cse::kernel::CancelEdge;
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
// CseData's link line, and Games/UntitledFighter/Data/CMakeLists.txt fails the
// configure when that happens. The emptiness half is deliberately NOT
// replicated -- a zero-width box
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

// --- What the cancel projection actually did ---------------------------------
//
// Filled in by buildCancels below and read by recordLosses. It exists as a
// struct rather than as a pile of out-parameters because every one of these
// numbers ends up in the loss table, and a projection that reports "134 cancels
// were lost" when it in fact carried all 134 with four separate approximations
// is worse than one that reports nothing: it is wrong in a way a reader will
// believe.
struct CancelStats {
    std::int32_t authored = 0;   // CharacterData::cancels.size()
    std::int32_t built    = 0;   // edges that reached FighterData::cancels
    std::int32_t dropped  = 0;   // endpoint did not map to a kernel move slot

    // Edges whose resolved window is empty because the authored delay outlives
    // the source move. These are LINKS, not cancels -- see the note on the loss
    // entry below.
    std::int32_t inertLink = 0;

    std::int32_t contactFrame  = 0;   // contact-gated, from a source with active > 1
    std::int32_t onBlockOrWhiff = 0;  // Contact values the kernel cannot observe
    std::int32_t uncertain     = 0;   // Cancel::certain == false
    std::int32_t withGuard     = 0;
    std::int32_t withEffect    = 0;

    // Moves that are the SOURCE of at least one edge and author no cancel
    // window. Counted per MOVE, not per edge: the invented window is a property
    // of the move, and counting it once per outgoing edge would make a move with
    // eleven follow-ups look eleven times worse than one with a single one.
    std::int32_t sourcesWithoutWindow = 0;
};

// --- The loss table ----------------------------------------------------------
//
// Every field of CharacterData that MatchData cannot carry, with the count of
// objects in THIS character that it touches and the direction the difference
// runs. Entries with count 0 stay in the list: "this character has no decay" and
// "nobody checked decay" must not look the same.
void recordLosses(const CharacterData& c, const CancelStats& cancels,
                  BuildReport& report) {
    std::int32_t stanced = 0, withEffect = 0, withGuard = 0, withPushback = 0;
    std::int32_t withHitCondition = 0, noReach = 0, withReach = 0;
    std::int32_t withHits = 0, withMotion = 0, withEscapeHatch = 0;

    for (const Move& m : c.moves) {
        if (m.stance != Stance::Any)        ++stanced;
        if (!m.effect.empty())              ++withEffect;
        if (!m.guard.empty())               ++withGuard;
        if (m.pushbackSub != 0)             ++withPushback;
        if (!m.hitConditionProse.empty())   ++withHitCondition;
        if (m.reachSub == kNoReach)         ++noReach; else ++withReach;
        if (!m.hits.empty())                ++withHits;
        if (!m.motion.empty())              ++withMotion;
        if (m.escapeHatchNeeded)            ++withEscapeHatch;
    }

    // --- Cancels ------------------------------------------------------------
    //
    // These come first because they used to be one line reading "the kernel has
    // no cancel system" with the whole edge count against it. It does now, so
    // the entry that replaces it is a projection report: what crossed, what did
    // not, and the four separate ways an edge that DID cross can still behave
    // differently from what the file says. Splitting them is the point. A single
    // "cancels: 134" line after the kernel grew cancels would be a lie in the
    // other direction, and a single "cancels: 0" would be a worse one.

    addLoss(report, "cancels (dropped)", BuildLossDirection::KernelOmits,
            cancels.dropped,
            "Edges whose `from` or `to` did not resolve to a kernel move slot. "
            "A dangling edge is worse than a missing one -- it would put a "
            "fighter into a moveId nothing describes -- so it is dropped and "
            "counted here rather than carried. Authored " + num(cancels.authored) +
            ", built " + num(cancels.built) + ", dropped " + num(cancels.dropped) +
            ".");

    addLoss(report, "cancels (link, not cancel)", BuildLossDirection::KernelPermits,
            cancels.inertLink,
            "Edges whose authored delay outlives the source move, so the resolved "
            "window is empty and the kernel can never take them. These are LINKS "
            "rather than cancels: the file is saying the follow-up becomes legal "
            "only after the source has fully recovered, which the ordinary button "
            "start already permits -- but it permits it whether or not the source "
            "connected, and the file requires contact. Every edge of the AOF2 "
            "character is one of these.");

    addLoss(report, "cancel.contact_frame", BuildLossDirection::KernelPermits,
            cancels.contactFrame,
            "Contact-gated edges out of a move with more than one active frame. "
            "Cancel::delay is measured from the tick the source CONNECTED, and "
            "GameState does not record that tick -- deliberately, because its "
            "layout is a wire contract with a cross-toolchain golden hash against "
            "it (Combat.h argues the trade). The window is resolved against the "
            "source's FIRST active frame instead, which is the earliest a hit "
            "could have landed, so the follow-up becomes available up to "
            "`active - 1` ticks before the file allows it.");

    addLoss(report, "cancel.on", BuildLossDirection::KernelPermits,
            cancels.onBlockOrWhiff,
            "Edges authored `on: block` or `on: whiff`. The kernel has no "
            "blocking at all -- Fighter::blockstun exists and nothing writes it "
            "-- so the only contact fact it can observe is whether the attack "
            "landed. A block-only edge is therefore taken after a HIT, and a "
            "whiff-only edge is taken after a hit as well as after a whiff. "
            "`on: hit` and `on: always` cross exactly and are not counted here.");

    addLoss(report, "cancel.certain", BuildLossDirection::KernelPermits,
            cancels.uncertain,
            "Edges the file marks `certain: false`: the transcription found the "
            "edge but could not establish the runtime condition gating it "
            "(CharacterData.h Cancel::certain). The kernel takes them "
            "unconditionally, so the character can chain in situations the "
            "original could not.");

    addLoss(report, "cancel.guard", BuildLossDirection::KernelPermits,
            cancels.withGuard,
            "The resource minimum an EDGE requires. CancelEdge carries no guard "
            "field, so this is still listed as a loss -- but the constraint is "
            "enforced in practice: the cancel scan refuses a target move whose "
            "own MoveDef::guard is unmet, and every authored edge guard in this "
            "tree restates its target's guard exactly. An edge demanding MORE "
            "than its target would be permitted where the file refuses, and the "
            "build warns per edge when it finds one. Kept as KernelPermits "
            "rather than promoted to Exact because that is the direction the "
            "remaining hole runs.");

    addLoss(report, "cancel.effect", BuildLossDirection::KernelOmits,
            cancels.withEffect,
            "The resource delta an EDGE applies, as distinct from the delta its "
            "target move applies -- which the kernel does carry and does bank. "
            "No character in this tree authors one, so this row counts zero "
            "everywhere and CancelEdge was left at 16 bytes rather than grown "
            "for a field nothing uses (ROADMAP M1.1b).");

    addLoss(report, "move.cancel_window (absent)", BuildLossDirection::KernelPermits,
            cancels.sourcesWithoutWindow,
            "Moves that are the source of at least one cancel and author no "
            "[open, close] window. The schema's window is what closes a cancel "
            "opportunity; with none, this bridge lets the window run to the last "
            "frame of the move, so a follow-up stays available for the whole of "
            "recovery. Moves that DO author one get exactly it, intersected with "
            "the per-edge delay.");

    addLoss(report, "character.walk_speed", BuildLossDirection::Exact,
            c.walkSpeedSub != 0 ? 1 : 0,
            "FighterData::walkSpeedSub carries the authored number and Simulate "
            "walks at it. CharacterData quantized it once at load, so this build "
            "rounds nothing and the kernel walks the character the file "
            "describes. A file that authors none leaves this zero and the kernel "
            "keeps its 2 px/tick placeholder, which is the pre-M1.1b behaviour "
            "and is why a silent file plays as it always did. Authored value, "
            "sub-units per tick: " + num(c.walkSpeedSub) + ".");

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

    addLoss(report, "move.guard", BuildLossDirection::Exact, withGuard,
            "The resource minimum a move requires. MoveDef::guard carries it and "
            "both start routes -- the button scan and the cancel scan -- refuse "
            "a move the fighter cannot afford, so a super that costs a full bar "
            "is no longer startable on an empty one. A refused slot falls "
            "through to the next one sharing the button rather than eating the "
            "press.");

    addLoss(report, "move.effect", BuildLossDirection::Exact, withEffect,
            "The resource delta a move applies. MoveDef::effect carries it and "
            "ResolveHits applies it on contact -- on block as well as on hit, "
            "which is the genre norm for meter -- clamped to each resource's "
            "authored floor and ceiling.");

    addLoss(report, "resources", BuildLossDirection::Exact,
            static_cast<std::int32_t>(c.resources.size()),
            "Declared resources with their initial, floor and ceiling. "
            "FighterData::resources carries them in FILE ORDER, which is the "
            "positional contract the prover keys on, and Fighter::res is primed "
            "from `initial` on the match's first tick.");

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

// --- The cancel projection ---------------------------------------------------
//
// THE ONE THING THIS FUNCTION DECIDES, AND IT IS WORTH SPELLING OUT.
//
// The file authors a DELAY: "ticks between the source CONNECTING and the
// follow-up being allowed to start" (CharacterData.h). The kernel wants a
// WINDOW in the source move's own frame numbering, because Fighter::moveFrame is
// the only clock a tick has and GameState records that the current attack has
// connected without recording when (Combat.h says why that stays true). The
// conversion needs a contact frame, so this function picks one:
//
//     contact = startup,  the FIRST frame the move's hitbox is live
//
// and every consequence of that choice is counted in CancelStats::contactFrame.
// It is the earliest contact possible, so the resolved window opens at the
// earliest tick the file could permit and never later -- the error is uniformly
// permissive, bounded by `active - 1` ticks, and pointing in a direction a
// reader can reason about. The other two candidates were considered:
//
//   * LAST active frame. Uniformly conservative instead, bounded the same way.
//     Rejected because it makes tight authored chains impossible to perform,
//     and a combo the file says exists but the game cannot do is the failure
//     this whole bridge is here to stop being invisible.
//   * The midpoint. Rejected on sight: it is wrong in BOTH directions, needs a
//     division, and no sentence describes what it means.
//
// The move's own [open, close] window, when it authors one, is intersected on
// top -- so an edge is available from the later of "the delay has elapsed" and
// "the move's cancel window has opened", through the earlier of "the window
// closes" and "the move ends". Both bounds inclusive, matching CancelEdge.
bool buildCancels(const CharacterData& c, const std::string& who,
                  FighterData& out, MoveIndexMap& moves,
                  CancelStats& stats, BuildReport& report) {
    const std::size_t moveCount = c.moves.size();
    stats.authored = static_cast<std::int32_t>(c.cancels.size());

    // THE CAPACITY DECISION AGAIN, and the same answer as for moves: refuse,
    // never truncate. A character missing its last cancels is not a simpler
    // character, it is one whose combo graph has had edges deleted -- and the
    // verdict ProverAdapter computed was computed over the whole graph, so a
    // truncating bridge would certify a character and ship a different one.
    if (stats.authored > kMaxBuildableCancels) {
        report.error =
            who + ": authors " + num(stats.authored) + " cancel edges and the "
            "kernel holds " + num(kMaxBuildableCancels) +
            ". REFUSED rather than truncated: dropping the tail would delete "
            "edges from the combo graph the prover's verdict was computed over, "
            "which is exactly the engine/analysis disagreement this bridge "
            "exists to make visible. Raise kMaxCancelsPerFighter in "
            "cse/kernel/Combat.h -- a wire layout change both peers must agree "
            "on -- or cut edges in the file.";
        return false;
    }

    moves.fileCancelByEdge.reserve(c.cancels.size());

    // Per MOVE, so a source with eleven follow-ups is counted once. See the
    // comment on CancelStats::sourcesWithoutWindow.
    std::vector<bool> windowlessSourceCounted(moveCount, false);

    for (std::size_t i = 0; i < c.cancels.size(); ++i) {
        const Cancel& e = c.cancels[i];
        const std::string where =
            who + ".cancels[" + num(static_cast<std::int64_t>(i)) + "]";

        // An endpoint that does not name a move this build produced. The loader
        // calls a dangling id a load error, so this is defence against a
        // character assembled by hand -- and it is a DROP rather than a refusal
        // because the rest of the graph is still worth playing, provided somebody
        // is told. Silently keeping the edge is the one option that is worse than
        // both: it would hand the kernel a moveId nothing describes.
        const bool fromOk = e.from != kInvalidMove &&
                            static_cast<std::size_t>(e.from) < moveCount;
        const bool toOk   = e.to   != kInvalidMove &&
                            static_cast<std::size_t>(e.to)   < moveCount;
        if (!fromOk || !toOk) {
            ++stats.dropped;
            report.warnings.push_back(
                where + ": dropped. Its " +
                std::string(!fromOk ? "`from`" : "`to`") +
                " endpoint does not name a move this character has, so carrying "
                "it would put a fighter into a move slot nothing describes.");
            continue;
        }

        const Move& src = c.moves[e.from];

        if (e.delay < 0) {
            report.error = where + ": negative delay (" + num(e.delay) +
                           " ticks). A cancel that becomes legal before the move "
                           "it cancels has connected is not a thing the file "
                           "means to say.";
            return false;
        }

        // Non-negative by the move loop's own check, which ran before this
        // function was called. Recomputed rather than read off MoveDef so that
        // this file's two duration expressions cannot drift apart.
        const std::int64_t duration = static_cast<std::int64_t>(src.startup) +
                                      static_cast<std::int64_t>(src.active) +
                                      static_cast<std::int64_t>(src.recovery);

        std::int64_t earliest = static_cast<std::int64_t>(src.startup) +
                                static_cast<std::int64_t>(e.delay);
        std::int64_t latest   = duration - 1;

        if (src.hasCancelWindow) {
            if (static_cast<std::int64_t>(src.cancelWindowOpen) > earliest)
                earliest = src.cancelWindowOpen;
            if (static_cast<std::int64_t>(src.cancelWindowClose) < latest)
                latest = src.cancelWindowClose;
        } else if (!windowlessSourceCounted[e.from]) {
            windowlessSourceCounted[e.from] = true;
            ++stats.sourcesWithoutWindow;
        }

        if (earliest < 0) earliest = 0;   // moveFrame is never negative

        // int32 is the kernel's frame type. A move long enough to overflow it is
        // a file that needs fixing rather than a number to silently wrap.
        if (earliest > 0x7FFFFFFF || latest > 0x7FFFFFFF || latest < -0x7FFFFFFF) {
            report.error = where + ": resolved cancel window [" + num(earliest) +
                           ", " + num(latest) + "] does not fit the kernel's "
                           "32-bit frame counter.";
            return false;
        }

        // An empty window. Kept and counted rather than dropped: the edge is a
        // faithful record of what the file says, the kernel's two comparisons
        // make it inert with no special case, and `cancels (link, not cancel)`
        // is a more useful thing for a designer to read than a shorter table.
        if (earliest > latest) ++stats.inertLink;

        const bool requiresContact =
            e.on == Contact::Hit || e.on == Contact::Block;

        // Only edges that ACTUALLY depend on a contact tick are affected by the
        // first-active-frame reading, and only when there is more than one active
        // frame for the contact to have landed on. A one-frame active window has
        // exactly one possible contact frame, so the reading is exact there.
        if (requiresContact && src.active > 1) ++stats.contactFrame;
        if (e.on == Contact::Block || e.on == Contact::Whiff) ++stats.onBlockOrWhiff;
        if (!e.certain)      ++stats.uncertain;
        if (!e.guard.empty()) ++stats.withGuard;
        if (!e.effect.empty()) ++stats.withEffect;

        // AN EDGE GUARD STRICTER THAN ITS TARGET'S IS THE ONLY ONE THE KERNEL
        // CAN GET WRONG, so it is the only one worth a warning.
        //
        // CancelEdge carries no guard of its own (ROADMAP M1.1b measured every
        // character in this tree and found all 51 authored edge guards restate
        // the target move's own guard exactly), so the kernel enforces the
        // constraint through MoveDef::guard on the target and the two agree.
        // That agreement is an observation about today's data, not a property of
        // the schema -- which is what makes it worth CHECKING rather than
        // assuming. An edge that demanded more than its target would be
        // permitted where the file refuses, and this is the line that says so.
        {
            const cse::data::Move& target = c.moves[e.to];
            for (const cse::data::ResourceAmount& g : e.guard) {
                std::int32_t targetMin = 0;
                bool         found     = false;
                for (const cse::data::ResourceAmount& t : target.guard)
                    if (t.resource == g.resource) { targetMin = t.value; found = true; break; }
                if (!found || targetMin < g.value)
                    report.warnings.push_back(
                        "cancel " + c.moves[e.from].id + " -> " + target.id +
                        " requires " + num(g.value) + " of resource " +
                        num(static_cast<std::int32_t>(g.resource)) +
                        " and `" + target.id + "` itself requires " +
                        (found ? num(targetMin) : std::string("none")) +
                        ". The kernel checks the TARGET's guard, so this cancel "
                        "is permitted where the file refuses it. Give the edge "
                        "guard to the move, or CancelEdge needs a guard of its "
                        "own (ROADMAP M1.1b).");
            }
        }

        CancelEdge edge{};
        edge.from          = MoveIndexMap::KernelMoveIdOf(e.from);
        edge.to            = MoveIndexMap::KernelMoveIdOf(e.to);
        edge.earliestFrame = static_cast<std::int32_t>(earliest);
        edge.latestFrame   = static_cast<std::int32_t>(latest);
        edge.onHit         = requiresContact ? std::uint8_t{1} : std::uint8_t{0};
        edge.pad_[0] = 0;   // explicit: these bytes are hashed by the handshake
        edge.pad_[1] = 0;
        edge.pad_[2] = 0;

        out.cancels[stats.built] = edge;
        moves.fileCancelByEdge.push_back(static_cast<CancelIndex>(i));
        ++stats.built;
    }

    out.cancelCount   = stats.built;
    moves.cancelCount = stats.built;
    return true;
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

    // WALK SPEED, CARRIED WHOLE. CharacterData already quantized it once at load
    // (D8: quantise at the boundary, never in the kernel), so this is a copy and
    // not a conversion -- there is no second rounding to lose anything to, which
    // is what lets the loss row below say `exact`. A character that authored none
    // arrives here as zero and the kernel keeps its placeholder.
    out.walkSpeedSub = character.walkSpeedSub;

    // The resource declarations, in FILE ORDER, which is the whole contract:
    // slot i here is slot i of Fighter::res, of MoveDef::effect and of the
    // prover's own vector (ADR-001 section 8 item 7, assertion A03). Nothing
    // sorts or renames on the way through, because any reordering here would be
    // invisible and would make two builds of the same file disagree.
    const std::size_t declared = character.resources.size();
    out.resourceCount = static_cast<std::int32_t>(
        declared < static_cast<std::size_t>(cse::kernel::kMaxResources)
            ? declared
            : static_cast<std::size_t>(cse::kernel::kMaxResources));
    for (std::int32_t i = 0; i < out.resourceCount; ++i) {
        const cse::data::ResourceDef& r = character.resources[static_cast<std::size_t>(i)];
        out.resources[i].initial    = r.initial;
        out.resources[i].floor      = r.floor;
        out.resources[i].ceiling    = r.ceiling;
        out.resources[i].hasCeiling = r.hasCeiling ? 1u : 0u;
    }
    if (declared > static_cast<std::size_t>(cse::kernel::kMaxResources))
        report.warnings.push_back(
            "this character declares " + num(static_cast<std::int32_t>(declared)) +
            " resources and the kernel holds " + num(cse::kernel::kMaxResources) +
            "; the extra declarations are dropped, and every effect or guard "
            "naming one of them is dropped with it.");

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
        // The byte that used to be pad_ here is negativeEdge now, and the
        // schema does not author it yet (ROADMAP M1.1d): zero is "press
        // only", which is the behaviour every shipped move has today. Still
        // written explicitly, because the handshake hashes these bytes.
        m.negativeEdge = 0;

        // RESOURCES, SPARSE IN THE FILE AND DENSE IN THE KERNEL. The authored
        // form is a sorted list of (index, value) because a character may
        // declare four resources and a move may touch one; the kernel form is a
        // fixed array because D4 forbids unbounded growth in anything the
        // simulation reads and because an array indexed by the contract needs no
        // search at tick time. Scattering happens here, once.
        //
        // Out-of-range indices are dropped rather than clamped: index 3 landing
        // on slot 0 would silently spend the wrong resource, and A03 already
        // guarantees the loader resolved every name against this character's own
        // declaration. A count that exceeded kMaxResources is a load error.
        for (const cse::data::ResourceAmount& e : src.effect)
            if (e.resource < cse::kernel::kMaxResources)
                m.effect[e.resource] = e.value;
        for (const cse::data::ResourceAmount& g : src.guard)
            if (g.resource < cse::kernel::kMaxResources) {
                m.guard[g.resource] = g.value;
                m.guardMask = static_cast<std::uint8_t>(
                    m.guardMask | (1u << g.resource));
            }

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

    // --- The cancel graph ----------------------------------------------------
    CancelStats cancels{};
    if (!buildCancels(character, who, out, moves, cancels, report)) {
        // Same shape as every other refusal in this file: leave nothing that
        // looks like a fighter behind. A FighterData with a move table and no
        // cancel graph is precisely the character this task existed to stop
        // shipping.
        out    = FighterData{};
        moves  = MoveIndexMap{};
        return false;
    }

    // --- Cancels nobody can press --------------------------------------------
    //
    // A cancel is taken by holding the TARGET move's buttons (Combat.cpp
    // FindCancel), so an edge into a move with no binding is unreachable however
    // correct its window is. That is a property of the caller's binding table
    // rather than of the character, which is why it is a warning and not a loss
    // -- but it is the difference between "the cancel system does not work" and
    // "you did not bind the follow-up", and those cost very different afternoons.
    //
    // Counted and reported ONCE. One warning per edge would be 134 lines for
    // Kung Fu Girl and would bury the fourteen that matter.
    std::int32_t unreachable = 0;
    for (std::int32_t i = 0; i < out.cancelCount; ++i) {
        const std::uint16_t to = out.cancels[i].to;
        if (to < out.moveCount && out.moves[to].button == 0) ++unreachable;
    }
    if (unreachable > 0) {
        report.warnings.push_back(
            who + ": " + num(unreachable) + " of " + num(cancels.built) +
            " cancel edges point at a move with no button bound, so they can "
            "never be taken. Bind the follow-up in BuildOptions::bindings; the "
            "edge itself is built and correct.");
    }

    recordLosses(character, cancels, report);
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
