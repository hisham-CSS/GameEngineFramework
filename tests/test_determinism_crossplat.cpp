// Cross-toolchain determinism: the claim that ARCHITECTURE.md makes for free,
// turned into something that can fail.
//
// D2 and the one-paragraph summary both say it: "cross-platform bit-identity is
// a property of integer arithmetic, not of a build flag". That is the reason no
// ARM CI leg, no Jolt determinism port and no NEON patch are on the critical
// path, and it is the reason NORTHSTAR Q1 (Windows<->Linux crossplay) could be
// answered (a) at zero cost. ARCHITECTURE.md D8 lists four things that defend
// the claim and is explicit that only the fourth converts it from an assertion
// into a fact: "a cross-toolchain hash test, which is the only thing that
// converts any of the above from an assertion into a fact". This is that test.
//
// test_kernel.cpp already proves determinism WITHIN one binary -- run the same
// inputs twice, memcmp. That cannot see the failure this file is about, because
// both runs share the same compiler, the same flags, and the same mistake.
//
// HOW A SINGLE BINARY TESTS TWO PLATFORMS. It cannot, and does not try. What it
// does is compute a value that is a pure function of the simulation and compare
// it against a GOLDEN CONSTANT written into this file. The cross-platform
// comparison is then performed by CI: MSVC compiles this and checks against the
// constant, gcc compiles this and checks against the same constant, and if both
// jobs are green the two toolchains agree byte for byte. The constant is the
// wire between them. Nothing here is meaningful unless this test runs on BOTH
// platforms -- a green Windows job alone proves only that Windows agrees with
// itself, which test_kernel.cpp already established.
//
// Like test_kernel.cpp, this links CseKernel and NOT the Engine. If it ever
// needs the Engine to build, ADR-002 CHOICE D has quietly failed.
#include <gtest/gtest.h>

#include "cse/kernel/Simulate.h"

#include <cstddef>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>

using namespace cse::kernel;

// --- The shape of the recorded match ---------------------------------------
//
// 3600 ticks is one full minute of match at 60 Hz. The length is not padding:
// the stage is 480 pixels from centre to wall and a walk is 2 pixels per tick,
// so a fighter needs 290 uninterrupted ticks just to reach the clamp. A
// 300-tick test never touches the branch, and a branch that is never driven is
// exactly where a portability difference sits undisturbed.
constexpr int kMatchTicks         = 3600;
constexpr int kCheckpointInterval = 1000;
constexpr int kCheckpointCount    = kMatchTicks / kCheckpointInterval;  // 3

// The match does not divide evenly by the checkpoint interval, and that is
// fine: the last 600 ticks sit past the final checkpoint, and a divergence
// there is bracketed by "every checkpoint matched but the total did not",
// which the failure report below spells out as its own case.
static_assert(kCheckpointCount >= 3,
              "the divergence-localisation tests index checkpoints 0, 1 and 2 "
              "directly; a shorter match needs those tests rewritten, not just "
              "this constant changed");

// Any non-zero seed. xorshift32 is absorbing at zero, and ResetMatch substitutes
// a constant for a zero seed -- so a zero here would still work, but it would
// make this file's seed and the kernel's fallback the same number by accident,
// and the day someone changed the fallback the golden would move for a reason
// nobody could see from here.
constexpr std::uint32_t kMatchSeed = 0xA5EED17Eu;

// ============================================================================
// GOLDEN CONSTANTS
// ============================================================================
//
// HOW TO RECORD THEM. Build and run this test. It fails, and prints a block of
// C++ that is exactly these lines with the computed values filled in. Paste it
// over the block below. That is the whole procedure -- there is no generator
// script, because a generator script is a second implementation of the thing
// being tested.
//
// THEN DO THE OTHER HALF. Values recorded on one platform prove nothing until
// the other platform agrees with them. Record on one, commit, and let CI run
// the other. If the second platform disagrees, DO NOT re-record: that
// disagreement is the exact bug this file exists to catch.
//
// WHEN YOU MAY CHANGE THEM. Only when you intended to change the simulation --
// tuning constants in Simulate.cpp, the shape of GameState, the scripted match
// below, or the checksum function. In that case the goldens are stale by
// construction and must be re-recorded on both platforms, and the two must
// agree. If you did NOT change any of those and this test is red, re-recording
// is the wrong fix and destroys the only evidence that something is wrong.
//
// The sentinel below is the "never recorded" state. A real FNV-1a value could
// in principle be zero; if that ever happens the test will report itself
// unrecorded forever, and the fix is to change kMatchSeed, not to weaken this.
constexpr std::uint32_t kGoldenUnrecorded = 0u;

