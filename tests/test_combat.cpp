// The first thing in the kernel that can make one fighter hit another.
//
// Four properties are worth more than the rest of this file put together, and
// they are the reason it is written the way it is:
//
//   1. A hit that reaches connects, and one that falls short does not -- with a
//      guard that the box was actually LIVE in the falling-short case, because
//      "no hit occurred" is the easiest assertion in the world to pass by
//      accident. A move that never started, a table that never loaded, a frame
//      window off by one: all of them produce a green "did not connect".
//   2. A multi-frame active window hits exactly ONCE. This is the oldest bug in
//      the genre and it is invisible in a single-frame test.
//   3. Mirroring is exact. The same scenario reflected produces exactly
//      reflected positions and identical everything-else, tick for tick. This is
//      ARCHITECTURE.md D2's rounding-asymmetry property, and it is the single
//      most valuable test here: a mirror that loses one sub-unit changes the
//      reach of a left-facing character, which changes whether a combo connects,
//      which is the difference between Terminating and Infinite in the analysis
//      this engine is the case study for.
//   4. Combat still round-trips through snapshot / restore / re-simulate byte
//      for byte, because everything above is worthless if it cannot roll back.
//
// Like test_kernel.cpp, this links CseKernel and NOT the Engine.
#include <gtest/gtest.h>

#include "cse/kernel/Simulate.h"

#include <cstring>
#include <vector>

using namespace cse::kernel;

namespace {

// Everything in this file is authored in pixels and converted once, here, so a
// reader can check a reach against a distance without multiplying by 256 in
// their head.
constexpr std::int32_t px(std::int32_t pixels) { return pixels * kSubUnitsPerPixel; }

MoveDef makeMove(std::int32_t startup, std::int32_t active, std::int32_t recovery,
                 std::int32_t damage, std::int32_t hitstun, Box hitbox,
                 std::uint16_t button) {
    // Field-by-field rather than an aggregate initialiser: MoveDef's member order
    // is chosen for packing (Combat.h), and a positional initialiser here would
    // silently swap two int32s the day that order changes.
    MoveDef m{};
    m.startup  = startup;
    m.active   = active;
    m.recovery = recovery;
    m.damage   = damage;
    m.hitstun  = hitstun;
    m.hitbox   = hitbox;
    m.button   = button;
    return m;
}

// The hurtbox is DELIBERATELY ASYMMETRIC about x = 0: [-14, +18) pixels. A
// symmetric body is its own mirror image, so every mirroring bug in this file
// would be invisible against one -- the test would pass with MirrorBox returning
// its argument.
constexpr Box kHurtbox{ px(-14), px(0), px(18), px(64) };

// Slot 1: a jab. Live on frames 3, 4 and 5 of a 14-tick move, reaching from 20
// to 60 pixels in front of the fighter, at chest height.
constexpr Box kJabBox{ px(20), px(32), px(60), px(48) };
constexpr std::int32_t kJabStartup  = 3;
constexpr std::int32_t kJabActive   = 3;
constexpr std::int32_t kJabRecovery = 8;
constexpr std::int32_t kJabDamage   = 30;
constexpr std::int32_t kJabHitstun  = 12;
constexpr std::int32_t kJabDuration = kJabStartup + kJabActive + kJabRecovery;

// Slot 2: the same reach at head height and above, so that a miss can be caused
// by the Y axis alone. A hit test written against X only passes every other test
// in this file.
constexpr Box kHighBox{ px(20), px(80), px(60), px(96) };

FighterData makeFighterData() {
    FighterData d{};
    d.hurtbox = kHurtbox;
    // Slot 0 stays zeroed: it is the reserved idle slot, so that Fighter::moveId
    // can index this array directly and 0 keeps meaning idle.
    d.moves[1] = makeMove(kJabStartup, kJabActive, kJabRecovery,
                          kJabDamage, kJabHitstun, kJabBox, kInputLP);
    d.moves[2] = makeMove(kJabStartup, kJabActive, kJabRecovery,
                          kJabDamage, kJabHitstun, kHighBox, kInputHP);
    d.moveCount = 3;              // slots 0, 1 and 2 are in use
    return d;
}

MatchData makeMatchData() {
    MatchData m{};
    m.p[0] = makeFighterData();
    m.p[1] = makeFighterData();
    return m;
}

// Put both fighters on the floor at the given pixel positions, idle and whole.
GameState openingAt(std::int32_t p0Pixels, std::int32_t p1Pixels) {
    GameState s{};
    ResetMatch(s, 0xBEEFu);
    s.p[0].posX = px(p0Pixels);
    s.p[1].posX = px(p1Pixels);
    return s;
}

InputPair inputs(std::uint16_t p0Bits, std::uint16_t p1Bits) {
    InputPair in{};
    in.p[0].bits = p0Bits;
    in.p[1].bits = p1Bits;
    return in;
}

// What one run of a scenario did, observed from OUTSIDE the kernel -- every
// counter here is derived from the states the simulation produced, never from
// what the script asked for.
struct Observed {
    int hitsOnP1        = 0;   // ticks where p1's health fell
    int hitsOnP0        = 0;
    int p0BoxLiveTicks  = 0;   // ticks p0 had a hitbox at all
    int p0OverlapTicks  = 0;   // ...and it was inside p1's body
    int tradeTicks      = 0;   // ticks both healths fell together
    std::int32_t maxHitstunP1 = 0;
};

void observe(Observed& o, const GameState& before, const GameState& after,
             const MatchData& data) {
    const bool p1Hurt = after.p[1].health < before.p[1].health;
    const bool p0Hurt = after.p[0].health < before.p[0].health;
    if (p1Hurt) ++o.hitsOnP1;
    if (p0Hurt) ++o.hitsOnP0;
    if (p1Hurt && p0Hurt) ++o.tradeTicks;

    // The box as it stands after the tick, which is the box the tick resolved
    // against: StepAttack advances the frame counter and ResolveHits reads it,
    // both inside the same Simulate call.
    Box hit{};
    if (ActiveHitbox(data.p[0], after.p[0], hit)) {
        ++o.p0BoxLiveTicks;
        if (BoxesOverlap(hit, Hurtbox(data.p[1], after.p[1]))) ++o.p0OverlapTicks;
    }
    if (static_cast<std::int32_t>(after.p[1].hitstun) > o.maxHitstunP1)
        o.maxHitstunP1 = static_cast<std::int32_t>(after.p[1].hitstun);
}

void expectBoxEq(const Box& expected, const Box& actual, const char* what) {
    EXPECT_EQ(expected.x0, actual.x0) << what << " x0";
    EXPECT_EQ(expected.y0, actual.y0) << what << " y0";
    EXPECT_EQ(expected.x1, actual.x1) << what << " x1";
    EXPECT_EQ(expected.y1, actual.y1) << what << " y1";
}

} // namespace

