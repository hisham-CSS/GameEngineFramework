// The ADR-005 P2 systems, each proven separately.
//
// WHY THIS FILE EXISTS AT ALL. The P2 expansion added blocking, hitstop,
// pushback, juggle, proration, hitstun decay, priority, typed invincibility and
// a fighter count that is no longer two -- and every one of those fields
// zero-initialises to "changes nothing", by deliberate design. That design is
// what let 56 existing tests keep passing across the change, and it is ALSO what
// would let all nine systems be subtly broken without a single test noticing.
// A mechanic whose only evidence is that nothing else broke has no evidence.
//
// So every test below authors the field it is about, and asserts the difference
// it makes. Each one is written so that its failure message names the mechanic
// rather than the number.
//
// The geometry is shared and deliberately generous: two bodies 34 px apart with
// a 40 px reach, so no assertion here can pass or fail because of distance.

#include "cse/kernel/Combat.h"
#include "cse/kernel/GameState.h"
#include "cse/kernel/Simulate.h"

#include <gtest/gtest.h>

#include <memory>

using namespace cse::kernel;

namespace {

constexpr std::int32_t px(std::int32_t v) { return v * kSubUnitsPerPixel; }

// Origins 34 px apart -> bodies 14 px apart, well inside the 40 px reach.
constexpr std::int32_t kLeftX  = -px(17);
constexpr std::int32_t kRightX =  px(17);

Box bodyBox()  { return Box{ px(-10), 0,      px(10), px(60) }; }
Box reachBox() { return Box{ 0,       0,      px(40), px(40) }; }

// Active on frames 1 and 2, so a test can put a fighter mid-active by setting
// moveFrame to 1 and never has to reason about the boundary.
MoveDef attack() {
    MoveDef m{};
    m.startup   = 1;
    m.active    = 2;
    m.recovery  = 5;
    m.damage    = 100;
    m.hitstun   = 12;
    m.blockstun = 6;
    m.hitbox    = reachBox();
    m.button    = kInputLP;
    return m;
}

void fillFighter(FighterData& d) {
    d.hurtbox   = bodyBox();
    d.maxHealth = 1000;
    d.juggleMax = 4;
    d.moveCount = 2;      // slot 0 is the reserved idle slot
    d.moves[1]  = attack();
}

// MatchData is ~64 KB (Combat.h says why that is fine and why it should not be
// copied casually). Heap-allocated here so a test body holds a pointer rather
// than three copies of it on the stack.
std::unique_ptr<MatchData> twoFighters() {
    auto d = std::make_unique<MatchData>();
    fillFighter(d->p[0]);
    fillFighter(d->p[1]);
    return d;
}

// Both fighters mid-active, facing each other, boxes overlapping. This is the
// state every priority and invincibility test starts from.
GameState facingOff() {
    GameState s{};
    ResetMatch(s, 0xC0FFEEu);
    s.p[0].posX = kLeftX;
    s.p[1].posX = kRightX;
    s.p[0].facing = 0;
    s.p[1].facing = 1;
    for (int i = 0; i < 2; ++i) {
        s.p[i].moveId    = 1;
        s.p[i].moveFrame = 1;   // inside [startup, startup + active)
    }
    return s;
}

}  // namespace

// --- Priority ---------------------------------------------------------------

TEST(P2Priority, EqualPriorityIsATradeAndBothLand) {
    // The default, and therefore the claim that every character authored before
    // this field plays identically. test_combat asserts the same thing from the
    // other direction; this one exists so that the priority file records its own
    // baseline rather than depending on a neighbour's.
    auto data = twoFighters();
    GameState s = facingOff();

    ResolveHits(s, *data);

    EXPECT_LT(s.p[0].health, 1000) << "p0 took nothing from an equal-priority trade";
    EXPECT_LT(s.p[1].health, 1000) << "p1 took nothing from an equal-priority trade";
}

TEST(P2Priority, HigherPriorityWinsOutrightAndTheLoserTakesNothing) {
    auto data = twoFighters();
    data->p[0].moves[1].priority = 1;   // p0's move beats p1's

    GameState s = facingOff();
    ResolveHits(s, *data);

    EXPECT_LT(s.p[1].health, 1000)
        << "the higher-priority attack did not land. Priority is meant to decide "
           "a mutual hit, not to suppress both.";
    EXPECT_EQ(s.p[0].health, 1000)
        << "the LOWER-priority attack still landed, so this was a trade rather "
           "than an outright win. Higher wins and the loser takes nothing.";
}

TEST(P2Priority, PriorityDoesNotSuppressAHitNobodyContested) {
    // Scope: priority orders ONE instant in which two hitboxes both connect. A
    // move that meets nothing must not consult it. The obvious wrong
    // implementation -- "the lower-priority fighter cannot hit" -- passes the
    // test above and fails this one.
    auto data = twoFighters();
    data->p[0].moves[1].priority = 5;
    data->p[1].moves[1].priority = -5;

    GameState s = facingOff();
    s.p[0].moveId    = 0;      // p0 is not attacking at all
    s.p[0].moveFrame = 0;

    ResolveHits(s, *data);

    EXPECT_LT(s.p[0].health, 1000)
        << "p1's attack was refused even though nothing contested it. Priority "
           "leaked out of the mutual-hit case.";
}

// --- Typed invincibility ----------------------------------------------------

TEST(P2Invincibility, AnAntiAirWindowStopsAnAerialAndNotAGroundedAttack) {
    // ADR-006's demonstration case, as a test. p1's move is invincible to
    // `aerial` while it runs; it must beat an airborne attacker and lose to a
    // grounded one. Two assertions, because either alone is passed by a
    // mistake: "always invincible" passes the first, "never invincible" the
    // second.
    auto data = twoFighters();
    MoveDef& antiAir = data->p[1].moves[1];
    antiAir.invulnCount   = 1;
    antiAir.invuln[0]     = InvincibilityWindow{ 0, 8, kAttackAerial, 0 };

    {
        GameState s = facingOff();
        s.p[0].airborne = 1;
        s.p[0].posY     = px(20);
        ResolveHits(s, *data);
        EXPECT_EQ(s.p[1].health, 1000)
            << "an AERIAL attack got through a window naming `aerial`. The match "
               "is an intersection: the attack carries {aerial, mid} and the "
               "window names {aerial}, so one shared bit is enough.";
    }
    {
        GameState s = facingOff();   // p0 grounded
        ResolveHits(s, *data);
        EXPECT_LT(s.p[1].health, 1000)
            << "a GROUNDED attack was stopped by a window naming only `aerial`. "
               "That would make an anti-air simply a good button, which is the "
               "balance property ADR-006 section 3.7 says the intersection rule "
               "exists to preserve.";
    }
}

TEST(P2Invincibility, AWindowNamingNoKindsStopsEverything) {
    // An empty kind list only ever NARROWS, and the identity of a narrowing is
    // everything. The dangerous misreading is the opposite one -- empty means
    // nothing -- which would make a full-invincibility reversal inert.
    auto data = twoFighters();
    data->p[1].moves[1].invulnCount = 1;
    data->p[1].moves[1].invuln[0]   = InvincibilityWindow{ 0, 8, 0, 0 };

    GameState s = facingOff();
    ResolveHits(s, *data);

    EXPECT_EQ(s.p[1].health, 1000)
        << "a window with no kinds failed to stop a grounded mid. Empty means "
           "EVERYTHING here, not nothing.";
}

TEST(P2Invincibility, AZeroTickWindowIsTheEmptySetAndProtectsNothing) {
    // A zero-length window is not a short window. It sits in a file looking like
    // protection while providing none, and the kernel must not round it up.
    auto data = twoFighters();
    data->p[1].moves[1].invulnCount = 1;
    data->p[1].moves[1].invuln[0]   = InvincibilityWindow{ 0, 0, 0, 0 };

    GameState s = facingOff();
    ResolveHits(s, *data);

    EXPECT_LT(s.p[1].health, 1000)
        << "a zero-tick window granted invincibility. Zero ticks is the empty "
           "set; the loader refuses one and the kernel must not honour it.";
}

TEST(P2Invincibility, TheWindowEndsWhenItSaysItDoes) {
    auto data = twoFighters();
    data->p[1].moves[1].invulnCount = 1;
    data->p[1].moves[1].invuln[0]   = InvincibilityWindow{ 0, 1, 0, 0 };  // frame 0 only

    GameState s = facingOff();
    s.p[1].moveFrame = 1;    // one past the window
    ResolveHits(s, *data);

    EXPECT_LT(s.p[1].health, 1000)
        << "invincibility outlived its window by at least a frame. A window of "
           "[from, from + ticks) that covers `from + ticks` is an off-by-one in "
           "the strongest property a move can have.";
}

// --- Blocking and guard height ----------------------------------------------

namespace {

// Drive one tick with the defender holding away from the attacker, so the guard
// pass has something to read. p0 attacks, p1 defends.
GameState blockingTick(const MatchData& data, std::uint8_t blockedAs,
                       bool crouchBlock) {
    GameState s{};
    ResetMatch(s, 1u);
    s.p[0].posX = kLeftX;
    s.p[1].posX = kRightX;
    s.p[0].moveId    = 1;
    s.p[0].moveFrame = 0;   // Simulate advances it to 1, which is active

    (void)blockedAs;

    InputPair in{};
    // p1 faces -X, so "back" for p1 is +X, i.e. right.
    in.p[1].bits = kInputRight | (crouchBlock ? kInputDown : 0u);

    Simulate(s, in, data);
    return s;
}

}  // namespace

TEST(P2Blocking, AHighGuardStopsAMidAttack) {
    auto data = twoFighters();
    data->p[0].moves[1].blockedAs = kBlockedAsMid;

    const GameState s = blockingTick(*data, kBlockedAsMid, /*crouchBlock=*/false);

    EXPECT_EQ(s.p[1].health, 1000)
        << "a mid attack went through a standing block. High guard stops "
           "{high, mid}.";
    EXPECT_GT(s.p[1].blockstun, 0)
        << "the attack was blocked but no blockstun was applied, so the defender "
           "is free on the next tick and blocking costs the attacker nothing.";
}

TEST(P2Blocking, AnOverheadGoesThroughALowBlock) {
    // The whole point of guard height: this is the mixup. If it fails, blocking
    // low is strictly dominant and the character has no offence.
    auto data = twoFighters();
    data->p[0].moves[1].blockedAs = kBlockedAsHigh;

    const GameState s = blockingTick(*data, kBlockedAsHigh, /*crouchBlock=*/true);

    EXPECT_LT(s.p[1].health, 1000)
        << "a HIGH attack was blocked by a LOW guard. Low block stops "
           "{low, mid}; an overhead is exactly the attack it does not stop.";
}

TEST(P2Blocking, ALowGoesThroughAStandingBlock) {
    auto data = twoFighters();
    data->p[0].moves[1].blockedAs = kBlockedAsLow;

    const GameState s = blockingTick(*data, kBlockedAsLow, /*crouchBlock=*/false);

    EXPECT_LT(s.p[1].health, 1000)
        << "a LOW attack was blocked by a HIGH guard. High block stops "
           "{high, mid}.";
}

TEST(P2Blocking, HoldingForwardIsNotAGuard) {
    auto data = twoFighters();

    GameState s{};
    ResetMatch(s, 1u);
    s.p[0].posX = kLeftX;
    s.p[1].posX = kRightX;
    s.p[0].moveId    = 1;
    s.p[0].moveFrame = 0;

    InputPair in{};
    in.p[1].bits = kInputLeft;   // toward the attacker, not away

    Simulate(s, in, *data);

    EXPECT_LT(s.p[1].health, 1000)
        << "holding TOWARD the attacker blocked. Guard is holding away, and the "
           "direction is derived from facing, which is derived from the sign of "
           "the position difference.";
}

TEST(P2Blocking, ChipDamageCannotFinishAFighter) {
    auto data = twoFighters();
    data->p[0].moves[1].chipDamage = 500;

    GameState s{};
    ResetMatch(s, 1u);
    s.p[0].posX = kLeftX;
    s.p[1].posX = kRightX;
    s.p[1].health    = 3;         // less than the chip
    s.p[0].moveId    = 1;
    s.p[0].moveFrame = 0;

    InputPair in{};
    in.p[1].bits = kInputRight;
    Simulate(s, in, *data);

    EXPECT_EQ(s.p[1].health, 1)
        << "chip damage killed. A block that finishes a round makes defence a "
           "losing option in exactly the situation defence exists for.";
}

// --- Hitstop ----------------------------------------------------------------

TEST(P2Hitstop, FreezesBothFightersAndDoesNotAdvanceTheMoveFrame) {
    auto data = twoFighters();
    data->p[0].moves[1].hitstop = 4;

    GameState s = facingOff();
    ResolveHits(s, *data);

    ASSERT_EQ(s.p[0].hitstop, 4) << "the ATTACKER was not frozen";
    ASSERT_EQ(s.p[1].hitstop, 4) << "the DEFENDER was not frozen";

    const std::uint16_t atkFrame = s.p[0].moveFrame;
    const std::uint16_t defStun  = s.p[1].hitstun;

    InputPair in{};
    Simulate(s, in, *data);

    EXPECT_EQ(s.p[0].moveFrame, atkFrame)
        << "the attacker's move advanced during hitstop. Hitstop is a FREEZE: "
           "startup 5 is still five ticks OF THE MOVE, they just take longer to "
           "arrive. A move that advances is a move whose frame data changed "
           "meaning.";
    EXPECT_EQ(s.p[1].hitstun, defStun)
        << "hitstun burned down during hitstop, so the freeze is shortening the "
           "combo it is supposed to punctuate.";
    EXPECT_EQ(s.p[0].hitstop, 3) << "hitstop did not count down";
}

// --- Pushback ---------------------------------------------------------------

TEST(P2Pushback, MovesTheDefenderAwayAndDecays) {
    auto data = twoFighters();
    data->p[0].moves[1].pushbackHit = px(4);

    GameState s = facingOff();
    const std::int32_t before = s.p[1].posX;

    ResolveHits(s, *data);
    ASSERT_GT(s.p[1].pushX, 0)
        << "pushback on a defender to the attacker's RIGHT must be positive. The "
           "direction comes from the sign of the position difference.";

    InputPair in{};
    Simulate(s, in, *data);

    EXPECT_GT(s.p[1].posX, before)
        << "the defender did not move away. Pushback rides its own field "
           "precisely because a fighter in hitstun has velX zeroed every tick.";

    const std::int32_t after = s.p[1].pushX;
    Simulate(s, in, *data);
    EXPECT_LT(s.p[1].pushX, after) << "pushback did not decay";
}

TEST(P2Pushback, IsMirroredForAnAttackerOnTheOtherSide) {
    // The mirror property this kernel is built around, applied to the newest
    // field that could break it.
    auto data = twoFighters();
    data->p[1].moves[1].pushbackHit = px(4);

    GameState s = facingOff();
    s.p[0].moveId = 0;   // only p1 attacks
    ResolveHits(s, *data);

    EXPECT_LT(s.p[0].pushX, 0)
        << "a defender to the attacker's LEFT was pushed right. Either the sign "
           "convention is inverted or somebody reached for `pos * facing`.";
}

// --- Knockdown --------------------------------------------------------------

TEST(P2Knockdown, PreventsActingUntilItExpires) {
    auto data = twoFighters();
    data->p[0].moves[1].knockdownTicks = 20;
    data->p[0].moves[1].hitstun        = 1;   // stun ends long before the knockdown

    GameState s = facingOff();
    ResolveHits(s, *data);
    ASSERT_EQ(s.p[1].knockdown, 20);

    InputPair in{};
    in.p[1].bits = kInputLP;   // the defender mashes

    for (int t = 0; t < 10; ++t) Simulate(s, in, *data);

    EXPECT_EQ(s.p[1].hitstun, 0) << "precondition: hitstun should be long gone";
    EXPECT_EQ(s.p[1].moveId, 0)
        << "a knocked-down fighter started a move. Knockdown is the state a "
           "MEATY is timed against; if it does not hold the fighter down there "
           "is nothing for a reversal to be a reversal to.";
    EXPECT_GT(s.p[1].knockdown, 0) << "the knockdown expired early";
}