// RE-RECORDED 2026-08-21 (second time today) for the BALLISTIC JUMP: horizontal
// velocity in the air is decided at takeoff and neither an attack nor a held
// direction recomputes it. Behaviour, not layout, and again a precise one: tick
// 1000 (the walk phases) did not move; tick 2000 did, and the jumps phase is
// ticks 1200-1800, where this script's "air control, such as it is" ticks now
// steer nothing. The script is kept pressing those directions on purpose -- an
// input that must do nothing is worth driving, and the day air steering becomes
// authorable these goldens must move again.
//
// ---- the previous record, kept for its reasoning ---------------------------
//
// RE-RECORDED 2026-08-21 from MSVC 19.44 / x64 / RelWithDebInfo, for the
// COMMITMENT RULE: a fighter no longer walks, jumps or changes posture while a
// move is running. Behaviour, not layout -- CrossPlatformLayout passes untouched.
//
// And it is a precise re-record: the tick-1000 and tick-2000 checkpoints did NOT
// move. Only tick 3000 did, which is the reversal phase -- the one stretch of
// the script that presses attack buttons WHILE walking (`k % 13 == 0` LP on top
// of held directions). That is exactly where commitment bites, and nowhere
// else, which is the shape a one-rule change should have. Had tick 1000 moved,
// the rule would be leaking into plain walking.
//
// gcc has NOT checked these yet; the Linux CI leg is the cross-toolchain half.
//
// ---- the previous record, kept for its reasoning ---------------------------
//
// RE-RECORDED 2026-08-20 from MSVC 19.44 / x64 / RelWithDebInfo, for the
// invisible wall -- and this one is a BEHAVIOUR re-record, not a layout one.
//
// The distinction is the whole reason this block is prose. The M1.1d record
// below moved because `Fighter` grew and the hash covers more bytes; the
// simulation did exactly what it did before. This one moves because the
// SIMULATION CHANGED: two fighters may no longer be more than
// kMaxSeparationSub apart, and the scripted match walks them apart in act 1 and
// again in the reversal phase, so positions genuinely differ from tick 30
// onward. `CrossPlatformLayout` passes untouched, which is what says the bytes
// are the same shape and only their contents moved.
//
// ADR-005 section 3 calls this the legitimate case and asks that the commit say
// which of the two it is. This one is behaviour.
//
// THE SCRIPT MOVED TOO, and it had to. Phases 1 and 2 used to walk the fighters
// to OPPOSITE walls, which the separation limit forbids outright -- nobody can
// reach a wall by walking away from somebody. The coverage assertions caught it
// exactly: `minPosX` came back -48128 against the -122880 this file expects,
// which is the test saying "your script no longer drives the -X clamp" rather
// than "your hash is wrong". The pair now crosses, then travels to each wall
// TOGETHER, which drives the clamp branch and the new separation branch in the
// same stretch.
//
// gcc has NOT checked these yet; the Linux CI leg is the cross-toolchain half.
//
// ---- the previous record, kept for its reasoning ---------------------------
//
// RE-RECORDED 2026-08-19 from MSVC 19.44 / x64 / RelWithDebInfo, for the
// ROADMAP M1.1d input-edge expansion.
//
// WHY THEY MOVED THIS TIME. Fighter went from 68 bytes to 76: an edge is
// `bits & ~prevButtons`, and Simulate is handed only the CURRENT tick's bits, so
// last tick's buttons and the buffered press have to live IN the state or a
// rollback replays a press as a hold (GameState.h says this at length). Eight
// bytes across kMaxFighters is 64, and GameState went 664 -> 728 to match.
//
// The layout half proves this is a byte-string move and not an arithmetic one:
// CrossPlatformLayout's sizes, alignments and per-field offsets were updated to
// the new shape and PASS. The scripted match here presses attack buttons but
// still never reaches hit detection, so the run itself is unchanged in what it
// exercises -- what changed is how many bytes each tick hashes over.
//
// ONE DERIVATION, and gcc has NOT yet checked these. The Linux CI leg is the
// cross-toolchain half and it has not run at the time of writing; until it is
// green these values prove that this toolchain is self-consistent and no more.
// If it comes back red, the answer is NOT to re-record again -- it is that
// something in the new fields reads differently on gcc, and the padding bytes
// are the first place to look.
//
// ---- the previous record, kept because it is the reasoning, not the number ---
//
// RE-RECORDED 2026-08-16 from MSVC 19.44 / x64 / RelWithDebInfo, for the
// ADR-005 P2 state expansion.
//
// WHY THEY MOVED, WHICH IS THE ONLY THING THAT MAKES A RE-RECORD LEGITIMATE.
// GameState changed shape: Fighter went from 36 bytes to 52 and GameState from
// 80 to 436, because the P2 batch added the state those systems need AND because
// the state stopped holding exactly two fighters (docs/adr/ADR-009). The hash is a
// hash of those bytes, so it is stale BY CONSTRUCTION -- which is the case this
// file's own "WHEN YOU MAY CHANGE THEM" paragraph names as the legitimate one.
//
// The layout half is what proves that reading rather than the hash half: the
// sizes, alignments and per-field offsets above were updated to the new shape
// and PASS, so what moved is the byte string and not the arithmetic. The
// scripted match here still never reaches hit detection, so nothing in this
// file's run exercises the new mechanics -- see the note at kMatchTicks.
//
// THE PREVIOUS RECORD HAD THREE INDEPENDENT DERIVATIONS AND THIS ONE HAS ONE.
// That is a real loss and it is stated rather than glossed: the 2026-08-13
// values were confirmed by the run, by a from-scratch Python reimplementation of
// the kernel written against GameState.h's field list, and by a second clean
// run. Re-deriving the Python model against a 436-byte state was not done here.
// What still holds is the property this file exists for -- two toolchains must
// agree -- and that is checked by CI rather than asserted by me.
//
// gcc has NOT yet checked these. Until the Linux CI leg runs this test, they
// prove that this toolchain is self-consistent and nothing more.
constexpr std::uint32_t kGoldenRollingHash = 0xAD470388u;
constexpr std::uint32_t kGoldenCheckpoint[kCheckpointCount] = {
    0x48918ACFu,  // tick 1000  (walk phases: unmoved by commitment OR ballistics)
    0x58189F45u,  // tick 2000  (jumps phase: air control no longer steers)
    0x46C2644Eu,  // tick 3000
};

// ============================================================================
// The structural facts the golden hash silently depends on.
// ============================================================================
//
// A byte-level hash over a struct is a hash of that struct's LAYOUT as much as
// of its values. If a field is added, reordered, widened, or if a compiler
// inserts a padding byte where the other compiler did not, the hash changes for
// a reason that has nothing to do with arithmetic -- and "the two toolchains
// disagree about arithmetic" is a five-alarm finding while "someone added a
// field" is a five-minute one. These numbers exist so the test can tell them
// apart, and they are checked BEFORE the hash so the specific failure lands
// first.
//
// The arithmetic, from GameState.h's field list. These moved once, in the
// ADR-005 P2 expansion, which is also when GameState stopped holding exactly two
// fighters (docs/adr/ADR-009):
//   Fighter   = 7 x int32 (28) + 8 x 16-bit (16) + 8 x uint8 (8)  = 52 bytes
//   GameState = 20-byte header + 8 x Fighter (416)                = 436 bytes
// Both are multiples of 4, which is the alignment of their widest member, so a
// conforming implementation inserts no tail padding either.
constexpr std::size_t kGoldenSizeofFighter   = 76;
constexpr std::size_t kGoldenSizeofGameState = 728;
constexpr std::size_t kGoldenAlignofFighter   = 4;
constexpr std::size_t kGoldenAlignofGameState = 4;

// Compile-time properties. These cannot differ between two conforming
// compilers, so they are static_asserts rather than expectations: the failure
// belongs on the person who added the member, in their own build, not in a CI
// log two platforms away. test_kernel.cpp asserts the same things for the
// rollback snapshot; here the reason is different, so the message is different.
static_assert(std::is_trivially_copyable_v<GameState>,
              "GameState stopped being trivially copyable. Checksum() reads its "
              "object representation, so the golden hash below is now hashing "
              "something whose bytes are not fully defined by its values.");
static_assert(std::is_trivially_copyable_v<Fighter>, "see GameState");
static_assert(std::is_standard_layout_v<GameState>,
              "GameState must stay standard-layout: the offsetof checks below "
              "are only well-defined for a standard-layout type, and the wire "
              "format is its raw bytes.");
static_assert(std::is_standard_layout_v<Fighter>, "see GameState");

namespace {

// --- The rolling hash -------------------------------------------------------

// Fold one 32-bit value into a running FNV-1a.
//
// The four bytes go in most-significant first, extracted with SHIFTS rather
// than by reinterpreting the value's storage. That is not fussiness: reading
// the bytes of a uint32 through a char pointer makes the result depend on the
// host's byte order, and this file's entire purpose is to be insensitive to the
// host. (GameState's own object representation IS byte-order dependent -- that
// is inherent in a wire format that is a memcpy of the struct, and both targets
// here are little-endian. The point is that the hash COMBINATION step adds no
// second, avoidable dependency of its own.)
void fold(std::uint32_t& h, std::uint32_t v) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        h ^= static_cast<std::uint8_t>((v >> shift) & 0xFFu);
        h *= 16777619u;
    }
}