// ============================================================================
// Boxes and mirroring -- the arithmetic, before any simulation touches it
// ============================================================================

TEST(CombatBox, MirroringIsNegateAndSwapAndIsItsOwnInverse) {
    const Box b{ px(20), px(32), px(60), px(48) };
    const Box m = MirrorBox(b);

    expectBoxEq(Box{ px(-60), px(32), px(-20), px(48) }, m, "mirrored jab");

    // Negation is exact, so mirroring twice must return the original BYTES, not
    // merely a box in the same place. Anything that rounded would drift here.
    expectBoxEq(b, MirrorBox(m), "double mirror");

    // Width and height are preserved exactly, which is what "no scaling step"
    // buys: a mirrored move has the same reach as the move it mirrors, by
    // construction rather than by luck.
    EXPECT_EQ(b.x1 - b.x0, m.x1 - m.x0);
    EXPECT_EQ(b.y1 - b.y0, m.y1 - m.y0);

    // Y is untouched. A fighter turning round does not turn upside down.
    EXPECT_EQ(b.y0, m.y0);
    EXPECT_EQ(b.y1, m.y1);
}

TEST(CombatBox, MirroringEveryOffsetInAPixelIsExactInBothDirections) {
    // Sub-unit granularity, both signs, including the ones nearest zero. A
    // rounding step that only shows up on odd values -- the classic shift-versus-
    // divide asymmetry D2 rejected the general fixed-point type over -- would
    // appear here and nowhere else in this file.
    for (std::int32_t v = -512; v <= 512; ++v) {
        const Box b{ v, 0, v + 7, 16 };
        const Box m = MirrorBox(b);
        ASSERT_EQ(-(v + 7), m.x0) << "v=" << v;
        ASSERT_EQ(-v,       m.x1) << "v=" << v;
        ASSERT_EQ(7,        m.x1 - m.x0) << "width changed at v=" << v;
        const Box back = MirrorBox(m);
        ASSERT_EQ(0, std::memcmp(&b, &back, sizeof(Box))) << "v=" << v;
    }
}

