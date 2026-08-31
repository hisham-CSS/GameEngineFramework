// The chains, actually performed.
//
// Until this file existed, the sentence "MatchBuilder drops 134 cancels for Kung
// Fu Girl" was the honest summary of the whole bridge, and it meant something
// specific and bad: the combo prover's verdict is a statement about paths through
// her cancel graph, and not one edge of that graph could be walked by the running
// game. A green build certified a character nobody could play.
//
// This file is the evidence that the graph is now walkable, and it is arranged so
// that each claim fails for exactly one reason:
//
//   1. A REAL COMBO. Kung Fu Girl's stand_lp connects and is cancelled into her
//      stand_mp six ticks before stand_lp would have released her. Both hits land
//      on the same defender. The numbers -- 23 damage, 64 damage, an 11-tick move
//      cancelled on frame 5 -- were read out of Exported/Characters/
//      kung_fu_girl.json, not copied from a summary.
//   2. THE WINDOW IS A WINDOW. Too early fails, too late fails, and both ends are
//      asserted twice: once through the tick, driving inputs, and once directly
//      against cse::kernel::CancelIsOpen, so that a window bug and a binding bug
//      cannot be confused for one another.
//   3. CONTACT IS REQUIRED. The same inputs at a distance where the source WHIFFS
//      do not produce the follow-up -- and the whiffing move is asserted to have
//      really run and really had a live box, because "nothing happened" is the
//      easiest green in the world to obtain by accident.
//   4. DETERMINISM SURVIVES IT. The whole combo, twice, byte-identical; and
//      re-simulated from a snapshot taken between the hit and the cancel, which is
//      the one instant where the state carries a fact (alreadyHitBits) that the
//      next tick's decision depends on.
//   5. THE COUNT IS THE FILE'S. Her 134 authored edges, all 134 carried, and a
//      loss table whose every cancel entry is recomputed here from the loaded
//      CharacterData rather than believed.
//
// THE GUARD THAT MAKES ALL OF IT MEAN SOMETHING. Several tests below run the
// identical script against a MatchData whose cancel table has been emptied and
// require a DIFFERENT outcome. Without that control, a kernel that started
// stand_mp for some unrelated reason would pass every assertion here.
#include <gtest/gtest.h>

#include "cse/data/CharacterData.h"
#include "cse/data/MatchBuilder.h"
#include "cse/kernel/Simulate.h"

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace cse::data;
using cse::kernel::CancelEdge;
using cse::kernel::Fighter;
using cse::kernel::FighterData;
using cse::kernel::GameState;
using cse::kernel::InputPair;
using cse::kernel::MatchData;

namespace {

// Authored in pixels and converted once, here, matching test_combat.cpp and
// test_match_bridge.cpp.
constexpr std::int32_t px(std::int32_t pixels) {
    return pixels * cse::kernel::kSubUnitsPerPixel;
}

// Kung Fu Girl's transcribed body: ground.front/ground.back 13 px, height 60 px.
constexpr std::int32_t kHalfWidth = px(13);
constexpr std::int32_t kHeight    = px(60);

// The build-wide resource order. Positional, so index 0 must mean the same
// resource in every file a build loads (CharacterData.h, ResourceDef).
const std::vector<std::string> kBuildResources = { "meter", "juggle" };

LoadOptions loadOptions() {
    LoadOptions o;
    o.expectedResources = kBuildResources;
    return o;
}

// Mirrors test_match_bridge.cpp. The walk-up fallback keeps the test runnable
// from a shell anywhere in the tree, and it cannot make anything pass vacuously
// because every load below ASSERTs.
std::string charactersDir() {
#ifdef CSE_CHARACTERS_DIR
    return CSE_CHARACTERS_DIR;
#else
    namespace fs = std::filesystem;
    fs::path here = fs::current_path();
    for (int i = 0; i < 8; ++i) {
        // The PHASE 0 CORPUS, which is test evidence rather than game content.
        // It lives in tests/fixtures/characters and is deliberately NOT staged
        // next to any executable -- see the README there for why transcriptions
        // of third-party MUGEN characters cannot ship. Walking up from the
        // current directory keeps this runnable from a build tree or a shell.
        const fs::path candidate = here / "tests" / "fixtures" / "characters";
        if (fs::exists(candidate / "kung_fu_girl.json")) return candidate.string();
        if (!here.has_parent_path() || here.parent_path() == here) break;
        here = here.parent_path();
    }
    return "tests/fixtures/characters";
#endif
}

void loadShipped(const char* file, CharacterData& out) {
    LoadReport report{};
    ASSERT_TRUE(LoadCharacterFile(charactersDir(), file, loadOptions(), out, report))
        << file << " failed to load: " << report.error;
    ASSERT_FALSE(out.moves.empty()) << file << " loaded with no moves";
}

BodySpec standardBody() {
    BodySpec b{};
    b.halfWidthSub = kHalfWidth;
    b.heightSub    = kHeight;
    return b;
}

MoveBinding bind(const char* id, std::uint16_t button) {
    MoveBinding b{};
    b.moveId = id;
    b.button = button;
    return b;
}

BuildOptions optionsWith(std::vector<MoveBinding> bindings) {
    BuildOptions o{};
    o.body     = standardBody();
    o.bindings = std::move(bindings);
    return o;
}

const BuildLoss* findLoss(const BuildReport& report, const char* field) {
    for (const BuildLoss& loss : report.losses)
        if (loss.field == field) return &loss;
    return nullptr;
}

// The edge from one kernel move slot to another, or null. Kung Fu Girl authors
// four DUPLICATE (from, to) pairs -- stand_lp reaches palm both on hit and on
// block -- so this returns the FIRST, which is also the one FindCancel would
// take, and the tests below only use it on pairs that are unique.
const CancelEdge* findEdge(const FighterData& d, std::uint16_t from, std::uint16_t to) {
    for (std::int32_t i = 0; i < d.cancelCount; ++i)
        if (d.cancels[i].from == from && d.cancels[i].to == to) return &d.cancels[i];
    return nullptr;
}

InputPair inputs(std::uint16_t p0Bits, std::uint16_t p1Bits) {
    InputPair in{};
    in.p[0].bits = p0Bits;
    in.p[1].bits = p1Bits;
    return in;
}

// --- Driving one attacker against a stationary defender ----------------------

// What a run did, per tick, observed from the states themselves. p1 is given no
// input at any point, so it never moves and never attacks: every number below is
// a consequence of p0's script and of the character data, which is what lets a
// single-tick assertion mean what it says.
struct Replay {
    GameState state{};
    std::vector<std::uint16_t> moveIdAt;         // p0's moveId at the END of tick i
    std::vector<std::uint16_t> moveFrameAt;
    std::vector<std::int32_t>  defenderHealthAt;