// Why the hash is rolled over every tick rather than taken once at the end.
//
// A final-state-only hash answers "did the two runs end up the same", and that
// is a weaker question than it looks. States are clamped (posX at the walls),
// snapped (posY to the floor on landing) and saturated (stun to zero), so a
// divergence can be erased by the very next tick and the two runs rejoin. The
// end-state hash then agrees, and reports a match while the two machines spent
// a stretch of ticks showing their players different things. Folding every
// intermediate state makes any divergence permanent in the hash, and the
// per-1000-tick checkpoints make it locatable.
//
// That is not a hypothetical: AFinalStateHashWouldHaveMissedThisEntirely, below,
// builds exactly such a divergence and shows the kernel's own final-state
// Checksum agreeing across it.

// --- Coverage ---------------------------------------------------------------
//
// What the run actually drove. Every claim this file makes about the scripted
// match is counted here and asserted below, because a golden hash over a match
// that turned out to be 3600 ticks of standing still would be perfectly stable
// and perfectly worthless.
struct Coverage {
    int ticksAtRightWall = 0;
    int ticksAtLeftWall  = 0;
    int ticksAirborne    = 0;
    int jumpsStarted     = 0;
    int landings         = 0;
    int ticksRising      = 0;
    int ticksFalling     = 0;
    int ticksInHitstun   = 0;
    int ticksInBlockstun = 0;
    int stunSuppressedAWalk = 0;
    int stunBlockedAJump    = 0;
    int bothDirectionsHeld  = 0;
    int ticksMoveFrameAdvancing = 0;
    int facingFlips      = 0;
    int rngHitZero       = 0;
    int distinctRngHighBytes = 0;
    bool rngHighByteSeen[256] = {};
    std::int32_t maxPosX = std::numeric_limits<std::int32_t>::min();
    std::int32_t minPosX = std::numeric_limits<std::int32_t>::max();
};

// --- The scripted match -----------------------------------------------------
//
// Six phases, chosen so that every branch in StepPhysics is driven for long
// enough to matter, and so that a human reading a divergence report can say
// what the fighters were doing at the tick it names.
constexpr int kPhaseWallsEnd    = 600;   // cross over, then travel to +X together
constexpr int kPhaseCrossEnd    = 1200;  // travel to -X together and stay pinned
constexpr int kPhaseJumpsEnd    = 1800;  // full jump arcs, offset between players
constexpr int kPhaseStunEnd     = 2400;  // hitstun and blockstun, air and ground
constexpr int kPhaseReversalEnd = 3000;  // contradictory and rapidly flipping input
                                         // ...then corner jumps, and a settle.

// The stage half-width from Simulate.cpp's anonymous namespace, duplicated
// because it is not exported. This copy is not load-bearing for the simulation
// -- it is only used to assert that the fighters actually REACHED the wall, so
// that "this test exercises the clamp" is a checked statement and not a comment.
// If Simulate.cpp's value changes, that assertion fails and says so, which is
// the correct outcome: the goldens are stale anyway.
constexpr std::int32_t kExpectedStageHalfWidth = 480 * kSubUnitsPerPixel;

InputPair scriptedInputs(int t) {
    InputPair in{};

    if (t < kPhaseWallsEnd) {
        // TWO THINGS, AND THE ORDER MATTERS, because the invisible wall changed
        // what a walk can reach. This phase used to send the pair to OPPOSITE
        // walls, which the separation limit now forbids by construction -- two
        // fighters may never be more than kMaxSeparationSub apart, so nobody can
        // walk to a wall by walking away from anybody.
        //
        // First the two converge and CROSS, which is what flips facing for both
        // and is still perfectly legal: the limit only ever objects to fighters
        // getting further apart.
        if (t < kPhaseWallsEnd / 2) {
            in.p[0].bits |= kInputRight;
            in.p[1].bits |= kInputLeft;
        } else {
            // Then they travel to +X TOGETHER, which is the only way anybody
            // reaches a wall now. The leader pins against the clamp and the
            // follower closes up behind, so this drives the clamp branch AND
            // the separation branch in the same stretch.
            //
            // Hold, do not tap: 300 ticks at 2 px is 600 px, more than the
            // 480 px half-width, so the back of this phase is spent with posX
            // pinned while velX is still non-zero -- the state a short scripted
            // test never reaches.
            in.p[0].bits |= kInputRight;
            in.p[1].bits |= kInputRight;
        }

    } else if (t < kPhaseCrossEnd) {
        // And back to -X together, so the other clamp gets the same treatment.
        in.p[0].bits |= kInputLeft;
        in.p[1].bits |= kInputLeft;

    } else if (t < kPhaseJumpsEnd) {
        const int k = t - kPhaseCrossEnd;
        // A 5-pixel impulse against a quarter-pixel-per-tick gravity is a
        // 39-tick arc, so a 48-tick cycle covers the whole of it plus a few
        // grounded ticks. Up is HELD for the first six ticks of the cycle: the
        // first press jumps, and the five after it must be ignored because the
        // fighter is already airborne. An input that must do nothing is worth
        // driving -- it is where an accidental double-jump would live.
        if (k % 48 < 6)  in.p[0].bits |= kInputUp;
        if (k % 48 == 20) in.p[1].bits |= kInputUp;   // p1 leaves at p0's apex,
                                                      // so the two are never in
                                                      // the same phase of flight
        if (t % 3 == 0) in.p[0].bits |= kInputRight;  // air control, such as it is
        if (t % 5 == 0) in.p[1].bits |= kInputLeft;

    } else if (t < kPhaseStunEnd) {
        const int k = t - kPhaseJumpsEnd;
        // Direction is held for the whole phase so that stun is VISIBLE: the
        // fighter is continuously asking to walk, and every tick it does not
        // move is a tick the stun branch decided.
        in.p[0].bits |= kInputRight;
        in.p[1].bits |= kInputLeft;
        if (k % 90 == 0)  in.p[0].bits |= kInputUp;   // jump, then get hit in the air
        if (k % 90 == 50) in.p[0].bits |= kInputUp;   // jump attempt while grounded
                                                      // AND stunned: must be refused.
                                                      // See scriptedEvents below --
                                                      // the hit at k==45 is what makes
                                                      // this tick interesting.
        if (k % 7 == 0)   in.p[1].bits |= kInputHP;

    } else if (t < kPhaseReversalEnd) {
        const int k = t - kPhaseStunEnd;
        // Left and Right together on alternate ticks. The two wishes cancel to
        // zero, which is a different route to velX == 0 than being stunned is,
        // and a fighter whose velocity flips sign every other tick is the worst
        // case for any future code that caches a direction.
        if (k % 2 == 0) in.p[0].bits |= (kInputLeft | kInputRight);
        else            in.p[0].bits |= kInputRight;
        if ((k / 10) % 2 == 0) in.p[1].bits |= kInputLeft;
        else                   in.p[1].bits |= kInputRight;
        // Attack buttons. They are inert in today's kernel -- nothing reads
        // them -- and they are in the script anyway, because they are on the
        // wire and in the input bits, and the day a button does something the
        // goldens must move. Better that they move for a reason written down
        // here than that this file quietly stops covering the real input space.
        if (k % 13 == 0) in.p[0].bits |= kInputLP;
        if (k % 17 == 0) in.p[1].bits |= kInputMK;

    } else {
        const int k = t - kPhaseReversalEnd;
        // Jumping in the corner: airborne and clamped at the same time, which
        // is the one combination the earlier phases keep apart. p1 does nothing
        // at all for six hundred ticks, which is its own branch.
        if (k < 480) {
            in.p[0].bits |= kInputRight;
            if (k % 60 == 0) in.p[0].bits |= kInputUp;
        }
        // ...and the last 120 ticks are silent, so the recorded final state is a
        // settled one. A final state caught mid-arc makes every human comparison
        // harder than it needs to be.
    }

    return in;
}