// AND A FIGHTER ON THE FLOOR CANNOT BE HIT, which is the half that makes a
// knockdown READABLE from outside.
//
// Asked for from play (2026-08-20): "we can't really tell when crouching or
// knockdowns occur". A knockdown that only stopped the defender ACTING looks
// exactly like a long hitstun from the other side of the screen -- you keep
// hitting them and they keep not answering. A knockdown that also refuses your
// attacks announces itself the first time one passes through.
//
// It is not a new authored mechanic and gets no field of its own: the opt-in is
// already `causes_knockdown` plus its duration, and this is what the kernel
// MEANS by the state those two put a fighter into. A move that knocks nobody
// down grants nobody this.
//
// OTG rules -- hitting a downed opponent on purpose -- are a later mechanic and
// will be an authored per-move field when they arrive, not a loosening here.
TEST(P2Knockdown, AFighterOnTheFloorCannotBeHit) {
    auto data = twoFighters();
    data->p[0].moves[1].knockdownTicks = 20;
    data->p[0].moves[1].hitstun        = 1;

    GameState s = facingOff();
    ResolveHits(s, *data);
    ASSERT_EQ(s.p[1].knockdown, 20) << "precondition: the defender went down";

    const std::int32_t healthOnTheFloor = s.p[1].health;

    // Hit them again, on the floor, from a move that is unambiguously active.
    for (int t = 0; t < 5; ++t) {
        s.p[0].moveId         = 1;
        s.p[0].moveFrame      = 1;
        s.p[0].alreadyHitBits = 0;
        ResolveHits(s, *data);
    }

    EXPECT_EQ(s.p[1].health, healthOnTheFloor)
        << "the defender lost " << (healthOnTheFloor - s.p[1].health)
        << " more health while knocked down. A body on the floor is not a "
           "target: without this a sweep becomes the opening of an unescapable "
           "loop, because the one state that should END pressure instead "
           "guarantees it.";
    EXPECT_GT(s.p[1].knockdown, 0)
        << "the knockdown expired inside the window this test measures";
}

// --- Juggle -----------------------------------------------------------------

TEST(P2Juggle, RefusesTheHitThatWouldOverspendTheBudget) {
    // The mechanism the prover's ranking certificate is written in. Its absence
    // is what let 33 of fighter_a's 41 cycles run forever.
    auto data = twoFighters();
    data->p[1].juggleMax               = 2;
    data->p[0].moves[1].juggleCost     = 2;

    GameState s = facingOff();
    s.p[0].moveId = 0;
    InputPair in{};
    Simulate(s, in, *data);          // p1's juggle is restored to 2 here
    ASSERT_EQ(s.p[1].juggle, 2);

    s.p[0].moveId    = 1;
    s.p[0].moveFrame = 1;
    ResolveHits(s, *data);
    ASSERT_EQ(s.p[1].juggle, 0) << "the first hit did not spend the budget";

    const std::int32_t afterFirst = s.p[1].health;

    // A second hit from a fresh window, with the budget now empty.
    s.p[0].alreadyHitBits = 0;
    ResolveHits(s, *data);

    EXPECT_EQ(s.p[1].health, afterFirst)
        << "a hit landed with an empty juggle budget. `nonNegative` is the "
           "condition that ends every cycle in the prover's certificate, and "
           "this is the kernel's half of it.";
}

// --- Proration --------------------------------------------------------------

TEST(P2Proration, LaterHitsInACombDealLessAndZeroReductionChangesNothing) {
    auto data = twoFighters();

    // Baseline: no reduction authored, so the second hit is worth the same.
    {
        GameState s = facingOff();
        s.p[0].moveId = 1; s.p[1].moveId = 0;
        ResolveHits(s, *data);
        const std::int32_t first = 1000 - s.p[1].health;
        s.p[0].alreadyHitBits = 0;
        ResolveHits(s, *data);
        const std::int32_t second = (1000 - s.p[1].health) - first;
        EXPECT_EQ(first, second)
            << "damage changed with no scaling authored. Zero must change "
               "nothing -- that is what keeps every pre-P2 character playing "
               "identically.";
    }

    // With a reduction, the second hit is worth strictly less.
    {
        data->p[0].moves[1].scalingReduction = 50;
        GameState s = facingOff();
        s.p[0].moveId = 1; s.p[1].moveId = 0;
        ResolveHits(s, *data);
        const std::int32_t first = 1000 - s.p[1].health;
        s.p[0].alreadyHitBits = 0;
        ResolveHits(s, *data);
        const std::int32_t second = (1000 - s.p[1].health) - first;
        EXPECT_LT(second, first)
            << "the second hit of a combo dealt as much as the first despite a "
               "50% scaling reduction.";
    }
}

// --- Hitstun decay ----------------------------------------------------------

TEST(P2HitstunDecay, ShortensLaterHitsAndNeverGoesBelowTheFloor) {
    auto data = twoFighters();
    data->p[1].hitstunDecayStep  = 3;
    data->p[1].hitstunDecayFloor = 6;

    GameState s = facingOff();
    s.p[0].moveId = 1; s.p[1].moveId = 0;

    ResolveHits(s, *data);
    const std::uint16_t first = s.p[1].hitstun;
    EXPECT_EQ(first, 12) << "the FIRST hit of a combo decayed. Decay reads the "
                            "count of hits already taken, which is zero here.";

    s.p[0].alreadyHitBits = 0;
    ResolveHits(s, *data);
    EXPECT_LT(s.p[1].hitstun, first) << "the second hit did not decay";

    // Drive it well past the floor.
    for (int i = 0; i < 10; ++i) {
        s.p[0].alreadyHitBits = 0;
        ResolveHits(s, *data);
    }
    EXPECT_GE(s.p[1].hitstun, 6)
        << "hitstun decayed below its authored floor. ADR-005 P2 item 6 records "
           "that a floor set wrong FABRICATED AN INFINITE on Kung Fu Girl -- the "
           "floor is the part of this rule that matters.";
}

TEST(P2HitstunDecay, IsOffByDefault) {
    auto data = twoFighters();   // step 0

    GameState s = facingOff();
    s.p[0].moveId = 1; s.p[1].moveId = 0;

    ResolveHits(s, *data);
    const std::uint16_t first = s.p[1].hitstun;
    s.p[0].alreadyHitBits = 0;
    ResolveHits(s, *data);

    EXPECT_EQ(s.p[1].hitstun, first)
        << "hitstun decayed with no decay authored. Every Phase-0 character "
           "leaves this field absent and must play exactly as it did.";
}

// --- More than two fighters (docs/adr/ADR-009) ----------------------------------

namespace {

std::unique_ptr<MatchData> fourFighters() {
    auto d = std::make_unique<MatchData>();
    for (int i = 0; i < 4; ++i) fillFighter(d->p[i]);
    return d;
}

// A 2v2: slots 0 and 1 on team 0, slots 2 and 3 on team 1.
MatchSetup tagSetup() {
    MatchSetup s = DefaultMatchSetup(0xBEEFu);
    s.fighterCount = 4;
    for (int i = 0; i < 4; ++i) {
        s.p[i].startHealth = 1000;
        s.p[i].active      = 1;
        s.p[i].team        = static_cast<std::uint8_t>(i < 2 ? 0 : 1);
        s.p[i].facing      = static_cast<std::uint8_t>(i < 2 ? 0 : 1);
    }
    s.p[0].startPosX = kLeftX;
    s.p[1].startPosX = kLeftX;
    s.p[2].startPosX = kRightX;
    s.p[3].startPosX = kRightX;
    return s;
}

}  // namespace

TEST(P2Teams, AnAttackerCannotHitItsOwnTeam) {
    // The whole reason the opponent test is `a.team != d.team` rather than
    // `1 - a`. Slot 1 is co-located with slot 0 and on the same side; it must be
    // untouchable while the far team is not.
    auto data = fourFighters();

    GameState s{};
    ResetMatch(s, tagSetup());
    s.p[0].moveId    = 1;
    s.p[0].moveFrame = 1;

    ResolveHits(s, *data);

    EXPECT_EQ(s.p[1].health, 1000)
        << "a fighter hit its own teammate standing on the same spot.";
    EXPECT_LT(s.p[2].health, 1000)
        << "the attack reached nobody on the opposing team, so slot targeting is "
           "broken in the other direction.";
}

TEST(P2Teams, AnAttackerLandsAtMostOneHitPerTick) {
    // Two opponents are co-located. A loop that forgot to stop after the first
    // defender would hit both and double every attack in a tag game.
    auto data = fourFighters();

    GameState s{};
    ResetMatch(s, tagSetup());
    s.p[0].moveId    = 1;
    s.p[0].moveFrame = 1;

    ResolveHits(s, *data);

    const bool hit2 = s.p[2].health < 1000;
    const bool hit3 = s.p[3].health < 1000;
    EXPECT_TRUE(hit2 || hit3) << "the attack landed on nobody";
    EXPECT_FALSE(hit2 && hit3)
        << "one active window hit two fighters on one tick. An attacker lands at "
           "most one hit per tick, on the lowest-numbered defender it overlaps.";
    EXPECT_TRUE(hit2)
        << "the hit went to the higher slot. The tie-break is lowest index, which "
           "is the same rule the button scan and the cancel scan use, and it is "
           "what two peers agree on without exchanging anything.";
}

TEST(P2Teams, ABenchedFighterNeitherActsNorIsHit) {
    auto data = fourFighters();

    MatchSetup setup = tagSetup();
    setup.p[2].active = 0;          // bench the front opponent

    GameState s{};
    ResetMatch(s, setup);
    s.p[0].moveId    = 1;
    s.p[0].moveFrame = 1;

    ResolveHits(s, *data);

    EXPECT_EQ(s.p[2].health, 1000)
        << "a benched fighter was hit. Benched means off the screen, and the "
           "whole point of benching rather than removing is that the fighter "
           "keeps its health for when it comes back.";
    EXPECT_LT(s.p[3].health, 1000)
        << "the attack did not fall through to the next active opponent.";
}

TEST(P2Teams, ABenchedFighterKeepsItsStunAcrossTheBench) {
    // Tagging out must not be a free escape from a combo.
    auto data = fourFighters();

    GameState s{};
    ResetMatch(s, tagSetup());
    s.p[2].hitstun = 30;
    s.p[2].active  = 0;

    InputPair in{};
    for (int t = 0; t < 10; ++t) Simulate(s, in, *data);

    EXPECT_EQ(s.p[2].hitstun, 30)
        << "a benched fighter burned down its hitstun while off screen, so "
           "tagging out escapes every combo in the game.";
}

TEST(P2Teams, FacingPicksTheNearestOpponentAndBreaksTiesByIndex) {
    auto data = fourFighters();

    MatchSetup setup = tagSetup();
    setup.p[2].startPosX = kRightX;          // exactly as far as slot 3
    setup.p[3].startPosX = kRightX;

    GameState s{};
    ResetMatch(s, setup);

    InputPair in{};
    Simulate(s, in, *data);

    EXPECT_EQ(s.p[0].facing, 0)
        << "slot 0 is left of both opponents and must face +X.";
    EXPECT_EQ(s.p[2].facing, 1)
        << "slot 2 is right of both opponents and must face -X. A tie between "
           "two equidistant opponents must resolve by index, not by whichever "
           "the loop saw first -- that is the hash-ordering desync this kernel "
           "already names twice.";
}

TEST(P2Rounds, AKnockoutEndsTheRoundAndCreditsTheOtherTeam) {
    auto data = twoFighters();

    MatchSetup setup = DefaultMatchSetup(7u);
    setup.roundsToWin  = 2;
    setup.p[1].startHealth = 1;

    GameState s{};
    ResetMatch(s, setup);
    s.p[0].posX = kLeftX;
    s.p[1].posX = kRightX;
    s.p[0].moveId    = 1;
    s.p[0].moveFrame = 0;

    InputPair in{};
    Simulate(s, in, *data);

    ASSERT_EQ(s.p[1].health, 0) << "precondition: the hit should have finished p1";
    EXPECT_EQ(s.roundState, kRoundOver)
        << "a fighter reached zero health and the round did not end.";
    EXPECT_EQ(s.roundsWon[0], 1) << "the surviving team was not credited";
    EXPECT_NE(s.roundState, kMatchOver)
        << "one round of a best-of-three ended the whole match.";
}

// --- One button, many moves, chosen by stance (ROADMAP M1.1c) ---------------

// THE BUG THIS PINS was found by playing the game, not by a test: training mode
// gave `air_mp` its own dedicated key and bound `stand_hk` to the MK bit, so the
// button you pressed had no relationship to the strength you got. Six buttons
// were spent on six MOVES, which left no button for a crouching normal and no
// way to express "the same strength, in the air".
//
// The kernel was never the problem. MoveDef::stance and StanceAllows have been
// here since ADR-006, and fighter_a.json already authors air_mp as `stance:
// air`. This test states the property the binding table has to stop breaking:
// ONE BIT, and the fighter's state picks which move it starts.
TEST(P3Attacks, OneButtonPicksTheMoveForTheStanceYouAreIn) {
    auto data = twoFighters();
    FighterData& d = data->p[0];

    // Two moves, one button, disjoint stances -- the shape every strength has
    // once bindings stop being one-per-move.
    d.moves[1]        = attack();
    d.moves[1].button = kInputMP;
    d.moves[1].stance = kStanceGround;

    d.moves[2]        = attack();
    d.moves[2].button = kInputMP;
    d.moves[2].stance = kStanceAir;
    d.moveCount       = 3;

    InputPair in{};
    in.p[0].bits = kInputMP;

    {
        GameState s{};
        ResetMatch(s, 0x5A17u);
        s.p[0].airborne = 0;
        Simulate(s, in, *data);
        EXPECT_EQ(1u, s.p[0].moveId)
            << "MP on the ground started move " << s.p[0].moveId
            << " rather than the grounded one. A button that ignores stance is a "
               "button spent on exactly one move.";
    }
    {
        GameState s{};
        ResetMatch(s, 0x5A17u);
        // HEIGHT, not just the flag. StepPhysics clears `airborne` the tick a
        // fighter reaches the floor, so setting the flag at posY 0 lands them
        // before the attack scan ever runs -- which is a real property of the
        // kernel and a trap for anyone writing an air test.
        s.p[0].airborne = 1;
        s.p[0].posY     = 4 * kSubUnitsPerPixel;
        s.p[0].velY     = kSubUnitsPerPixel;
        Simulate(s, in, *data);
        EXPECT_EQ(2u, s.p[0].moveId)
            << "MP in the air started move " << s.p[0].moveId
            << " rather than the air one. This is the air_mp bug: the air normal "
               "needed its own dedicated key because the MP bit could only ever "
               "mean one move.";
    }
}

// The scan takes the FIRST slot whose button matches and whose stance allows, so
// two moves on one button with overlapping stances is not a preference -- the
// lower slot wins forever and the higher one can never start. That is the
// failure `MatchBuilder` already warns about as "a binding that can never
// start", and (a) makes it newly easy to author.
TEST(P3Attacks, TwoMovesOnOneButtonWithOverlappingStancesShadowTheHigherSlot) {
    auto data = twoFighters();
    FighterData& d = data->p[0];

    d.moves[1]        = attack();
    d.moves[1].button = kInputMP;
    d.moves[1].stance = kStanceAny;     // overlaps everything below it

    d.moves[2]        = attack();
    d.moves[2].button = kInputMP;
    d.moves[2].stance = kStanceAir;
    d.moveCount       = 3;

    InputPair in{};
    in.p[0].bits = kInputMP;

    GameState s{};
    ResetMatch(s, 0x5A17u);
    s.p[0].airborne = 1;
    s.p[0].posY     = 4 * kSubUnitsPerPixel;
    s.p[0].velY     = kSubUnitsPerPixel;
    Simulate(s, in, *data);

    EXPECT_EQ(1u, s.p[0].moveId)
        << "the kernel picked slot " << s.p[0].moveId
        << ". This test documents the hazard rather than a wish: the first "
           "matching slot wins, so an overlapping stance makes the later move "
           "unreachable. The DATA LAYER is where this is refused -- see the "
           "build report -- because the kernel cannot tell a shadow from a "
           "deliberate ordering.";
}

// --- Input edges and buffering (ROADMAP M1.1d) ------------------------------