    // A cancel is not "the move id changed". A move that ends naturally and is
    // immediately restarted by a held button also changes the id, also lands on
    // frame 0, and is a LINK -- the thing the kernel could already do before this
    // feature existed. So this counts only transitions where the SOURCE STILL HAD
    // FRAMES LEFT, which is the whole claim: the move was interrupted.
    int cancelsTaken = 0;

    // The tick each cancel happened on, so a test can name one rather than count.
    std::vector<int> cancelTicks;
};

Replay drive(const MatchData& data, std::int32_t p0Sub, std::int32_t p1Sub,
          const std::vector<std::uint16_t>& p0Bits) {
    Replay r{};
    cse::kernel::ResetMatch(r.state, 0xA11CEu);
    r.state.p[0].posX = p0Sub;
    r.state.p[1].posX = p1Sub;

    for (std::size_t t = 0; t < p0Bits.size(); ++t) {
        const std::uint16_t beforeId    = r.state.p[0].moveId;
        const std::uint16_t beforeFrame = r.state.p[0].moveFrame;

        cse::kernel::Simulate(r.state, inputs(p0Bits[t], 0u), data);

        const std::uint16_t afterId = r.state.p[0].moveId;
        if (beforeId != 0 && afterId != 0 && afterId != beforeId) {
            const cse::kernel::MoveDef* src = cse::kernel::MoveAt(data.p[0], beforeId);
            // beforeFrame + 1 is the frame the source WOULD have reached. If that
            // is still inside the move, the move did not end -- it was cancelled.
            if (src != nullptr &&
                static_cast<std::int32_t>(beforeFrame) + 1 < cse::kernel::MoveDuration(*src)) {
                ++r.cancelsTaken;
                r.cancelTicks.push_back(static_cast<int>(t));
            }
        }

        r.moveIdAt.push_back(afterId);
        r.moveFrameAt.push_back(r.state.p[0].moveFrame);
        r.defenderHealthAt.push_back(r.state.p[1].health);
    }
    return r;
}

// THE CONTROL. The same character with its cancel graph deleted and nothing else
// touched. Every "the cancel did it" claim below is paired with a run against
// this, because a follow-up that would have started anyway proves nothing.
MatchData withoutCancels(const MatchData& data) {
    MatchData copy = data;
    copy.p[0].cancelCount = 0;
    copy.p[1].cancelCount = 0;
    return copy;
}

// --- Kung Fu Girl, bound for one chain ---------------------------------------

struct Kfg {
    CharacterData character;
    MatchBuild    build;
    std::uint16_t lp = 0;
    std::uint16_t mp = 0;
};

// Only two moves are bound, and that is deliberate. Her file authors eleven
// outgoing edges from stand_lp, the FIRST of which is stand_lp into itself;
// FindCancel takes the first edge whose target's buttons are held, so binding
// only stand_lp and stand_mp makes the edge under test the only one reachable
// and the assertion below about WHICH move started a real assertion.
void buildKfg(Kfg& out) {
    loadShipped("kung_fu_girl.json", out.character);
    if (::testing::Test::HasFatalFailure()) return;

    const BuildOptions o = optionsWith({ bind("stand_lp", cse::kernel::kInputLP),
                                         bind("stand_mp", cse::kernel::kInputMP) });
    ASSERT_TRUE(BuildMatchData(out.character, o, out.character, o, out.build))
        << "p0: " << out.build.report[0].error << " / p1: " << out.build.report[1].error;

    out.lp = out.build.moves[0].Find("stand_lp");
    out.mp = out.build.moves[0].Find("stand_mp");
    ASSERT_NE(0u, out.lp);
    ASSERT_NE(0u, out.mp);
}

// stand_lp reaches 57 px and stand_mp reaches 54 px, and stand_lp KNOCKS THE
// DEFENDER BACK 13 px. Those three numbers are hers, not tuned until this
// worked, and together they say the gap has to leave room for the recoil: the
// push decays by halves so it carries the defender about 23 px in total, and
// stand_mp has to still be reaching when it lands.
//
// 20 px does it with margin -- 43 px when the follow-up connects, against a
// 54 px reach. The 50 px this used to be had FOUR pixels of margin and was
// calibrated when the bridge dropped pushback, so nobody was ever moved; wiring
// it (ROADMAP M1.3d) turned that margin into a miss and the second hit of the
// chain simply stopped landing.
//
// The lesson is a real one about the genre rather than about the harness: a
// light into a medium is not confirmable at maximum range, because the light's
// own knockback takes the defender out of the medium's. If either reach or the
// pushback moves, this test should fail rather than quietly test one hit.
constexpr std::int32_t kComboGap = px(20);
std::int32_t comboDefenderAt() { return kComboGap + 2 * kHalfWidth; }

// LP on tick 0, MP on tick 5, nothing else ever. One tick of each, not a hold:
// a held button repeats and would make the assertions depend on run length.
std::vector<std::uint16_t> comboScript(std::size_t ticks) {
    std::vector<std::uint16_t> s(ticks, 0u);
    s[0] = cse::kernel::kInputLP;
    s[5] = cse::kernel::kInputMP;
    return s;
}

// --- A synthetic character whose cancel window CLOSES EARLY -------------------
//
// Every authored cancel window in the shipped data closes on the move's last
// frame, so "too late to cancel" and "the move ended" are the same tick and the
// closing bound cannot be tested against real data at all. This character exists
// to separate them: move `a` runs for 24 ticks and its window shuts on frame 10,
// leaving thirteen frames in which the follow-up button is held, the source is
// still running, and nothing may happen.
Move synthMove(const char* id, std::int32_t startup, std::int32_t active,
               std::int32_t recovery, std::int32_t hitstunTicks,
               std::int32_t damagePoints, std::int32_t reachPixels) {
    Move m{};
    m.id               = id;
    m.startup          = startup;
    m.active           = active;
    m.recovery         = recovery;
    m.hitstun          = hitstunTicks;
    m.damageHundredths = damagePoints * 100;
    m.reachSub         = px(reachPixels);
    return m;
}

Cancel synthCancel(const CharacterData& c, const char* from, const char* to,
                   std::int32_t delay, Contact on) {
    Cancel e{};
    e.from  = c.FindMove(from);
    e.to    = c.FindMove(to);
    e.delay = delay;
    e.on    = on;
    return e;
}

constexpr std::int32_t kWindowOpen  = 6;
constexpr std::int32_t kWindowClose = 10;
constexpr std::int32_t kSourceTicks = 2 + 2 + 20;   // startup + active + recovery

CharacterData windowCharacter() {
    CharacterData c{};
    c.id   = "window_dummy";
    c.name = "Window Dummy";

    Move a = synthMove("a", 2, 2, 20, 40, 10, 50);
    a.hasCancelWindow   = true;
    a.cancelWindowOpen  = kWindowOpen;
    a.cancelWindowClose = kWindowClose;

    c.moves.push_back(a);
    c.moves.push_back(synthMove("b", 2, 2, 5, 20, 20, 50));
    c.RebuildIndices();

    // delay 0, so the window's own open (6) is what decides, and the test is
    // about the window rather than about the arithmetic that produced it.
    c.cancels.push_back(synthCancel(c, "a", "b", 0, Contact::Hit));
    c.RebuildIndices();
    return c;
}

MatchBuild buildWindowCharacter(const CharacterData& c) {
    MatchBuild build{};
    const BuildOptions o = optionsWith({ bind("a", cse::kernel::kInputLP),
                                         bind("b", cse::kernel::kInputMP) });
    EXPECT_TRUE(BuildMatchData(c, o, c, o, build)) << build.report[0].error;
    return build;
}

// Close enough that `a` (50 px of reach) connects, so the on-hit gate is
// satisfied and the window is the only thing left to fail.
std::int32_t windowDefenderAt() { return px(20) + 2 * kHalfWidth; }

} // namespace

