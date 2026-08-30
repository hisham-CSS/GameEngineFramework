// The readouts: what the simulation is doing, and what the analysis said about
// it.
//
// ===========================================================================
// THE RULE THIS FILE EXISTS TO OBEY
// ===========================================================================
// EVERYTHING DRAWN HERE IS READ FROM THE SIMULATION OR FROM THE ANALYSIS. Not
// recomputed beside them, not accumulated in parallel, not remembered from last
// frame. The model below is pointers to the real objects and a handful of the
// host's own scalars, and it is rebuilt every frame from nothing, precisely so
// that there is no place for a second copy of a fact to live.
//
// The failure this is aimed at is ARCHITECTURE.md D8 -- the engine and the
// analysis disagreeing by a frame -- moved down to the presentation layer, where
// it is WORSE than it is in the analysis. A designer whose tool disagrees with
// the engine can open both files and find out. A playtester told "+2" by a HUD
// that computed it with its own arithmetic cannot: they will simply conclude the
// game is wrong about a number the game never said.
//
// So the frame counters, the stun, the move ids and the hit counts below are
// fields, not sums. There are exactly two derivations in this file -- the
// frame-advantage answer and the shadowed-binding answer -- and each one names
// the kernel fields and the kernel rule it came from, in a comment, at its
// declaration.
//
// THE ONE THING THAT IS REMEMBERED, AND WHY IT IS NOT A SECOND COPY OF ANYTHING.
// LatchedAdvantage below is measured on a PAST TICK and carried forward. That is
// not the thing the rule above forbids, and the distinction is worth stating
// because it is the distinction between an observer and a shadow copy: a shadow
// copy answers a question about NOW using bytes written THEN, and can therefore
// disagree with the simulation. A latched measurement answers a question about
// THEN, carries the tick it was taken on, and prints it -- so there is nothing
// for it to disagree with. ComboWatcher is the same object one layer down: hits,
// damage and gapTicks are all accumulated from ticks that have gone, and this
// file has drawn them from the beginning. What makes both honest is that the
// tick is on the screen.
//
// ===========================================================================
// THE SCREEN HAS A BOTTOM, AND THIS FILE IS THE ONE THAT HAS TO BELIEVE IT
// ===========================================================================
// Every panel here is as tall as the words that went into it, which is right,
// and it means NO PANEL'S HEIGHT IS KNOWN UNTIL IT HAS BEEN LAID OUT. A column
// that starts at the top and stacks panels downward therefore has no idea
// whether it has run off the bottom of the window, and at 1280x720 -- the size
// the player opens at -- it did: the bindings panel ran to y730 in a 720 px
// window and the legend, drawn on the same layer and submitted later, painted
// over what was left of it.
//
// So the layout is MEASURED. Each panel is laid out twice, through the same Pen
// with its ink turned off the first time, so the measured height and the drawn
// height cannot differ -- they are the same code. A column is given a bottom,
// and a panel that does not fit inside it is DROPPED WHOLE and named, rather
// than drawn over the top of the furniture. What that costs and what it buys is
// argued at Column in the implementation; the rule it enforces is simply that
// nothing on this screen may be painted where something else already is.
//
// ===========================================================================
// WHY THE MODEL IS BORROWED POINTERS RATHER THAN A FLATTENED SNAPSHOT
// ===========================================================================
// A struct of copied values would be easier to pass around and would be the
// wrong shape twice over. It allocates and copies once per frame for no reason;
// and, structurally, it is a second representation of the state, which is the
// thing this whole file is built not to have. The model is filled in and used
// inside one call, so every pointer in it is alive for exactly as long as it is
// read.
#pragma once

#include "Engine.h"

#include "FightView.h"

#include "cse/data/CharacterData.h"
#include "cse/data/MatchBuilder.h"
#include "cse/data/ProverAdapter.h"

#include "cse/game/ComboWatcher.h"
#include "cse/game/FightSession.h"

#include "cse/kernel/Combat.h"
#include "cse/kernel/GameState.h"

#include <cstdint>
#include <string>
#include <vector>

namespace untitledfighter {

// --- One row of the binding table, as the playtester sees it ------------------

struct BindingRow {
    const char*   keyLabel = "";   // what to press
    const char*   moveId   = "";   // the id in the character file
    std::uint16_t button   = 0;    // the cse::kernel::kInput* bits this holds
    std::uint16_t slot     = 0;    // kernel move id; 0 when this character has no
                                   // such move (MoveIndexMap::Find's own sentinel)

