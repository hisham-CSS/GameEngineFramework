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

// --- More than two fighters (docs/ADR-009) ----------------------------------

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