// Holding a button used to restart the move every time it recovered. That is
// not a fighting-game mechanic, and it made any recorded "combo" suspect: one
// key held is not a link, and the showcase's whole claim is that its loops are
// performable rather than mashed.
//
// Found by playing (review point R0). No test could have reported it, because
// "held" is exactly what StepAttack's scan said it did.
TEST(P3Input, HoldingAButtonStartsTheMoveOnceNotEveryRecovery) {
    auto data = twoFighters();
    // startup 1 + active 2 + recovery 5 = 8 ticks, so a held button under the
    // old rule restarted on tick 9 and every 8 ticks after it.
    GameState s{};
    ResetMatch(s, 0x1D7u);

    InputPair in{};
    in.p[0].bits = kInputLP;

    // COUNT moveFrame == 0, not a change of moveId. A move that restarts the
    // instant it recovers never leaves slot 1, so watching moveId transition
    // from not-1 to 1 counts ZERO restarts and the test passes against the very
    // bug it is named for. Found by reverting it, which is what reverting is
    // for.
    int starts = 0;
    for (int t = 0; t < 40; ++t) {
        Simulate(s, in, *data);            // held, every tick, never released
        if (s.p[0].moveId == 1 && s.p[0].moveFrame == 0) ++starts;
    }

    EXPECT_EQ(1, starts)
        << "the move started " << starts << " times while the button was merely "
           "HELD. A press is an edge; holding is one press, however long it "
           "lasts.";
}

// The other half of the same field: releasing and pressing again is two presses,
// so edge detection must not make a button single-use.
TEST(P3Input, ReleasingAndPressingAgainStartsItAgain) {
    auto data = twoFighters();
    GameState s{};
    ResetMatch(s, 0x1D7u);

    InputPair held{}, none{};
    held.p[0].bits = kInputLP;

    int starts = 0;
    for (int cycle = 0; cycle < 3; ++cycle) {
        const std::uint16_t before = s.p[0].moveId;
        Simulate(s, held, *data);
        if (s.p[0].moveId == 1 && before != 1) ++starts;
        for (int t = 0; t < 9; ++t) Simulate(s, none, *data);  // recover, released
    }

    EXPECT_EQ(3, starts)
        << "three separate presses produced " << starts
        << " moves. Edge detection must distinguish a hold from a press, not "
           "turn a button into a one-shot.";
}

// A press during recovery is kept and spent the tick the fighter can act. This
// is what makes a link feel like timing rather than a coin flip -- and the
// window is authored, so a character that declares none behaves as before.
TEST(P3Input, APressDuringRecoveryFiresTheTickTheFighterCanAct) {
    auto data = twoFighters();
    // Generous on purpose. The property is "a press the fighter could not act on
    // is spent the tick they can", not "it survives exactly N ticks" -- encoding
    // the move's duration here would make this a test about the bench's frame
    // data, and it would go red the day someone retuned it.
    data->p[0].inputBufferFrames = 60;

    GameState s{};
    ResetMatch(s, 0x1D7u);

    InputPair press{}, none{};
    press.p[0].bits = kInputLP;

    Simulate(s, press, *data);                 // starts the move
    ASSERT_EQ(1u, s.p[0].moveId);
    ASSERT_EQ(0u, s.p[0].moveFrame);

    // Released, THEN pressed again while the move is still running -- too early,
    // on purpose. The release matters: two presses on consecutive ticks with
    // nothing between them is a HOLD, which is one press however long it lasts,
    // and there would be no second edge to buffer. The first draft of this test
    // got that wrong and reported the buffer dropping a press that was never
    // made.
    Simulate(s, none,  *data);
    Simulate(s, press, *data);
    int restarts = 0;
    for (int t = 0; t < 30; ++t) {
        Simulate(s, none, *data);
        if (s.p[0].moveId == 1 && s.p[0].moveFrame == 0) ++restarts;
    }

    EXPECT_EQ(1, restarts)
        << "the early press produced " << restarts << " restarts. Zero means the "
           "buffer dropped it -- a buffer that never fires is not a buffer. More "
           "than one means it was aged rather than CONSUMED, and one press "
           "started several moves.";
}

// And with no authored window, nothing is remembered -- the pre-M1.1d kernel.
TEST(P3Input, WithNoAuthoredWindowAPressDuringRecoveryIsForgotten) {
    auto data = twoFighters();
    data->p[0].inputBufferFrames = 0;          // the file authored none

    GameState s{};
    ResetMatch(s, 0x1D7u);

    InputPair press{}, none{};
    press.p[0].bits = kInputLP;

    Simulate(s, press, *data);
    ASSERT_EQ(1u, s.p[0].moveId);
    for (int t = 0; t < 3; ++t) Simulate(s, none, *data);
    Simulate(s, press, *data);                 // during recovery, not buffered
    for (int t = 0; t < 6; ++t) Simulate(s, none, *data);

    EXPECT_EQ(0u, s.p[0].moveId)
        << "a press was remembered by a character that authors no buffer window. "
           "Opt-in means a silent file gets the behaviour it had before the "
           "field existed.";
}

// Negative edge: hold, then release, and the move comes out -- for a move that
// asked for it, and only for that move.
TEST(P3Input, ANegativeEdgeMoveFiresOnReleaseAndOnlyWhenAuthored) {
    auto data = twoFighters();
    data->p[0].moves[1].negativeEdge = 1;      // this one accepts a release
    data->p[1].moves[1].negativeEdge = 0;      // this one does not

    GameState s{};
    ResetMatch(s, 0x1D7u);

    InputPair held{}, none{};
    held.p[0].bits = kInputLP;
    held.p[1].bits = kInputLP;

    // Both start on the press, then both finish their move while held.
    for (int t = 0; t < 9; ++t) Simulate(s, held, *data);
    ASSERT_EQ(0u, s.p[0].moveId) << "p0 never returned to idle";
    ASSERT_EQ(0u, s.p[1].moveId) << "p1 never returned to idle";

    Simulate(s, none, *data);                  // the release tick

    EXPECT_EQ(1u, s.p[0].moveId)
        << "releasing the button did not start the move that authored "
           "negativeEdge. Hold, motion, release is the mechanic this field is "
           "named for.";
    EXPECT_EQ(0u, s.p[1].moveId)
        << "a move that did NOT author negativeEdge fired on release. Every "
           "mechanic is opt-in; a silent file must behave as it did before.";
}

// --- A buffered press and the cancel system ---------------------------------
//
// THE DECISION THIS PINS: a buffered press triggers a cancel, and the cancel
// CONSUMES it. Both halves are choices and both are load-bearing.
//
// Triggering is what makes links and cancels performable by a human. A player
// aiming at a two-frame window presses slightly early far more often than
// slightly late, so a cancel that reads only the current tick's bits is a cancel
// that punishes the common miss and rewards nothing. The buffered press IS the
// input meant for the cancel; it simply arrived before the window opened.
//
// Consuming is what stops the fighter choosing its own timing. StepAttack's
// cancel branch returns before the button scan, so a buffered press that
// survived the cancel it triggered would still be sitting there for the NEXT
// window, and a single press would walk a fighter several moves down a chain.
namespace {

// The source is slot 1 (LP), the follow-up slot 2 (MP), and one edge joins them
// in the middle of the source's life. `onHit` is 0 so the edge is available on a
// whiff: this file's bench has no defender in range, and requiring contact would
// make the test depend on hit detection rather than on the buffer.
//
// THE WINDOW CLOSES BEFORE THE MOVE DOES, and that gap is the entire instrument.
// `attack()` is 1 + 2 + 5 = 8 ticks, so its last frame is 7. A buffered press
// that the CANCEL took shows up with the source interrupted somewhere in
// [4, 6]; a buffered press the cancel ignored is still sitting in the buffer
// when the source ends, and the BUTTON scan starts the same follow-up off frame
// 7 instead. Both routes produce exactly one start of slot 2, so a test that
// only counted starts -- as the first draft of this one did -- passes with the
// change reverted and proves nothing. The frame is what tells them apart.
constexpr std::int32_t kSourceDuration = 8;
constexpr std::int32_t kCancelOpens    = 4;
constexpr std::int32_t kCancelCloses   = 6;

std::unique_ptr<MatchData> chainBench() {
    auto d = twoFighters();
    MoveDef follow = attack();
    follow.button  = kInputMP;
    d->p[0].moves[2]   = follow;
    d->p[0].moveCount  = 3;

    CancelEdge& e   = d->p[0].cancels[0];
    e.from          = 1;
    e.to            = 2;
    e.earliestFrame = kCancelOpens;
    e.latestFrame   = kCancelCloses;
    e.contactMask   = 0;   // ungated: no contact condition
    d->p[0].cancelCount = 1;
    return d;
}

// Press LP, release, press MP too early, then go silent and watch. Returns the
// number of ticks that started slot 2, and reports the source's frame on the
// tick before the first of them.
int runEarlyCancel(MatchData& data, std::int32_t& sourceFrameBefore) {
    GameState s{};
    ResetMatch(s, 0x1D7u);

    InputPair lp{}, mp{}, none{};
    lp.p[0].bits = kInputLP;
    mp.p[0].bits = kInputMP;

    Simulate(s, lp, data);
    EXPECT_EQ(1u, s.p[0].moveId) << "the source move did not start";

    // The release is not decoration: LP down then MP down on consecutive ticks
    // is still an edge for MP, but going through neutral is what a human does
    // and it keeps this test measuring the buffer rather than a chord.
    Simulate(s, none, data);
    Simulate(s, mp, data);

    sourceFrameBefore = -1;
    int starts = 0;
    for (int t = 0; t < 30; ++t) {
        const std::int32_t before = static_cast<std::int32_t>(s.p[0].moveFrame);
        const std::uint16_t beforeId = s.p[0].moveId;
        Simulate(s, none, data);
        if (s.p[0].moveId == 2u && s.p[0].moveFrame == 0u) {
            if (starts == 0 && beforeId == 1u) sourceFrameBefore = before;
            ++starts;
        }
    }
    return starts;
}

}  // namespace

TEST(P3Input, ABufferedPressTakesTheCancelTheTickItsWindowOpens) {
    auto data = chainBench();
    data->p[0].inputBufferFrames = 60;   // generous; see the note above

    std::int32_t frameBefore = -1;
    const int starts = runEarlyCancel(*data, frameBefore);

    EXPECT_EQ(1, starts)
        << "the early MP produced " << starts << " starts of the follow-up. Zero "
           "means a buffered press cannot take a cancel, so every link has to be "
           "hit on its exact frame. More than one means the cancel did not "
           "CONSUME the press, and one button push walked the fighter down the "
           "chain more than once.";

    EXPECT_GE(frameBefore, kCancelOpens - 1)
        << "the follow-up came out with the source on frame " << frameBefore
        << ", before the window at [" << kCancelOpens << ", " << kCancelCloses
        << "] could have opened. A buffer may make a cancel EASIER to reach; it "
           "may not make one available early.";
    EXPECT_LE(frameBefore, kCancelCloses)
        << "the follow-up came out with the source on frame " << frameBefore
        << " and the window closes at " << kCancelCloses << ". Frame "
        << (kSourceDuration - 1)
        << " means the source was SPENT rather than interrupted: the cancel "
           "ignored the buffered press and the button scan picked it up after "
           "the move ended, which is the pre-change behaviour.";
}

// And the control: the same early press, with no authored window, is forgotten.
// This is what says the buffer is the mechanism above rather than a coincidence
// of the frame numbers.
TEST(P3Input, WithNoAuthoredWindowAnEarlyPressMissesTheCancelEntirely) {
    auto data = chainBench();
    data->p[0].inputBufferFrames = 0;    // the file authored none

    std::int32_t frameBefore = -1;
    const int starts = runEarlyCancel(*data, frameBefore);

    EXPECT_EQ(0, starts)
        << "a press made before the cancel window opened still reached the "
           "follow-up, for a character that authors no buffer at all. Opt-in "
           "means a silent file behaves exactly as it did before the field "
           "existed -- neither the cancel route nor the button route may "
           "remember a press this character never asked to have remembered.";
}

// --- Movement parameters come from the file ---------------------------------
//
// The kernel's `kWalkSpeed` is one of a block of constants Simulate.cpp itself
// labels "placeholders for values that will come from character data", and walk
// speed is the one that decides whether a MICROWALK LOOP exists: the attacker
// steps forward between two hits to stay in range, and a pixel per tick is the
// difference between a string that drops and one that repeats forever. It is
// also the loop the corner-only prover cannot see, so the engine is the only
// thing that can show it.
//
// Measured as a DISTANCE OVER TICKS rather than by reading velX, because a
// harness that reads the same field the kernel wrote proves only that the field
// exists. Position is what a hitbox is tested against.
namespace {

std::int32_t walkedRightFor(MatchData& data, int ticks) {
    GameState s{};
    ResetMatch(s, 0x1D7u);
    const std::int32_t startX = s.p[0].posX;

    InputPair right{};
    right.p[0].bits = kInputRight;
    for (int t = 0; t < ticks; ++t) Simulate(s, right, data);
    return s.p[0].posX - startX;
}

}  // namespace

TEST(P3Movement, WalkSpeedComesFromTheFile) {
    constexpr int          kTicks   = 10;
    constexpr std::int32_t kAuthored = 3 * kSubUnitsPerPixel;   // fighter_a's number

    auto data = twoFighters();
    data->p[0].walkSpeedSub = kAuthored;

    EXPECT_EQ(walkedRightFor(*data, kTicks), kAuthored * kTicks)
        << "the fighter did not travel the authored speed for " << kTicks
        << " ticks. A walk speed the kernel does not read is a balance number "
           "the file cannot set, and the microwalk variant (ADR-011 section 4) "
           "is authored as +1 px/tick FROM THE BASE -- so a base the file does "
           "not own makes that variant unexpressible.";
}

// And a file that authors nothing plays exactly as it did before the field
// existed. This is the half that says the change is opt-in rather than a
// retuning of every character that has not been revisited.
TEST(P3Movement, ACharacterThatAuthorsNoWalkSpeedKeepsThePlaceholder) {
    constexpr int          kTicks       = 10;
    constexpr std::int32_t kPlaceholder = 2 * kSubUnitsPerPixel;   // Simulate.cpp's

    auto data = twoFighters();
    ASSERT_EQ(data->p[0].walkSpeedSub, 0)
        << "the bench authored a walk speed, so this test cannot say what an "
           "unauthored character does.";

    EXPECT_EQ(walkedRightFor(*data, kTicks), kPlaceholder * kTicks)
        << "an unauthored character no longer walks at the kernel's placeholder. "
           "Every harness that builds a synthetic FighterData -- including the "
           "cross-toolchain scripted match, whose golden hash is recorded "
           "against these positions -- expects the old number.";
}

// --- Resources ---------------------------------------------------------------
//
// A resource is whatever the character file says it is: meter, juggle, a stock
// of one-per-round reversals. The kernel holds four integer slots and no names
// at all, because the loader resolved every authored name to an INDEX once and
// index i means the same resource in every file a build loads. That is the
// contract the prover keys on positionally (ADR-001 section 8 item 7, A03), and
// the third test below is the one that would notice it being broken.
namespace {

constexpr std::int32_t kMeterSlot  = 0;
constexpr std::int32_t kSecondSlot = 1;

// One resource, meter-shaped: starts empty, cannot go below zero, caps at 100.
// The move on slot 1 gains 25 of it per contact.
std::unique_ptr<MatchData> meterBench() {
    auto d = twoFighters();
    for (int p = 0; p < 2; ++p) {
        FighterData& fd = d->p[p];
        fd.resourceCount              = 1;
        fd.resources[kMeterSlot]      = ResourceDef{ 0, 0, 100, 1u, {0, 0, 0} };
        fd.moves[1].effect[kMeterSlot] = 25;
    }
    return d;
}

}  // namespace

TEST(P3Resources, MeterGainsOnHitAndSpendsOnGuard) {
    auto data = meterBench();
    GameState s = facingOff();

    ASSERT_EQ(s.p[0].res[kMeterSlot], 0)
        << "the bench opened with meter already in it, so a gain cannot be told "
           "from the starting value";

    ResolveHits(s, *data);

    EXPECT_EQ(s.p[0].res[kMeterSlot], 25)
        << "the attacker's move authored +25 meter and landed, and the fighter "
           "holds " << s.p[0].res[kMeterSlot]
        << ". A resource nothing writes is a resource no move can cost.";

    // AND IT STOPS AT THE CEILING. Asserted by driving past it rather than by
    // reading the field back, because a clamp that is written but never reached
    // is a clamp nobody has tested.
    //
    // The move is RE-ARMED each time: a hit interrupts the defender's move, and
    // this bench is symmetric, so the first exchange leaves neither fighter
    // holding an active hitbox. Clearing alreadyHitBits alone gains nothing --
    // which is how the first draft of this loop measured one hit and called it
    // ten.
    for (int i = 0; i < 5; ++i) {
        for (int q = 0; q < 2; ++q) {
            s.p[q].moveId         = 1;
            s.p[q].moveFrame      = 1;
            s.p[q].alreadyHitBits = 0;
        }
        ResolveHits(s, *data);
    }
    EXPECT_EQ(s.p[0].res[kMeterSlot], 100)
        << "meter reached " << s.p[0].res[kMeterSlot]
        << " against an authored ceiling of 100. A resource that runs past its "
           "ceiling makes every guard below it meaningless.";
}