// The hits the kernel cannot yet deal itself.
//
// Simulate() has no hit detection: nothing in the kernel ever writes hitstun,
// blockstun, health, meter, comboHits or moveId today, so a match driven by
// inputs alone leaves the entire stun path -- the `else { velX = 0; }` branch,
// the countdown, the actionable() gate -- completely undriven. This function
// stands in for the hit-detection layer that will land with ADR-001's move
// table, writing the fields a hit would write.
//
// It is a pure function of the TICK INDEX. It reads the state only to clamp,
// and it consults no clock, no RNG and no platform facility, so it cannot be
// the thing that makes one platform disagree with another. When real hit
// detection arrives this function should shrink to nothing and the goldens will
// need re-recording; that is expected and is written down in the golden block.
void scriptedEvents(GameState& s, int t) {
    if (t >= kPhaseJumpsEnd && t < kPhaseStunEnd) {
        const int k = t - kPhaseJumpsEnd;

        if (k % 90 == 5) {
            // p0 is airborne here -- it jumped at k==0, and in the first cycle
            // it is still finishing the arc it started at the tail of the jump
            // phase. Either way this is a hit taken IN THE AIR: velX is forced
            // to zero while gravity keeps running, which is a combination
            // neither a grounded hit nor a plain jump produces.
            s.p[0].hitstun   = 21;
            s.p[0].comboHits = static_cast<std::uint8_t>(s.p[0].comboHits + 1);
            s.p[0].health    = s.p[0].health > 40 ? s.p[0].health - 40 : 0;
            s.p[1].res[0]   += 12;
        }
        if (k % 90 == 20) {
            // A blocked hit on p1: shorter, and it is the blockstun counter
            // rather than the hitstun one. Both gate actionable(), and a
            // one-line edit that checks only hitstun would pass every other
            // test in the suite.
            s.p[1].blockstun = 9;
            s.p[1].res[0]   += 3;
        }
        if (k % 90 == 45) {
            // p0 landed at k==38, so this one is taken on the ground, and it is
            // still burning at k==50 when the script presses Up.
            s.p[0].hitstun   = 18;
            s.p[0].comboHits = static_cast<std::uint8_t>(s.p[0].comboHits + 1);
            s.p[0].health    = s.p[0].health > 25 ? s.p[0].health - 25 : 0;
        }
        if (k % 90 == 70) {
            s.p[1].hitstun   = 30;
            s.p[1].comboHits = static_cast<std::uint8_t>(s.p[1].comboHits + 1);
            s.p[1].health    = s.p[1].health > 55 ? s.p[1].health - 55 : 0;
            s.p[0].res[0]   += 9;
        }
        if (k % 90 == 89) {
            s.p[0].comboHits = 0;      // the combo drops between cycles
            s.p[1].comboHits = 0;
        }
    }

    if (t >= kPhaseStunEnd && t < kPhaseReversalEnd) {
        const int k = t - kPhaseStunEnd;
        // moveId/moveFrame: the only fields whose update in Simulate() is
        // conditional on a value nothing else in this test ever sets. Start a
        // move, let its frame counter run, then cancel it back to idle.
        if (k % 64 == 0)  { s.p[0].moveId = static_cast<std::uint16_t>(3 + (k / 64) % 5);
                            s.p[0].moveFrame = 0; }
        if (k % 64 == 30) { s.p[0].moveId = 0; s.p[0].moveFrame = 0; }
        if (k % 50 == 0)  { s.p[1].moveId = 7; s.p[1].moveFrame = 0; }
        if (k % 50 == 22) { s.p[1].moveId = 0; s.p[1].moveFrame = 0; }
    }
}

// Count what this tick exercised. Everything here is derived from the pre- and
// post-step states, so it stays correct if the script is rewritten: it observes
// what the kernel DID rather than restating what the script asked for.
void observe(Coverage& c, const GameState& pre, const GameState& post,
             const InputPair& in) {
    for (int i = 0; i < 2; ++i) {
        const Fighter& a = pre.p[i];
        const Fighter& b = post.p[i];
        const std::uint16_t bits = in.p[i].bits;
        const bool held    = (bits & (kInputLeft | kInputRight)) != 0;
        const bool stunned = a.hitstun > 0 || a.blockstun > 0;

        // Wall detection without knowing where the wall is: posX is
        // clamp(a.posX + velX), so a non-zero velX that moved the fighter
        // nowhere can only mean the clamp fired.
        if (b.velX != 0 && b.posX == a.posX) {
            if (b.velX > 0) ++c.ticksAtRightWall; else ++c.ticksAtLeftWall;
        }
        if (b.airborne) {
            ++c.ticksAirborne;
            if (b.velY > 0) ++c.ticksRising;
            if (b.velY < 0) ++c.ticksFalling;
        }
        if (!a.airborne && b.airborne) ++c.jumpsStarted;
        if (a.airborne && !b.airborne) ++c.landings;

        if (a.hitstun   > 0) ++c.ticksInHitstun;
        if (a.blockstun > 0) ++c.ticksInBlockstun;
        // Note the pre-state: a fighter with exactly one tick of stun left has
        // it burned down BEFORE actionable() is consulted, so that tick the
        // fighter does move. Counting from the pre-state and requiring the
        // velocity to be zero keeps that off-by-one honest instead of hiding it.
        if (stunned && held && b.velX == 0) ++c.stunSuppressedAWalk;
        if (stunned && (bits & kInputUp) && !a.airborne && !b.airborne)
            ++c.stunBlockedAJump;
        if (!stunned && (bits & kInputLeft) && (bits & kInputRight) && b.velX == 0)
            ++c.bothDirectionsHeld;

        if (b.moveFrame == static_cast<std::uint16_t>(a.moveFrame + 1))
            ++c.ticksMoveFrameAdvancing;
        if (a.facing != b.facing) ++c.facingFlips;   // counted per fighter, so
                                                     // one crossing is two flips

        if (b.posX > c.maxPosX) c.maxPosX = b.posX;
        if (b.posX < c.minPosX) c.minPosX = b.posX;
    }

    if (post.rng == 0) ++c.rngHitZero;
    c.rngHighByteSeen[post.rng >> 24] = true;
}

struct RunResult {
    std::uint32_t rolling = 2166136261u;          // FNV-1a offset basis
    std::uint32_t checkpoint[kCheckpointCount] = {};
    Coverage cov{};
    GameState final_{};
};