// ============================================================================
// 1. A two-move combo, actually performed
// ============================================================================

TEST(CancelCombo, StandLpIsCancelledIntoStandMpAndBothHitsLand) {
    Kfg k{};
    buildKfg(k);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    const FighterData& d = k.build.data.p[0];

    // THE EDGE, AS THE BRIDGE RESOLVED IT. Her file says stand_lp cancels into
    // stand_mp after a delay of 2 ticks on hit, and stand_lp's authored cancel
    // window is [5, 10]. stand_lp starts up in 3, so the delay resolves to frame
    // 5, the window opens on 5, and the later of the two wins. The close is the
    // window's own 10, which is also stand_lp's last frame (3 + 2 + 6 = 11).
    const CancelEdge* edge = findEdge(d, k.lp, k.mp);
    ASSERT_NE(nullptr, edge) << "the stand_lp -> stand_mp edge did not survive the build";
    EXPECT_EQ(5, edge->earliestFrame);
    EXPECT_EQ(10, edge->latestFrame);
    EXPECT_EQ(1u, edge->onHit) << "her file authors this edge `on: hit`";

    const std::int32_t lpDuration = cse::kernel::MoveDuration(d.moves[k.lp]);
    ASSERT_EQ(11, lpDuration)
        << "stand_lp is 3 + 2 + 6 ticks in her file; the early-start claim below "
           "is measured against this number";

    const Replay r = drive(k.build.data, 0, comboDefenderAt(), comboScript(25));

    // Tick 3 is stand_lp's first active frame, and the hit lands there.
    EXPECT_EQ(1000 - 23, r.defenderHealthAt[3])
        << "stand_lp (23 damage) did not connect at a 50 px gap";

    // Tick 4: the fighter has connected but the window has not opened. This is
    // the "too early" case appearing inside the successful run, which is worth
    // more than the same assertion in isolation -- it proves the window is what
    // held the cancel back rather than the input.
    EXPECT_EQ(k.lp, r.moveIdAt[4]);
    EXPECT_EQ(4u, r.moveFrameAt[4]);

    // Tick 5: the cancel. moveFrame resets, because it is a new move.
    EXPECT_EQ(k.mp, r.moveIdAt[5]) << "stand_lp was not cancelled into stand_mp";
    EXPECT_EQ(0u, r.moveFrameAt[5]);
    EXPECT_EQ(1, r.cancelsTaken);
    ASSERT_EQ(1u, r.cancelTicks.size());
    EXPECT_EQ(5, r.cancelTicks[0]);

    // EARLY, and by how much. stand_lp holds the fighter for 11 ticks, so the
    // earliest an uncancelled stand_mp could have begun is tick 11.
    EXPECT_LT(r.cancelTicks[0], lpDuration)
        << "the follow-up started no earlier than stand_lp's own recovery would "
           "have allowed, which is a LINK and not a cancel";

    // Tick 12: stand_mp's first active frame (7 startup from tick 5), and it
    // connects on a defender the previous move already hit -- which it can only
    // do because the cancel cleared alreadyHitBits.
    EXPECT_EQ(1000 - 23 - 64, r.defenderHealthAt[12])
        << "the follow-up did not land. If health is 977, the multi-hit guard "
           "carried over from stand_lp and the new active window was born spent.";
    EXPECT_EQ(1000 - 23 - 64, r.state.p[1].health);
    EXPECT_GT(r.state.p[1].hitstun, 0u);

    // THE CONTROL. Identical inputs, identical character, cancel graph deleted.
    const MatchData none = withoutCancels(k.build.data);
    const Replay flat = drive(none, 0, comboDefenderAt(), comboScript(25));
    EXPECT_EQ(0, flat.cancelsTaken);
    EXPECT_EQ(k.lp, flat.moveIdAt[5])
        << "with no cancel table the fighter still left stand_lp on tick 5, so "
           "the combo above was not the cancel system doing anything";
    EXPECT_EQ(1000 - 23, flat.state.p[1].health)
        << "the follow-up landed without a cancel edge to reach it through";
}