// A guarded move is one the fighter may not START below the minimum -- on
// EITHER route into it, because a cancel and a button press are two doors into
// the same room and a door left open is the whole cost avoided.
//
// The refusal is a FALL-THROUGH and not a swallowed press: slot order still
// decides, so an unaffordable super lets the next slot sharing that button
// answer instead. That is how a super and a heavy normal live on one button in
// the genre, and it is the half a test that only checked "the super did not come
// out" would miss.
TEST(P3Resources, AGuardedCancelRefusesBelowTheMinimum) {
    auto data = meterBench();
    FighterData& fd = data->p[0];

    // Slot 2: the expensive follow-up. Same button as nothing else, so the only
    // question about it is whether it can be afforded.
    fd.moves[2]                   = fd.moves[1];
    fd.moves[2].button            = kInputMP;
    fd.moves[2].effect[kMeterSlot] = 0;
    fd.moves[2].guard[kMeterSlot]  = 50;
    fd.moves[2].guardMask          = 1u << kMeterSlot;
    fd.moveCount                   = 3;

    // And a cancel into it from slot 1, so both routes are exercised.
    CancelEdge& e   = fd.cancels[0];
    e.from          = 1;
    e.to            = 2;
    e.earliestFrame = 1;
    e.latestFrame   = 6;
    e.contactMask   = 0;   // ungated: no contact condition
    fd.cancelCount  = 1;

    // --- the button route, broke ---------------------------------------------
    {
        GameState s{};
        ResetMatch(s, 0x1D7u);
        InputPair mp{};
        mp.p[0].bits = kInputMP;
        Simulate(s, mp, *data);

        EXPECT_EQ(s.p[0].moveId, 0u)
            << "the expensive move started with " << s.p[0].res[kMeterSlot]
            << " meter against a minimum of 50. A guard that does not refuse is "
               "a cost the character never pays.";
    }

    // --- the button route, paid ----------------------------------------------
    {
        GameState s{};
        ResetMatch(s, 0x1D7u);
        Simulate(s, InputPair{}, *data);      // tick 0 primes; then afford it
        s.p[0].res[kMeterSlot] = 50;

        InputPair mp{};
        mp.p[0].bits = kInputMP;
        Simulate(s, mp, *data);

        EXPECT_EQ(s.p[0].moveId, 2u)
            << "the fighter holds exactly the minimum and the move still did not "
               "start. The guard is a MINIMUM, so equal must pass -- an "
               "off-by-one here makes every cost one higher than the file says.";
    }

    // --- the cancel route, broke ---------------------------------------------
    {
        GameState s{};
        ResetMatch(s, 0x1D7u);
        InputPair lp{}, mp{};
        lp.p[0].bits = kInputLP;
        mp.p[0].bits = kInputMP;

        Simulate(s, lp, *data);
        ASSERT_EQ(s.p[0].moveId, 1u) << "the source move did not start";
        s.p[0].res[kMeterSlot] = 0;           // undo the source's own gain

        Simulate(s, mp, *data);
        EXPECT_EQ(s.p[0].moveId, 1u)
            << "the cancel into the expensive move was taken with "
            << s.p[0].res[kMeterSlot]
            << " meter. A guard checked on one route into a move and not the "
               "other is not a guard.";
    }

    // --- the cancel route, paid ----------------------------------------------
    {
        GameState s{};
        ResetMatch(s, 0x1D7u);
        InputPair lp{}, mp{};
        lp.p[0].bits = kInputLP;
        mp.p[0].bits = kInputMP;

        Simulate(s, lp, *data);
        ASSERT_EQ(s.p[0].moveId, 1u);
        s.p[0].res[kMeterSlot] = 50;

        Simulate(s, mp, *data);
        EXPECT_EQ(s.p[0].moveId, 2u)
            << "the cancel was refused although the minimum was met.";
    }
}

// THE CONTRACT ITSELF: index i in the file is index i in the kernel.
//
// Not a restatement of the loader's own assertion. The loader proves it resolved
// NAMES to indices consistently; this proves the BRIDGE did not reorder them on
// the way through, which is the step where a sort or a map would be invisible.
// Two resources with different numbers is the smallest arrangement in which a
// swap is detectable at all -- with one slot every ordering is the same
// ordering.
TEST(P3Resources, IndexOrderIsTheFilesOrder) {
    auto data = twoFighters();
    FighterData& fd = data->p[0];
    fd.resourceCount           = 2;
    fd.resources[kMeterSlot]   = ResourceDef{ 7,  0, 100, 1u, {0, 0, 0} };
    fd.resources[kSecondSlot]  = ResourceDef{ 3, -5,  50, 1u, {0, 0, 0} };

    // Distinct deltas, so a swap shows up as the wrong slot moving rather than
    // as no change at all.
    fd.moves[1].effect[kMeterSlot]  = 10;
    fd.moves[1].effect[kSecondSlot] = -1;

    GameState s = facingOff();
    ASSERT_EQ(s.p[0].res[kMeterSlot], 0)
        << "facingOff() does not run a tick, so nothing has primed yet";

    // One tick to prime, then the hit.
    GameState primed{};
    ResetMatch(primed, 0xC0FFEEu);
    Simulate(primed, InputPair{}, *data);
    EXPECT_EQ(primed.p[0].res[kMeterSlot],  7)
        << "slot 0 primed to " << primed.p[0].res[kMeterSlot]
        << " and the file declares 7 first. If this is 3 the two declarations "
           "were swapped between the file and the kernel, and every effect, "
           "guard and prover comparison in this build is off by one slot.";
    EXPECT_EQ(primed.p[0].res[kSecondSlot], 3);

    s.p[0].res[kMeterSlot]  = 7;
    s.p[0].res[kSecondSlot] = 3;
    ResolveHits(s, *data);

    EXPECT_EQ(s.p[0].res[kMeterSlot],  17) << "slot 0 took slot 1's delta";
    EXPECT_EQ(s.p[0].res[kSecondSlot],  2) << "slot 1 took slot 0's delta";
}

// --- The crouching body ------------------------------------------------------
//
// Asked for from play (2026-08-20): "we can't really tell when crouching or
// knockdowns occur -- jumping at least puts your hitbox in the air". A crouch
// that changes no box is a crouch nobody can see, and it is the posture a low
// attack exists to catch.
TEST(P2Crouch, ACrouchingFighterHasTheShorterBody) {
    constexpr std::int32_t kStand  = 60 * kSubUnitsPerPixel;
    constexpr std::int32_t kCrouch = 34 * kSubUnitsPerPixel;

    auto data = twoFighters();
    data->p[0].hurtbox       = Box{ -kSubUnitsPerPixel * 13, 0, kSubUnitsPerPixel * 13, kStand };
    data->p[0].crouchHurtbox = Box{ -kSubUnitsPerPixel * 13, 0, kSubUnitsPerPixel * 13, kCrouch };

    GameState s{};
    ResetMatch(s, 0x1D7u);

    const Box standing = Hurtbox(data->p[0], s.p[0]);
    EXPECT_EQ(standing.y1 - standing.y0, kStand)
        << "precondition: a fighter who is not crouching has the standing body";

    s.p[0].crouching = 1;
    const Box ducked = Hurtbox(data->p[0], s.p[0]);
    EXPECT_EQ(ducked.y1 - ducked.y0, kCrouch)
        << "crouching left the body " << (ducked.y1 - ducked.y0)
        << " sub-units tall and the file authors " << kCrouch
        << ". A crouch that does not change the body cannot duck anything, and "
           "from outside it is a posture with no consequence at all.";
}

// And a character that authors none keeps one body for both postures, which is
// what says this is opt-in rather than a retuning of everyone.
TEST(P2Crouch, ACharacterWithNoCrouchBodyKeepsItsStandingOne) {
    auto data = twoFighters();
    ASSERT_EQ(data->p[0].crouchHurtbox.y1, data->p[0].crouchHurtbox.y0)
        << "the bench authored a crouch body, so this test cannot say what an "
           "unauthored character does";

    GameState s{};
    ResetMatch(s, 0x1D7u);
    const Box standing = Hurtbox(data->p[0], s.p[0]);

    s.p[0].crouching = 1;
    const Box ducked = Hurtbox(data->p[0], s.p[0]);

    EXPECT_EQ(ducked.y1 - ducked.y0, standing.y1 - standing.y0)
        << "an unauthored character changed shape on crouching. A degenerate box "
           "is how this field spells 'the file did not say', and reading it as a "
           "real body gives every character a zero-height crouch that nothing "
           "can hit.";
}

// A MOVE'S OWN BODY OUTRANKS THE POSTURE, because the move is the more specific
// statement. Without this ordering a crouching move that authors a low profile
// would have it silently replaced by the generic crouch.
TEST(P2Crouch, AMovesOwnHurtboxOutranksTheCrouchingOne) {
    constexpr std::int32_t kCrouch   = 34 * kSubUnitsPerPixel;
    constexpr std::int32_t kOverride = 18 * kSubUnitsPerPixel;

    auto data = twoFighters();
    data->p[0].crouchHurtbox = Box{ -kSubUnitsPerPixel * 13, 0, kSubUnitsPerPixel * 13, kCrouch };
    data->p[0].moves[1].hasHurtboxOverride = 1;
    data->p[0].moves[1].hurtboxOverride =
        Box{ -kSubUnitsPerPixel * 13, 0, kSubUnitsPerPixel * 13, kOverride };

    GameState s{};
    ResetMatch(s, 0x1D7u);
    s.p[0].crouching = 1;
    s.p[0].moveId    = 1;
    s.p[0].moveFrame = 1;

    const Box box = Hurtbox(data->p[0], s.p[0]);
    EXPECT_EQ(box.y1 - box.y0, kOverride)
        << "the move authors an " << kOverride << " body and got "
        << (box.y1 - box.y0)
        << ". A slide that ducks a fireball has said what crouching looks like "
           "for those frames; layering the generic crouch over it ignores half "
           "the file.";
}

// --- The invisible wall ------------------------------------------------------
//
// Described from play (2026-08-20): "there is a maximum distance they can be
// away from the character before they reach an invisible wall ... if the
// opposing character starts to move closer, the player can keep moving backwards
// till they hit a corner or till the opposing character stops moving."
//
// Two claims, and they are separable, so they are two tests.
TEST(P2Stage, RetreatStopsAtTheMaximumSeparation) {
    auto data = twoFighters();
    GameState s{};
    ResetMatch(s, 0x1D7u);
    s.p[0].posX = 0;
    s.p[1].posX = 0;

    InputPair back{};
    back.p[1].bits = kInputRight;   // p1 walks away; p0 stands still

    // Long enough to cross the limit several times over at any walk speed.
    for (int t = 0; t < 600; ++t) Simulate(s, back, *data);

    const std::int32_t gap = s.p[1].posX - s.p[0].posX;
    EXPECT_LE(gap, kMaxSeparationSub)
        << "the retreating fighter reached " << gap
        << " sub-units from a stationary opponent and the limit is "
        << kMaxSeparationSub
        << ". Without the wall a player can simply walk out of the game.";
    EXPECT_EQ(gap, kMaxSeparationSub)
        << "the retreat stopped SHORT of the limit at " << gap
        << ", so something other than the wall is holding them.";
    EXPECT_EQ(s.p[0].posX, 0)
        << "the stationary fighter was dragged along by the clamp. Only the one "
           "who moved may be stopped; pulling the other is how a player gets "
           "shoved backwards by an opponent who is running away.";
}

// AND THE WALL MOVES WITH THE CHASER, which is the half that makes it a fighting
// game rather than a cage. A test that only checked the limit would pass on an
// implementation that pinned the pair together forever.
TEST(P2Stage, RetreatResumesForExactlyAsLongAsTheOpponentAdvances) {
    auto data = twoFighters();
    GameState s{};
    ResetMatch(s, 0x1D7u);
    s.p[0].posX = 0;
    s.p[1].posX = 0;

    InputPair back{};
    back.p[1].bits = kInputRight;
    for (int t = 0; t < 600; ++t) Simulate(s, back, *data);

    const std::int32_t pinned = s.p[1].posX;
    ASSERT_EQ(pinned - s.p[0].posX, kMaxSeparationSub) << "precondition: at the wall";

    // Now the opponent chases. The retreat must resume, and the pair must stay
    // exactly at the limit rather than the chaser closing the gap.
    InputPair chase{};
    chase.p[0].bits = kInputRight;   // p0 advances
    chase.p[1].bits = kInputRight;   // p1 keeps backing away
    for (int t = 0; t < 30; ++t) Simulate(s, chase, *data);

    EXPECT_GT(s.p[1].posX, pinned)
        << "the opponent advanced for 30 ticks and the retreating fighter did "
           "not gain a single sub-unit. The wall is anchored to the STAGE rather "
           "than to the opponent, so backing away is a one-way door.";
    // ONE WALK STEP INSIDE THE LIMIT WHILE THE CHASE IS ON, and that is the
    // pre-move anchor showing through rather than a rounding error. p1 is
    // clamped against where p0 stood at the TOP of the tick, so while both walk
    // at the same speed p1 ends each tick exactly one step behind the limit --
    // a constant gap, not a growing one.
    const std::int32_t chasing = s.p[1].posX - s.p[0].posX;
    EXPECT_LT(chasing, kMaxSeparationSub)
        << "the gap reached the limit DURING the chase, which would mean the "
           "clamp is reading the opponent's post-move position and handing the "
           "retreating player the chaser's own step in the tick they took it.";
    EXPECT_GT(chasing, kMaxSeparationSub - 4 * kSubUnitsPerPixel)
        << "the gap fell to " << chasing << ", far more than a walk step inside "
        << kMaxSeparationSub << ". The chaser is closing rather than the pair "
           "holding station, so the retreat is not keeping up.";

    // AND IT SETTLES EXACTLY AT THE LIMIT WHEN THE CHASE STOPS, which is the
    // sentence the whole rule is for: you may keep the ground you were given,
    // and you may not take more.
    InputPair keepGoing{};
    keepGoing.p[1].bits = kInputRight;
    for (int t = 0; t < 10; ++t) Simulate(s, keepGoing, *data);

    EXPECT_EQ(s.p[1].posX - s.p[0].posX, kMaxSeparationSub)
        << "the chaser stopped and the retreating fighter settled at "
        << (s.p[1].posX - s.p[0].posX) << " rather than at the limit "
        << kMaxSeparationSub << ".";
}

// And the absolute corner still wins: the wall is a limit on SEPARATION and the
// stage is a limit on POSITION, and a fighter backed into a corner has run out
// of the second one whatever the first allows.
TEST(P2Stage, TheAbsoluteCornerStillStopsTheRetreat) {
    auto data = twoFighters();
    GameState s{};
    ResetMatch(s, 0x1D7u);
    s.p[0].posX = kStageHalfWidthSub - kMaxSeparationSub;
    s.p[1].posX = kStageHalfWidthSub - kSubUnitsPerPixel;

    InputPair back{};
    back.p[0].bits = kInputRight;   // the chaser advances, so the wall follows
    back.p[1].bits = kInputRight;   // and the retreating fighter keeps going

    for (int t = 0; t < 600; ++t) Simulate(s, back, *data);

    EXPECT_EQ(s.p[1].posX, kStageHalfWidthSub)
        << "the retreating fighter ended at " << s.p[1].posX
        << " and the stage edge is " << kStageHalfWidthSub
        << ". The separation limit must never let anybody past the corner: it "
           "only ever pulls fighters together.";
}