// Run the whole scripted match, folding the state of every tick into the hash.
//
// perturbAtTick, when non-negative, nudges one fighter's height by perturbDelta
// sub-units after that tick's step. Only the localisation tests use it; it is
// the tool that proves the checkpoints can actually find a divergence, rather
// than merely claiming to in a failure message.
RunResult runScriptedMatch(int perturbAtTick = -1, std::int32_t perturbDelta = 1) {
    RunResult r{};

    GameState s{};
    ResetMatch(s, kMatchSeed);

    // Fold the opening position too. A change in ResetMatch is a change in the
    // match, and a hash that starts after the first Simulate would miss a
    // different starting distance entirely.
    fold(r.rolling, Checksum(s));

    for (int t = 0; t < kMatchTicks; ++t) {
        scriptedEvents(s, t);
        const GameState fed = s;              // what Simulate is about to see
        const InputPair in = scriptedInputs(t);
        Simulate(s, in);
        if (t == perturbAtTick) s.p[1].posY += perturbDelta;
        observe(r.cov, fed, s, in);

        fold(r.rolling, Checksum(s));

        const int done = t + 1;
        if (done % kCheckpointInterval == 0)
            r.checkpoint[done / kCheckpointInterval - 1] = r.rolling;
    }

    for (int i = 0; i < 256; ++i)
        if (r.cov.rngHighByteSeen[i]) ++r.cov.distinctRngHighBytes;

    r.final_ = s;
    return r;
}

// --- Reporting --------------------------------------------------------------

std::string hex32(std::uint32_t v) {
    static const char kDigits[] = "0123456789ABCDEF";
    std::string out = "0x";
    for (int shift = 28; shift >= 0; shift -= 4)
        out.push_back(kDigits[(v >> shift) & 0xFu]);
    out.push_back('u');
    return out;
}

// The failure message's most useful half: the replacement source text, spelled
// out so that recording a golden is a copy and a paste rather than a
// transcription of eight hex digits by hand at the end of a long day.
std::string regenerationBlock(const RunResult& r) {
    std::string s;
    s += "\n";
    s += "---- computed values: paste over the GOLDEN CONSTANTS block ----\n";
    s += "constexpr std::uint32_t kGoldenRollingHash = " + hex32(r.rolling) + ";\n";
    s += "constexpr std::uint32_t kGoldenCheckpoint[kCheckpointCount] = {\n";
    for (int i = 0; i < kCheckpointCount; ++i) {
        s += "    " + hex32(r.checkpoint[i]) + ",  // tick "
           + std::to_string((i + 1) * kCheckpointInterval) + "\n";
    }
    s += "};\n";
    s += "----------------------------------------------------------------\n";
    return s;
}

// Turn a tick index into the sentence a human needs: what were the fighters
// doing there. A divergence report that says only "tick 2431" makes the reader
// go and re-derive the phase table by hand.
const char* phaseAt(int tick) {
    if (tick < kPhaseWallsEnd)    return "walking out to the walls";
    if (tick < kPhaseCrossEnd)    return "crossing over";
    if (tick < kPhaseJumpsEnd)    return "jump arcs";
    if (tick < kPhaseStunEnd)     return "hitstun and blockstun";
    if (tick < kPhaseReversalEnd) return "opposing and alternating input";
    return "corner jumps, then settling";
}

bool goldensAreUnrecorded() {
    if (kGoldenRollingHash == kGoldenUnrecorded) return true;
    for (int i = 0; i < kCheckpointCount; ++i)
        if (kGoldenCheckpoint[i] == kGoldenUnrecorded) return true;
    return false;
}

// The layout checks, factored out so the golden test can run them FIRST. gtest
// orders tests within a suite by declaration, but --gtest_shuffle and
// --gtest_filter both defeat that, and "the more specific failure fires first"
// has to hold when someone runs this test alone.
void assertLayoutMatchesTheGolden() {
    ASSERT_EQ(kGoldenSizeofFighter, sizeof(Fighter))
        << "sizeof(Fighter) changed. The golden hash is a hash of these bytes, "
           "so it is stale by construction -- but note that this is a LAYOUT "
           "change, not an arithmetic one, and the two have completely different "
           "consequences for crossplay.";
    ASSERT_EQ(kGoldenSizeofGameState, sizeof(GameState))
        << "sizeof(GameState) changed. Same story as Fighter: re-record the "
           "goldens on both platforms, and check that the two agree.";
    ASSERT_EQ(kGoldenAlignofFighter, alignof(Fighter));
    ASSERT_EQ(kGoldenAlignofGameState, alignof(GameState));

    // Offsets, so a layout failure names the field instead of only the total.
    // If two toolchains ever disagree HERE, the disagreement is about the ABI
    // and no amount of integer discipline in Simulate.cpp will save the wire
    // format.
    ASSERT_EQ(std::size_t{0},  offsetof(Fighter, posX));
    ASSERT_EQ(std::size_t{4},  offsetof(Fighter, posY));
    ASSERT_EQ(std::size_t{8},  offsetof(Fighter, velX));
    ASSERT_EQ(std::size_t{12}, offsetof(Fighter, velY));
    ASSERT_EQ(std::size_t{16}, offsetof(Fighter, health));
    // res[] replaced a named `meter` at this offset and is four times as wide,
    // which is why every offset below moved and the goldens were re-recorded.
    ASSERT_EQ(std::size_t{20}, offsetof(Fighter, res));
    // Pushback rides its own int32 rather than velX, because a fighter who
    // cannot act has velX zeroed every tick and being hit is exactly that.
    ASSERT_EQ(std::size_t{36}, offsetof(Fighter, pushX));

    ASSERT_EQ(std::size_t{40}, offsetof(Fighter, moveId));
    ASSERT_EQ(std::size_t{42}, offsetof(Fighter, moveFrame));
    ASSERT_EQ(std::size_t{44}, offsetof(Fighter, hitstun));
    ASSERT_EQ(std::size_t{46}, offsetof(Fighter, blockstun));
    ASSERT_EQ(std::size_t{48}, offsetof(Fighter, hitstop));
    ASSERT_EQ(std::size_t{50}, offsetof(Fighter, knockdown));
    ASSERT_EQ(std::size_t{52}, offsetof(Fighter, juggle));
    ASSERT_EQ(std::size_t{54}, offsetof(Fighter, scaling));

    ASSERT_EQ(std::size_t{56}, offsetof(Fighter, facing));
    ASSERT_EQ(std::size_t{57}, offsetof(Fighter, airborne));
    ASSERT_EQ(std::size_t{58}, offsetof(Fighter, comboHits));
    ASSERT_EQ(std::size_t{59}, offsetof(Fighter, alreadyHitBits));

    // THE EIGHT-BYTE GROUP IS THE POINT OF THIS BLOCK. GameState.h used to warn
    // that a FIFTH uint8 would need three more explicit bytes or the compiler
    // would insert tail padding nobody initialises. The P2 expansion took that
    // warning at its word and added exactly four -- team, active, crouching,
    // guard -- which fills the group and keeps the struct a multiple of its
    // 4-byte alignment. A NINTH uint8 here reopens the same hazard.
    ASSERT_EQ(std::size_t{60}, offsetof(Fighter, team));
    ASSERT_EQ(std::size_t{61}, offsetof(Fighter, active));
    ASSERT_EQ(std::size_t{62}, offsetof(Fighter, crouching));
    ASSERT_EQ(std::size_t{63}, offsetof(Fighter, guard));

    // M1.3's reaction fields, reserved by M1.1a and written by nothing yet.
    // reaction and bounces take the group to TEN uint8s, and flags follows on a
    // 2-aligned offset, so the struct still ends on a multiple of 4 with no
    // tail padding -- which has_unique_object_representations_v in GameState.h
    // now checks rather than leaving to this arithmetic.
    ASSERT_EQ(std::size_t{64}, offsetof(Fighter, reaction));
    ASSERT_EQ(std::size_t{65}, offsetof(Fighter, bounces));
    ASSERT_EQ(std::size_t{66}, offsetof(Fighter, flags));

    ASSERT_EQ(std::size_t{0},  offsetof(GameState, tick));
    ASSERT_EQ(std::size_t{4},  offsetof(GameState, rng));
    ASSERT_EQ(std::size_t{8},  offsetof(GameState, roundTimer));
    ASSERT_EQ(std::size_t{12}, offsetof(GameState, roundNumber));
    ASSERT_EQ(std::size_t{14}, offsetof(GameState, roundState));
    ASSERT_EQ(std::size_t{15}, offsetof(GameState, fighterCount));
    ASSERT_EQ(std::size_t{16}, offsetof(GameState, roundsWon));
    ASSERT_EQ(std::size_t{18}, offsetof(GameState, roundsToWin));
    // The event ring sits between the header and the fighters, so p moved by
    // its 96 bytes plus evCount and three explicit pad bytes.
    ASSERT_EQ(std::size_t{20},  offsetof(GameState, ev));
    ASSERT_EQ(std::size_t{116}, offsetof(GameState, evCount));
    ASSERT_EQ(std::size_t{120}, offsetof(GameState, p))
        << "The compiler inserted padding between GameState's header and its "
           "fighters, or a member was added. Either way the hashed byte string "
           "is no longer the one the golden was recorded from.";
}