TEST(CombatBox, NegatingWithoutSwappingWouldBeExactAndStillWrong) {
    // The rejected alternative, kept executable so its cost is written down in a
    // form that cannot go stale. Multiplying each edge by -1 is exact -- it
    // loses no bits at all -- and it produces an inside-out rectangle that every
    // overlap test in the engine reports as empty. A left-facing character would
    // simply never hit anything, and no rounding argument would explain it.
    const Box b{ px(20), px(0), px(60), px(64) };
    const Box naive{ -b.x0, b.y0, -b.x1, b.y1 };       // negate, forget the swap
    const Box correct = MirrorBox(b);

    EXPECT_GT(naive.x0, naive.x1) << "the naive mirror is supposed to be "
                                     "inside-out; if it is not, this test has "
                                     "stopped demonstrating anything";
    EXPECT_LT(correct.x0, correct.x1);

    const Box body{ px(-40), px(0), px(-10), px(64) };
    EXPECT_FALSE(BoxesOverlap(naive, body))
        << "an inside-out box overlapped something, so the overlap test is not "
           "checking both edges";
    EXPECT_TRUE(BoxesOverlap(correct, body));
}

TEST(CombatBox, OverlapIsHalfOpenAndSurvivesMirroringBothBoxes) {
    const Box a{ 0, 0, 100, 100 };

    // Touching is not overlapping. With inclusive bounds this is where the
    // off-by-one lives, and a one-sub-unit reach difference is exactly what
    // decides a combo.
    EXPECT_FALSE(BoxesOverlap(a, Box{ 100, 0, 200, 100 }));
    EXPECT_TRUE (BoxesOverlap(a, Box{  99, 0, 200, 100 }));
    EXPECT_FALSE(BoxesOverlap(a, Box{ 0, 100, 100, 200 }));
    EXPECT_TRUE (BoxesOverlap(a, Box{ 0,  99, 100, 200 }));

    // ...and the predicate is invariant when BOTH boxes are mirrored, which is
    // the property that makes a mirrored match play identically rather than
    // merely similarly.
    const Box cases[] = {
        { 100,   0, 200, 100 },   // touching
        {  99,   0, 200, 100 },   // overlapping by one sub-unit
        { -50, -50,   1,  1  },   // overlapping across the origin
        { 500, 500, 600, 600 },   // far away
    };
    for (const Box& b : cases) {
        EXPECT_EQ(BoxesOverlap(a, b), BoxesOverlap(MirrorBox(a), MirrorBox(b)))
            << "overlap changed when both boxes were mirrored";
    }
}

TEST(CombatBox, PlacingAMirroredFighterIsTheMirrorOfPlacingTheOriginal) {
    // The composition property the whole mirror test rests on:
    //   PlaceBox(b, -x, y, facing^1) == MirrorBox(PlaceBox(b, x, y, facing))
    // Mirror then translate must equal translate then mirror, for every offset.
    for (std::int32_t p = -3000; p <= 3000; p += 7) {
        const Box facingRight = PlaceBox(kJabBox, p, px(3), 0);
        const Box facingLeft  = PlaceBox(kJabBox, -p, px(3), 1);
        const Box expected    = MirrorBox(facingRight);
        ASSERT_EQ(0, std::memcmp(&expected, &facingLeft, sizeof(Box)))
            << "placement and mirroring stopped commuting at posX=" << p;
    }
}

TEST(CombatBox, TheClampThatPreventsOverflowIsSymmetricAboutZero) {
    // PlaceBox clamps its inputs so the arithmetic cannot overflow for ANY
    // GameState, including one built by hand or arriving over a wire. A clamp
    // with asymmetric bounds would be a rounding rule hiding inside a safety
    // check -- so the guard is tested for the property it must not break.
    const Box wild{ -kMaxBoxCoord * 4, 0, kMaxBoxCoord * 4, px(64) };
    const Box right = PlaceBox(wild, kMaxWorldCoord * 4, 0, 0);
    const Box left  = PlaceBox(wild, -kMaxWorldCoord * 4, 0, 1);
    const Box expected = MirrorBox(right);
    EXPECT_EQ(0, std::memcmp(&expected, &left, sizeof(Box)))
        << "the overflow guard is not symmetric, so it introduces exactly the "
           "left/right difference it exists alongside";

    EXPECT_TRUE(BoxIsValid(kJabBox));
    EXPECT_FALSE(BoxIsValid(Box{ 10, 0, 10, 10 })) << "empty in X";
    EXPECT_FALSE(BoxIsValid(Box{ 10, 0, 20,  0 })) << "empty in Y";
    EXPECT_FALSE(BoxIsValid(wild)) << "out of range: a loader must reject this";
}