// --- Push boxes --------------------------------------------------------------
//
// ROADMAP M1.2, asked for from play (2026-08-20): "the enemy collider should be
// blocking collisions rather than trigger -- we don't want to move through them
// ... this prevents players and enemies overlapping hurtboxes and missing
// attacks because of that."
namespace {

// A bench whose fighters have a body they cannot share.
std::unique_ptr<MatchData> pushBench() {
    auto d = twoFighters();
    const Box body{ -13 * kSubUnitsPerPixel, 0, 13 * kSubUnitsPerPixel, 60 * kSubUnitsPerPixel };
    d->p[0].pushbox = body;
    d->p[1].pushbox = body;
    return d;
}

std::int32_t gapBetweenBodies(const GameState& s, const MatchData& d) {
    const Box a = PlaceBox(d.p[0].pushbox, s.p[0].posX, s.p[0].posY, s.p[0].facing);
    const Box b = PlaceBox(d.p[1].pushbox, s.p[1].posX, s.p[1].posY, s.p[1].facing);
    return (a.x0 > b.x0 ? a.x0 : b.x0) - (a.x1 < b.x1 ? a.x1 : b.x1);
}

}  // namespace

TEST(P3Pushbox, FightersNeverOverlapAfterSeparation) {
    auto data = pushBench();
    GameState s{};
    ResetMatch(s, 0x1D7u);
    s.p[0].posX = 0;
    s.p[1].posX = 0;   // exactly coincident, the worst case

    InputPair walkIn{};
    walkIn.p[0].bits = kInputRight;   // and keep pressing into them

    for (int t = 0; t < 120; ++t) {
        Simulate(s, walkIn, *data);
        ASSERT_GE(gapBetweenBodies(s, *data), 0)
            << "tick " << t << ": the two bodies overlap by "
            << -gapBetweenBodies(s, *data)
            << " sub-units. Two fighters in the same place make every range in "
               "the game meaningless -- attacks whiff for a reason nobody "
               "watching can see.";
    }
}

// AN EQUAL SPLIT IS A MIRROR, and this is what says so. The same collision
// reflected through x = 0 must produce reflected positions; an "always push the
// left one" rule passes the overlap test above and fails this one.
TEST(P3Pushbox, SeparationIsAnExactMirror) {
    auto data = pushBench();

    GameState right{}, left{};
    ResetMatch(right, 0x1D7u);
    ResetMatch(left,  0x1D7u);
    right.p[0].posX =  4 * kSubUnitsPerPixel;  left.p[0].posX = -4 * kSubUnitsPerPixel;
    right.p[1].posX = -4 * kSubUnitsPerPixel;  left.p[1].posX =  4 * kSubUnitsPerPixel;

    // An ODD overlap on purpose: an even one is resolved by any halving, and the
    // rounding is exactly where a mirror breaks.
    right.p[0].posX += 1;  left.p[0].posX -= 1;

    for (int t = 0; t < 30; ++t) {
        Simulate(right, InputPair{}, *data);
        Simulate(left,  InputPair{}, *data);
        ASSERT_EQ(right.p[0].posX, -left.p[0].posX)
            << "tick " << t << ": separation stopped being a reflection. A "
               "resolution that favours a side turns which way you are facing "
               "into a frame advantage.";
        ASSERT_EQ(right.p[1].posX, -left.p[1].posX) << "tick " << t;
    }
}

// The corner is a wall on both sides: a fighter with nowhere to go absorbs none
// of the separation and the other one takes all of it.
TEST(P3Pushbox, TheCornerIsAWallOnBothSides) {
    auto data = pushBench();

    for (int side = 0; side < 2; ++side) {
        const std::int32_t wall = side == 0 ? -kStageHalfWidthSub : kStageHalfWidthSub;

        GameState s{};
        ResetMatch(s, 0x1D7u);
        s.p[0].posX = wall;
        s.p[1].posX = wall;   // both jammed into the same corner

        for (int t = 0; t < 60; ++t) Simulate(s, InputPair{}, *data);

        EXPECT_GE(gapBetweenBodies(s, *data), 0)
            << "side " << side << ": the corner swallowed the separation and the "
               "two bodies still overlap.";
        EXPECT_GE(s.p[0].posX, -kStageHalfWidthSub) << "side " << side;
        EXPECT_LE(s.p[0].posX,  kStageHalfWidthSub) << "side " << side;
        EXPECT_GE(s.p[1].posX, -kStageHalfWidthSub)
            << "side " << side << ": separation pushed a fighter through the wall.";
        EXPECT_LE(s.p[1].posX,  kStageHalfWidthSub)
            << "side " << side << ": separation pushed a fighter through the wall.";
    }
}

// AND A JUMP GOES OVER, which is what makes an airborne approach a way past
// somebody rather than a bounce off them.
TEST(P3Pushbox, AnAirborneFighterPassesOverAGroundedOne) {
    auto data = pushBench();
    GameState s{};
    ResetMatch(s, 0x1D7u);
    s.p[0].posX = 0;
    s.p[1].posX = 0;
    s.p[0].airborne = 1;
    s.p[0].posY     = 40 * kSubUnitsPerPixel;

    const std::int32_t before0 = s.p[0].posX;
    const std::int32_t before1 = s.p[1].posX;
    Simulate(s, InputPair{}, *data);

    EXPECT_EQ(s.p[1].posX, before1)
        << "a grounded fighter was shoved sideways by an airborne one directly "
           "above them. Nothing may push in the air: that is what a cross-up is.";
    EXPECT_EQ(s.p[0].posX, before0)
        << "the airborne fighter was pushed horizontally by the body below it.";
}

// THE WALL STOPS THE BODY, NOT THE ORIGIN.
//
// Asked for from play (2026-08-20): "we should calculate corner bounds from the
// back edge of the collider rather than the middle ... we don't want the player
// or enemy to disappear half into the corner."
TEST(P3Pushbox, TheCornerStopsTheBodyRatherThanTheOrigin) {
    auto data = pushBench();
    const std::int32_t halfWidth = data->p[0].pushbox.x1;
    ASSERT_GT(halfWidth, 0) << "precondition: the bench has a body";

    for (int side = 0; side < 2; ++side) {
        GameState s{};
        ResetMatch(s, 0x1D7u);
        // Alone against one wall, walking into it. The partner is parked far
        // enough away that neither the separation limit nor the pushbox has
        // anything to say about this.
        s.p[0].posX = 0;
        s.p[1].posX = 0;

        InputPair walk{};
        const std::uint16_t into = side == 0 ? kInputLeft : kInputRight;
        walk.p[0].bits = into;
        walk.p[1].bits = into;   // travel together, or the wall is unreachable

        for (int t = 0; t < 900; ++t) Simulate(s, walk, *data);

        // THE LEADER, not slot 0. The two start coincident, so the pushbox
        // separates them and one arrives at the wall a body ahead of the other;
        // which slot leads is an accident of the separation and not the subject
        // of this test.
        const int lead = side == 0 ? (s.p[0].posX < s.p[1].posX ? 0 : 1)
                                   : (s.p[0].posX > s.p[1].posX ? 0 : 1);
        const Box body = PlaceBox(data->p[lead].pushbox, s.p[lead].posX,
                                  s.p[lead].posY, s.p[lead].facing);
        const std::int32_t edge = side == 0 ? body.x0 : body.x1;
        const std::int32_t wall = side == 0 ? -kStageHalfWidthSub : kStageHalfWidthSub;

        EXPECT_EQ(edge, wall)
            << "side " << side << ": the body's edge stopped at " << edge
            << " and the wall is at " << wall
            << ". Clamping the ORIGIN puts half a fighter through the wall, "
               "which reads as the stage eating them rather than as them "
               "standing against it.";
    }

    // AND THE ORIGIN IS SHORT OF THE WALL BY EXACTLY THE BODY, which is the
    // arithmetic that says the allowance is the pushbox and not something else
    // that happens to be about the right size.
    GameState s{};
    ResetMatch(s, 0x1D7u);
    s.p[0].posX = 0;
    s.p[1].posX = 0;
    InputPair right{};
    right.p[0].bits = kInputRight;
    right.p[1].bits = kInputRight;
    for (int t = 0; t < 900; ++t) Simulate(s, right, *data);

    const std::int32_t furthest = s.p[0].posX > s.p[1].posX ? s.p[0].posX : s.p[1].posX;
    EXPECT_EQ(furthest, kStageHalfWidthSub - halfWidth)
        << "the leading origin reached " << furthest << " and the wall less a "
        << halfWidth << "-wide half-body is "
        << (kStageHalfWidthSub - halfWidth) << ".";
}

// --- How long a jump lasts, against how long an aerial takes -----------------
//
// The question behind "intelligently gate the move state in the combo graph"
// (asked 2026-08-20). An air-to-air self-cancel is the ONE cycle
// tests/test_gap_extent.cpp finds performable through the cancel system end to
// end -- but a fighter in the air is FALLING, and `airborne` is cleared by
// POSITION alone (Simulate.cpp clears it at posY <= 0, with no reference to what
// move is running). So the loop is bounded by the arc whether or not the cancel
// window says otherwise, and the bound is arithmetic rather than opinion.
//
// Measured here rather than asserted from the constants, because the constants
// are a file-local tuning block and the arc is what the integration actually
// produces.
TEST(P2Movement, AJumpIsAFixedNumberOfTicksAndBoundsAnyAirLoop) {
    auto data = twoFighters();

    GameState s{};
    ResetMatch(s, 0x1D7u);
    ASSERT_EQ(s.p[0].airborne, 0u) << "precondition: on the ground";

    InputPair up{};
    up.p[0].bits = kInputUp;
    Simulate(s, up, *data);
    ASSERT_NE(s.p[0].airborne, 0u) << "the jump did not start";

    int airTicks = 1;
    for (int t = 0; t < 600 && s.p[0].airborne != 0; ++t) {
        Simulate(s, InputPair{}, *data);
        if (s.p[0].airborne != 0) ++airTicks;
    }

    EXPECT_EQ(s.p[0].airborne, 0u)
        << "the fighter never came down in 600 ticks, so gravity is not acting "
           "and every 'air loop' in the analysis is unbounded for a reason that "
           "has nothing to do with the cancel graph.";

    // The number itself, recorded rather than asserted tightly: what matters is
    // that it is FINITE and that it is the ceiling on any air-to-air loop.
    RecordProperty("jump_air_ticks", airTicks);
    EXPECT_GT(airTicks, 10)
        << "the jump lasted " << airTicks
        << " ticks, which is too short for any aerial to come out at all";
    EXPECT_LT(airTicks, 200)
        << "the jump lasted " << airTicks << " ticks; that is not an arc.";

    // AND THAT CEILING IS WHAT THE COMBO GRAPH DOES NOT MODEL. Measured: the arc
    // is 38 ticks and `air_mp` is 22, so a jump holds ONE full repetition and
    // most of a second -- the fighter lands partway through the second one. The
    // third needs a fresh jump, a jump needs a landing, and a landing is not a
    // cancel: it is a gap, and a gap is the defender's turn.
    //
    // tests/test_gap_extent.cpp calls `air_mp > air_mp` the one cycle performable
    // through the cancel system end to end, and by the graph's own reckoning it
    // is unbounded. It is not. The graph never asks how long a fighter can stay
    // in the air, which is what "gate the move state" (asked 2026-08-20) means
    // and why 1.7 repetitions is the honest ceiling.
    EXPECT_LT(airTicks, 22 * 4)
        << "the arc holds four or more repetitions of a 22-tick aerial, which "
           "would make the air self-loop a much better approximation of an "
           "infinite than this test assumes. Re-derive the gating argument.";
}

// --- Commitment: a move, once started, owns the fighter ----------------------
//
// Asked for from play (2026-08-21): "we can move and attack and crouch and move
// as well. these are all things that are not possible in normal fighting games
// (with certain moves being an exception - rather than a rule) - so lets figure
// out how to model the player movement and actions closer to how street fighter
// works."
//
// THE RULE: while a move is running the fighter does not walk, does not jump and
// does not change posture. That is what a frame count MEANS -- startup, active,
// recovery are the frames you have given up -- and a range measured against an
// attacker who can slide forward during startup is not a range.
//
// THE EXCEPTION IS PER-MOVE AND AUTHORED, never a kernel rule: a move that wants
// to carry the fighter authors its own motion (ROADMAP M1.3(b), "movement is a
// move"). Until then nothing moves during an attack, which is the conservative
// default and the one every measured range in this repo already assumed.
//
// The gate reads "a move was ALREADY RUNNING at the top of this tick", not
// "a move is running now", and the difference is one tick and it is the whole
// of what keeps aerials startable: Up+button on one tick must still take off
// -- StepPhysics runs before StepAttack, so on that tick no move is running yet
// and the jump goes through -- while Up pressed a tick into a grounded move must
// not.
TEST(P2Commitment, AFighterDoesNotWalkDuringItsOwnAttack) {
    auto data = twoFighters();
    GameState s{};
    ResetMatch(s, 0x1D7u);

    InputPair press{};
    press.p[0].bits = kInputLP;
    Simulate(s, press, *data);
    ASSERT_EQ(s.p[0].moveId, 1u) << "precondition: the move is running";

    const std::int32_t before = s.p[0].posX;
    InputPair walk{};
    walk.p[0].bits = kInputRight;
    Simulate(s, walk, *data);

    EXPECT_EQ(s.p[0].moveId, 1u) << "the move ended early; this measures something else";
    EXPECT_EQ(s.p[0].posX, before)
        << "the fighter slid " << (s.p[0].posX - before)
        << " sub-units during its own attack. A range measured against an "
           "attacker who can walk forward mid-startup is not a range, and the "
           "genre gives the frames up on the press.";
}

TEST(P2Commitment, AFighterDoesNotJumpDuringItsOwnAttack) {
    auto data = twoFighters();
    GameState s{};
    ResetMatch(s, 0x1D7u);

    InputPair press{};
    press.p[0].bits = kInputLP;
    Simulate(s, press, *data);
    ASSERT_EQ(s.p[0].moveId, 1u);
    ASSERT_EQ(s.p[0].airborne, 0u);

    InputPair up{};
    up.p[0].bits = kInputUp;
    Simulate(s, up, *data);

    EXPECT_EQ(s.p[0].airborne, 0u)
        << "the fighter took off in the middle of a grounded attack. A jump is "
           "a commitment of its own and cannot be layered on top of another.";
}

TEST(P2Commitment, AFighterDoesNotChangePostureDuringItsOwnAttack) {
    auto data = twoFighters();
    GameState s{};
    ResetMatch(s, 0x1D7u);

    InputPair press{};
    press.p[0].bits = kInputLP;            // a standing move
    Simulate(s, press, *data);
    ASSERT_EQ(s.p[0].moveId, 1u);
    ASSERT_EQ(s.p[0].crouching, 0u);

    InputPair down{};
    down.p[0].bits = kInputDown;
    Simulate(s, down, *data);

    EXPECT_EQ(s.p[0].crouching, 0u)
        << "the fighter crouched mid-attack. The hurtbox would change under a "
           "move whose frame data was authored against the standing body, and "
           "every low-profile number in the file would be wrong.";
}

// AND THE ONE-TICK EXCEPTION THAT KEEPS AERIALS POSSIBLE. The press that starts
// an air move and the jump that makes it legal land on the SAME tick, and the
// commitment gate must let that tick through or no aerial can ever start from
// the ground.
TEST(P2Commitment, UpAndButtonOnOneTickStillStartsAnAerial) {
    auto data = twoFighters();
    data->p[0].moves[1].stance = kStanceAir;

    GameState s{};
    ResetMatch(s, 0x1D7u);

    InputPair upAttack{};
    upAttack.p[0].bits = kInputUp | kInputLP;
    Simulate(s, upAttack, *data);

    EXPECT_NE(s.p[0].airborne, 0u)
        << "the jump was refused on the tick it was pressed, because the gate "
           "read 'a move is running NOW' instead of 'a move was running at the "
           "top of the tick'. That one-tick difference is what makes an aerial "
           "startable at all.";
    EXPECT_EQ(s.p[0].moveId, 1u)
        << "the aerial did not start. Up+button must take off and attack in one "
           "tick, or every air move needs a scripted jump and a timed press.";
}

// What it does NOT gate: a stunned or downed fighter was never acting anyway,
// and a fighter who is IDLE may do all three. The rule is about moves, not about
// the fighter in general.
TEST(P2Commitment, AnIdleFighterStillWalksJumpsAndCrouches) {
    auto data = twoFighters();
    GameState s{};
    ResetMatch(s, 0x1D7u);
    const std::int32_t start = s.p[0].posX;

    InputPair right{};  right.p[0].bits = kInputRight;
    Simulate(s, right, *data);
    EXPECT_GT(s.p[0].posX, start) << "an idle fighter did not walk";

    InputPair down{};   down.p[0].bits = kInputDown;
    Simulate(s, down, *data);
    EXPECT_NE(s.p[0].crouching, 0u) << "an idle fighter did not crouch";

    InputPair up{};     up.p[0].bits = kInputUp;
    Simulate(s, up, *data);
    EXPECT_NE(s.p[0].airborne, 0u) << "an idle fighter did not jump";
}