// Every claim the golden test makes about what the match covered, checked. Kept
// separate so the coverage failures read as what they are -- "this test has
// stopped testing what it says it tests" -- and are not confused with a
// determinism failure.
void expectTheMatchActuallyExercisedTheKernel(const RunResult& r) {
    const Coverage& c = r.cov;

    EXPECT_EQ(static_cast<std::uint32_t>(kMatchTicks), r.final_.tick)
        << "the match did not run to completion";

    EXPECT_GT(c.ticksAtRightWall, 100)
        << "the fighters never spent meaningful time pinned against the +X "
           "clamp, so the golden hash does not cover the clamp branch";
    EXPECT_GT(c.ticksAtLeftWall, 100)
        << "same for the -X clamp";
    EXPECT_EQ(kExpectedStageHalfWidth, c.maxPosX)
        << "the furthest right any fighter reached was " << c.maxPosX
        << " sub-units, not the " << kExpectedStageHalfWidth << " this file "
           "expects. Either the stage half-width in Simulate.cpp changed (in "
           "which case the goldens are stale and must be re-recorded) or the "
           "script no longer walks anyone into the wall.";
    EXPECT_EQ(-kExpectedStageHalfWidth, c.minPosX)
        << "same for the left wall; furthest left was " << c.minPosX;

    EXPECT_GT(c.jumpsStarted, 20)  << "the jump branch was barely driven";
    EXPECT_GT(c.landings, 20)      << "jumps were started but not landed";
    EXPECT_GT(c.ticksAirborne, 500)
        << "only " << c.ticksAirborne << " fighter-ticks were spent airborne, "
           "so gravity accumulated over almost nothing";
    EXPECT_GT(c.ticksRising, 100) << "no meaningful ascent";
    EXPECT_GT(c.ticksFalling, 100)
        << "the falling half of the arc is where gravity accumulates; if this "
           "is small the arcs were truncated";

    EXPECT_GT(c.ticksInHitstun, 100)   << "hitstun was barely driven";
    EXPECT_GT(c.ticksInBlockstun, 20)  << "blockstun was barely driven";
    EXPECT_GT(c.stunSuppressedAWalk, 50)
        << "no tick was found where a fighter asked to walk and stun refused. "
           "The scripted hits and the held directions have drifted out of "
           "alignment and the stun path is no longer covered.";
    EXPECT_GT(c.stunBlockedAJump, 0)
        << "no tick was found where a grounded, stunned fighter pressed Up and "
           "did not jump. See scriptedInputs' k%90==50 and scriptedEvents' "
           "k%90==45 -- they have to stay in step.";
    EXPECT_GT(c.bothDirectionsHeld, 100)
        << "the opposing-directions-cancel branch was not driven";
    EXPECT_GT(c.ticksMoveFrameAdvancing, 100)
        << "moveFrame never advanced, so the moveId != 0 branch is uncovered";
    EXPECT_GE(c.facingFlips, 4)
        << "the fighters crossed fewer than twice (each crossing flips both "
           "fighters, so this counts two per crossing)";

    // The PRNG is snapshot-owned state that advances every tick whether or not
    // anything consumes it, so it is in the hash on every one of these ticks.
    // A stream stuck at a constant would still hash stably and would still be
    // wrong.
    EXPECT_EQ(0, c.rngHitZero)
        << "the PRNG reached zero, which xorshift32 cannot leave. Every "
           "subsequent tick draws the same value and the stream is dead.";
    EXPECT_GT(c.distinctRngHighBytes, 200)
        << "only " << c.distinctRngHighBytes << " distinct high bytes in "
        << kMatchTicks << " draws; the stream is not moving as a 32-bit "
           "xorshift should";
}

} // namespace

// ============================================================================
// The tests.
// ============================================================================

TEST(CrossPlatformLayout, TheBytesTheGoldenHashIsTakenOver) {
    // Declared first, and duplicated as a precondition inside the golden test,
    // because "the struct grew a field" and "the two toolchains do different
    // arithmetic" produce the same symptom and need very different responses.
    assertLayoutMatchesTheGolden();
}