TEST(CancelCombo, TheSameChainAsALinkArrivesSixTicksLater) {
    // The cancel's value stated as a number rather than as an adjective. Holding
    // MP from tick 11 -- the first tick stand_lp releases the fighter -- reaches
    // the same stand_mp by the ordinary button start, and its hit lands on tick
    // 18 instead of tick 12.
    Kfg k{};
    buildKfg(k);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    std::vector<std::uint16_t> link(25, 0u);
    link[0] = cse::kernel::kInputLP;
    for (std::size_t t = 11; t < link.size(); ++t) link[t] = cse::kernel::kInputMP;

    const Replay r = drive(k.build.data, 0, comboDefenderAt(), link);
    EXPECT_EQ(0, r.cancelsTaken)
        << "MP is not held until tick 11 and the window shuts on frame 10, so "
           "nothing here may be a cancel";
    EXPECT_EQ(k.mp, r.moveIdAt[11]);
    EXPECT_EQ(0u, r.moveFrameAt[11]);
    EXPECT_EQ(1000 - 23,      r.defenderHealthAt[12]);   // the cancel had landed by here
    EXPECT_EQ(1000 - 23 - 64, r.defenderHealthAt[18]);   // the link lands six ticks later
}

// ============================================================================
// 2. The window is a window
// ============================================================================

TEST(CancelWindow, TooEarlyAndTooLateBothFailAgainstTheTickItself) {
    const CharacterData c = windowCharacter();
    const MatchBuild build = buildWindowCharacter(c);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    const std::uint16_t a = build.moves[0].Find("a");
    const std::uint16_t b = build.moves[0].Find("b");
    ASSERT_NE(0u, a);
    ASSERT_NE(0u, b);

    const CancelEdge* edge = findEdge(build.data.p[0], a, b);
    ASSERT_NE(nullptr, edge);
    EXPECT_EQ(kWindowOpen,  edge->earliestFrame);
    EXPECT_EQ(kWindowClose, edge->latestFrame);
    ASSERT_LT(kWindowClose, kSourceTicks - 1)
        << "this character exists to have a window that shuts before the move "
           "does; if it no longer does, `too late` below is unreachable";

    // One press on one tick, so the tick the press lands on is the only variable.
    const auto pressAt = [&](std::size_t tick, std::size_t ticks) {
        std::vector<std::uint16_t> s(ticks, 0u);
        s[0]    = cse::kernel::kInputLP;
        s[tick] = cse::kernel::kInputMP;
        return drive(build.data, 0, windowDefenderAt(), s);
    };

    // The move connects on tick 2 (startup 2), so contact is satisfied from tick
    // 3 onward and every failure below is the window's doing.
    {
        std::vector<std::uint16_t> lpOnly(30, 0u);
        lpOnly[0] = cse::kernel::kInputLP;
        const Replay contact = drive(build.data, 0, windowDefenderAt(), lpOnly);
        ASSERT_EQ(1000 - 10, contact.defenderHealthAt[2])
            << "`a` did not connect, so nothing below is testing a window";
    }

    const Replay early = pressAt(kWindowOpen - 1, 30);
    EXPECT_EQ(0, early.cancelsTaken);
    EXPECT_EQ(a, early.moveIdAt[kWindowOpen - 1])
        << "the cancel fired one tick before its window opened";

    const Replay onOpen = pressAt(kWindowOpen, 30);
    EXPECT_EQ(1, onOpen.cancelsTaken);
    EXPECT_EQ(b, onOpen.moveIdAt[kWindowOpen])
        << "the cancel did not fire on the first tick of its window, so the "
           "`too early` result above may just be a window that never opens";

    const Replay onClose = pressAt(kWindowClose, 30);
    EXPECT_EQ(1, onClose.cancelsTaken);
    EXPECT_EQ(b, onClose.moveIdAt[kWindowClose])
        << "the last tick of the window is inclusive and did not work";

    const Replay late = pressAt(kWindowClose + 1, 30);
    EXPECT_EQ(0, late.cancelsTaken);
    EXPECT_EQ(a, late.moveIdAt[kWindowClose + 1])
        << "the cancel fired one tick after its window shut";

    // AND THE PRESS WAS REAL. A `too late` press that is simply dropped forever
    // is indistinguishable from a binding that does not work, so press the button
    // after the window shuts and require the follow-up to arrive -- as a LINK, on
    // the tick `a` releases the fighter.
    //
    // THIS USED TO HOLD THE BUTTON and rely on the hold being retried every tick,
    // which stopped being true when a press became an edge (ROADMAP M1.1d). The
    // mechanism it needs is the one M1.1d adds for exactly this: a press that
    // arrives while the fighter cannot act is BUFFERED and spent the tick they
    // can. So the character authors a window long enough to span the wait, and
    // the test now demonstrates the buffer instead of depending on rapid-fire.
    cse::kernel::MatchData buffered = build.data;
    buffered.p[0].inputBufferFrames = kSourceTicks;

    std::vector<std::uint16_t> held(kSourceTicks + 2, 0u);
    held[0] = cse::kernel::kInputLP;
    held[kWindowClose + 1] = cse::kernel::kInputMP;   // one press, too late

    const Replay heldRun = drive(buffered, 0, windowDefenderAt(), held);
    EXPECT_EQ(0, heldRun.cancelsTaken);
    EXPECT_EQ(a, heldRun.moveIdAt[kSourceTicks - 1])
        << "`a` left early even though its window was shut when the press "
           "arrived";
    EXPECT_EQ(b, heldRun.moveIdAt[kSourceTicks])
        << "the buffered press never produced `b` at all, so `too late` above "
           "was not the window refusing -- it was the input never arriving";
    EXPECT_EQ(0u, heldRun.moveFrameAt[kSourceTicks]);
}

