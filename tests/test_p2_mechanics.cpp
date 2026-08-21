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
        // HEIGHT, not just the flag. stepFighter clears `airborne` the tick a
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
    e.onHit         = 0;
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
    e.onHit         = 0;
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