TEST(CrossPlatformDeterminism, TheScriptedMatchHashesToTheRecordedValue) {
    assertLayoutMatchesTheGolden();
    ASSERT_FALSE(HasFatalFailure())
        << "GameState's layout is not the one the golden hash was recorded "
           "from, so comparing the hash would only tell you something you "
           "already know. Fix the layout question first.";

    const RunResult r = runScriptedMatch();
    expectTheMatchActuallyExercisedTheKernel(r);

    if (goldensAreUnrecorded()) {
        ADD_FAILURE()
            << "The golden constants in this file have never been recorded -- "
               "they are still the placeholder.\n"
               "This is the expected state of a freshly written test, and the "
               "fix is mechanical: paste the block below over the GOLDEN "
               "CONSTANTS block, then confirm the OTHER platform's CI job "
               "agrees with it. Until a second toolchain has checked these "
               "numbers, this test proves nothing about crossplay.\n"
            << regenerationBlock(r);
        return;
    }

    // Localise before reporting. The hash is cumulative, so the first
    // checkpoint that differs brackets the divergence into a 1000-tick window,
    // and the phase table above turns that window into a sentence about what
    // the fighters were doing.
    int firstBad = -1;
    for (int i = 0; i < kCheckpointCount; ++i) {
        if (r.checkpoint[i] != kGoldenCheckpoint[i]) { firstBad = i; break; }
    }

    if (firstBad >= 0) {
        const int lo = firstBad * kCheckpointInterval;
        const int hi = (firstBad + 1) * kCheckpointInterval;
        ADD_FAILURE()
            << "First divergence is in ticks " << lo << ".." << hi << ".\n"
            << "  checkpoint at tick " << hi << ": recorded "
            << hex32(kGoldenCheckpoint[firstBad]) << ", got "
            << hex32(r.checkpoint[firstBad]) << "\n"
            << "  every earlier checkpoint matched, so the two simulations were "
               "still identical at tick " << lo << ".\n"
            << "  That window runs from " << phaseAt(lo) << " through "
            << phaseAt(hi - 1) << ".";
    } else if (r.rolling != kGoldenRollingHash) {
        // Every checkpoint matched and the total did not, which brackets the
        // divergence into the tail after the last checkpoint just as tightly.
        const int lo = kCheckpointCount * kCheckpointInterval;
        ADD_FAILURE()
            << "First divergence is in ticks " << lo << ".." << kMatchTicks
            << " -- after the last checkpoint, during " << phaseAt(lo) << ".";
    }

    EXPECT_EQ(kGoldenRollingHash, r.rolling)
        << "\nThe rolling hash of a " << kMatchTicks << "-tick fully scripted "
           "match does not match the value recorded in this file.\n"
           "\n"
           "WHAT THIS MEANS. Not that the test is broken. This binary produced "
           "a different simulation from the one recorded, and the recorded one "
           "is what the other toolchain produces. ARCHITECTURE.md's claim that "
           "bit-identity is a property of integer arithmetic rather than of a "
           "build flag is, on this evidence, false for this build -- and "
           "Windows<->Linux crossplay (NORTHSTAR Q1, answered (a)) is void "
           "until the difference is understood. Do not re-record the constant "
           "to make this green; re-recording destroys the only evidence that "
           "anything is wrong.\n"
           "\n"
           "WHERE TO LOOK, in order:\n"
           "  1. Did CrossPlatformLayout pass? If not, this is a layout change, "
           "not an arithmetic one. Start there.\n"
           "  2. Did Simulate.cpp, GameState.h, Checksum(), or the scripted "
           "match in this file change? Then the goldens are stale by "
           "construction: re-record on BOTH platforms and confirm the two "
           "agree.\n"
           "  3. Neither? Then the kernel has acquired arithmetic that is not "
           "platform-independent. The candidates, in the order they have "
           "actually happened to people: a float or double that crept into the "
           "kernel target, a libm call, an arithmetic right shift of a negative "
           "value (implementation-defined in C++17, which is this repo's "
           "standard), a signed overflow, an int/long width assumption, or a "
           "read of a padding byte that one compiler leaves indeterminate.\n"
        << regenerationBlock(r);
}

TEST(CrossPlatformDeterminism, TheScriptedMatchIsReproducibleWithinThisBinary) {
    // Weaker than the golden test and not redundant with it. The scripted match
    // and its injected hits are NEW code that the golden depends on, and if any
    // of it were accidentally stateful -- a static counter in a helper, say --
    // the golden would be a property of the run rather than of the simulation,
    // and the two platforms would disagree for a reason that has nothing to do
    // with either of them.
    const RunResult a = runScriptedMatch();
    const RunResult b = runScriptedMatch();

    EXPECT_EQ(a.rolling, b.rolling)
        << "two runs of the same scripted match in the same process produced "
           "different rolling hashes; something in this file or in the kernel "
           "is not a pure function of (state, inputs)";
    for (int i = 0; i < kCheckpointCount; ++i)
        EXPECT_EQ(a.checkpoint[i], b.checkpoint[i]) << "checkpoint " << i;
    EXPECT_EQ(0, std::memcmp(&a.final_, &b.final_, sizeof(GameState)));

    // Vacuity guard: an empty match would satisfy everything above.
    EXPECT_NE(2166136261u, a.rolling)
        << "the rolling hash is still the FNV offset basis, so nothing was "
           "folded into it";
}

TEST(CrossPlatformDeterminism, TheCheckpointsBracketADivergence) {
    // The golden test's failure message promises to name a 1000-tick window.
    // This is what makes that promise true, and it needs no recorded constant,
    // so it is meaningful on the very first run -- before the goldens exist.
    //
    // The perturbation is one sub-unit, a 256th of a pixel, applied at tick
    // 1500: the smallest difference the state can hold.
    const RunResult clean     = runScriptedMatch();
    const RunResult perturbed = runScriptedMatch(1500);

    EXPECT_EQ(clean.checkpoint[0], perturbed.checkpoint[0])
        << "the tick-1000 checkpoint changed although the perturbation was "
           "applied at tick 1500. The rolling hash is picking up something "
           "other than the state it is supposed to be hashing, and every "
           "window this test's failure messages name would be wrong.";
    EXPECT_NE(clean.checkpoint[1], perturbed.checkpoint[1])
        << "a one-sub-unit difference introduced at tick 1500 never reached "
           "the tick-2000 checkpoint, so the hash is not folding every tick";
    EXPECT_NE(clean.checkpoint[2], perturbed.checkpoint[2])
        << "the divergence did not survive to tick 3000";
    EXPECT_NE(clean.rolling, perturbed.rolling)
        << "the divergence did not survive to the end of the match";
}

TEST(CrossPlatformDeterminism, AFinalStateHashWouldHaveMissedThisEntirely) {
    // The reason the hash is rolled rather than taken once at the end, made
    // concrete: a divergence that the simulation ERASES.
    //
    // p1 is idle for the whole of the last phase -- grounded, velY zero,
    // airborne clear -- so dropping its height by one sub-unit is undone on the
    // very next tick by the floor snap in StepPhysics (`if (posY <= 0) { posY =
    // 0; velY = 0; airborne = 0; }`). One tick later the two runs are byte-
    // identical again and stay that way to the end of the match.
    //
    // So the two matches end in exactly the same state, and the kernel's own
    // Checksum -- the four bytes ADR-002 CHOICE C puts on the wire -- agrees.
    // They were not the same match. On two machines that is a frame where the
    // players saw different things and nothing ever reported it.
    const RunResult clean     = runScriptedMatch();
    const RunResult perturbed = runScriptedMatch(3100, -1);

    ASSERT_EQ(0, std::memcmp(&clean.final_, &perturbed.final_, sizeof(GameState)))
        << "the perturbation at tick 3100 was NOT erased, so this test is no "
           "longer demonstrating the thing it exists to demonstrate. Check "
           "that p1 is still idle and grounded through the final phase -- if "
           "the scripted match changed, move the perturbation to a tick where "
           "it is.";
    EXPECT_EQ(Checksum(clean.final_), Checksum(perturbed.final_))
        << "final-state checksums differ, which contradicts the memcmp above";

    // ...and the rolling hash catches what the final state cannot.
    EXPECT_NE(clean.rolling, perturbed.rolling)
        << "the rolling hash agreed on two matches that were provably "
           "different at tick 3101. It is not folding every tick, and the "
           "golden constant it protects is weaker than a final-state hash "
           "would have been.";
    // The checkpoints, all of which precede the perturbation, must be untouched
    // -- which is also how a reader learns the divergence is after tick 3000.
    for (int i = 0; i < kCheckpointCount; ++i)
        EXPECT_EQ(clean.checkpoint[i], perturbed.checkpoint[i])
            << "checkpoint " << i << " changed, though it precedes tick 3100";
}