TEST(CancelWindow, TheKernelPredicateAgreesWithTheTickAtEveryBoundary) {
    // The same four boundaries again, asked of cse::kernel::CancelIsOpen with a
    // Fighter assembled by hand. Driving inputs proves the whole path works;
    // this proves WHICH half of the path refused, which is the difference
    // between a window off by one and a button that is not bound.
    const CharacterData c = windowCharacter();
    const MatchBuild build = buildWindowCharacter(c);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    const std::uint16_t a = build.moves[0].Find("a");
    const CancelEdge* edge = findEdge(build.data.p[0], a, build.moves[0].Find("b"));
    ASSERT_NE(nullptr, edge);

    Fighter f{};
    f.moveId         = a;
    f.alreadyHitBits = 0x2;      // connected on slot 1, which is the other fighter

    f.moveFrame = static_cast<std::uint16_t>(kWindowOpen - 1);
    EXPECT_FALSE(cse::kernel::CancelIsOpen(f, *edge));
    f.moveFrame = static_cast<std::uint16_t>(kWindowOpen);
    EXPECT_TRUE(cse::kernel::CancelIsOpen(f, *edge));
    f.moveFrame = static_cast<std::uint16_t>(kWindowClose);
    EXPECT_TRUE(cse::kernel::CancelIsOpen(f, *edge)) << "the close bound is inclusive";
    f.moveFrame = static_cast<std::uint16_t>(kWindowClose + 1);
    EXPECT_FALSE(cse::kernel::CancelIsOpen(f, *edge));

    // An edge out of a move the fighter is not performing, and out of no move at
    // all. Both are ways a scan over a shared table goes wrong.
    f.moveFrame = static_cast<std::uint16_t>(kWindowOpen);
    f.moveId    = static_cast<std::uint16_t>(a + 1);
    EXPECT_FALSE(cse::kernel::CancelIsOpen(f, *edge));
    f.moveId = 0;
    EXPECT_FALSE(cse::kernel::CancelIsOpen(f, *edge));
}

// ============================================================================
// 3. Contact is required
// ============================================================================

TEST(CancelContact, AnOnHitEdgeDoesNotFireOnAWhiff) {
    Kfg k{};
    buildKfg(k);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    // Far enough that stand_lp (57 px of reach) cannot connect, and nothing else
    // about the run changes: same script, same character, same bindings.
    const std::int32_t farAway = px(300) + 2 * kHalfWidth;
    const Replay whiff = drive(k.build.data, 0, farAway, comboScript(25));

    EXPECT_EQ(1000, whiff.defenderHealthAt[24]) << "the `whiff` connected";
    EXPECT_EQ(0, whiff.cancelsTaken);
    EXPECT_EQ(k.lp, whiff.moveIdAt[5])
        << "the on-hit cancel fired even though the source never connected";

    // NON-VACUITY, TWO WAYS.
    //
    // First: the move really ran and really had a live box. "No cancel occurred"
    // is also what a move that never started looks like.
    EXPECT_EQ(k.lp, whiff.moveIdAt[3]);
    EXPECT_EQ(3u, whiff.moveFrameAt[3]);
    EXPECT_EQ(0u, whiff.state.p[0].alreadyHitBits);
    {
        GameState mid{};
        cse::kernel::ResetMatch(mid, 1u);
        mid.p[0].posX = 0;
        mid.p[1].posX = farAway;
        for (int t = 0; t <= 3; ++t)
            cse::kernel::Simulate(mid, inputs(t == 0 ? cse::kernel::kInputLP : 0u, 0u),
                                  k.build.data);
        cse::kernel::Box live{};
        ASSERT_TRUE(cse::kernel::ActiveHitbox(k.build.data.p[0], mid.p[0], live))
            << "stand_lp had no live hitbox on its first active frame, so its "
               "failure to connect proves nothing about contact";
    }

    // Second: the identical script at a distance where it DOES connect produces
    // the cancel. Same inputs, same table -- only the gap differs.
    const Replay hit = drive(k.build.data, 0, comboDefenderAt(), comboScript(25));
    EXPECT_EQ(1, hit.cancelsTaken)
        << "the on-hit edge did not fire even on a hit, so the whiff result "
           "above is about a broken edge rather than about contact";
}

TEST(CancelContact, AlreadyHitBitsIsTheContactRecordAndAClearedOneRefuses) {
    // The gate stated at the kernel level, because the whiff test above changes
    // a distance and this one changes exactly one byte of state.
    const CharacterData c = windowCharacter();
    const MatchBuild build = buildWindowCharacter(c);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    const CancelEdge* edge = findEdge(build.data.p[0], build.moves[0].Find("a"),
                                      build.moves[0].Find("b"));
    ASSERT_NE(nullptr, edge);
    ASSERT_EQ(1u, edge->onHit);

    Fighter f{};
    f.moveId    = build.moves[0].Find("a");
    f.moveFrame = static_cast<std::uint16_t>(kWindowOpen);

    f.alreadyHitBits = 0;
    EXPECT_FALSE(cse::kernel::CancelIsOpen(f, *edge));
    f.alreadyHitBits = 0x2;
    EXPECT_TRUE(cse::kernel::CancelIsOpen(f, *edge));
}

// ============================================================================
// 4. Determinism survives the whole thing
// ============================================================================