// ============================================================================
// Connecting, and failing to
// ============================================================================

TEST(CombatHit, AnAttackInRangeConnects) {
    const MatchData data = makeMatchData();
    GameState s = openingAt(0, 40);          // p0's jab reaches 20..60 px ahead

    Observed o{};
    for (int t = 0; t < kJabDuration; ++t) {
        const GameState before = s;
        Simulate(s, inputs(kInputLP, 0), data);
        observe(o, before, s, data);
    }

    EXPECT_EQ(1, o.hitsOnP1);
    EXPECT_EQ(1000 - kJabDamage, s.p[1].health);
    EXPECT_EQ(kJabHitstun, o.maxHitstunP1)
        << "the defender never reached the authored hitstun value";
    EXPECT_EQ(0, o.hitsOnP0) << "the defender pressed nothing and hit nobody";
}

TEST(CombatHit, AnAttackOutOfRangeDoesNotConnectAlthoughItIsLive) {
    const MatchData data = makeMatchData();
    GameState s = openingAt(0, 120);         // 120 px away; the jab ends at 60

    Observed o{};
    for (int t = 0; t < kJabDuration; ++t) {
        const GameState before = s;
        Simulate(s, inputs(kInputLP, 0), data);
        observe(o, before, s, data);
    }

    EXPECT_EQ(0, o.hitsOnP1);
    EXPECT_EQ(1000, s.p[1].health);
    EXPECT_EQ(0u, s.p[1].hitstun);

    // THE GUARD THAT MAKES THE ABOVE MEAN ANYTHING. Without it this test passes
    // when the move never starts, when the table fails to load, when the active
    // window is off by one, and when hit detection is deleted outright.
    EXPECT_EQ(kJabActive, o.p0BoxLiveTicks)
        << "the attacker's hitbox was live for " << o.p0BoxLiveTicks
        << " ticks, not " << kJabActive << ". The move did not run, so 'it did "
           "not connect' is vacuous.";
    EXPECT_EQ(0, o.p0OverlapTicks);
    EXPECT_EQ(0u, s.p[0].alreadyHitBits)
        << "the attacker recorded a connection it never made";
}

TEST(CombatHit, AMissInYAloneIsStillAMiss) {
    // Same reach, same distance, box above the body. A hit test written against
    // the X axis only passes every other test in this file and fails this one.
    const MatchData data = makeMatchData();
    GameState s = openingAt(0, 40);

    Observed o{};
    for (int t = 0; t < kJabDuration; ++t) {
        const GameState before = s;
        Simulate(s, inputs(kInputHP, 0), data);      // slot 2: the high box
        observe(o, before, s, data);
    }

    EXPECT_EQ(2u, s.p[0].moveId) << "the high move never started";
    EXPECT_EQ(0, o.hitsOnP1);
    EXPECT_EQ(1000, s.p[1].health);
    EXPECT_EQ(kJabActive, o.p0BoxLiveTicks)
        << "the high box was never live, so this test proves nothing about Y";
}

// ============================================================================
// The multi-hit bug
// ============================================================================

TEST(CombatHit, AMultiFrameActiveWindowConnectsExactlyOnce) {
    const MatchData data = makeMatchData();
    GameState s = openingAt(0, 40);

    Observed o{};
    for (int t = 0; t < kJabDuration; ++t) {
        const GameState before = s;
        Simulate(s, inputs(kInputLP, 0), data);
        observe(o, before, s, data);
    }

    // The vacuity guard, and the whole point of the test: the box was inside the
    // defender's body on MORE THAN ONE tick, and the defender was hurt on
    // exactly one of them. If the overlap count were 1 this test would pass
    // against a kernel with no multi-hit guard at all.
    EXPECT_EQ(kJabActive, o.p0OverlapTicks)
        << "the hitbox overlapped the defender on " << o.p0OverlapTicks
        << " ticks; 'exactly once' is only interesting when this is > 1";
    EXPECT_GT(o.p0OverlapTicks, 1);
    EXPECT_EQ(1, o.hitsOnP1)
        << "a " << kJabActive << "-frame active window dealt damage "
        << o.hitsOnP1 << " times. This is the multi-hit bug: every string in "
           "the game becomes an infinite for a reason that has nothing to do "
           "with the character.";
    EXPECT_EQ(1000 - kJabDamage, s.p[1].health);
    EXPECT_EQ(kJabHitstun, o.maxHitstunP1)
        << "hitstun was re-applied or stacked across the active window";
}

