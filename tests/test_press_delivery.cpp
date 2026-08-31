// ROADMAP M1.3h: the host delivers every press.
//
// The kernel's input buffer (M1.1e) cannot fix what never arrives, and the
// measured gap was in the HOST: the pad is sampled as LEVELS only on the fixed
// steps a session tick actually runs, so a tap that goes down and up entirely
// between two run ticks -- under slow motion, while paused, or inside render
// frames that run no tick at all -- vanished before the kernel ever saw it.
// Under slow motion at divisor 8 that sampled the pad at 7.5 Hz, in the
// training tool whose whole point is practising links slowly.
//
// The fix is PressAccumulator (cse/game/InputSource.h): Note the press edges
// the input layer served on EVERY fixed step, Spend them into the level bits
// of the next tick that runs, BEFORE LatchedInputSource records them -- so
// replay, rollback and the desync checksum read the same bytes the simulation
// did, and nothing downstream can tell a delivered tap from a perfectly timed
// hold.
//
// WHAT THIS FILE CANNOT COVER, RECORDED RATHER THAN GLOSSED: the mode's own
// FixedTick glue (UntitledFighterMode.cpp) is five lines mirroring the Host
// harness below, but the mode cannot be constructed headlessly -- every input
// read goes through Application, whose constructor creates a real GLFW window
// -- so the RULE is pinned here against the real InputMap, the real
// LatchedInputSource, the real FightSession and the real kernel, and the
// mode's copy of the rule is the one thing left to the eye. The GLFW half
// (sticky keys, Window.h) is a platform behaviour no headless harness can
// reach; its record is the comment at the glfwSetInputMode call.
#include <gtest/gtest.h>

#include "Engine.h"

#include "cse/data/CharacterData.h"
#include "cse/data/MatchBuilder.h"
#include "cse/game/FightSession.h"
#include "cse/game/InputSource.h"

#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_map>

using cse::data::BuildMatchData;
using cse::data::BuildOptions;
using cse::data::CharacterData;
using cse::data::LoadCharacterFile;
using cse::data::LoadOptions;
using cse::data::LoadReport;
using cse::data::MatchBuild;
using cse::game::FightSession;
using cse::game::FightSetup;
using cse::game::LatchedInputSource;
using cse::game::PressAccumulator;

namespace {

// Same walk-up as test_variants.cpp: prefer the staged copy the suite runs
// against, fall back to the source tree for a bare IDE run.
std::string charactersDir() {
    namespace fs = std::filesystem;
    const char* const marker = "fighter_a.json";
    fs::path here = fs::current_path();
    for (int i = 0; i < 8; ++i) {
        const fs::path staged = here / "Exported" / "Characters";
        if (fs::exists(staged / marker)) return staged.string();
        if (!here.has_parent_path() || here.parent_path() == here) break;
        here = here.parent_path();
    }
    here = fs::current_path();
    for (int i = 0; i < 8; ++i) {
        const fs::path source =
            here / "Games" / "UntitledFighter" / "Assets" / "Characters";
        if (fs::exists(source / marker)) return source.string();
        if (!here.has_parent_path() || here.parent_path() == here) break;
        here = here.parent_path();
    }
    return "Exported/Characters";
}

// The scripted-input fake from test_input_map.cpp: the virtual poll seams
// exist exactly so a test can be the keyboard.
class FakeInput : public MyCoreEngine::InputMap {
public:
    std::unordered_map<int, bool> keys;

protected:
    bool pollKey(GLFWwindow*, int key) const override {
        auto it = keys.find(key);
        return it != keys.end() && it->second;
    }
    bool pollMouseButton(GLFWwindow*, int) const override { return false; }
    bool pollGamepad(GLFWgamepadstate& out) const override {
        (void)out;
        return false;
    }
};

constexpr const char* kActLP = "PadLP";
constexpr int         kKeyLP = GLFW_KEY_U;

// THE HOST'S DELIVERY RULE, the same lines UntitledFighterMode::FixedTick
// runs, driven here against the real engine InputMap and the real session so
// "the tap reaches the kernel" is measured and not modelled. `accumulate`
// false is the SHIPPED pre-M1.3h rule (levels only), kept so the vanish this
// work package exists to close stays measured beside its fix.
struct Host {
    FakeInput           input;
    MatchBuild          build{};
    FightSession        session{};
    LatchedInputSource  local{ 0u, "YOU" };
    PressAccumulator    taps{};
    std::uint16_t       lpSlot = 0;