TEST(CancelDeterminism, TheComboIsBitIdenticalAcrossRunsAndAcrossARollback) {
    Kfg k{};
    buildKfg(k);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    const auto script = comboScript(30);
    const std::int32_t p1At = comboDefenderAt();

    const Replay a = drive(k.build.data, 0, p1At, script);
    const Replay b = drive(k.build.data, 0, p1At, script);
    EXPECT_EQ(0, std::memcmp(&a.state, &b.state, sizeof(GameState)))
        << "two runs of the same combo diverged";
    EXPECT_EQ(cse::kernel::Checksum(a.state), cse::kernel::Checksum(b.state));

    // THE INTERESTING SNAPSHOT. Tick 3 is the tick stand_lp connects, so the
    // saved state carries alreadyHitBits set and moveFrame 3 -- and the decision
    // the next few ticks make depends on both. A cancel system that kept the
    // contact fact anywhere other than GameState would reproduce the straight run
    // by luck here and fail on the remote peer.
    GameState straight{};
    cse::kernel::ResetMatch(straight, 0xA11CEu);
    straight.p[0].posX = 0;
    straight.p[1].posX = p1At;
    for (std::size_t t = 0; t < script.size(); ++t)
        cse::kernel::Simulate(straight, inputs(script[t], 0u), k.build.data);

    GameState mid{};
    cse::kernel::ResetMatch(mid, 0xA11CEu);
    mid.p[0].posX = 0;
    mid.p[1].posX = p1At;
    for (std::size_t t = 0; t < 4; ++t)
        cse::kernel::Simulate(mid, inputs(script[t], 0u), k.build.data);

    ASSERT_NE(0u, mid.p[0].alreadyHitBits)
        << "the snapshot was taken before the hit, so it does not carry the fact "
           "the cancel depends on and this test is not about a cancel";

    const GameState snapshot = mid;             // the snapshot is a copy
    GameState resimmed = snapshot;
    for (std::size_t t = 4; t < script.size(); ++t)
        cse::kernel::Simulate(resimmed, inputs(script[t], 0u), k.build.data);

    EXPECT_EQ(0, std::memcmp(&straight, &resimmed, sizeof(GameState)))
        << "re-simulating from a snapshot taken between the hit and the cancel "
           "did not reproduce the straight-through run";
    EXPECT_EQ(cse::kernel::Checksum(straight), cse::kernel::Checksum(resimmed));

    // Every rewind depth up to D4's 8-tick budget, so an off-by-one that only
    // shows at one depth cannot hide.
    for (std::size_t depth = 1; depth <= 8; ++depth) {
        GameState rewound{};
        cse::kernel::ResetMatch(rewound, 0xA11CEu);
        rewound.p[0].posX = 0;
        rewound.p[1].posX = p1At;
        for (std::size_t t = 0; t + depth < script.size(); ++t)
            cse::kernel::Simulate(rewound, inputs(script[t], 0u), k.build.data);
        for (std::size_t t = script.size() - depth; t < script.size(); ++t)
            cse::kernel::Simulate(rewound, inputs(script[t], 0u), k.build.data);
        EXPECT_EQ(0, std::memcmp(&straight, &rewound, sizeof(GameState)))
            << "rewind depth " << depth << " diverged";
    }

    // NON-VACUITY. All of the above is trivially true of a run in which nothing
    // happens, and specifically of one in which no cancel ever fires.
    EXPECT_EQ(1, a.cancelsTaken);
    EXPECT_EQ(1000 - 23 - 64, a.state.p[1].health);

    const Replay flat = drive(withoutCancels(k.build.data), 0, p1At, script);
    EXPECT_NE(0, std::memcmp(&a.state, &flat.state, sizeof(GameState)))
        << "the combo run is byte-identical to the same run with the cancel table "
           "emptied, so nothing in this test involved a cancel";
}

// ============================================================================
// 5. Her real edges, and what the table now says they cost
// ============================================================================

TEST(CancelBridge, EveryOneOfKungFuGirlsAuthoredEdgesSurvivesTheMapping) {
    Kfg k{};
    buildKfg(k);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    // 134 edges in kung_fu_girl.json. Asserted against the LOADED character as
    // well as against the literal, so that a loader that started dropping edges
    // is caught here rather than silently agreeing with a stale constant.
    ASSERT_EQ(134u, k.character.cancels.size())
        << "kung_fu_girl.json no longer authors 134 cancel edges; every count in "
           "this file was read out of that file and needs re-reading";

    EXPECT_EQ(134, k.build.data.p[0].cancelCount);
    EXPECT_EQ(134, k.build.moves[0].cancelCount);
    ASSERT_EQ(134u, k.build.moves[0].fileCancelByEdge.size());

    // Nothing was dropped, so the map is the identity -- and asserting the map
    // rather than only the count is what would catch a build that carried 134
    // edges belonging to the wrong pairs.
    for (std::size_t i = 0; i < k.build.moves[0].fileCancelByEdge.size(); ++i) {
        SCOPED_TRACE(i);
        ASSERT_EQ(static_cast<CancelIndex>(i), k.build.moves[0].fileCancelByEdge[i]);

        const Cancel& src = k.character.cancels[i];
        const CancelEdge& built = k.build.data.p[0].cancels[i];
        EXPECT_EQ(MoveIndexMap::KernelMoveIdOf(src.from), built.from);
        EXPECT_EQ(MoveIndexMap::KernelMoveIdOf(src.to),   built.to);

        // The contact collapse, move for move: Hit and Block require contact,
        // Whiff and Always do not. See CancelEdge::onHit.
        const bool requiresContact = src.on == Contact::Hit || src.on == Contact::Block;
        EXPECT_EQ(requiresContact ? 1u : 0u, built.onHit);

        // Padding is hashed by the connect handshake, so an unwritten byte is a
        // byte two peers can disagree about.
        EXPECT_EQ(0u, built.pad_[0]);
        EXPECT_EQ(0u, built.pad_[1]);
        EXPECT_EQ(0u, built.pad_[2]);

        // And every window the bridge resolved is inside the move it belongs to.
        const cse::kernel::MoveDef& from = k.build.data.p[0].moves[built.from];
        EXPECT_GE(built.earliestFrame, 0);
        EXPECT_LE(built.latestFrame, cse::kernel::MoveDuration(from) - 1)
            << "a window that outlives its move would make `latestFrame` a lie: "
               "the move ends first and the fighter goes idle";
    }
}