TEST(CombatHit, TheNextRepetitionOfTheSameMoveCanConnectAgain) {
    // The other half of the multi-hit guard, and the bug on its far side: a flag
    // that is set and never cleared makes a jab connect once per MATCH.
    const MatchData data = makeMatchData();
    GameState s = openingAt(0, 40);

    // TWO PRESSES, not one hold. This used to hold the button for both cycles on
    // the stated grounds that "the move repeats" -- which it did, and that was
    // the rapid-fire bug ROADMAP M1.1d removed: a press is an edge, so a hold is
    // one press however long it lasts. The property under test is unchanged and
    // is about alreadyHitBits, not about input: two cycles of the same move must
    // land two hits, or the multi-hit guard is never cleared.
    Observed o{};
    for (int t = 0; t < kJabDuration * 2; ++t) {
        const GameState before = s;
        const std::uint16_t bits =
            (t % kJabDuration == 0) ? kInputLP : 0u;   // press, then let go
        Simulate(s, inputs(bits, 0), data);
        observe(o, before, s, data);
    }

    EXPECT_EQ(2, o.hitsOnP1)
        << "two full move cycles landed " << o.hitsOnP1 << " hits. One means "
           "alreadyHitBits is never cleared; more than two means the window "
           "guard leaks.";
    EXPECT_EQ(1000 - kJabDamage * 2, s.p[1].health);
}

TEST(CombatHit, BeingHitInterruptsTheDefendersOwnMove) {
    const MatchData data = makeMatchData();
    GameState s = openingAt(0, 40);

    // p1 starts its jab one tick later than p0, so p0's hit lands first and p1
    // is interrupted mid-move rather than trading.
    Simulate(s, inputs(kInputLP, 0), data);
    ASSERT_EQ(1u, s.p[0].moveId);

    for (int t = 0; t < kJabDuration; ++t) Simulate(s, inputs(kInputLP, kInputLP), data);

    EXPECT_LT(s.p[1].health, 1000) << "p0 never landed, so nothing was interrupted";
    EXPECT_EQ(1000, s.p[0].health)
        << "p1's move survived the hit and connected anyway -- a fighter in "
           "hitstun kept a live hitbox";
}

TEST(CombatHit, ASimultaneousTradeIsSymmetric) {
    // Both fighters throw the same jab from the same distance on the same tick.
    // Resolution is deliberately order-independent -- every overlap is decided
    // before any effect is applied -- so neither slot may win by being checked
    // first. An order-dependent kernel passes every OTHER test in this file and
    // quietly makes player 1 lose trades forever.
    const MatchData data = makeMatchData();
    GameState s = openingAt(-20, 20);

    Observed o{};
    for (int t = 0; t < kJabDuration; ++t) {
        const GameState before = s;
        Simulate(s, inputs(kInputLP, kInputLP), data);
        observe(o, before, s, data);
    }

    EXPECT_EQ(1, o.tradeTicks) << "the two hits did not land on the same tick, "
                                  "so this is not testing a trade";
    EXPECT_EQ(s.p[0].health, s.p[1].health);
    EXPECT_EQ(s.p[0].hitstun, s.p[1].hitstun);
    EXPECT_EQ(s.p[0].alreadyHitBits, s.p[1].alreadyHitBits);
    EXPECT_EQ(1000 - kJabDamage, s.p[0].health) << "the trade did not land";
}

// ============================================================================
// The mirror -- D2's rounding-asymmetry property, at match scale
// ============================================================================