    // THE SECOND DERIVATION IN THIS FILE, and it is a rule out of Combat.cpp.
    //
    // StepAttack starts a move from IDLE by scanning FighterData::moves in slot
    // order and taking the first one all of whose bits are held. So if an
    // EARLIER slot's button mask is a SUBSET of this one's, holding this move's
    // bits also satisfies that earlier move, and the earlier move wins every
    // single time -- this row's key can never start this row's move from
    // neutral. That is the exact reason a stance modifier ("down plus punch")
    // cannot be expressed against a character whose standing punch comes first
    // in the file.
    //
    // It is SHOWN rather than silently avoided, because a key that does nothing
    // and says nothing is the failure this project keeps naming: the shipped
    // example script referenced an action nobody bound, read false forever, and
    // there was nothing to see (InputMap.h). The move is still reachable by a
    // CANCEL -- FindCancel scans the cancel table, not the move table, so
    // nothing shadows it there -- and the row says that too.
    bool shadowed = false;
};

// --- Frame advantage ----------------------------------------------------------

// WHICH OF THE KERNEL'S THREE WAYS OUT the answer below came by. It is on the
// screen next to the number, because +10 by way of a cancel and +10 by way of a
// recovery are two different pieces of advice to a player and the number alone
// cannot tell them apart.
enum class ActRoute : std::uint8_t {
    Free,       // nothing is holding this fighter; the next tick is theirs
    Stun,       // Fighter::hitstun / blockstun has to run out first
    Recovery,   // the move in progress has to reach MoveDuration
    Cancel,     // a CancelEdge opens first, and this is the earliest one
};

struct Actionable {
    // Ticks from the END of the tick most recently simulated. Always at least 1:
    // a tick that has already run cannot be acted on.
    std::int32_t  ticks = 1;
    ActRoute      route = ActRoute::Free;
    // The kernel move id the earliest cancel leads to; 0 unless route is Cancel.
    std::uint16_t cancelTo = 0;
};

// The soonest this fighter could start a move again, if they press the fastest
// thing available to them.
//
// EVERY TERM IS A KERNEL FIELD OR A KERNEL FUNCTION, AND THERE ARE THREE:
//
//   Fighter::hitstun / Fighter::blockstun. StepPhysics decrements these at the
//   TOP of a tick, before `Actionable()` -- which is `hitstun == 0 &&
//   blockstun == 0` -- is evaluated. So a fighter observed with
//   stun h at the end of tick t is free on tick t + h, and on t + 1 when h is 0
//   or 1. That is the same rule ComboWatcher.h states as "actionable at tick t
//   iff their hitstun as observed at the end of tick t-1 was <= 1". It is a
//   FLOOR under the other two: StepAttack's `if (!Actionable(f)) return` sits
//   above both the cancel test and the button scan.
//
//   Fighter::moveId / Fighter::moveFrame against cse::kernel::MoveDuration.
//   StepAttack increments moveFrame and ends the move when it REACHES the
//   duration, in the same call that then runs the button scan (Combat.cpp), so a
//   fighter observed in a move at frame f is free on tick t + (duration - f).
//   MoveDuration is CALLED and not re-added from startup + active + recovery,
//   so this cannot come to disagree with the kernel about how long a move is --
//   it clamps each part at zero, and a re-implementation that did not would give
//   a move with an authored negative recovery a different length here than in
//   the tick.
//
//   FighterData::cancels, THROUGH cse::kernel::CancelIsOpen. THIS IS THE TERM
//   THE READOUT ORIGINALLY DID NOT HAVE, AND IT IS THE ONE THIS GAME IS ABOUT.
//   StepAttack runs the cancel test BETWEEN the lifecycle and the button scan
//   (Combat.cpp), so waiting out the recovery is only one of the two ways a move
//   ends -- on fighter_a's `stand_lp -> stand_mp delay 2` the attacker is free
//   from moveFrame 5, not from 14, and a readout that reported 14 was answering
//   a question about a fighting game with no cancel system in it, by NINE TICKS.
//
//   The window and the contact gate are asked of CancelIsOpen rather than
//   compared here, against a copy of the fighter with moveFrame moved forward to
//   the candidate frame; the target's existence and its non-zero button are
//   FindCancel's own two skips. What is left in this file is one max() -- the
//   earliest candidate is `max(edge.earliestFrame, moveFrame + 1)`, because
//   FindCancel runs after `++f.moveFrame` -- and a minimum over the table, which
//   answers a DIFFERENT question from FindCancel's: FindCancel returns the first
//   edge that is open with THESE buttons held on THIS tick, and this returns the
//   earliest tick any edge could open at all. Ties go to the first edge in file
//   order, which is FindCancel's own tie-break and is chosen here for its reason.
Actionable TicksUntilActionable(const cse::kernel::FighterData& data,
                                const cse::kernel::Fighter& fighter);

struct Advantage {
    // False when the question is not being asked -- see FrameAdvantage.
    bool          known = false;
    // Positive: the attacker recovers first, by this many ticks.
    std::int32_t  ticks = 0;
    // The two halves the difference was taken between, kept so the readout can
    // show its working. A single signed number is not teachable on its own;
    // "they act in 22, you in 2" is.
    std::int32_t  attackerTicks = 0;
    std::int32_t  defenderTicks = 0;
    ActRoute      attackerRoute = ActRoute::Free;
    std::uint16_t attackerCancelTo = 0;
};

// How many ticks earlier than the defender the fighter in `attackerSlot` can
// act, if each of them does the fastest thing available.
//
// `known` is false unless the DEFENDER IS IN STUN. Outside that the difference
// is still computable and means nothing a playtester would recognise as frame
// advantage -- it is just which of two idle fighters happens to be mid-animation
// -- and a number on screen that means something different depending on the
// situation is a number nobody can learn from.
//
// IT IS A TICK COUNT, AND IT IS NOT THE FIGURE THE CHARACTER FILE QUOTES.
// fighter_a's stand_lp carries `frame_advantage.on_hit: +2`, which is
// `hitstun - (active - 1 + recovery)` -- the number for a player who waits out
// the whole move. This function answers +10 on the same hit, and the ENTIRE
// difference is the two terms above that the file's arithmetic does not have:
// one tick because the kernel applies hitstun in ResolveHits at the bottom of
// the tick the boxes overlapped and decrements it at the top of the next, so the
// stun is worth one tick less in ticks than it is on paper; and nine because the
// cancel window opens at frame 5 of a 14-tick move. THE NUMBER RETURNED HERE IS
// THE ONE THE GAME HONOURS. Where the file's own note and this readout differ,
// the difference is the finding, not the bug -- and `attackerRoute` is on screen
// so that a reader can see which of the two numbers they are looking at.
Advantage FrameAdvantage(const cse::kernel::MatchData& data,
                         const cse::kernel::GameState& state,
                         int attackerSlot);

// --- Frame advantage, as the thing a playtester can actually read -------------

// ONE ANSWER, ABOUT ONE HIT, HELD STILL.
//
// FrameAdvantage above is a pure function of the state it is handed, and asking
// it every frame is what a HUD naturally does. On this mode that produces a
// number that MEANS SOMETHING DIFFERENT ON EVERY TICK, which the top of this
// header forbids in so many words, and it does it twice over:
//
//   * A playtester MASHING restarts the move as soon as it recovers -- which
//     since ROADMAP M1.1d takes a fresh press rather than a held key, and is
//     exactly what a playtester does. The attacker's clock jumps back to the
//     top of the move while the defender's stun keeps running down, so the
//     difference steps DOWN by a whole move on the restart tick and back UP on
//     the next contact. On fighter_a's air_mp -- self-cancelling at frame 8 of
//     a 22-tick move -- that is a value alternating several times a second,
//     forever, while the judge underneath correctly reads TRUE COMBO. A row
//     that contradicts the verdict beneath it twice a second on the one
//     interaction the mode is built around is worse than no row.
//     (TrainingModeReadout.ARepeatedPressRestartsTheMoveSoALiveAdvantageStrobesForever
//     measures the strobe; it was named for a held button until M1.1d.)
//
//   * "Advantage" is not a property of an instant in the first place. It is a
//     property of a HIT: the question is what that hit bought, and the answer
//     stopped being about the exchange the moment the attacker committed to
//     something else.
//
// So the answer is taken ONCE, on the tick a hit connects, and held. The host
// owns the latch because the host is what sees ticks; this file draws it. The
// contact tick is not detected here or there -- it is READ off
// ComboReport::lastHitTick, which is ComboWatcher signal 3 with its multi-hit
// guard and its move-started disjunct already applied, so there is exactly one
// implementation of "a hit landed" in the running game.
struct LatchedAdvantage {
    // False before the first hit of the session, and after a reset. The row then
    // reads "-" and says why, rather than reading a number about nothing.
    bool          measured = false;
    // The answer, as measured at the END of the tick named below.
    Advantage     advantage{};
    // THE TICK IT WAS MEASURED ON, AND IT IS PRINTED. This field is what makes
    // the latch a record rather than a stale copy: a reader can see that the
    // number is about tick 431 and that the fight is on tick 507.
    std::uint32_t tick = 0;
    // The attacker's move as observed on that tick -- the move that connected.
    std::uint16_t moveId = 0;
};

// --- Everything the readouts draw ---------------------------------------------

struct FightHudModel {
    // --- borrowed, never copied, valid for the duration of the draw only -----
    const cse::kernel::GameState*   state     = nullptr;
    const cse::kernel::MatchData*   data      = nullptr;
    const cse::data::MoveIndexMap*  names[2]  = { nullptr, nullptr };
    const cse::data::CharacterData* character = nullptr;
    // Null when the analysis did not run at all; `analysisError` then says why.
    const cse::data::ProverResult*  analysis  = nullptr;
    const cse::game::ComboWatcher*  watcher   = nullptr;