TEST(CancelBridge, TheLossTableCountsWhatTheProjectionActuallyCost) {
    Kfg k{};
    buildKfg(k);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    const BuildReport& r = k.build.report[0];

    // RECOMPUTED FROM THE LOADED FILE, not copied from the builder. If the
    // builder and this loop ever disagree, one of them is counting the wrong
    // thing and the table stops being evidence.
    std::int32_t uncertain = 0, guarded = 0, effected = 0, blockOrWhiff = 0, contactFrame = 0;
    for (const Cancel& e : k.character.cancels) {
        const Move& from = k.character.moves[e.from];
        const bool requiresContact = e.on == Contact::Hit || e.on == Contact::Block;
        if (!e.certain)          ++uncertain;
        if (!e.guard.empty())    ++guarded;
        if (!e.effect.empty())   ++effected;
        if (e.on == Contact::Block || e.on == Contact::Whiff) ++blockOrWhiff;
        if (requiresContact && from.active > 1) ++contactFrame;
    }

    // The literals are the file's, counted independently of the loop above so
    // that a loader bug cannot make both sides wrong in the same direction.
    EXPECT_EQ(103, uncertain);
    EXPECT_EQ(41,  guarded);
    EXPECT_EQ(0,   effected);
    EXPECT_EQ(4,   blockOrWhiff);
    EXPECT_EQ(132, contactFrame);

    struct ExpectedLoss {
        const char*        field;
        std::int32_t       count;
        BuildLossDirection direction;
    };
    const ExpectedLoss expected[] = {
        { "cancels (dropped)",           0, BuildLossDirection::KernelOmits   },
        { "cancels (link, not cancel)",  0, BuildLossDirection::KernelPermits },
        { "cancel.contact_frame",      132, BuildLossDirection::KernelPermits },
        { "cancel.on",                   4, BuildLossDirection::KernelPermits },
        { "cancel.certain",            103, BuildLossDirection::KernelPermits },
        { "cancel.guard",               41, BuildLossDirection::KernelPermits },
        { "cancel.effect",               0, BuildLossDirection::KernelOmits   },
        { "move.cancel_window (absent)", 8, BuildLossDirection::KernelPermits },
    };

    for (const ExpectedLoss& e : expected) {
        SCOPED_TRACE(e.field);
        const BuildLoss* loss = findLoss(r, e.field);
        ASSERT_NE(nullptr, loss) << "the loss table no longer mentions this field";
        EXPECT_EQ(e.count, loss->count);
        EXPECT_EQ(e.direction, loss->direction);
        EXPECT_FALSE(loss->note.empty())
            << "a loss with no note is a field name, not an explanation";
    }

    // The builder's numbers and this test's recomputation, joined.
    EXPECT_EQ(uncertain,    findLoss(r, "cancel.certain")->count);
    EXPECT_EQ(guarded,      findLoss(r, "cancel.guard")->count);
    EXPECT_EQ(effected,     findLoss(r, "cancel.effect")->count);
    EXPECT_EQ(blockOrWhiff, findLoss(r, "cancel.on")->count);
    EXPECT_EQ(contactFrame, findLoss(r, "cancel.contact_frame")->count);

    // THE ENTRY THAT IS GONE. `cancels` said 134 and meant "none of these can be
    // taken". It must not come back wearing the same name, because a reader who
    // sees it will believe the old sentence.
    EXPECT_EQ(nullptr, findLoss(r, "cancels"))
        << "the old whole-system loss entry is still in the table, and it now "
           "says something false: the kernel has a cancel system";
    EXPECT_EQ(nullptr, findLoss(r, "move.cancel_window"))
        << "the old cancel-window entry claimed the window was inert; it is now "
           "the thing that closes a cancel opportunity";

    // AND THE FLAG IS STILL FALSE, computed rather than remembered. Cancels were
    // the dominant reason it was false and they no longer are; nineteen entries
    // still bite, five of them about cancels. `bites` counts NONZERO rows
    // whatever their direction, so M1.3e's stance flip to Exact moves nothing
    // here (25 stanced moves still count) and the new blocked_as row is zero
    // for this character.
    EXPECT_EQ(19, r.lossesThatBite);
    EXPECT_FALSE(r.playsAsAnalysed)
        << "the bridge is claiming the kernel plays the character ProverAdapter "
           "analysed. It does not -- see the loss table -- and the cancel system "
           "landing does not by itself make that sentence true.";

    // 26 -> 27 when M1.3e added move.blocked_as; -> 28 when M1.1e added
    // character.input_buffer_frames.
    EXPECT_EQ(28u, r.losses.size())
        << "the loss table grew or shrank. That is fine, and it has to be "
           "recorded here, because the point of the table is that somebody "
           "counted.";
}

TEST(CancelBridge, AllThreeShippedCharactersProjectTheirEdges) {
    // Counted out of the three files. The AOF2 character is the interesting one:
    // every one of its 26 edges resolves to an EMPTY window, because its authored
    // delays are longer than the moves they cancel. Those are links wearing the
    // schema's cancel shape, and the table has to say so rather than report 26
    // working cancels.
    struct Expected {
        const char*  file;
        std::int32_t edges;
        std::int32_t inertLinks;
        std::int32_t windowlessSources;
    };
    const Expected expected[] = {
        { "kung_fu_girl.json",            134,  0,  8 },
        { "kung_fu_man.json",              87,  0,  7 },
        { "aof2_strength_training.json",   26, 26, 10 },
    };

    for (const Expected& e : expected) {
        SCOPED_TRACE(e.file);
        CharacterData c{};
        loadShipped(e.file, c);
        if (::testing::Test::HasFatalFailure()) return;

        ASSERT_EQ(static_cast<std::size_t>(e.edges), c.cancels.size())
            << "this file no longer authors the number of edges this test was "
               "written against";

        MatchBuild build{};
        BuildOptions options{};
        options.body = standardBody();
        ASSERT_TRUE(BuildMatchData(c, options, c, options, build))
            << build.report[0].error;

        EXPECT_EQ(e.edges, build.data.p[0].cancelCount);
        EXPECT_EQ(e.edges, build.moves[0].cancelCount);

        const BuildLoss* link = findLoss(build.report[0], "cancels (link, not cancel)");
        ASSERT_NE(nullptr, link);
        EXPECT_EQ(e.inertLinks, link->count);

        const BuildLoss* absent = findLoss(build.report[0], "move.cancel_window (absent)");
        ASSERT_NE(nullptr, absent);
        EXPECT_EQ(e.windowlessSources, absent->count);

        const BuildLoss* dropped = findLoss(build.report[0], "cancels (dropped)");
        ASSERT_NE(nullptr, dropped);
        EXPECT_EQ(0, dropped->count) << "an authored edge did not survive mapping";

        // An inert edge is one the kernel can never take, and the check that it
        // really cannot is an empty window rather than a promise.
        std::int32_t empty = 0;
        for (std::int32_t i = 0; i < build.data.p[0].cancelCount; ++i) {
            const CancelEdge& edge = build.data.p[0].cancels[i];
            if (edge.earliestFrame > edge.latestFrame) ++empty;
        }
        EXPECT_EQ(e.inertLinks, empty);

        EXPECT_FALSE(build.report[0].playsAsAnalysed);
    }
}