TEST(CombatMirror, AMirroredScenarioProducesExactlyMirroredState) {
    const MatchData data = makeMatchData();

    // p0 walks toward p1 while jabbing; p1 stands still and gets hit. The
    // mirrored run reflects both positions and swaps left for right in the
    // input. Nothing else changes -- same seed, same data, same tick count.
    //
    // The starting distance is chosen so the two NEVER share an X coordinate:
    // p0 covers 80 pixels in 40 ticks and starts 90 short of p1. That matters,
    // and the test asserts it below. Facing is decided by `p0.posX <= p1.posX`,
    // and at exact equality that rule gives the SAME answer in both runs rather
    // than the mirrored one -- a real asymmetry, found by an earlier version of
    // this very test walking the fighters through each other. It lives in a tie
    // that pushboxes will make unreachable once they exist, so keeping the
    // scenario clear of it is deliberate; asserting that it is clear of it is
    // what stops the exclusion from going silent.
    GameState normal   = openingAt(-70,  20);
    GameState mirrored = openingAt( 70, -20);

    constexpr int kTicks = 40;
    int hitsNormal = 0, hitsMirrored = 0, sharedX = 0;

    for (int t = 0; t < kTicks; ++t) {
        const std::int32_t healthBeforeN = normal.p[1].health;
        const std::int32_t healthBeforeM = mirrored.p[1].health;

        // LP pressed rather than held, for ROADMAP M1.1d's reason: a hold is one
        // press. The direction bit stays held on purpose -- walking IS a level,
        // and holding it is what puts the two runs at mirrored positions, which
        // is the thing being compared.
        const std::uint16_t attack = (t % kJabDuration == 0) ? kInputLP : 0u;
        Simulate(normal,   inputs(attack | kInputRight, 0), data);
        Simulate(mirrored, inputs(attack | kInputLeft,  0), data);

        if (normal.p[1].health   < healthBeforeN) ++hitsNormal;
        if (mirrored.p[1].health < healthBeforeM) ++hitsMirrored;
        if (normal.p[0].posX == normal.p[1].posX) ++sharedX;

        for (int i = 0; i < 2; ++i) {
            const Fighter& n = normal.p[i];
            const Fighter& m = mirrored.p[i];

            // The reflected quantities. Exact negation, not "within one".
            ASSERT_EQ(-n.posX, m.posX)
                << "positions stopped being reflections at tick " << t
                << " for fighter " << i << ". One sub-unit here is 1/256th of a "
                   "pixel of reach, and reach decides whether a combo connects.";
            ASSERT_EQ(-n.velX, m.velX) << "tick " << t << " fighter " << i;
            ASSERT_EQ(n.facing ^ 1u, m.facing) << "tick " << t << " fighter " << i;

            // ...and everything else must be bit-identical. A mirror that got
            // the positions right and the move frames wrong is still a desync.
            ASSERT_EQ(n.posY, m.posY)             << "tick " << t << " f" << i;
            ASSERT_EQ(n.velY, m.velY)             << "tick " << t << " f" << i;
            ASSERT_EQ(n.health, m.health)         << "tick " << t << " f" << i;
            for (int r = 0; r < cse::kernel::kMaxResources; ++r)
                ASSERT_EQ(n.res[r], m.res[r])     << "tick " << t << " f" << i << " res" << r;
            ASSERT_EQ(n.moveId, m.moveId)         << "tick " << t << " f" << i;
            ASSERT_EQ(n.moveFrame, m.moveFrame)   << "tick " << t << " f" << i;
            ASSERT_EQ(n.hitstun, m.hitstun)       << "tick " << t << " f" << i;
            ASSERT_EQ(n.blockstun, m.blockstun)   << "tick " << t << " f" << i;
            ASSERT_EQ(n.airborne, m.airborne)     << "tick " << t << " f" << i;
            ASSERT_EQ(n.comboHits, m.comboHits)   << "tick " << t << " f" << i;
            ASSERT_EQ(n.alreadyHitBits, m.alreadyHitBits)
                << "the two runs disagree about which fighters an active window "
                   "has already hit, at tick " << t << " fighter " << i;
        }
        ASSERT_EQ(normal.tick, mirrored.tick);
        ASSERT_EQ(normal.rng,  mirrored.rng);
    }

    // Vacuity guards. Every assertion above is trivially true for two fighters
    // that stood still and never touched each other.
    EXPECT_GT(hitsNormal, 0) << "nothing was hit, so the mirror test covered "
                                "walking and nothing else";
    EXPECT_EQ(hitsNormal, hitsMirrored);
    EXPECT_EQ(0, sharedX) << "the fighters shared an X coordinate, which is the "
                             "one tie the facing rule does not mirror. Move the "
                             "scenario, do not weaken the assertion.";
    EXPECT_NE(px(-70), normal.p[0].posX) << "p0 never walked";
}