    bool accumulate  = true;
    int  slowDivisor = 1;
    int  slowCounter = 0;
    bool paused      = false;
    int  pendingSteps = 0;

    // Move starts observed across the whole drive, counted the way the kernel
    // defines a start (a move on frame 0 that was not there the tick before).
    int  moveStarts = 0;
    std::uint16_t prevMoveId    = 0;
    std::uint16_t prevMoveFrame = 0;

    void BringUp() {
        input.bindKey(kActLP, kKeyLP);

        CharacterData character{};
        LoadReport    report{};
        LoadOptions   options;
        options.expectedResources = { "meter", "juggle" };
        ASSERT_TRUE(LoadCharacterFile(charactersDir(), "fighter_a.json",
                                      options, character, report))
            << report.error;

        BuildOptions bo{};
        bo.bindings.push_back({ "stand_lp", cse::kernel::kInputLP });
        ASSERT_TRUE(BuildMatchData(character, bo, character, bo, build))
            << build.report[0].error;
        lpSlot = build.moves[0].Find("stand_lp");
        ASSERT_NE(lpSlot, 0u);

        // The default opening (+/-100 px) keeps stand_lp WHIFFING: no contact,
        // no freeze, so every timing below is the move's own frame data.
        FightSetup setup{};
        setup.data = &build.data;
        std::string error;
        ASSERT_TRUE(session.Begin(setup, error)) << error;
        session.SetInputSource(0, &local);
    }

    // One render frame: poll once, then run `steps` fixed steps -- the
    // Application's loop in miniature, including its latch-retirement rule
    // (Application.cpp: a latch survives only a frame that OWED a tick and ran
    // none, which at high frame rates is most of them).
    void Frame(int steps) {
        input.update(nullptr);
        for (int i = 0; i < steps; ++i) FixedStep();
        const bool awaitingTick = (steps == 0);
        if (!awaitingTick) input.clearPressLatches();
    }

    void FixedStep() {
        input.beginInputPhase();

        // The mode's notePadPresses_: the edges this step, whether or not a
        // tick runs. consumePressed and not wasPressed for InputMap.h's own
        // reason -- a frame-scoped edge misses most presses above the fixed
        // rate and multiplies them below it.
        std::uint16_t pressed = 0;
        if (input.consumePressed(kActLP)) pressed |= cse::kernel::kInputLP;
        if (accumulate) taps.Note(pressed);

        // The run gate, verbatim from UntitledFighterMode::FixedTick.
        bool run = false;
        if (pendingSteps > 0) {
            --pendingSteps;
            run = true;
        } else if (!paused) {
            if (++slowCounter >= slowDivisor) {
                slowCounter = 0;
                run = true;
            }
        }
        if (!run) return;

        // readPad_ (levels), then Spend, then LATCH, THEN TICK.
        cse::kernel::Input in{};
        if (input.isDown(kActLP)) in.bits |= cse::kernel::kInputLP;
        if (accumulate) in.bits = taps.Spend(in.bits);
        ASSERT_TRUE(local.Latch(session.CurrentTick(), in));
        session.Tick();

        const cse::kernel::Fighter& f = session.State().p[0];
        const bool started =
            f.moveId != 0 && f.moveFrame == 0 &&
            (prevMoveId != f.moveId || prevMoveFrame != 0);
        if (started) ++moveStarts;
        prevMoveId    = f.moveId;
        prevMoveFrame = f.moveFrame;
    }

    std::uint16_t MoveNow() const { return session.State().p[0].moveId; }
};

} // namespace

// ============================================================================
// 1. The accumulator itself
// ============================================================================