// ============================================================================
// What a bad edge does, and what too many do
// ============================================================================

TEST(CancelBridge, ADanglingEdgeIsDroppedCountedAndNamed) {
    // The loader calls a dangling id a load error, so this is defence against a
    // character assembled by hand -- and it is the one case where cancelCount
    // must NOT equal the authored count. Carrying the edge would hand the kernel
    // a moveId nothing describes.
    CharacterData c = windowCharacter();
    Cancel bad{};
    bad.from  = c.FindMove("a");
    bad.to    = kInvalidMove;
    bad.delay = 0;
    bad.on    = Contact::Hit;
    c.cancels.push_back(bad);
    c.RebuildIndices();

    ASSERT_EQ(2u, c.cancels.size());

    const MatchBuild build = buildWindowCharacter(c);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    EXPECT_EQ(1, build.data.p[0].cancelCount)
        << "the dangling edge was carried into the kernel";
    ASSERT_EQ(1u, build.moves[0].fileCancelByEdge.size());
    EXPECT_EQ(0u, build.moves[0].fileCancelByEdge[0])
        << "the surviving edge is the FIRST authored one, and the map has to say "
           "so or a panel will attribute its label to the wrong edge";

    const BuildLoss* dropped = findLoss(build.report[0], "cancels (dropped)");
    ASSERT_NE(nullptr, dropped);
    EXPECT_EQ(1, dropped->count);
    EXPECT_EQ(BuildLossDirection::KernelOmits, dropped->direction);

    bool warned = false;
    for (const std::string& w : build.report[0].warnings)
        if (w.find("cancels[1]") != std::string::npos) warned = true;
    EXPECT_TRUE(warned) << "a dropped edge that nobody is told about is a silent "
                           "difference between the file and the game";
}

namespace {

// A character with `count` self-edges on one move, for the capacity boundary.
CharacterData characterWithCancels(std::int32_t count) {
    CharacterData c{};
    c.id = "many_cancels";
    c.moves.push_back(synthMove("a", 2, 2, 20, 40, 10, 50));
    c.moves.push_back(synthMove("b", 2, 2, 5,  20, 20, 50));
    c.RebuildIndices();
    for (std::int32_t i = 0; i < count; ++i)
        c.cancels.push_back(synthCancel(c, "a", "b", 0, Contact::Hit));
    c.RebuildIndices();
    return c;
}

} // namespace

TEST(CancelCapacity, ExactlyTheCapBuildsAndOneMoreIsRefused) {
    // The passing side first, so the refusal cannot be passing for the wrong
    // reason -- a builder that refused 100 edges would also refuse 257 and this
    // suite would still be green.
    {
        const CharacterData c = characterWithCancels(kMaxBuildableCancels);
        FighterData  data{};
        MoveIndexMap map{};
        BuildReport  report{};
        ASSERT_TRUE(BuildFighterData(c, optionsWith({}), data, map, report)) << report.error;
        EXPECT_EQ(kMaxBuildableCancels, data.cancelCount);
        EXPECT_EQ(kMaxBuildableCancels, map.cancelCount);
    }
    {
        const CharacterData c = characterWithCancels(kMaxBuildableCancels + 1);
        FighterData  data{};
        MoveIndexMap map{};
        BuildReport  report{};
        EXPECT_FALSE(BuildFighterData(c, optionsWith({}), data, map, report));
        ASSERT_FALSE(report.error.empty()) << "a refusal with no error message";

        // Both numbers, because the person reading it has to decide which edges
        // to cut and "too many cancels" does not help them.
        EXPECT_NE(std::string::npos,
                  report.error.find(std::to_string(kMaxBuildableCancels + 1)));
        EXPECT_NE(std::string::npos,
                  report.error.find(std::to_string(kMaxBuildableCancels)));

        // And nothing is left behind. A FighterData carrying a move table and a
        // truncated cancel graph is exactly the character this refusal exists to
        // stop shipping.
        EXPECT_EQ(0, data.moveCount);
        EXPECT_EQ(0, data.cancelCount);
        EXPECT_EQ(0, map.cancelCount);
        EXPECT_TRUE(map.fileCancelByEdge.empty());
    }
}

TEST(CancelBridge, AnEdgeIntoAnUnboundMoveIsReportedRatherThanSilentlyDead) {
    // A cancel is taken by holding the TARGET's buttons, so an edge into a move
    // nobody bound is unreachable however correct its window is. That is the
    // caller's binding table and not the character, which is why it is a warning
    // -- but "the cancel system does not work" and "you did not bind the
    // follow-up" cost very different afternoons.
    const CharacterData c = windowCharacter();
    MatchBuild build{};
    const BuildOptions o = optionsWith({ bind("a", cse::kernel::kInputLP) });   // no `b`
    ASSERT_TRUE(BuildMatchData(c, o, c, o, build)) << build.report[0].error;

    bool warned = false;
    for (const std::string& w : build.report[0].warnings)
        if (w.find("no button bound") != std::string::npos) warned = true;
    EXPECT_TRUE(warned);

    // And it really is dead, checked against the kernel rather than believed.
    std::vector<std::uint16_t> s(30, 0u);
    s[0] = cse::kernel::kInputLP;
    for (std::size_t t = 1; t < s.size(); ++t) s[t] = cse::kernel::kInputMP;
    const Replay r = drive(build.data, 0, windowDefenderAt(), s);
    EXPECT_EQ(0, r.cancelsTaken);
}
