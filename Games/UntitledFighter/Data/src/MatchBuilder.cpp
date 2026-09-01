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
    std::int32_t offMid = 0, withHitstop = 0, withPosAdd = 0;
    std::int32_t withCornerPush = 0, withCounter = 0;
    std::int32_t withAirHitstun = 0, withLaunch = 0;

    for (const Move& m : c.moves) {
        if (m.cornerPushSub != 0) ++withCornerPush;
        for (const MotionKey& k : m.motion)
            if (k.posAddXSub != 0 || k.posAddYSub != 0) { ++withPosAdd; break; }
        if (m.stance != Stance::Any)        ++stanced;
        if (m.blockedAs != BlockHeight::Mid) ++offMid;
        if (!m.effect.empty())              ++withEffect;
        if (!m.guard.empty())               ++withGuard;
        if (m.pushbackSub != 0)             ++withPushback;
        if (m.hitstopTicks != 0)            ++withHitstop;
        if (m.counterHitstunBonus != 0 || m.counterDamageBonusHundredths != 0)
            ++withCounter;
        if (m.airHitstunTicks > 0)          ++withAirHitstun;
        if (m.launchVelYSub > 0)            ++withLaunch;
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

    addLoss(report, "cancel.on", BuildLossDirection::Exact,
            cancels.onBlockOrWhiff,
            "Edges authored `on: block` or `on: whiff`, carried whole since "
            "ROADMAP M1.3 slice (a): CancelEdge::contactMask keeps all four "
            "Contact values (hit / block / whiff as bits; `always` is the "
            "ungated 0), and the attacker can now OBSERVE all three outcomes "
            "-- alreadyHitBits for contact, its blocked mirror in "
            "Fighter::flags' low byte for how the contact went. `on: hit` no "
            "longer fires off a blocked contact, which the old one-bit "
            "collapse permitted. Counted so the row still says how many edges "
            "the OLD reading used to move. The MODEL keeps its own collapse "
            "({hit, always} usable) and its own `cancel.on` ledger row -- a "
            "whiff edge the kernel honours is still an edge the prover's "
            "graph skips, the D8 gap the kara showcase variant exists to "
            "demonstrate. Conversion caveat: MUGEN's Movecontact means hit OR "
            "block, and the corpus transcribed it as `hit` (schema KNOWN "
            "GAP), so a converted character may now refuse a chain MUGEN "
            "allowed on block -- that is the FILE's claim honoured, not a "
            "kernel choice.");

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

    std::int32_t juggleSpenders = 0;
    for (const Move& m : c.moves)
        for (const ResourceAmount& e : m.effect)
            for (std::size_t r = 0; r < c.resources.size(); ++r)
                if (static_cast<std::size_t>(e.resource) == r &&
                    c.resources[r].name == "juggle" && e.value < 0)
                    ++juggleSpenders;
    addLoss(report, "character.movement", BuildLossDirection::Exact,
            (c.jumpImpulseSub != 0 ? 1 : 0) + (c.gravitySub != 0 ? 1 : 0),
            "engine.movement's jump_impulse_sub and gravity_sub, carried whole "
            "into the FighterData slots the kernel has consulted since M1.1b "
            "(ROADMAP M1.3(b1), ADR-014). Kernel semantics at load -- +Y up, "
            "positive, explicit zero refused as the unauthored sentinel -- so "
            "the carry is two copies. A silent file keeps the placeholder arc "
            "(5 px/tick against 0.25 px/tick^2, 38 airborne ticks). The MODEL "
            "has no jump vocabulary at all, so an authored arc changes the "
            "GAME's strings and none of the prover's -- the D8 gap ADR-011 "
            "section 2.8 assigns to this ledger, and what the floaty_jump "
            "variant exhibits.");

    addLoss(report, "resource.juggle (gate)", BuildLossDirection::Exact,
            juggleSpenders,
            "The juggle budget, wired as the ranking certificate's own "
            "mechanism (M1.1f): FighterData::juggleMax carries the resource's "
            "authored initial and each spending move's cost mirrors its "
            "authored delta, so ResolveHits REFUSES the overspending hit -- "
            "the one thing the clamped effect path never could. A character "
            "with no resource named `juggle` keeps zero on both halves and "
            "the gate never fires.");

    addLoss(report, "character.input_buffer_frames", BuildLossDirection::Exact,
            c.inputBufferFrames != 0 ? 1 : 0,
            "The modern input buffer's window, carried whole into "
            "FighterData::inputBufferFrames; the kernel keeps a press made "
            "while the fighter cannot act for this many ticks and consumes it "
            "the exact tick they can, on both start routes. A window of N "
            "accepts N+1 ticks (the press tick plus N), so the genre's "
            "three-frame feel is authored as 2. Zero is no buffering. "
            "Authored value: " + num(c.inputBufferFrames) + ".");

    addLoss(report, "move.pushback", BuildLossDirection::Exact, withPushback,
            "Defender displacement on hit, carried whole into MoveDef::pushbackHit "
            "and applied by ResolveHits. ADR-001 section 6.3 records that this "
            "number is ESTIMATED in every shipped character and that the midscreen "
            "verdict turns on it -- so the estimate is now the game's behaviour "
            "rather than a number nothing reads, which makes the estimate matter "
            "MORE and not less. Saturated at the int16 slot, with a warning "
            "naming both numbers when a file exceeds it.");

    addLoss(report, "move.corner_push", BuildLossDirection::Exact, withCornerPush,
            "engine.reaction.corner_push_vel_sub, carried whole into "
            "MoveDef::cornerPushHit (M1.6's microwalk slice): when a hit "
            "lands on a defender the wall already stops, the ATTACKER recoils "
            "by this much -- the wall absorbs the defender's pushback and the "
            "pressure re-opens the gap a microwalk then closes. fighter_a "
            "authors the key at 0 on all 22 moves, so the shipped character "
            "is unchanged and the microwalk showcase variant is where the "
            "field first bites. The MODEL has no corner in its vocabulary at "
            "all; its midscreen/corner split is a stage choice, not a rule "
            "per hit.");

    addLoss(report, "move.counter_hit", BuildLossDirection::Exact, withCounter,
            "engine.reaction.counter_hit {hitstun_bonus, damage_bonus}, carried "
            "whole into MoveDef::counterHitstunBonus/counterDamageBonus (ROADMAP "
            "M1.3(c)): ResolveHits adds both when the defender is caught "
            "MID-STARTUP -- startup only; a trade is a trade and a punish is its "
            "own reward. The MODEL charges the bonus per OPENING (ADR-015 option "
            "3): its counter verdict charges every hit, first hit in the game, "
            "which is the Permissive direction and is named in the prover's own "
            "loss table rather than here.");

    addLoss(report, "move.air_hitstun", BuildLossDirection::Exact, withAirHitstun,
            "engine.reaction.air_hitstun_ticks, carried whole into "
            "MoveDef::airHitstun (ROADMAP M1.3(d)): ResolveHits charges it as "
            "the BASE stun against an AIRBORNE defender, falling back to the "
            "ground number where the file authors none. It was loaded and "
            "thrown away from the day the reaction block landed; the launcher "
            "is what made it reachable. fighter_a authors it on all 22 moves; "
            "no MUGEN transcription authors a nonzero value.");

    addLoss(report, "move.launch", BuildLossDirection::Exact, withLaunch,
            "engine.reaction.launch {vel_x_sub, vel_y_sub}, carried whole "
            "(ROADMAP M1.3(d)): a clean hit takes the defender off the ground "
            "with the authored velocity, X pointed away from the attacker by "
            "the kernel's position rule, the arc then owned by StepPhysics "
            "like a jump -- and kept through stun (Fighter::reaction marks a "
            "launched body; an UN-launched air hit still drops straight, the "
            "behaviour the crossplat golden pins). The MODEL has no defender "
            "position at all; the air OPENING is where the file's air numbers "
            "reach a verdict.");

    addLoss(report, "move.hitstop", BuildLossDirection::Exact, withHitstop,
            "Impact freeze on hit, carried whole into MoveDef::hitstop (ROADMAP "
            "M1.3i) and imposed on BOTH fighters by ResolveHits, so every clock "
            "-- move frames, stun, the arc -- stands still together and no "
            "frame-data relationship moves; only wall-clock periods stretch. "
            "The MODEL still has no freeze in its vocabulary, which is fine "
            "for the certificate (relationships are freeze-invariant) and "
            "visible to a masher: the freeze shifts re-press phase against a "
            "one-tick link, which is what the one_frame_link variants zero it "
            "for. Saturated at the uint16 slot.");

    addLoss(report, "move.stance", BuildLossDirection::Exact, stanced,
            "What the fighter must be in to START the move, mapped by name into "
            "MoveDef::stance and enforced by StanceAllows on both start routes. "
            "Selection reads the held INPUT (is Down held now) and the posture "
            "then follows the started move, so a cross-posture gatling works "
            "and two variants of a shared button stop shadowing each other -- "
            "the wire ROADMAP M1.3e records moving the measured headline.");

    addLoss(report, "move.blocked_as", BuildLossDirection::Exact, offMid,
            "The guard height that stops the move, mapped by name into "
            "MoveDef::blockedAs -- the kernel's zero is Mid (stopped by both "
            "guards), so an unmapped value could never invent an unblockable. "
            "With it carried, a low goes through a standing block and an "
            "overhead through a crouching one, on the shipped file and not "
            "only on synthetic benches.");

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

    addLoss(report, "move.engine.motion", BuildLossDirection::Exact, withMotion,
            "The attacker's own velocity keys, carried whole since ROADMAP "
            "M1.3(b2): MoveDef::motion owns a committed fighter's velocity "
            "from each key's tick to the next -- the lunge, the hop kick's "
            "physics, the divekick -- with the one Y-sign flip (MUGEN "
            "Y-down to kernel Y-up) applied here at load. A move authoring "
            "none keeps commitment's zero velocity, which is every move "
            "before this wire. The MODEL never reads motion -- a special "
            "that travels forward still connects, to the prover, only from "
            "where it started -- so the D8 gap moved from the GAME lacking "
            "the mechanic to the MODEL lacking the vocabulary.");

    addLoss(report, "move.engine.motion (pos_add)", BuildLossDirection::KernelOmits,
            withPosAdd,
            "Teleport components (MUGEN PosAdd) on motion keys. NOT carried: "
            "a position ADD interacts with the wall clamp and the pushbox "
            "separation in ways nothing has pinned, and carrying it as a "
            "velocity would smear a step across a tick. One authored key in "
            "the corpus (fighter_a_infinite's special_dash_punch, a 2 px "
            "step); its dash still flies the velocity half of its keys.");

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
                           " ticks). A cancel that becomes legal before its "
                           "own anchor frame is not a thing the file means to "
                           "say.";
            return false;
        }

        // Non-negative by the move loop's own check, which ran before this
        // function was called. Recomputed rather than read off MoveDef so that
        // this file's two duration expressions cannot drift apart.
        const std::int64_t duration = static_cast<std::int64_t>(src.startup) +
                                      static_cast<std::int64_t>(src.active) +
                                      static_cast<std::int64_t>(src.recovery);

        // WHERE `delay` COUNTS FROM depends on what the edge waits for. A
        // hit- or block-gated edge waits for CONTACT, so its delay anchors at
        // the source's first active frame (the earliest a contact exists to
        // count from -- the `cancel.contact_frame` row is the cost of that
        // reading). A WHIFF edge waits for nothing: the move is whiffing from
        // its first frame, so its delay anchors at frame 0 -- which is what
        // makes a kara (`on: whiff` in the first startup frames) authorable
        // at all, since startup anchoring puts every frame below `startup`
        // out of reach. `always` KEEPS the startup anchor it has shipped
        // with since v1: no authored `always` edge exists that wants earlier
        // frames (measured across the corpus: 89, all with this reading),
        // and moving 89 edges' windows to fix zero of them is churn, not
        // fidelity.
        std::int64_t earliest =
            (e.on == Contact::Whiff ? std::int64_t{0}
                                    : static_cast<std::int64_t>(src.startup)) +
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
        // The four Contact values, BY NAME (the M1.3e enum lesson: the data
        // and kernel orders must never be cast into each other). `always` is
        // the ungated 0 -- the byte the old collapse gave it -- and `hit`
        // is kContactHit == 1, the byte the old collapse gave it, so an
        // all-hit character's MatchData hash does not move.
        switch (e.on) {
            case Contact::Hit:    edge.contactMask = cse::kernel::kContactHit;   break;
            case Contact::Block:  edge.contactMask = cse::kernel::kContactBlock; break;
            case Contact::Whiff:  edge.contactMask = cse::kernel::kContactWhiff; break;
            case Contact::Always: edge.contactMask = 0;                          break;
        }
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

    // THE PUSHBOX DEFAULTS TO THE BODY, which is the roadmap's plan (M1.2:
    // "default from MatchBuilder.h's BodySpec") and is the conservative reading
    // of a file that does not author one: a character occupies the space it can
    // be hit in. `engine.constants.default_pushbox_sub` exists on `fighter_a`
    // and is deliberately NOT read yet -- it is authored in MUGEN's Y-DOWN
    // convention, which that file warns about at length in its own
    // `the_y_axis_trap_in_this_very_file` note, and reading it without the
    // conversion would bury a body sixty pixels underground where nothing
    // touches it. That wire is M1.2's, with the flip written down and tested.
    out.pushbox = out.hurtbox;

    // THE CROUCHING BODY, same width and the authored height. Left degenerate
    // when the file authors none, which is how the kernel reads "unauthored" and
    // is why a character written before this field keeps one box for both
    // postures.
    //
    // A crouch TALLER than the stand is refused rather than clamped: it is not a
    // near miss to be tidied, it is a file saying something impossible, and
    // silently making it the standing height would hide the typo behind a
    // character who cannot duck anything.
    if (character.crouchHeightSub > 0) {
        if (character.crouchHeightSub > height) {
            report.error = who + ": engine.constants.crouch_height_px is " +
                           num(character.crouchHeightSub / cse::kernel::kSubUnitsPerPixel) +
                           " px and the standing body is " +
                           num(height / cse::kernel::kSubUnitsPerPixel) +
                           " px. A crouch cannot be taller than the stand.";
            return false;
        }
        out.crouchHurtbox = Box{ -halfWidth, 0, halfWidth, character.crouchHeightSub };
    }

    // WALK SPEED, CARRIED WHOLE. CharacterData already quantized it once at load
    // (D8: quantise at the boundary, never in the kernel), so this is a copy and
    // not a conversion -- there is no second rounding to lose anything to, which
    // is what lets the loss row below say `exact`. A character that authored none
    // arrives here as zero and the kernel keeps its placeholder.
    out.walkSpeedSub = character.walkSpeedSub;

    // JUMP PHYSICS, CARRIED WHOLE (ROADMAP M1.3(b1), ADR-014). The loader
    // already enforced kernel semantics (+Y up, positive, explicit-zero
    // refused), so this is two copies into the FighterData slots the kernel
    // has consulted since M1.1b -- `!= 0 ? authored : placeholder` -- and a
    // silent file arrives as zero and keeps the placeholder arc every
    // measured count was derived on.
    out.jumpImpulseSub = character.jumpImpulseSub;
    out.gravitySub     = character.gravitySub;

    // THE INPUT BUFFER WINDOW, CARRIED WHOLE (ROADMAP M1.1e). Integer ticks,
    // already bounded at 255 by the loader for the kernel's uint8 age; zero is
    // no buffering, the behaviour every silent file has always had.
    out.inputBufferFrames = character.inputBufferFrames;

    // THE JUGGLE BUDGET (ROADMAP M1.1f): Fighter::juggle becomes the MIRROR of
    // the resource the file calls `juggle` -- the same authored numbers the
    // ranking certificate is computed from, wired into the one gate that can
    // REFUSE a hit where ApplyEffects only clamps. Found BY NAME, because the
    // positional contract (A03) fixes order across files of one build but says
    // nothing about which slot juggling lives in; a character with no resource
    // named juggle keeps zero on both halves, and the gate -- juggleCost > 0 --
    // never fires for it, which is every character before this wire.
    //
    // BOTH HALVES OR NEITHER, per the ROADMAP entry: a budget with no cost
    // never depletes, a cost with no budget refuses every hit. The costs land
    // in the per-move loop below, from the same resource index.
    std::int32_t juggleResource = -1;
    for (std::size_t r = 0; r < character.resources.size() &&
                            r < static_cast<std::size_t>(cse::kernel::kMaxResources); ++r)
        if (character.resources[r].name == "juggle")
            juggleResource = static_cast<std::int32_t>(r);
    if (juggleResource >= 0) {
        constexpr std::int32_t kMaxBudget = 32767;   // Fighter::juggle is int16
        std::int32_t budget = character.resources[juggleResource].initial;
        if (budget < 0) budget = 0;
        if (budget > kMaxBudget) budget = kMaxBudget;
        out.juggleMax = budget;
    }

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

        // STANCE AND GUARD HEIGHT (ROADMAP M1.3e), mapped BY NAME and never by
        // cast: the two enum families order their values differently -- data
        // Air is 2 where kernel kStanceAir is 4, data High is 0 where the
        // kernel makes Mid the zero -- so a bare static_cast is in range,
        // compiles, and ships crouching normals as air moves and overheads as
        // mids. This wire is what makes `stand_hk` stop shadowing `crouch_hk`:
        // StanceAllows can finally tell the two variants of a shared button
        // apart, so 12 of fighter_a's 18 normals become performable at all.
        switch (src.stance) {
            case cse::data::Stance::Ground:    m.stance = cse::kernel::kStanceGround;    break;
            case cse::data::Stance::Air:       m.stance = cse::kernel::kStanceAir;       break;
            case cse::data::Stance::Standing:  m.stance = cse::kernel::kStanceStanding;  break;
            case cse::data::Stance::Crouching: m.stance = cse::kernel::kStanceCrouching; break;
            case cse::data::Stance::Any:       m.stance = cse::kernel::kStanceAny;       break;
        }
        switch (src.blockedAs) {
            case cse::data::BlockHeight::High: m.blockedAs = cse::kernel::kBlockedAsHigh; break;
            case cse::data::BlockHeight::Low:  m.blockedAs = cse::kernel::kBlockedAsLow;  break;
            case cse::data::BlockHeight::Mid:  m.blockedAs = cse::kernel::kBlockedAsMid;  break;
        }

        // KNOCKBACK, the first of ADR-005 P2's mechanics this bridge ever
        // carried. The kernel has applied `pushbackHit` since P2 and every
        // shipped move authors a `pushback`; until ROADMAP M1.3d nothing joined
        // them, so a built character's hits moved nobody.
        //
        // SATURATED, NOT WRAPPED. MoveDef stores this as an int16 to hold its
        // size and CharacterData stores sub-units as int32. A bare cast turns a
        // 128-pixel pushback into a PULL, which reads as a physics bug rather
        // than as the range error it is. A file past the limit gets the limit
        // and a warning naming both numbers.
        {
            constexpr std::int32_t kMaxPushback = 32767;
            std::int32_t push = src.pushbackSub;
            if (push > kMaxPushback || push < -kMaxPushback) {
                const std::int32_t clamped = push > 0 ? kMaxPushback : -kMaxPushback;
                report.warnings.push_back(
                    where + ": pushback " + num(push) +
                    " sub-units exceeds MoveDef::pushbackHit's int16 range and is "
                    "clamped to " + num(clamped) + " (" +
                    num(clamped / cse::kernel::kSubUnitsPerPixel) +
                    " px). A move authored past that is describing a distance no "
                    "fighting game uses.");
                push = clamped;
            }
            m.pushbackHit = static_cast<std::int16_t>(push);
        }

        // CORNER PUSH, CARRIED WHOLE (M1.6's microwalk slice), saturated at
        // its int16 slot for pushback's own reason.
        {
            constexpr std::int32_t kMaxPushback = 32767;
            std::int32_t recoil = src.cornerPushSub;
            if (recoil > kMaxPushback) recoil = kMaxPushback;
            if (recoil < -kMaxPushback) recoil = -kMaxPushback;
            m.cornerPushHit = static_cast<std::int16_t>(recoil);
        }

        // COUNTER HIT (ROADMAP M1.3(c)), carried whole: the stun bonus in
        // ticks, the damage bonus through the SAME hundredths-to-points rule
        // as the damage it rides on -- one documented quantization, applied
        // identically, so a bonus of 0.5 loses its half-point exactly where
        // a damage of 0.5 does.
        m.counterHitstunBonus = src.counterHitstunBonus;
        m.counterDamageBonus  = damagePointsFromHundredths(src.counterDamageBonusHundredths);

        // AIR HITSTUN AND THE LAUNCH (ROADMAP M1.3(d)), carried whole. The
        // air number was loaded-and-uncarried from the day the reaction block
        // landed -- the D8 gap ADR-015's air opening reads the file about --
        // and the launcher is what makes it REACHABLE: ResolveHits reads the
        // air number only off an airborne defender, and nothing put one there
        // until launch crossed. Negative air stun clamps to zero (a negative
        // means "no stun" at the schema level and the kernel's fallback wants
        // zero as its sentinel); the loader already refused a non-positive
        // launch Y and a negative launch X.
        m.airHitstun    = src.airHitstunTicks > 0 ? src.airHitstunTicks : 0;
        m.launchVelXSub = src.launchVelXSub;
        m.launchVelYSub = src.launchVelYSub;

        // HITSTOP, CARRIED WHOLE (ROADMAP M1.3i). It was held back from M1.3d
        // for a measured reason -- the freeze moves every frame-exact count in
        // the old hand-derived sweep -- and that objection died when M1.4 made
        // the counts properties: the freeze moves wall-clock periods and no
        // frame-data RELATIONSHIP, because it freezes BOTH fighters and
        // "startup 5 is still five ticks OF THE MOVE" (Combat.h). Saturated at
        // the uint16 slot for the reason pushback is.
        {
            constexpr std::int32_t kMaxFreeze = 65535;
            std::int32_t freeze = src.hitstopTicks;
            if (freeze < 0) freeze = 0;
            if (freeze > kMaxFreeze) freeze = kMaxFreeze;
            m.hitstop = static_cast<std::uint16_t>(freeze);
        }

        // KNOCKDOWN, from engine.reaction, saturated at its unsigned 16-bit
        // slot for the reason pushback is: a wrap turns a 20-tick knockdown
        // into an 18-hour one.
        {
            constexpr std::int32_t kMaxTicks = 65535;

            // The FILE says "this knocks down" and "getting up takes N"; the
            // kernel counts one number down. A move that does not knock down
            // leaves this zero however long its fall_recover is, because
            // fall_recover describes the knockdown and not the move.
            const std::int32_t fall = src.causesKnockdown ? src.fallRecoverTicks : 0;
            m.knockdownTicks = static_cast<std::uint16_t>(
                fall < 0 ? 0 : (fall > kMaxTicks ? kMaxTicks : fall));
        }

        // AUTHORED MOTION, CARRIED AS VELOCITIES (ROADMAP M1.3(b2), ADR-014).
        // The file's keys are MUGEN-signed -- velYSub DOWN-positive -- and the
        // kernel's velY is +Y up, so the ONE sign flip happens here, at load,
        // where D8 says the one documented conversion lives. Keys are taken in
        // ascending fromTick order (sorted here, stably, so an unsorted file
        // and a sorted one build the same bytes); negative ticks are refused
        // as meaning nothing; keys past the kernel's fixed bound are dropped
        // with a warning naming the count. `pos_add` teleport components are
        // NOT carried -- a teleport interacts with the wall clamp in ways no
        // test has pinned yet -- and the `move.engine.motion (pos_add)` row
        // counts what that drops.
        {
            std::vector<cse::data::MotionKey> keys = src.motion;
            std::stable_sort(keys.begin(), keys.end(),
                             [](const cse::data::MotionKey& a,
                                const cse::data::MotionKey& b) {
                                 return a.tick < b.tick;
                             });
            std::int32_t kept = 0;
            for (const cse::data::MotionKey& k : keys) {
                if (k.tick < 0) {
                    report.error = where + ": motion key at tick " +
                                   num(k.tick) + " -- a key before the move "
                                   "starts is not a thing the file means.";
                    return false;
                }
                if (kept >= cse::kernel::kMaxMotionKeys) {
                    report.warnings.push_back(
                        where + ": authors " +
                        num(static_cast<std::int64_t>(keys.size())) +
                        " motion keys and the kernel holds " +
                        num(cse::kernel::kMaxMotionKeys) +
                        "; the extra keys are dropped from the end of the "
                        "sorted list, so the move flies its opening.");
                    break;
                }
                m.motion[kept].fromTick = k.tick;
                m.motion[kept].velXSub  = k.velXSub;
                m.motion[kept].velYSub  = -k.velYSub;   // MUGEN Y-down -> kernel Y-up
                ++kept;
            }
            m.motionCount = kept;
        }


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
        for (const cse::data::ResourceAmount& e : src.effect) {
            if (e.resource < cse::kernel::kMaxResources)
                m.effect[e.resource] = e.value;
            // The juggle SPEND doubles as the gate's cost (M1.1f): the same
            // authored number, mirrored into the field ResolveHits can refuse
            // on, where the effect path only clamps at the floor. A juggle
            // GAIN is not a cost and gates nothing.
            if (juggleResource >= 0 &&
                static_cast<std::int32_t>(e.resource) == juggleResource &&
                e.value < 0) {
                std::int32_t cost = -e.value;
                if (cost > 32767) cost = 32767;   // MoveDef::juggleCost is int16
                m.juggleCost = static_cast<std::int16_t>(cost);
            }
        }
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
    // StepAttack scans the table from slot 1 upward and takes the FIRST move
    // all of whose bits are held AND whose stance the held input selects. So a
    // move whose buttons are a superset of an earlier move's can never start
    // -- UNLESS their stances are ones a single tick's input tells apart,
    // because since ROADMAP M1.3e `StanceAllows` reads the held input and the
    // two are SELECTED rather than shadowed: {LP} standing at slot 1 and
    // {Down|LP} crouching at slot 12 both start, Down decides, and that pair
    // is the natural way somebody binds crouching normals. The warning fires
    // only where the stance test cannot separate the two.
    //
    // Reported rather than reordered. Reordering would change the move indices,
    // and those indices are the thing the whole file exists to keep aligned with
    // the ids ProverAdapter reports.
    const auto grounded = [](Stance s) {
        return s == Stance::Standing || s == Stance::Crouching || s == Stance::Ground;
    };
    const auto stanceSeparates = [&grounded](Stance a, Stance b) {
        if ((a == Stance::Air && grounded(b)) || (b == Stance::Air && grounded(a)))
            return true;
        return (a == Stance::Standing && b == Stance::Crouching) ||
               (a == Stance::Crouching && b == Stance::Standing);
    };
    for (std::size_t j = 0; j < moveCount; ++j) {
        if (button[j] == 0) continue;
        for (std::size_t i = 0; i < j; ++i) {
            if (button[i] == 0) continue;
            if ((button[j] & button[i]) != button[i]) continue;
            if (stanceSeparates(character.moves[j].stance,
                                character.moves[i].stance)) continue;
            report.warnings.push_back(
                who + ": move `" + character.moves[j].id + "` (slot " +
                num(static_cast<std::int64_t>(j + 1)) + ") can never start. Its "
                "buttons are a superset of `" + character.moves[i].id + "`'s (slot " +
                num(static_cast<std::int64_t>(i + 1)) + "), their stances do not "
                "tell the two apart, and StepAttack takes the first move in slot "
                "order whose buttons are all held, so the earlier one always "
                "wins.");
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