// --- The jump is ballistic ---------------------------------------------------
//
// Asked for from play (2026-08-21): "jumping normals should not block movement
// like this - certain jumping normals might affect jump trajectory (like
// divekicks) but mostly they keep their momentum during the entire jump."
//
// THE MODEL: a jump's horizontal velocity is decided AT TAKEOFF -- neutral,
// forward or back, from the direction held on the jump tick -- and nothing in
// the air recomputes it. Not an attack: pressing an air normal keeps the arc,
// which is the reported bug (commitment zeroed velX and the fighter stopped
// dead mid-air). And not a held direction either: classic SF has no air
// steering, and "they keep their momentum during the entire jump" is a
// statement about the ARC, not about the attack. A divekick that changes
// trajectory is an authored per-move motion (ROADMAP M1.3(b)), never a kernel
// rule.
TEST(P2Ballistic, AnAirNormalKeepsTheJumpsMomentum) {
    auto data = twoFighters();
    data->p[0].moves[1].stance = kStanceAir;

    GameState s{};
    ResetMatch(s, 0x1D7u);

    // Forward jump: Right held on the takeoff tick decides the arc.
    InputPair jumpFwd{};
    jumpFwd.p[0].bits = kInputUp | kInputRight;
    Simulate(s, jumpFwd, *data);
    ASSERT_NE(s.p[0].airborne, 0u) << "precondition: took off";
    const std::int32_t arcVel = s.p[0].velX;
    ASSERT_GT(arcVel, 0) << "precondition: the forward jump carries velocity";

    // Two ticks into the arc, press the air normal. Momentum must survive it.
    Simulate(s, InputPair{}, *data);
    InputPair attack{};
    attack.p[0].bits = kInputLP;
    Simulate(s, attack, *data);
    ASSERT_EQ(s.p[0].moveId, 1u) << "precondition: the aerial started";

    const std::int32_t before = s.p[0].posX;
    Simulate(s, InputPair{}, *data);

    EXPECT_EQ(s.p[0].velX, arcVel)
        << "the air normal changed the arc's horizontal velocity from " << arcVel
        << " to " << s.p[0].velX
        << ". A jumping attack rides the jump; stopping dead mid-air is how a "
           "jump-in becomes unusable, because the attack lands BEHIND where the "
           "jump was taking you.";
    EXPECT_GT(s.p[0].posX, before)
        << "the fighter stopped advancing mid-arc while its aerial ran.";
}

TEST(P2Ballistic, TheArcIsDecidedAtTakeoffAndHeldDirectionsDoNotSteerIt) {
    auto data = twoFighters();

    GameState s{};
    ResetMatch(s, 0x1D7u);

    // Neutral jump: no direction on the takeoff tick.
    InputPair up{};
    up.p[0].bits = kInputUp;
    Simulate(s, up, *data);
    ASSERT_NE(s.p[0].airborne, 0u);
    ASSERT_EQ(s.p[0].velX, 0) << "precondition: a neutral jump goes straight up";

    // Hold Right for the rest of the arc. A ballistic jump ignores it.
    const std::int32_t apexX = s.p[0].posX;
    InputPair right{};
    right.p[0].bits = kInputRight;
    for (int t = 0; t < 10; ++t) Simulate(s, right, *data);

    EXPECT_EQ(s.p[0].velX, 0)
        << "holding Right mid-air steered a neutral jump. The arc is decided at "
           "takeoff: where you land is chosen when you leave the ground, which "
           "is what makes a jump a COMMITMENT and anti-airs a read rather than "
           "a chase.";
    EXPECT_EQ(s.p[0].posX, apexX)
        << "the neutral jump drifted " << (s.p[0].posX - apexX) << " sub-units.";
}

TEST(P2Ballistic, LandingRestoresGroundRules) {
    auto data = twoFighters();

    GameState s{};
    ResetMatch(s, 0x1D7u);

    InputPair jumpFwd{};
    jumpFwd.p[0].bits = kInputUp | kInputRight;
    Simulate(s, jumpFwd, *data);
    ASSERT_NE(s.p[0].airborne, 0u);

    // Ride the arc down holding nothing, then walk on the ground again.
    for (int t = 0; t < 120 && s.p[0].airborne != 0; ++t)
        Simulate(s, InputPair{}, *data);
    ASSERT_EQ(s.p[0].airborne, 0u) << "never landed";

    const std::int32_t landedX = s.p[0].posX;
    InputPair left{};
    left.p[0].bits = kInputLeft;
    Simulate(s, left, *data);
    EXPECT_LT(s.p[0].posX, landedX)
        << "a landed fighter no longer walks; the ballistic rule leaked past "
           "the landing.";
}

// A DOWNED FIGHTER IS LYING DOWN, and the body says so.
//
// Asked for from play (2026-08-21): "knockdowns still don't seem to be visible
// (maybe we should remove the hurtbox and just have a smaller collision box for
// their body during a knockdown so we can better visualize it)". The colour cue
// alone did not read, and the reason is fair: a box that keeps standing height
// looks like a fighter who is standing, whatever colour it is.
//
// This is SIMULATION, not presentation -- the view draws Hurtbox() and may not
// invent a pose the sim did not produce (ADR-011: pose is a pure function of
// state). So the kernel's own answer changes: while knocked down the body is the
// standing box TIPPED OVER -- as long as the body was tall, as tall as it was
// wide. Nothing can hit it anyway (AFighterOnTheFloorCannotBeHit), so the shape
// changes no exchange; what it changes is that a knockdown LOOKS like one.
TEST(P2Knockdown, ADownedFightersBodyIsLyingDown)  {
    auto data = twoFighters();
    data->p[0].moves[1].knockdownTicks = 20;
    data->p[0].moves[1].hitstun        = 1;

    GameState s = facingOff();
    ResolveHits(s, *data);
    ASSERT_EQ(s.p[1].knockdown, 20) << "precondition: the defender went down";

    const Box standing = Hurtbox(data->p[1], s.p[0]);   // p0 is untouched
    const Box downed   = Hurtbox(data->p[1], s.p[1]);

    const std::int32_t standW = standing.x1 - standing.x0;
    const std::int32_t standH = standing.y1 - standing.y0;
    const std::int32_t downW  = downed.x1 - downed.x0;
    const std::int32_t downH  = downed.y1 - downed.y0;

    EXPECT_EQ(downH, standW)
        << "the downed body is " << downH << " tall and the standing body is "
        << standW << " wide; lying down swaps the two.";
    EXPECT_EQ(downW, standH)
        << "the downed body is " << downW << " long and the standing body is "
        << standH << " tall; a knockdown that does not change the silhouette is "
           "a knockdown nobody can see, which is the bug as reported.";
    EXPECT_EQ(downed.y0, 0)
        << "the downed body floats: its floor edge is " << downed.y0
        << " and a body lying on the ground starts at 0.";
}

// AND A CROUCHING MOVE CANNOT START ON THE TICK YOU LAND, which is an ordering
// consequence rather than a rule anybody wrote.
//
// StepPhysics computes `crouching` from `airborne` BEFORE the landing clamp
// runs, so on the tick the fighter touches down `airborne` is still 1 at that
// line and `crouching` is forced to 0. StepAttack then sees a grounded fighter
// who is not crouching. It costs exactly one tick, and it is invisible until
// something asks for a crouching move out of a landing.
TEST(P2Movement, ACrouchingMoveCannotStartOnTheTickOfLanding) {
    auto data = twoFighters();
    data->p[0].moves[1].stance = kStanceCrouching;

    GameState s{};
    ResetMatch(s, 0x1D7u);

    InputPair up{};
    up.p[0].bits = kInputUp;
    Simulate(s, up, *data);
    ASSERT_NE(s.p[0].airborne, 0u) << "precondition: airborne";

    // Fall, holding Down and the button the whole way, so the only thing that
    // can decide the outcome is the landing tick's own ordering.
    InputPair downAttack{};
    downAttack.p[0].bits = kInputDown | kInputLP;

    int landedOnTick = -1;
    for (int t = 0; t < 120 && landedOnTick < 0; ++t) {
        Simulate(s, downAttack, *data);
        if (s.p[0].airborne == 0) landedOnTick = t;
    }
    ASSERT_GE(landedOnTick, 0) << "never landed";

    EXPECT_EQ(s.p[0].crouching, 0u)
        << "the fighter was crouching on the tick it landed. If that is now "
           "true the ordering in StepPhysics changed -- `crouching` is computed "
           "before the landing clamp, so `airborne` is still set when it runs.";
    EXPECT_EQ(s.p[0].moveId, 0u)
        << "a crouching move started on the landing tick. It cannot: the fighter "
           "is not crouching yet, and StanceAllows says no.";
}

// --- The buffer survives a direction tap -------------------------------------
//
// Found by adversarial review (2026-08-21). Capture stored ALL 16 pressed bits,
// and a press replaces the buffer wholesale -- so a defender who buffers HP for
// a wake-up reversal and then taps forward (or down, or up) before waking has
// the reversal silently replaced by a direction that can never match a bound
// move. "It eats my inputs sometimes" is exactly what that plays like.
//
// The rule: a press that could not start ANY move this character has -- no
// move's button mask intersects it -- does not overwrite a press that could.
TEST(P3Input, ADirectionTapDoesNotClobberABufferedReversal) {
    auto data = twoFighters();
    data->p[0].inputBufferFrames = 10;

    GameState s{};
    ResetMatch(s, 0x1D7u);
    s.p[0].hitstun = 6;                     // waking up in six ticks

    InputPair press{};
    press.p[0].bits = kInputLP;             // the reversal, buffered mid-stun
    Simulate(s, press, *data);
    ASSERT_EQ(s.p[0].moveId, 0u) << "precondition: still stunned";

    InputPair tap{};
    tap.p[0].bits = kInputRight;            // a nervous direction tap
    Simulate(s, tap, *data);
    Simulate(s, InputPair{}, *data);        // and released again

    // Ride out the stun with nothing held.
    for (int t = 0; t < 6; ++t) Simulate(s, InputPair{}, *data);

    EXPECT_EQ(s.p[0].moveId, 1u)
        << "the buffered reversal did not fire on wake-up. A direction tap "
           "matching no move replaced it in the buffer, which is pure loss: the "
           "tap could never start anything, and the press it destroyed could.";
}

// --- The buffer and the freeze ----------------------------------------------
//
// Hitstop advances NOTHING about a fighter, and before ROADMAP M1.3f that
// included the input bookkeeping -- which by accident did two right things (a
// pre-freeze buffer's age paused; a press HELD across the freeze read as a
// fresh edge on thaw) and one wrong one: a press-and-RELEASE made entirely
// inside the freeze was eaten, and a tap-confirm inside an eight-tick freeze
// is the exact input modern fighting games are built to accept. These three
// pin all three behaviours, because the crossplat golden runs a match with no
// moves and no hitstop and can catch none of them. `hitstop` is written by
// hand here for the same reason the stun tests write `hitstun` by hand: the
// bridge deliberately holds the authored field back (M1.3i).

TEST(P3Input, ATapEntirelyInsideTheFreezeStillBuffers) {
    auto data = twoFighters();
    data->p[0].inputBufferFrames = 10;

    GameState s{};
    ResetMatch(s, 0x1D7u);
    s.p[0].hitstop = 4;

    InputPair press{};
    press.p[0].bits = kInputLP;
    Simulate(s, press, *data);                  // pressed inside the freeze...
    Simulate(s, InputPair{}, *data);            // ...and released inside it too
    Simulate(s, InputPair{}, *data);
    Simulate(s, InputPair{}, *data);            // the freeze runs out
    ASSERT_EQ(s.p[0].hitstop, 0u) << "precondition: the freeze is over";

    Simulate(s, InputPair{}, *data);            // first tick the fighter runs

    EXPECT_EQ(s.p[0].moveId, 1u)
        << "a tap made entirely inside hitstop started nothing after the thaw. "
           "The freeze must gate the fighter's ADVANCE, not the recording of "
           "what the player asked for -- eating the tap-confirm makes every "
           "freeze a hole in the buffer it sits inside.";
}

TEST(P3Input, ABufferedPressOutlivesAFreezeLongerThanItsWindow) {
    auto data = twoFighters();
    data->p[0].inputBufferFrames = 2;           // the modern three-tick feel

    GameState s{};
    ResetMatch(s, 0x1D7u);
    s.p[0].hitstun = 3;

    InputPair press{};
    press.p[0].bits = kInputLP;
    Simulate(s, press, *data);                  // buffered mid-stun
    ASSERT_EQ(s.p[0].moveId, 0u) << "precondition: still stunned";

    s.p[0].hitstop = 6;                         // a freeze LONGER than the window
    for (int t = 0; t < 6; ++t) Simulate(s, InputPair{}, *data);
    for (int t = 0; t < 2; ++t) Simulate(s, InputPair{}, *data);

    EXPECT_EQ(s.p[0].moveId, 1u)
        << "the buffered press expired during the freeze. Frozen ticks are not "
           "time to the fighter, so they must not be time to the buffer: a "
           "window of 2 that ages through an 6-tick freeze is a window of "
           "nothing.";
}

// THE DONE-WHEN OF ROADMAP M1.1e, at the kernel: a one-frame link is a coin
// without the buffer and a certainty with the genre's two-tick window. The
// press is made two ticks early and HELD -- what a human trying a tight link
// actually does -- so without a buffer there is never an edge on the one tick
// that matters, and with one the press is consumed on exactly that tick.
TEST(P3Input, AOneFrameLinkNeedsTheWindowAHumanCannotHitAlone) {
    for (const std::int32_t window : { 0, 2 }) {
        auto data = twoFighters();
        data->p[0].inputBufferFrames = window;

        GameState s{};
        ResetMatch(s, 0x1D7u);
        s.p[0].posX = kLeftX;
        s.p[1].posX = kRightX;

        InputPair press{}, none{};
        press.p[0].bits = kInputLP;

        // t0: the opener. attack() is 1+2+5 = 8 ticks, hits on t1, stun 12 --
        // so the RESTART route connects only when the second press lands on
        // exactly t8, the tick the move ends and the button scan runs in the
        // same StepAttack call.
        Simulate(s, press, *data);
        ASSERT_EQ(s.p[0].moveId, 1u);

        Simulate(s, none, *data);   // t1: the hit lands; the button released
        ASSERT_LT(s.p[1].health, 1000) << "the opener whiffed; fix the bench";
        for (int t = 2; t < 6; ++t) Simulate(s, none, *data);

        // t6: the early press, then HELD. Two ticks before the link's one
        // usable tick.
        int secondHitTick = -1;
        for (int t = 6; t < 14; ++t) {
            const std::int32_t before = s.p[1].health;
            Simulate(s, press, *data);
            if (s.p[1].health < before && secondHitTick < 0) secondHitTick = t;
        }

        if (window == 0) {
            EXPECT_EQ(secondHitTick, -1)
                << "with no buffer the early held press produced a second hit "
                   "at t" << secondHitTick
                << "; a hold is one press and the link's tick had no edge";
        } else {
            EXPECT_EQ(secondHitTick, 9)
                << "with a 2-tick window the buffered press must be consumed "
                   "on exactly t8 -- the link's one tick -- and hit on t9, "
                   "inside the defender's stun";
            EXPECT_GT(s.p[1].hitstun, 0u)
                << "the second hit landed after the stun expired, so this "
                   "was a reset rather than the link";
        }
    }
}

// --- Stance: selection reads the input, posture follows the move ------------
//
// The design hole that killed the third stance attempt (ROADMAP M1.3e), as
// tests. Commitment freezes INPUT-driven posture while a move runs -- correct
// -- but selection used to read that frozen posture, so an ordinary gatling
// like stand_mp -> crouch_hp (hold Down, the cancel takes you into the crouch)
// was refused, and 120 of 121 measured cycles collapsed when stance was first
// wired. The fix is two rules (ADR-012 rule 3): selection reads the INPUT --
// is Down held NOW -- and the posture then FOLLOWS THE MOVE. A crouching move
// makes the fighter crouching, a standing move stands them up, and a move that
// states no posture (any/ground/air) leaves the established posture alone --
// which is `crouching`'s second authorized writer, the move-start rule.