// ============================================================================
// Rollback
// ============================================================================

namespace {

// A scenario with both fighters attacking, one of them jumping, and hits landing
// throughout -- so that a rollback across it is rolling back combat and not just
// two people walking.
InputPair combatScript(int t) {
    std::uint16_t a = kInputLP;
    if (t % 5 == 0)  a |= kInputRight;
    std::uint16_t b = 0;
    if (t % 3 == 0)  b |= kInputLP;
    if (t % 17 == 0) b |= kInputUp;
    if (t % 7 == 0)  b |= kInputHP;
    return inputs(a, b);
}

GameState runFrom(const GameState& start, const MatchData& data,
                  int from, int to, int* hits) {
    GameState s = start;
    for (int t = from; t < to; ++t) {
        const std::int32_t h0 = s.p[0].health, h1 = s.p[1].health;
        Simulate(s, combatScript(t), data);
        if (hits != nullptr && (s.p[0].health < h0 || s.p[1].health < h1)) ++*hits;
    }
    return s;
}

} // namespace

TEST(CombatRollback, ResimulatingCombatFromASnapshotReproducesTheStraightRun) {
    const MatchData data = makeMatchData();
    const GameState start = openingAt(-20, 20);

    int hitsStraight = 0;
    const GameState straight = runFrom(start, data, 0, 300, &hitsStraight);

    const GameState snapshot = runFrom(start, data, 0, 200, nullptr);
    const GameState restored = snapshot;              // the snapshot IS a memcpy
    int hitsResim = 0;
    const GameState resimulated = runFrom(restored, data, 200, 300, &hitsResim);

    EXPECT_EQ(0, std::memcmp(&straight, &resimulated, sizeof(GameState)))
        << "re-simulating combat from a restored snapshot did not reproduce the "
           "straight-through run. Something a hit writes is not inside "
           "GameState -- alreadyHitBits, a move frame, or a counter cached "
           "outside the state.";
    EXPECT_EQ(Checksum(straight), Checksum(resimulated));

    // Vacuity guard, in the house style: the rolled-back window must contain
    // real combat, or this proves only that two idle fighters stay idle.
    EXPECT_GT(hitsResim, 0)
        << "no hit landed in the re-simulated window (ticks 200..300), so the "
           "round-trip covered no combat at all";
    EXPECT_GT(hitsStraight, hitsResim)
        << "every hit in the match happened inside the re-simulated window, "
           "which means the first 200 ticks were inert";
    EXPECT_LT(straight.p[0].health, 1000);
    EXPECT_LT(straight.p[1].health, 1000);
}

TEST(CombatRollback, EveryRewindDepthUpToTheBudgetIsExact) {
    // ARCHITECTURE.md D4 budgets 8 ticks of re-simulation per rollback event.
    // Checking every depth rather than only the deepest is what catches an
    // off-by-one that lives at one specific depth -- and an active window is
    // three frames wide, so a hit can sit anywhere inside the rewind.
    const MatchData data = makeMatchData();
    const GameState start = openingAt(-20, 20);

    int hits = 0;
    for (int depth = 1; depth <= 8; ++depth) {
        const GameState straight = runFrom(start, data, 0, 120, &hits);
        const GameState mid      = runFrom(start, data, 0, 120 - depth, nullptr);
        const GameState redone   = runFrom(mid, data, 120 - depth, 120, nullptr);
        ASSERT_EQ(0, std::memcmp(&straight, &redone, sizeof(GameState)))
            << "rewind depth " << depth << " diverged";
    }

    // Vacuity guard: eight identical rewinds through 120 ticks of two fighters
    // standing still would pass every assertion above.
    EXPECT_GT(hits, 0) << "no hit landed anywhere in the scenario being rewound";
}