TEST(PressAccumulator, NotesAccumulateAndSpendDeliversExactlyOnce) {
    PressAccumulator taps;
    EXPECT_EQ(taps.Pending(), 0u);

    // Two steps' presses merge; a step with nothing changes nothing.
    taps.Note(cse::kernel::kInputLP);
    taps.Note(0);
    taps.Note(cse::kernel::kInputMP);
    EXPECT_EQ(taps.Pending(), cse::kernel::kInputLP | cse::kernel::kInputMP);

    // Spend ORs into the levels and is spent: the very next Spend passes the
    // levels through untouched. A pulse delivered twice would be a press the
    // player made once and the kernel saw twice.
    const std::uint16_t held = cse::kernel::kInputHK;
    EXPECT_EQ(taps.Spend(held),
              held | cse::kernel::kInputLP | cse::kernel::kInputMP);
    EXPECT_EQ(taps.Pending(), 0u);
    EXPECT_EQ(taps.Spend(held), held);

    // Clear is for match resets: a press aimed at a match that is gone.
    taps.Note(cse::kernel::kInputLP);
    taps.Clear();
    EXPECT_EQ(taps.Pending(), 0u);
}

// ============================================================================
// 2. The measured vanish, kept beside its fix
// ============================================================================

// The SHIPPED rule (levels only, latched on run ticks): under slow motion at
// divisor 4, a tap made and released between run ticks reaches nothing --
// four times in a row. This is the defect measured by the buffer review, and
// it must stay measured: if this test ever fails, level reads have started
// delivering sub-divisor taps by some other route, and the accumulator's
// reason needs re-deriving.
TEST(PressDelivery, ATapBetweenSlowMotionRunTicksVanishesThroughTheLevelReadAlone) {
    Host host;
    host.BringUp();
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    host.accumulate  = false;
    host.slowDivisor = 4;

    for (int round = 0; round < 4; ++round) {
        // Steps 1..3 of the divisor window skip; the tap lives entirely
        // inside them.
        host.Frame(1);                       // skip (counter 1)
        host.input.keys[kKeyLP] = true;
        host.Frame(1);                       // skip (counter 2) -- press seen, not sampled
        host.input.keys[kKeyLP] = false;
        host.Frame(1);                       // skip (counter 3) -- released again
        host.Frame(1);                       // RUN: level read finds nothing
        EXPECT_EQ(host.MoveNow(), 0u)
            << "round " << round << ": the level read delivered a tap that was "
               "up on the run tick; the accumulator's premise is stale";
    }
    EXPECT_EQ(host.moveStarts, 0);
}

// ============================================================================
// 3. The Done-when, part one: slow motion latches taps across non-run ticks
// ============================================================================

TEST(PressDelivery, ATapBetweenSlowMotionRunTicksIsDeliveredOnTheNextRunTick) {
    Host host;
    host.BringUp();
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    host.slowDivisor = 4;

    host.Frame(1);                           // skip
    host.input.keys[kKeyLP] = true;
    host.Frame(1);                           // skip -- Note captures the edge
    host.input.keys[kKeyLP] = false;
    host.Frame(1);                           // skip -- tap fully over
    host.Frame(1);                           // RUN -- Spend delivers the pulse

    EXPECT_EQ(host.MoveNow(), host.lpSlot)
        << "the tap was made and released between run ticks and the kernel "
           "never started stand_lp: the press did not arrive.";

    // And the record says so: the delivered bit is IN the latched input for
    // the tick that ran, which is what keeps replay and rollback honest.
    const cse::game::InputSample latched = host.local.At(0);
    ASSERT_TRUE(latched.authored);
    EXPECT_EQ(latched.input.bits & cse::kernel::kInputLP, cse::kernel::kInputLP);

    // The pulse is one tick wide: the next run tick reads the true level
    // (nothing held), so the kernel sees a release, not a phantom hold.
    host.Frame(1);
    host.Frame(1);
    host.Frame(1);
    host.Frame(1);                           // second RUN
    const cse::game::InputSample after = host.local.At(1);
    ASSERT_TRUE(after.authored);
    EXPECT_EQ(after.input.bits & cse::kernel::kInputLP, 0u);
}

// ============================================================================
// 4. The Done-when, part two: any render rate the harness can simulate
// ============================================================================