// ============================================================================
// Sub-unit arithmetic: the representation the whole claim rests on.
// ============================================================================
//
// D2 chose plain int32 in 256ths of a pixel over a general fixed-point type,
// and gave a specific reason for rejecting the alternative: a Fixed type invites
// `>>` as a cheaper `/`, and the two round differently for negative values --
// `>>` toward minus infinity, `/` toward zero. A simulation that rounds toward
// minus infinity is NOT MIRROR-SYMMETRIC: a character walking left loses a
// least-significant bit that the same character walking right keeps, and the
// error accumulates. These tests pin the properties that make the chosen
// representation safe, so a future change to a different one cannot be quiet.

namespace {

// What an arithmetic right shift would give: rounding toward minus infinity.
// Written as division rather than as `>>` on purpose -- shifting a negative
// value right is implementation-defined in C++17 (fixed only in C++20), and
// this repo is on C++17, so asserting anything about `>>` here would be
// asserting about this compiler rather than about the language.
std::int32_t floorDiv(std::int32_t v, std::int32_t d) {
    const std::int32_t q = v / d;
    return (v % d != 0 && ((v < 0) != (d < 0))) ? q - 1 : q;
}

} // namespace

TEST(SubUnitArithmetic, OnePixelIsExactlyTwoHundredAndFiftySixSubUnits) {
    EXPECT_EQ(256, kSubUnitsPerPixel);

    // A power of two, which is the whole point: friction and knockback are
    // authored as halvings, and a halving that rounds is a desync waiting for
    // the first tick where the two peers disagree about the last bit.
    EXPECT_EQ(0, kSubUnitsPerPixel & (kSubUnitsPerPixel - 1))
        << "kSubUnitsPerPixel is no longer a power of two, so halving a "
           "velocity is now a rounding decision rather than a shift of the "
           "representation";

    for (std::int32_t d : {2, 4, 8, 16}) {
        EXPECT_EQ(0, kSubUnitsPerPixel % d)
            << "a pixel no longer divides evenly by " << d;
        EXPECT_EQ(kSubUnitsPerPixel, (kSubUnitsPerPixel / d) * d)
            << "dividing a pixel by " << d << " and multiplying back is lossy";
    }
}

TEST(SubUnitArithmetic, IntegerDivisionTruncatesTowardZeroForBothSigns) {
    // C++11 onwards defines integer division as truncating toward zero for both
    // signs (before that it was implementation-defined, which is why this is
    // worth writing down rather than assuming). Everything in the kernel that
    // scales a quantity depends on it, and D2's `scaleBy` helper is built
    // directly on top of it.
    EXPECT_EQ(3,  7 / 2);
    EXPECT_EQ(-3, -7 / 2);
    EXPECT_EQ(1,  7 % 2);
    EXPECT_EQ(-1, -7 % 2);

    // The property that actually matters is the symmetric one: dividing a
    // negative must give exactly the negation of dividing the positive. That is
    // mirror symmetry at the level of a single operation.
    for (std::int32_t v = -4096; v <= 4096; ++v) {
        for (std::int32_t d : {2, 4, 8, 16, 256}) {
            ASSERT_EQ(-(v / d), (-v) / d)
                << "division is not symmetric about zero for v=" << v
                << " d=" << d << ", so a mirrored character does not move like "
                   "its reflection";
        }
    }
}

TEST(SubUnitArithmetic, RoundingTowardMinusInfinityWouldBreakTheMirror) {
    // Not a test of the kernel: a test of the ALTERNATIVE, kept executable so
    // the cost of the rejected representation is written down in a form that
    // cannot go stale. If someone ever replaces `/` with `>>` for speed, this
    // is what they are buying.
    EXPECT_NE(floorDiv(-7, 2), -7 / 2)
        << "floor division and truncating division agree here, which means "
           "this test has stopped demonstrating anything";
    EXPECT_NE(floorDiv(-7, 2), -floorDiv(7, 2))
        << "floor division is symmetric about zero, which it is not";
    EXPECT_EQ(-7 / 2, -(7 / 2))
        << "truncating division is symmetric about zero, which is the property "
           "the kernel relies on";
}

TEST(SubUnitArithmetic, WalkingLeftAndRightAreExactMirrorsThroughSimulate) {
    // The same property, at the level of the simulation rather than of one
    // operator. Two matches that are reflections of each other must stay exact
    // reflections for as long as they run, including through the clamp at the
    // wall.
    GameState right{}, left{};
    ResetMatch(right, kMatchSeed);
    ResetMatch(left,  kMatchSeed);

    // Reflect the whole opening position, not just the fighter under test, so
    // that the two runs really are mirror images and facing resolves the same
    // way in both.
    right.p[0].posX =  0;
    left.p[0].posX  =  0;
    right.p[1].posX =  100 * kSubUnitsPerPixel;
    left.p[1].posX  = -100 * kSubUnitsPerPixel;

    const int kTicks = 1200;   // 2400 pixels of walking: far enough that the
                               // clamp is reached and then held on both sides
    for (int t = 0; t < kTicks; ++t) {
        // BOTH FIGHTERS, SAME DIRECTION, because since the invisible wall landed
        // nobody reaches a wall alone: a lone walker is stopped
        // kMaxSeparationSub from a stationary opponent, six pixels short of the
        // clamp, and the vacuity guard below would never fire again. Walking the
        // pair together restores it and leaves the mirror property -- which is
        // what this test is actually about -- untouched, because the separation
        // clamp is as symmetric as everything else here.
        InputPair r{}, l{};
        r.p[0].bits |= kInputRight;
        r.p[1].bits |= kInputRight;
        l.p[0].bits |= kInputLeft;
        l.p[1].bits |= kInputLeft;
        Simulate(right, r);
        Simulate(left,  l);

        ASSERT_EQ(right.p[0].posX, -left.p[0].posX)
            << "positions stopped being reflections at tick " << t;
        ASSERT_EQ(right.p[0].velX, -left.p[0].velX)
            << "velocities stopped being reflections at tick " << t;
    }

    // Vacuity guard: the mirror is trivially true for a fighter that never
    // moved, and it is only interesting once the clamp has had its say.
    EXPECT_EQ(kExpectedStageHalfWidth,  right.p[0].posX)
        << "the right-walking fighter never reached the wall, so this test "
           "never exercised the clamp";
    EXPECT_EQ(-kExpectedStageHalfWidth, left.p[0].posX)
        << "the left-walking fighter never reached the wall";
}