TEST(CombatRollback, TheChecksumNoticesAHitThatDidNotHappen) {
    // ADR-002 CHOICE C exchanges this checksum every 8 ticks and STOPS the match
    // on a mismatch. A hit is now part of the state, so it has to be part of the
    // hash -- including the flag that records it, which is the field most likely
    // to be forgotten because nothing on screen shows it.
    const MatchData data = makeMatchData();
    GameState a = openingAt(0, 40);
    for (int t = 0; t < 5; ++t) Simulate(a, inputs(kInputLP, 0), data);
    ASSERT_NE(0u, a.p[0].alreadyHitBits) << "no hit landed, so there is no flag "
                                            "to notice a difference in";

    GameState b = a;
    b.p[0].alreadyHitBits = 0;            // "this window has not connected yet"
    EXPECT_NE(Checksum(a), Checksum(b))
        << "the desync checksum cannot see the multi-hit guard, so two peers "
           "could disagree about whether an attack has already landed and never "
           "find out";
}

// ============================================================================
// The seam back to the pre-hitbox kernel
// ============================================================================

TEST(CombatData, WithoutCharacterDataNothingCanStartAndNothingCanHit) {
    // The two-argument Simulate must be the pre-hitbox kernel EXACTLY, not
    // approximately: the cross-toolchain golden hashes in
    // test_determinism_crossplat.cpp were recorded against it, and a re-record
    // is how the evidence that two platforms agree gets destroyed.
    GameState viaOverload = openingAt(0, 40);
    GameState viaEmpty    = openingAt(0, 40);

    for (int t = 0; t < 100; ++t) {
        const InputPair in = combatScript(t);
        Simulate(viaOverload, in);
        Simulate(viaEmpty, in, kNoMoves);
    }

    EXPECT_EQ(0, std::memcmp(&viaOverload, &viaEmpty, sizeof(GameState)));
    EXPECT_EQ(1000, viaOverload.p[0].health);
    EXPECT_EQ(1000, viaOverload.p[1].health);
    EXPECT_EQ(0u, viaOverload.p[0].moveId) << "a move started with no move table";
    EXPECT_EQ(0u, viaOverload.p[0].alreadyHitBits);
}

TEST(CombatData, AMoveIdTheTableDoesNotDescribeIsInertRatherThanFatal) {
    // A state can name a move this character does not have: a harness sets it
    // (test_determinism_crossplat.cpp does), or a peer sends a state built from
    // different content. The frame counter advances and nothing else happens --
    // no box, no hit, and above all no read past the end of moves[].
    const MatchData data = makeMatchData();
    GameState s = openingAt(0, 40);
    s.p[0].moveId    = 900;                 // far outside a 32-slot table
    s.p[0].moveFrame = 0;

    for (int t = 0; t < 10; ++t) Simulate(s, inputs(0, 0), data);

    EXPECT_EQ(900u, s.p[0].moveId) << "an undescribed move was cleared, which "
                                      "changes behaviour the pre-hitbox kernel "
                                      "had and other tests depend on";
    EXPECT_EQ(10u, s.p[0].moveFrame);
    EXPECT_EQ(1000, s.p[1].health);
    EXPECT_EQ(nullptr, MoveAt(data.p[0], 900));
    EXPECT_EQ(nullptr, MoveAt(data.p[0], 0)) << "slot 0 is the idle slot and is "
                                                "never a move";
    Box unused{};
    EXPECT_FALSE(ActiveHitbox(data.p[0], s.p[0], unused));
}

TEST(CombatData, TheActiveWindowIsExactlyTheAuthoredFrames) {
    // Frame accounting, asserted directly rather than inferred from a hit: the
    // box is live on frames [startup, startup + active) and the move ends after
    // startup + active + recovery ticks. Every number a designer authors means
    // one of these, and an off-by-one here is a change to every character in the
    // game at once.
    const MatchData data = makeMatchData();
    GameState s = openingAt(0, 400);          // far out of range: this test is
                                              // about frames, not about hitting
    std::vector<int> liveFrames;
    for (int t = 0; t < kJabDuration; ++t) {
        Simulate(s, inputs(kInputLP, 0), data);
        Box box{};
        if (ActiveHitbox(data.p[0], s.p[0], box))
            liveFrames.push_back(static_cast<int>(s.p[0].moveFrame));
    }

    ASSERT_EQ(static_cast<std::size_t>(kJabActive), liveFrames.size());
    for (int i = 0; i < kJabActive; ++i)
        EXPECT_EQ(kJabStartup + i, liveFrames[static_cast<std::size_t>(i)]);

    EXPECT_EQ(kJabDuration - 1, static_cast<std::int32_t>(s.p[0].moveFrame))
        << "the move ended early or late";
}