namespace {

// The chain bench with postures: slot 1 is a STANDING move, slot 2 a CROUCHING
// one, the whiff-open edge joining them mid-life, and a second edge back.
std::unique_ptr<MatchData> stanceBench() {
    auto d = chainBench();
    d->p[0].moves[1].stance = kStanceStanding;
    d->p[0].moves[2].stance = kStanceCrouching;

    CancelEdge& back = d->p[0].cancels[1];
    back.from          = 2;
    back.to            = 1;
    back.earliestFrame = kCancelOpens;
    back.latestFrame   = kCancelCloses;
    back.contactMask   = 0;   // ungated: no contact condition
    d->p[0].cancelCount = 2;
    return d;
}

}  // namespace

TEST(P2Stance, ACancelIntoACrouchingMoveIsTakenWithDownHeldAndCrouchesTheBody) {
    auto data = stanceBench();
    GameState s{};
    ResetMatch(s, 0x1D7u);

    InputPair lp{}, downMp{};
    lp.p[0].bits     = kInputLP;
    downMp.p[0].bits = kInputDown | kInputMP;

    Simulate(s, lp, *data);                       // the standing source starts
    ASSERT_EQ(s.p[0].moveId, 1u) << "precondition: the source did not start";
    ASSERT_EQ(s.p[0].crouching, 0u) << "precondition: started standing";

    Simulate(s, InputPair{}, *data);              // frames 1..3 tick by
    Simulate(s, InputPair{}, *data);
    Simulate(s, InputPair{}, *data);
    Simulate(s, downMp, *data);                   // frame 4: inside the window

    EXPECT_EQ(s.p[0].moveId, 2u)
        << "holding Down did not take the cancel into the crouching follow-up. "
           "Selection must read the INPUT, not the posture commitment froze -- "
           "a gatling into a crouching normal is ordinary in the genre, and "
           "refusing it collapsed 120 of 121 measured cycles.";
    EXPECT_EQ(s.p[0].crouching, 1u)
        << "the crouching move started but the fighter is not crouching. "
           "Posture follows the move: the crouching body is what its frame "
           "data was authored against.";
}

TEST(P2Stance, ACancelIntoACrouchingMoveIsRefusedWithoutDown) {
    auto data = stanceBench();
    GameState s{};
    ResetMatch(s, 0x1D7u);

    InputPair lp{}, mp{};
    lp.p[0].bits = kInputLP;
    mp.p[0].bits = kInputMP;                      // no direction

    Simulate(s, lp, *data);
    ASSERT_EQ(s.p[0].moveId, 1u);
    Simulate(s, InputPair{}, *data);
    Simulate(s, InputPair{}, *data);
    Simulate(s, InputPair{}, *data);
    Simulate(s, mp, *data);

    EXPECT_EQ(s.p[0].moveId, 1u)
        << "a crouching follow-up started with no Down held. Selection reads "
           "the input; without the direction the stance is not asked for, and "
           "the source must keep running.";
}

TEST(P2Stance, ACancelIntoAStandingMoveStandsACrouchingFighterUp) {
    auto data = stanceBench();
    GameState s{};
    ResetMatch(s, 0x1D7u);

    InputPair downMp{}, lp{};
    downMp.p[0].bits = kInputDown | kInputMP;     // the crouching source
    lp.p[0].bits     = kInputLP;                  // the standing follow-up

    Simulate(s, downMp, *data);
    ASSERT_EQ(s.p[0].moveId, 2u) << "precondition: the crouching source did not start";
    ASSERT_EQ(s.p[0].crouching, 1u) << "precondition: not crouching";

    Simulate(s, InputPair{}, *data);              // Down released mid-move
    Simulate(s, InputPair{}, *data);
    Simulate(s, InputPair{}, *data);
    Simulate(s, lp, *data);                       // frame 4: the window is open

    EXPECT_EQ(s.p[0].moveId, 1u)
        << "the standing follow-up was refused out of a crouching move. With "
           "Down released, selection asks for a standing move and the frozen "
           "posture must not veto it.";
    EXPECT_EQ(s.p[0].crouching, 0u)
        << "the standing move started but the fighter stayed crouching. "
           "Posture follows the move.";
}

TEST(P2Stance, AMoveThatStatesNoPostureKeepsTheOneEstablished) {
    // chainBench unmodified: both moves are kStanceAny. A fighter who started
    // a move crouching keeps the crouch through an any-stance cancel, because
    // a move that states no posture has nothing to impose -- and a hurtbox
    // that silently stood up mid-chain would be hittable by everything the
    // crouch was ducking.
    auto data = chainBench();
    GameState s{};
    ResetMatch(s, 0x1D7u);

    InputPair downLp{}, downMp{};
    downLp.p[0].bits = kInputDown | kInputLP;
    downMp.p[0].bits = kInputDown | kInputMP;

    Simulate(s, downLp, *data);
    ASSERT_EQ(s.p[0].moveId, 1u);
    ASSERT_EQ(s.p[0].crouching, 1u) << "precondition: started crouching";

    Simulate(s, downLp, *data);                   // frames tick by, Down held
    Simulate(s, downLp, *data);
    Simulate(s, downLp, *data);
    Simulate(s, downMp, *data);                   // the cancel

    ASSERT_EQ(s.p[0].moveId, 2u) << "precondition: the any-stance cancel was refused";
    EXPECT_EQ(s.p[0].crouching, 1u)
        << "an any-stance follow-up changed the posture. Only a move that "
           "STATES a posture may impose one.";
}

TEST(P3Input, AReleaseInsideTheFreezeStillFiresTheNegativeEdge) {
    auto data = twoFighters();
    data->p[0].moves[1].negativeEdge = 1;

    GameState s{};
    ResetMatch(s, 0x1D7u);
    s.p[0].prevButtons = kInputLP;              // held before the freeze began
    s.p[0].hitstop     = 3;

    // Released on the first frozen tick, and never pressed again.
    for (int t = 0; t < 3; ++t) Simulate(s, InputPair{}, *data);
    ASSERT_EQ(s.p[0].hitstop, 0u) << "precondition: the freeze is over";

    Simulate(s, InputPair{}, *data);

    EXPECT_EQ(s.p[0].moveId, 1u)
        << "a release made inside the freeze fired nothing after it. The "
           "falling edge must survive to the first tick the fighter runs -- "
           "which is why the previous-buttons latch may not advance while the "
           "fighter is frozen.";
}

// --- The contact mask (ROADMAP M1.3 slice (a)) -------------------------------
//
// CancelEdge::contactMask uncollapses the schema's four `on` values: an edge
// names the contact outcomes that open it -- kContactHit, kContactBlock,
// kContactWhiff, any set of them -- and 0 stays UNGATED, the byte every
// hand-built bench and every `on: always` file already carries. The attacker
// observes the outcome through two fields: alreadyHitBits says a contact
// happened, and the low byte of Fighter::flags -- the blocked mirror, written
// only in ResolveHits' blocked arm -- says whether a guard stopped it.
//
// Why it matters enough to be the slice that goes first: under the old one-bit
// collapse an `on: hit` chain fired off a BLOCKED contact (block confirms into
// full strings for free), an `on: block` edge fired off a clean HIT, and a
// kara -- `on: whiff` in the first frames of a move -- was not expressible at
// all, because any nonzero gate demanded contact.
namespace {

// The shared chain bench with a configurable contact condition on its one
// edge. Source slot 1 (LP, 1+2+5), follow-up slot 2 (MP); the window spans
// [1, 6] so both the pre-contact frames and the post-contact frames are
// inside it, and which of them the edge accepts is entirely the mask's call.
std::unique_ptr<MatchData> contactBench(std::uint8_t mask) {
    auto d = twoFighters();
    MoveDef follow = attack();
    follow.button  = kInputMP;
    d->p[0].moves[2]  = follow;
    d->p[0].moveCount = 3;

    CancelEdge& e   = d->p[0].cancels[0];
    e.from          = 1;
    e.to            = 2;
    e.earliestFrame = 1;
    e.latestFrame   = 6;
    e.contactMask   = mask;
    d->p[0].cancelCount = 1;
    return d;
}

// Press LP on tick 0, press MP on tick `pressTick`, and report whether the
// cancel was taken by then. The defender holds `defenderBits` throughout --
// kInputRight is p1's "back", i.e. a standing guard. Frame arithmetic, for
// reading the call sites: the source starts on tick 0 at frame 0; on tick N
// (N >= 1) the frame becomes N and the cancel test runs at that frame BEFORE
// ResolveHits -- so contact lands on the frame-1 tick and is first visible to
// a cancel on the frame-2 tick.
bool cancelTaken(MatchData& data, int pressTick, std::uint16_t defenderBits) {
    GameState s{};
    ResetMatch(s, 0x1D7u);
    s.p[0].posX = kLeftX;
    s.p[1].posX = kRightX;

    InputPair in{};
    in.p[0].bits = kInputLP;
    in.p[1].bits = defenderBits;
    Simulate(s, in, data);
    EXPECT_EQ(s.p[0].moveId, 1u) << "the source move did not start";

    bool taken = false;
    for (int t = 1; t <= pressTick + 1 && !taken; ++t) {
        in.p[0].bits = (t == pressTick) ? kInputMP : 0u;
        Simulate(s, in, data);
        taken = s.p[0].moveId == 2u;
    }
    return taken;
}

}  // namespace

// The Done-when test. `on: whiff` inside the source's first frames is the
// kara: the move has connected on nothing yet, alreadyHitBits is 0, and that
// IS the whiff observation -- no new state needed for this third of the mask.
// Fighters far apart so the whole life of the move is an honest whiff.
TEST(P3Cancels, AKaraCancelFiresOnWhiffInsideItsWindow) {
    auto data = contactBench(kContactWhiff);

    GameState s{};
    ResetMatch(s, 0x1D7u);
    s.p[0].posX = -px(300);   // nowhere near the 40 px reach
    s.p[1].posX =  px(300);

    InputPair in{};
    in.p[0].bits = kInputLP;
    Simulate(s, in, *data);
    ASSERT_EQ(s.p[0].moveId, 1u);

    in.p[0].bits = kInputMP;   // frame 1: startup, nothing connected
    Simulate(s, in, *data);

    EXPECT_EQ(s.p[0].moveId, 2u)
        << "an `on: whiff` edge inside its window did not fire while the move "
           "had connected on nothing. The kara is the mechanic this mask "
           "exists for, and the whiff bit is its authorable form.";
    EXPECT_EQ(s.p[0].moveFrame, 0u);
}

// The other half of `on: whiff`: contact CLOSES it. Same edge, same window,
// fighters in range -- the press arrives one tick after the hit lands, the
// window is still open, and the edge must refuse.
TEST(P3Cancels, AWhiffOnlyEdgeClosesTheTickContactBecomesVisible) {
    auto data = contactBench(kContactWhiff);
    EXPECT_FALSE(cancelTaken(*data, /*pressTick=*/2, /*defenderBits=*/0))
        << "an `on: whiff` edge fired AFTER its move connected. Whiff means "
           "whiff: once alreadyHitBits records a contact, this gate is shut "
           "for the rest of the window.";
}

// `on: hit` refuses a blocked contact. This is the behaviour change the mask
// exists to make: under the old collapse both outcomes set the same bit and a
// block-confirm chained for free.
TEST(P3Cancels, AHitOnlyEdgeRefusesABlockedContact) {
    auto data = contactBench(kContactHit);

    EXPECT_TRUE(cancelTaken(*data, /*pressTick=*/2, /*defenderBits=*/0))
        << "an `on: hit` edge did not fire off a clean hit, so the mask broke "
           "the ordinary hit-confirm while distinguishing outcomes.";

    // kInputRight is "back" for the right-hand fighter: a standing guard, and
    // attack()'s blockedAs defaults to mid, which high guard stops.
    EXPECT_FALSE(cancelTaken(*data, /*pressTick=*/2, /*defenderBits=*/kInputRight))
        << "an `on: hit` edge fired off a BLOCKED contact. The file said hit; "
           "a guard stopped this one; chaining anyway is the one-bit collapse "
           "this mask deleted.";
}

// And the mirror image: `on: block` is the block-confirm the genre authors
// (a blocked string that stays safe by cancelling into a safer follow-up),
// and it must NOT be reachable off a clean hit.
TEST(P3Cancels, ABlockOnlyEdgeFiresOnBlockAndRefusesACleanHit) {
    auto data = contactBench(kContactBlock);

    EXPECT_TRUE(cancelTaken(*data, /*pressTick=*/2, /*defenderBits=*/kInputRight))
        << "an `on: block` edge did not fire off a blocked contact, so the "
           "block bit is not being observed at all.";

    EXPECT_FALSE(cancelTaken(*data, /*pressTick=*/2, /*defenderBits=*/0))
        << "an `on: block` edge fired off a clean HIT. Block means block; the "
           "outcome the guard decides is the outcome the mask reads.";
}

// The observation's bookkeeping: the blocked mirror is a subset of
// alreadyHitBits while the window lives, and both die together when the move
// ends -- GameState.h promises the invariant, so a test owns it.
TEST(P3Cancels, TheBlockedMirrorLivesAndDiesWithTheContactRecord) {
    auto data = contactBench(kContactBlock);

    GameState s{};
    ResetMatch(s, 0x1D7u);
    s.p[0].posX = kLeftX;
    s.p[1].posX = kRightX;

    InputPair in{};
    in.p[0].bits = kInputLP;
    in.p[1].bits = kInputRight;   // guard up the whole time
    Simulate(s, in, *data);

    in.p[0].bits = 0;
    Simulate(s, in, *data);       // frame 1: the blocked contact lands

    const std::uint16_t blockedBits =
        static_cast<std::uint16_t>(s.p[0].flags & kFlagsBlockedBits);
    ASSERT_NE(s.p[0].alreadyHitBits, 0u) << "precondition: contact happened";
    EXPECT_NE(blockedBits, 0u)
        << "a guard stopped the contact and the attacker's blocked mirror "
           "says nothing did";
    EXPECT_EQ(static_cast<std::uint8_t>(blockedBits),
              static_cast<std::uint8_t>(s.p[0].alreadyHitBits))
        << "the blocked mirror is not a subset of alreadyHitBits";

    // Run the source out. Both records clear on the same tick, so the next
    // performance of the same move starts with a clean slate.
    for (int t = 0; t < 10; ++t) Simulate(s, InputPair{}, *data);
    ASSERT_EQ(s.p[0].moveId, 0u) << "precondition: the move has ended";
    EXPECT_EQ(s.p[0].alreadyHitBits, 0u);
    EXPECT_EQ(s.p[0].flags & kFlagsBlockedBits, 0u)
        << "the blocked mirror outlived the contact record it mirrors";
}

// --- Authored motion (ROADMAP M1.3(b2), ADR-014 step two) --------------------
//
// Commitment's promise had two halves: a committed fighter's velocity is zero,
// UNLESS ITS MOVE SAYS OTHERWISE. The first half landed with M1.3f's pipeline;
// this is the second -- MoveDef::motion, the fixed-bound velocity keys that
// let a lunge carry the fighter, a hop kick leave the ground mid-move, and a
// divekick rewrite an arc it is already flying. Zero keys is every move
// authored before the field: the silent test at the bottom is the regression
// guard the other four lean on.
namespace {

// Far enough apart that nothing connects: these tests are about the ATTACKER's
// own body, and a hit would drag pushback, hitstop and interrupts into what
// should be pure kinematics.
constexpr std::int32_t kFarLeft  = -px(300);
constexpr std::int32_t kFarRight =  px(300);

std::unique_ptr<MatchData> motionBench(std::initializer_list<MotionKey> keys) {
    auto d = twoFighters();
    MoveDef& m = d->p[0].moves[1];
    std::int32_t n = 0;
    for (const MotionKey& k : keys) {
        m.motion[n] = k;
        ++n;
    }
    m.motionCount = n;
    return d;
}

GameState apartState() {
    GameState s{};
    ResetMatch(s, 0x1D7u);
    s.p[0].posX = kFarLeft;
    s.p[1].posX = kFarRight;
    return s;
}

}  // namespace