// Above the fixed rate most render frames run ZERO fixed steps, and a tap can
// begin and end inside them. The InputMap latch carries it across those
// frames (the Application keeps a latch alive exactly while a tick is owed
// and none has run -- pinned in test_input_map.cpp), and the accumulator
// carries it from the fixed step that consumes it to the tick that runs.
TEST(PressDelivery, ATapInsideZeroTickFramesIsDeliveredWhenTheNextTickRuns) {
    Host host;
    host.BringUp();
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    host.input.keys[kKeyLP] = true;
    host.Frame(0);                           // 144fps frame, no tick owed yet
    host.input.keys[kKeyLP] = false;
    host.Frame(0);                           // released before any step ran
    host.Frame(1);                           // the tick arrives

    EXPECT_EQ(host.MoveNow(), host.lpSlot)
        << "a tap inside zero-tick render frames never reached the kernel; "
           "either the InputMap latch was retired early or nothing consumed "
           "it into the accumulator.";
}

// ============================================================================
// 5. Frame-step: a tap made while paused comes out on the step
// ============================================================================

// This is what stepping one tick at a time is FOR: press the button while the
// match is frozen, step, and the move comes out on the stepped tick. Before
// M1.3h the paused steps sampled nothing and the tap vanished.
TEST(PressDelivery, ATapWhilePausedComesOutOnTheFrameStep) {
    Host host;
    host.BringUp();
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    host.paused = true;

    host.input.keys[kKeyLP] = true;
    host.Frame(1);                           // paused step: noted, not run
    host.input.keys[kKeyLP] = false;
    host.Frame(1);                           // still paused, tap over

    host.pendingSteps = 1;                   // the step key
    host.Frame(1);                           // the ONE stepped tick

    EXPECT_EQ(host.MoveNow(), host.lpSlot)
        << "the tap made during pause did not come out on the frame step";
}

// ============================================================================
// 6. What the fix must NOT do
// ============================================================================

// A held button starts a move ONCE (review point R0's finding, readPad_'s
// essay). The accumulator notes the press edge on the same step the level
// read already carries it, and the OR must change nothing: one start, then a
// hold the kernel correctly ignores when the move runs out.
TEST(PressDelivery, AHeldButtonStillStartsExactlyOneMove) {
    Host host;
    host.BringUp();
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    host.input.keys[kKeyLP] = true;
    for (int i = 0; i < 30; ++i) host.Frame(1);   // held across stand_lp's
                                                  // whole 14-tick life twice over
    EXPECT_EQ(host.moveStarts, 1)
        << "holding the button restarted the move; the accumulator turned a "
           "hold into repeated presses.";
    EXPECT_EQ(host.MoveNow(), 0u)
        << "stand_lp is 14 ticks and 30 ran; the fighter should be idle under "
           "a held button.";
}

// ============================================================================
// 7. The record replays
// ============================================================================

// Delivery happens BEFORE the latch, so the latched log IS the match. A
// second session fed the same log, tick for tick, lands on the same bytes --
// the property every downstream guarantee (replay, rollback, the desync
// checksum) rests on, asserted over a drive that used every delivery path
// above: slow motion, a paused tap, a frame step and plain running.
TEST(PressDelivery, TheDeliveredTapsAreInTheRecordAndReplayBitIdentically) {
    Host host;
    host.BringUp();
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    host.slowDivisor = 2;
    host.input.keys[kKeyLP] = true;
    host.Frame(1);                           // skip: noted
    host.input.keys[kKeyLP] = false;
    host.Frame(1);                           // run: delivered
    host.Frame(1);                           // skip
    host.Frame(1);                           // run
    host.paused = true;
    host.input.keys[kKeyLP] = true;
    host.Frame(1);                           // paused: noted
    host.input.keys[kKeyLP] = false;
    host.pendingSteps = 1;
    host.Frame(1);                           // stepped: delivered
    host.paused      = false;
    host.slowDivisor = 1;
    for (int i = 0; i < 20; ++i) host.Frame(1);

    const std::uint32_t ticks = host.local.NextTick();
    ASSERT_GT(ticks, 0u);

    FightSetup setup{};
    setup.data = &host.build.data;
    FightSession replay{};
    std::string  error;
    ASSERT_TRUE(replay.Begin(setup, error)) << error;
    replay.SetInputSource(0, &host.local);
    for (std::uint32_t t = 0; t < ticks; ++t) replay.Tick();

    EXPECT_EQ(std::memcmp(&replay.State(), &host.session.State(),
                          sizeof(cse::kernel::GameState)), 0)
        << "the same latched log did not replay to the same bytes; something "
           "was delivered outside the record.";
}