    // The PLAYER SIDE's build report. Here for one line, and that line is the
    // point of the whole screen: the analysis's termination certificate names a
    // resource, and BuildReport::losses is where the bridge says in its own words
    // whether the kernel carries that resource at all. Joining those two READ
    // facts is what lets the HUD tell a playtester that the reason this character
    // is certified safe is not simulated in the match they are playing.
    const cse::data::BuildReport*   build     = nullptr;

    // Borrowed strings the mode owns, drawn verbatim. Verbatim because each was
    // written by the layer that actually failed, and this file has no better
    // sentence to offer than the one that layer already wrote.
    const std::string* setupError    = nullptr;
    const std::string* analysisError = nullptr;
    const std::string* demoNote      = nullptr;
    const std::string* fatal         = nullptr;

    const std::vector<BindingRow>* bindings = nullptr;

    // --- the host's own bookkeeping, which is not simulation state ------------
    bool          matchReady   = false;
    std::uint32_t tick         = 0;   // FightSession::CurrentTick
    std::uint32_t highWater    = 0;   // FightSession::HighWaterTick
    std::uint32_t checksum     = 0;   // FightSession::Checksum
    std::uint64_t modeTicks    = 0;   // fixed steps this MODE has been given
    float         hostHz       = 0.0f;
    bool          paused       = false;
    int           slowDivisor  = 1;   // 1 = full speed, 2/4/8 = one tick in N
    int           playerSlot   = 0;

    // Which source is speaking for the player's slot at `tick`, from
    // FallbackInputSource::Active(tick)->Name() -- the pure question the
    // composition exposes for exactly this readout. Null when neither source
    // authors this tick.
    const char*   speaking      = nullptr;
    bool          demoArmed     = false;   // the analysis has a witness to perform

    // The fighters are standing MIDSCREEN rather than in the corner. The HUD
    // says so and says what it costs, because every verdict it draws is
    // corner-only by construction.
    bool          stageMidscreen = false;
    std::uint32_t demoRemaining = 0;       // ticks of script left to play

    // The last hit's frame advantage, latched by the host on the tick it landed.
    // BY VALUE, unlike everything above it: this is not a live object to borrow,
    // it is four scalars the host measured and owns, and copying them is the
    // whole point -- see LatchedAdvantage.
    LatchedAdvantage hitAdvantage{};

    std::int32_t stageHalfWidthSub = 0;
};

// Screen space, pixel units, origin top-left. The caller has already begun the
// frame; this only draws.
void DrawFightHud(MyCoreEngine::Renderer2D& r2d, const MyCoreEngine::Font& font,
                  int widthPx, int heightPx, const FightHudModel& model);

} // namespace untitledfighter