TEST(P3Movement, ALungeCarriesTheFighterAndTheStickSteersNothing) {
    // One key from tick 0: 2 px/tick toward the fighter's facing, for the
    // move's whole 8-tick life.
    auto data = motionBench({ { 0, 512, 0 } });

    GameState s = apartState();
    InputPair in{};
    in.p[0].bits = kInputLP;
    Simulate(s, in, *data);
    ASSERT_EQ(s.p[0].moveId, 1u);
    const std::int32_t start = s.p[0].posX;

    // Held AWAY for the whole move: if the stick could steer, the lunge would
    // shorten. Motion owns the velocity; the stick owns nothing.
    in.p[0].bits = kInputLeft;
    for (int t = 0; t < 8; ++t) Simulate(s, in, *data);

    EXPECT_EQ(s.p[0].moveId, 0u) << "precondition: the 8-tick move has ended";
    EXPECT_EQ(s.p[0].posX, start + 8 * 512)
        << "the lunge did not carry the fighter its authored 2 px on each of "
           "the move's 8 ticks -- either motion is not applied or the held "
           "stick steered a committed fighter.";

    // And the mirror: the same key on a left-facing fighter travels -X. A
    // branch, never facing multiplied into a coordinate -- but the OUTCOME is
    // what a player sees, so the outcome is what is asserted.
    GameState mirrored = apartState();
    mirrored.p[0].posX   = kFarRight;
    mirrored.p[1].posX   = kFarLeft;
    mirrored.p[0].facing = 1;
    mirrored.p[1].facing = 0;
    in.p[0].bits = kInputLP;
    Simulate(mirrored, in, *data);
    ASSERT_EQ(mirrored.p[0].moveId, 1u);
    const std::int32_t mStart = mirrored.p[0].posX;
    in.p[0].bits = 0;
    for (int t = 0; t < 8; ++t) Simulate(mirrored, in, *data);
    EXPECT_EQ(mirrored.p[0].posX, mStart - 8 * 512)
        << "a left-facing lunge did not mirror; `forward` is the fighter's "
           "own forward or the mechanic is unusable on half the screen.";
}

TEST(P3Movement, AHopKickLeavesTheGroundMidMoveAndLandsBackIntoIt) {
    // Up at 3 px/tick from frame 1, down at the same rate from frame 4: a
    // four-tick hop inside an 8-tick move. `airborneFromTick` is the
    // CLASSIFICATION half (attack kinds, invulnerability); this is the
    // PHYSICS half the map recorded as missing.
    auto data = motionBench({ { 1, 0, 768 }, { 4, 0, -768 } });

    GameState s = apartState();
    InputPair in{};
    in.p[0].bits = kInputLP;
    Simulate(s, in, *data);
    ASSERT_EQ(s.p[0].moveId, 1u);

    in.p[0].bits = 0;
    bool leftGround = false;
    std::int32_t apex = 0;
    for (int t = 0; t < 12; ++t) {
        Simulate(s, in, *data);
        if (s.p[0].airborne) leftGround = true;
        if (s.p[0].posY > apex) apex = s.p[0].posY;
    }

    EXPECT_TRUE(leftGround)
        << "the hop's upward key never set `airborne`; a positive authored "
           "velY IS leaving the ground.";
    EXPECT_EQ(apex, 768 * 3)
        << "three ticks of +768 should peak at 2304 sub-units (9 px); the "
           "apex says gravity or the stick interfered with an authored arc.";
    EXPECT_EQ(s.p[0].airborne, 0u) << "the hop did not land";
    EXPECT_EQ(s.p[0].posY, 0);
    EXPECT_EQ(s.p[0].moveId, 0u)
        << "the move should have run out grounded; a hop is not a knockdown";
}

TEST(P3Movement, ADivekickRewritesTheArcItIsFlying) {
    // The move is startable in the air (stance Any) and dives: forward 2 px,
    // down 4 px, every tick, gravity-free -- the segment IS the trajectory.
    auto data = motionBench({ { 0, 512, -1024 } });

    GameState s = apartState();
    InputPair in{};
    in.p[0].bits = kInputUp;
    Simulate(s, in, *data);
    ASSERT_NE(s.p[0].airborne, 0u);

    // Ride the ballistic arc for 5 ticks, then press the dive.
    in.p[0].bits = 0;
    for (int t = 0; t < 5; ++t) Simulate(s, in, *data);
    ASSERT_NE(s.p[0].airborne, 0u);
    const std::int32_t ballisticVelY = s.p[0].velY;

    in.p[0].bits = kInputLP;
    Simulate(s, in, *data);
    ASSERT_EQ(s.p[0].moveId, 1u) << "the dive did not start in the air";

    const std::int32_t beforeX = s.p[0].posX;
    Simulate(s, in, *data);
    EXPECT_EQ(s.p[0].velY, -1024)
        << "the divekick's authored velY did not replace the ballistic "
        << ballisticVelY << "; the arc is supposed to be REWRITTEN.";
    EXPECT_EQ(s.p[0].posX - beforeX, 512)
        << "the dive's forward component did not carry";

    const std::int32_t velYFirst = s.p[0].velY;
    Simulate(s, InputPair{}, *data);
    if (s.p[0].airborne)
        EXPECT_EQ(s.p[0].velY, velYFirst)
            << "velY changed between two ticks of one motion segment, so "
               "gravity is being applied under an authored trajectory -- the "
               "keys are RESOLVED states, not impulses.";
}

TEST(P3Movement, ASilentMoveStillDoesNotMove) {
    // motionCount 0: commitment's zero velocity, byte for byte -- every move
    // authored before this field, and every hand-built bench in this suite.
    auto data = twoFighters();

    GameState s = apartState();
    InputPair in{};
    in.p[0].bits = kInputLP;
    Simulate(s, in, *data);
    ASSERT_EQ(s.p[0].moveId, 1u);
    const std::int32_t start = s.p[0].posX;

    in.p[0].bits = kInputRight;   // held toward the opponent, the whole move
    for (int t = 0; t < 8; ++t) Simulate(s, in, *data);

    EXPECT_EQ(s.p[0].posX, start)
        << "a move with no motion keys moved, so either commitment broke or "
           "a zero-initialised motion block is not inert -- the "
           "scalingReduction incident wearing a new field.";
}

// --- Corner push (M1.6's microwalk slice) ------------------------------------
//
// When the wall already stops the defender, their pushback is absorbed; the
// authored corner push sends the pressure back through the ATTACKER instead.
// Zero -- every shipped move today -- is byte-for-byte the old behaviour.
TEST(P3Movement, ACornerPushRecoilsTheAttackerOnlyAtTheWall) {
    auto data = twoFighters();
    data->p[0].moves[1].cornerPushHit = 1536;   // ~12 px total recoil
    // A pushbox, so WallLimitFor has a body to stop; both fighters share it.
    data->p[0].pushbox = bodyBox();
    data->p[1].pushbox = bodyBox();

    // Mid-stage: the defender is nowhere near a wall, and the recoil must
    // not fire -- corner push is a fact about the WALL, not about the hit.
    {
        GameState s = facingOff();
        ResolveHits(s, *data);
        EXPECT_EQ(s.p[0].pushX, 0)
            << "the attacker recoiled from a mid-stage hit; corner push fired "
               "without a corner";
    }

    // Cornered: the defender's origin stands at its own wall limit.
    {
        GameState s = facingOff();
        const std::int32_t lim = WallLimitFor(data->p[1], s.p[1]);
        s.p[1].posX = lim;                         // body against the right wall
        s.p[0].posX = lim - px(14);                // adjacent, boxes overlapping
        ResolveHits(s, *data);
        EXPECT_LT(s.p[0].pushX, 0)
            << "a hit on the cornered defender queued no recoil on the "
               "attacker (pushX should point AWAY from the wall)";
        EXPECT_LT(s.p[1].health, 1000) << "precondition: the hit landed";
    }
}

// --- Counter-hit (ROADMAP M1.3(c), the first mechanic under ADR-015) ---------
//
// The reserved MoveDef bytes stop being reserved: a move may author a price
// for catching the defender STARTING something. Startup only -- not active (a
// trade is a trade, and charging it as a counter would make every trade a
// counter for both sides), not recovery (a punish is already its own reward)
// -- and off by default: zero bonus is every move authored before the field,
// and the unpatched MatchData hash must not move.

TEST(P3Reactions, CounterHitAddsTheAuthoredStun) {
    auto data = twoFighters();
    data->p[0].moves[1].counterHitstunBonus = 7;
    data->p[0].moves[1].counterDamageBonus  = 50;

    // COUNTER: the defender is mid-startup when the hit lands. attack() has
    // startup 1, so moveFrame 0 is the startup frame -- and a fighter whose
    // move has not reached active has no hitbox, so only p0 connects.
    {
        GameState s = facingOff();
        s.p[1].moveFrame = 0;
        ResolveHits(s, *data);
        EXPECT_EQ(s.p[1].hitstun, 12 + 7)
            << "the authored counter bonus did not reach the stun";
        EXPECT_EQ(s.p[1].health, 1000 - (100 + 50))
            << "the authored counter damage bonus did not reach the health";
    }

    // NOT a counter: the defender is idle. Base numbers exactly.
    {
        GameState s = facingOff();
        s.p[1].moveId    = 0;
        s.p[1].moveFrame = 0;
        ResolveHits(s, *data);
        EXPECT_EQ(s.p[1].hitstun, 12) << "an idle defender was charged as a counter";
        EXPECT_EQ(s.p[1].health, 1000 - 100);
    }

    // NOT a counter: the defender is ACTIVE -- this is a trade, both connect,
    // and a trade charged as a counter would hand both sides a bonus for the
    // same instant.
    {
        GameState s = facingOff();   // both mid-active by construction
        ResolveHits(s, *data);
        EXPECT_EQ(s.p[1].hitstun, 12) << "a trade was charged as a counter";
    }

    // OFF BY DEFAULT: no bonus authored, the same mid-startup catch pays base.
    {
        auto plain = twoFighters();
        GameState s = facingOff();
        s.p[1].moveFrame = 0;
        ResolveHits(s, *plain);
        EXPECT_EQ(s.p[1].hitstun, 12)
            << "a move that authors no counter_hit grew counter semantics";
        EXPECT_EQ(s.p[1].health, 1000 - 100);
    }
}

TEST(P3Reactions, ALauncherPutsTheDefenderInTheAirAndAirHitstunTakesOver) {
    auto data = twoFighters();
    data->p[0].moves[1].launchVelYSub = 2000;
    data->p[0].moves[1].launchVelXSub = 500;
    data->p[0].moves[1].airHitstun    = 30;

    // THE LAUNCH: a clean hit on a GROUNDED defender leaves the ground with
    // the authored velocity, X pointed away from the attacker by the same
    // position rule pushback uses. The launching hit itself is charged the
    // GROUND number -- the defender was grounded when it connected.
    GameState s = facingOff();
    s.p[1].moveId    = 0;
    s.p[1].moveFrame = 0;
    ResolveHits(s, *data);
    EXPECT_EQ(s.p[1].airborne, 1) << "the launcher did not launch";
    EXPECT_EQ(s.p[1].velY, 2000);
    EXPECT_EQ(s.p[1].velX, 500) << "launch X did not point away from the attacker";
    EXPECT_EQ(s.p[1].hitstun, 12)
        << "the launching hit was charged air hitstun; the defender was "
           "grounded when it landed";

    // THE ARC SURVIVES THE NEXT TICK. A launched body is marked
    // (Fighter::reaction) so the airborne-stun straight-drop rule leaves its
    // velocity alone -- without the mark, StepPhysics would eat the launch
    // one tick after the launcher connected.
    EXPECT_EQ(s.p[1].reaction, kReactionLaunched);
    {
        const std::int32_t xBefore = s.p[1].posX;
        InputPair in{};
        Simulate(s, in, *data);
        EXPECT_EQ(s.p[1].velX, 500)
            << "the airborne-stun rule zeroed a LAUNCHED body's arc";
        EXPECT_GT(s.p[1].posX, xBefore) << "the launched body did not travel";
    }

    // THE JUGGLE: a fresh contact on the now-airborne defender reads the
    // move's authored AIR number instead of its ground one.
    s.p[0].alreadyHitBits = 0;
    s.p[0].flags          = 0;
    s.p[0].moveId         = 1;
    s.p[0].moveFrame      = 1;
    ResolveHits(s, *data);
    EXPECT_EQ(s.p[1].hitstun, 30)
        << "an airborne defender was charged ground hitstun";

    // AND A BODY MERELY HIT OUT OF ITS JUMP STILL DROPS STRAIGHT -- the
    // behaviour the crossplat golden pins: no launch authored, so no mark,
    // and the next physics tick zeroes the jump's velX.
    {
        GameState j = facingOff();
        j.p[1].moveId    = 0;
        j.p[1].moveFrame = 0;
        j.p[1].airborne  = 1;
        j.p[1].posY      = px(30);
        j.p[1].velX      = 700;
        auto plainAir = twoFighters();
        ResolveHits(j, *plainAir);
        ASSERT_GT(j.p[1].hitstun, 0);
        EXPECT_EQ(j.p[1].reaction, 0)
            << "an ordinary air hit was marked as a launch";
        InputPair in{};
        Simulate(j, in, *plainAir);
        EXPECT_EQ(j.p[1].velX, 0)
            << "an air-reset kept its arc; the straight drop is recorded "
               "golden behaviour, not a free variable";
    }

    // OFF BY DEFAULT, both halves: no launch authored keeps the defender on
    // the ground; no air number authored charges an airborne defender the
    // ground number -- ADR-011's silence rule, at the kernel layer.
    auto plain = twoFighters();
    GameState g = facingOff();
    g.p[1].moveId    = 0;
    g.p[1].moveFrame = 0;
    ResolveHits(g, *plain);
    EXPECT_EQ(g.p[1].airborne, 0) << "a move that authors no launch launched";

    GameState a = facingOff();
    a.p[1].moveId    = 0;
    a.p[1].moveFrame = 0;
    a.p[1].airborne  = 1;
    a.p[1].posY      = px(30);
    ResolveHits(a, *plain);
    EXPECT_EQ(a.p[1].hitstun, 12)
        << "a move with no air number changed its stun against an airborne "
           "defender";
}

TEST(P3Reactions, AWallBounceReturnsTheDefenderIntoRange) {
    auto data = twoFighters();
    data->p[0].moves[1].launchVelYSub = 2000;
    data->p[0].moves[1].launchVelXSub = 800;
    data->p[0].moves[1].hitstun       = 60;   // long enough to fly the whole arc stunned
    data->p[0].moves[1].airHitstun    = 60;
    data->p[0].moves[1].onHitReaction = kOnHitWallBounce;

    // The corner geometry the exhibit is about: the defender is nearly AT its
    // wall, so the launch drives it in and the wall must give it back.
    GameState s = facingOff();
    s.p[1].moveId    = 0;
    s.p[1].moveFrame = 0;
    s.p[1].posX      = kStageHalfWidthSub - px(2);
    s.p[0].posX      = s.p[1].posX - px(34);

    ResolveHits(s, *data);
    ASSERT_EQ(s.p[1].reaction, kReactionWallBounceArmed)
        << "the hit did not arm the bounce";
    ASSERT_EQ(s.p[1].velX, 800);

    InputPair in{};
    bool bounced = false;
    for (int t = 0; t < 20 && !bounced; ++t) {
        Simulate(s, in, *data);
        if (s.p[1].velX < 0) bounced = true;
    }
    EXPECT_TRUE(bounced) << "the wall never returned the defender";
    EXPECT_EQ(s.p[1].velX, -800)
        << "the bounce changed the speed as well as the direction";
    EXPECT_EQ(s.p[1].bounces, 1) << "the spend was not recorded";
    EXPECT_EQ(s.p[1].reaction, kReactionLaunched)
        << "the bounce did not spend itself -- a second wall would bounce "
           "again with no fresh arming hit";

    // OFF BY DEFAULT: the same launch with no on_hit authored just rams the
    // wall -- the clamp holds the body and nothing comes back.
    auto plain = twoFighters();
    plain->p[0].moves[1].launchVelYSub = 2000;
    plain->p[0].moves[1].launchVelXSub = 800;
    plain->p[0].moves[1].hitstun       = 60;
    plain->p[0].moves[1].airHitstun    = 60;

    GameState g = facingOff();
    g.p[1].moveId    = 0;
    g.p[1].moveFrame = 0;
    g.p[1].posX      = kStageHalfWidthSub - px(2);
    g.p[0].posX      = g.p[1].posX - px(34);
    ResolveHits(g, *plain);
    ASSERT_EQ(g.p[1].reaction, kReactionLaunched);
    for (int t = 0; t < 20; ++t) {
        Simulate(g, in, *plain);
        EXPECT_GE(g.p[1].velX, 0)
            << "a move that authors no on_hit reaction bounced at tick " << t;
    }
    EXPECT_EQ(g.p[1].bounces, 0);
}
