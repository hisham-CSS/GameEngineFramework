// A transcribed character driving the real simulation.
//
// Everything before this file was half a loop. CseData could load Kung Fu Girl
// out of Exported/Characters/kung_fu_girl.json and ProverAdapter could tell you
// whether her cancels terminate; CseKernel could fight, with hitboxes invented
// by a test. The two had never met. This file is where the character in the file
// is the character in the tick, and the four things it is here to prove are:
//
//   1. THE NUMBERS SURVIVE THE CROSSING. Her move count and specific moves'
//      frame data come out of MatchData equal to what the JSON says. The
//      expected values below were read out of the JSON directly, not copied from
//      a summary, and they are written as pixels and ticks so a reader can check
//      them against the file without multiplying by 256 in their head.
//   2. SHE RUNS, AND ROLLS BACK. Several hundred ticks of a real match with the
//      real MatchData: byte-identical across two runs, and byte-identical again
//      when the last 150 ticks are re-simulated from a snapshot. That is the
//      whole point of the task. It is also the assertion most likely to pass
//      vacuously -- a run in which nothing ever happens is trivially
//      deterministic -- so it counts hits, live hitboxes and move starts, and
//      fails if any of them is zero.
//   3. HER REACH IS HERS. Two of her own moves at the SAME distance, one that
//      connects and one that does not, chosen because their authored reaches
//      (81 px and 30 px) sit either side of it. No invented numbers, and the
//      falling-short case asserts the hitbox was LIVE, because "no hit occurred"
//      is the easiest green in the world to get by accident.
//   4. TOO MANY MOVES IS A REFUSAL. 31 moves build; 32 do not, and the failure
//      leaves no half-built MatchData behind.
//
// This test links CseData AND CseKernel, which no other test does. That is not a
// crack in the D2 boundary -- it is the shape the boundary predicts. CseData
// still does not link CseKernel (Games/UntitledFighter/Data/CMakeLists.txt
// asserts it) and the kernel still links nothing
// (Games/UntitledFighter/Kernel/CMakeLists.txt asserts that). A match-setup layer
// that reads one and fills in the other is exactly what Combat.h's opening note
// describes, and a test of it has to see both.
#include <gtest/gtest.h>

#include "cse/data/CharacterData.h"
#include "cse/data/MatchBuilder.h"
#include "cse/kernel/Simulate.h"

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace cse::data;
using cse::kernel::GameState;
using cse::kernel::InputPair;
using cse::kernel::MatchData;

namespace {

// Authored in pixels and converted once, here, matching test_combat.cpp.
constexpr std::int32_t px(std::int32_t pixels) {
    return pixels * cse::kernel::kSubUnitsPerPixel;
}

// Kung Fu Girl's transcribed body: ground.front/ground.back 13 px and height 60
// px (engine.constants in her file, which the loader does not read -- see the
// `hurtbox` entry in the loss table).
constexpr std::int32_t kHalfWidth = px(13);
constexpr std::int32_t kHeight    = px(60);

// The build-wide resource order, as test_prover_adapter.cpp and
// test_character_data.cpp use it. Positional, so index 0 must mean the same
// resource in every file a build loads.
const std::vector<std::string> kBuildResources = { "meter", "juggle" };

LoadOptions loadOptions() {
    LoadOptions o;
    o.expectedResources = kBuildResources;
    return o;
}

// Where the shipped characters live. Mirrors test_prover_adapter.cpp: the
// walk-up fallback keeps the test runnable from a shell anywhere in the tree,
// and it cannot make anything pass vacuously because every load below ASSERTs.
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

BodySpec kungFuGirlBody() {
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

// Six of her normals on six distinct single buttons. Single buttons on purpose:
// no mask here is a subset of another, so nothing is shadowed and the
// shadowing warning tested further down cannot fire by accident and be mistaken
// for the thing it is meant to catch.
std::vector<MoveBinding> sixNormals() {
    return {
        bind("stand_lp",     cse::kernel::kInputLP),
        bind("stand_mp",     cse::kernel::kInputMP),
        bind("stand_hp_far", cse::kernel::kInputHP),
        bind("stand_lk_far", cse::kernel::kInputLK),
        bind("stand_mk",     cse::kernel::kInputMK),
        bind("stand_hk_far", cse::kernel::kInputHK),
    };
}

BuildOptions kungFuGirlOptions(std::vector<MoveBinding> bindings) {
    BuildOptions o{};
    o.body     = kungFuGirlBody();
    o.bindings = std::move(bindings);
    return o;
}

const BuildLoss* findLoss(const BuildReport& report, const char* field) {
    for (const BuildLoss& loss : report.losses)
        if (loss.field == field) return &loss;
    return nullptr;
}

bool warningMentions(const BuildReport& report, const std::string& needle) {
    for (const std::string& w : report.warnings)
        if (w.find(needle) != std::string::npos) return true;
    return false;
}

// --- Synthetic characters ----------------------------------------------------
//
// Built through the same struct the loader fills and then RebuildIndices(),
// which CharacterData.h names as the documented path for a character assembled
// by hand.

Move syntheticMove(const std::string& id) {
    Move m{};
    m.id              = id;
    m.startup         = 3;
    m.active          = 2;
    m.recovery        = 6;
    m.hitstun         = 9;
    m.damageHundredths = 1000;
    m.reachSub        = px(50);
    return m;
}

CharacterData syntheticCharacter(int moveCount) {
    CharacterData c{};
    c.id   = "synthetic";
    c.name = "Synthetic";
    for (int i = 0; i < moveCount; ++i)
        c.moves.push_back(syntheticMove("m" + std::to_string(i)));
    c.RebuildIndices();
    return c;
}

// --- Running a match ---------------------------------------------------------

InputPair inputs(std::uint16_t p0Bits, std::uint16_t p1Bits) {
    InputPair in{};
    in.p[0].bits = p0Bits;
    in.p[1].bits = p1Bits;
    return in;
}

// A scripted mirror match, in three acts.
//
// ACT 1, ticks 0-29: walk in. ResetMatch opens the two fighters 200 px apart,
// which is a 174 px body-to-body gap -- further than any move in her file
// reaches. Thirty ticks of walking at the kernel's 2 px/tick closes it to 54 px,
// inside every reach in the table. Without this act nothing ever connects and
// the determinism assertions below are about an empty room.
//
// ACT 2, ticks 30-209: p0 attacks. p1 does nothing, so p0's hits are guaranteed
// rather than dependent on who happened to be in hitstun.
//
// ACT 3, ticks 210+: p1 answers, densely, while p0 has stopped. The two acts are
// SEPARATED on purpose. An earlier version had both fighters pressing buttons
// throughout, and whether p1 ever landed a hit turned out to depend on whether a
// gap opened in p0's pressure -- which is a fine thing for a game and a terrible
// thing for a test's non-vacuity guard to rest on.
std::vector<InputPair> scriptedMatch(int ticks) {
    using namespace cse::kernel;
    std::vector<InputPair> seq;
    seq.reserve(static_cast<std::size_t>(ticks));
    for (int t = 0; t < ticks; ++t) {
        std::uint16_t a = 0, b = 0;
        if (t < 30) {
            a |= kInputRight;
            b |= kInputLeft;
        } else if (t < 210) {
            if (t % 17 == 0) a |= kInputLP;
            if (t % 23 == 0) a |= kInputMK;
            if (t % 31 == 0) a |= kInputHP;
            if (t % 53 == 0) a |= kInputUp;    // a jump and its whole arc
        } else {
            // AND p1 WALKS BACK IN FIRST, which it did not have to do before
            // ROADMAP M1.3d wired pushback. Act 2 is 180 ticks of p0 landing
            // hits, and every one of them knocks p1 further away; by tick 210
            // p1 is well outside its own reach and answering from there is
            // answering into empty air. Twenty ticks of walking is what a
            // player does after being pushed out, and it is what makes "p1
            // never landed a hit" a fact about the exchange rather than about
            // the distance the previous act left them at.
            //
            // LEFT, and for the whole act. p0 stands on the left, and 180 ticks
            // of taking hits pins p1 against the RIGHT wall Simulate clamps at
            // -- so the distance to walk back is most of the stage, not the
            // twenty ticks the first version of this guessed at. Walking on
            // every tick costs nothing: the attacks below still come out, and a
            // fighter already in range simply keeps walking into an opponent it
            // cannot pass through.
            b |= kInputLeft;
            if (t % 7  == 0) b |= kInputLK;
            if (t % 29 == 0) b |= kInputHK;
            if (t % 61 == 0) b |= kInputUp;
        }
        seq.push_back(inputs(a, b));
    }
    return seq;
}

// What a run actually did, observed from outside: every counter is derived from
// the states the simulation produced, never from what the script asked for.
struct Observed {
    int hitsOnP0     = 0;
    int hitsOnP1     = 0;
    int movesStarted[2] = { 0, 0 };
    // Every tick on which a move BEGINS — a start, a cancel into a different
    // move, or a cancel into the SAME move. Added when cancels landed.
    //
    // Detected as "in a move, at frame 0" rather than as a moveId transition,
    // and the difference is not pedantry: Kung Fu Girl's loop here is
    // `stand_lk_far` cancelling into ITSELF, so the id never changes and a
    // transition detector sees nothing at all. The frame counter is what
    // actually resets, so it is what actually says a move began.
    int movesEntered[2] = { 0, 0 };
    int boxLiveTicks[2] = { 0, 0 };
};

GameState runFrom(const GameState& start, const MatchData& data,
                  const std::vector<InputPair>& seq,
                  std::size_t from, std::size_t to, Observed* observed = nullptr) {
    GameState s = start;
    for (std::size_t i = from; i < to; ++i) {
        const GameState before = s;
        cse::kernel::Simulate(s, seq[i], data);
        if (!observed) continue;
        if (s.p[0].health < before.p[0].health) ++observed->hitsOnP0;
        if (s.p[1].health < before.p[1].health) ++observed->hitsOnP1;
        for (int p = 0; p < 2; ++p) {
            if (before.p[p].moveId == 0 && s.p[p].moveId != 0) ++observed->movesStarted[p];
            if (s.p[p].moveId != 0 && s.p[p].moveFrame == 0)
                ++observed->movesEntered[p];
            cse::kernel::Box box{};
            if (cse::kernel::ActiveHitbox(data.p[p], s.p[p], box)) ++observed->boxLiveTicks[p];
        }
    }
    return s;
}

} // namespace

// ============================================================================
// 1. The numbers survive the crossing
// ============================================================================

namespace {

// One build of Kung Fu Girl against herself, shared by the tests that only read
// it. Built fresh per test rather than as a global: a MatchData mutated by
// accident in one test and read in another is the kind of coupling that makes a
// failure impossible to localise.
struct KfgMatch {
    CharacterData character;
    MatchBuild    build;
};

void buildKungFuGirl(KfgMatch& out, std::vector<MoveBinding> bindings) {
    loadShipped("kung_fu_girl.json", out.character);
    if (::testing::Test::HasFatalFailure()) return;
    const BuildOptions options = kungFuGirlOptions(std::move(bindings));
    ASSERT_TRUE(BuildMatchData(out.character, options, out.character, options, out.build))
        << "p0: " << out.build.report[0].error << " / p1: " << out.build.report[1].error;
}

} // namespace

TEST(MatchBridgeTranscription, MoveCountIsEveryAuthoredMovePlusTheReservedIdleSlot) {
    KfgMatch m{};
    buildKungFuGirl(m, sixNormals());
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    // 25 moves in kung_fu_girl.json, plus slot 0, which Combat.h reserves so
    // that Fighter::moveId is a direct index with no arithmetic at any lookup.
    EXPECT_EQ(25u, m.character.moves.size())
        << "kung_fu_girl.json no longer has 25 moves; the expectations in this "
           "file were read out of that file and need re-reading.";
    EXPECT_EQ(26, m.build.data.p[0].moveCount);
    EXPECT_EQ(26, m.build.moves[0].moveCount);

    // Slot 0 stays zeroed. If anything ever writes it, moveId 0 stops meaning
    // idle and every idle fighter acquires a hitbox.
    const cse::kernel::MoveDef& idle = m.build.data.p[0].moves[0];
    EXPECT_EQ(0, idle.startup);
    EXPECT_EQ(0, idle.active);
    EXPECT_EQ(0, idle.recovery);
    EXPECT_EQ(0, idle.damage);
    EXPECT_EQ(0, idle.hitstun);
    EXPECT_EQ(0u, idle.button);
    EXPECT_EQ(0, idle.hitbox.x0);
    EXPECT_EQ(0, idle.hitbox.x1);
}

TEST(MatchBridgeTranscription, FrameDataMatchesTheJsonMoveForMove) {
    KfgMatch m{};
    buildKungFuGirl(m, sixNormals());
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    const cse::kernel::FighterData& d = m.build.data.p[0];

    // Every number below is from Exported/Characters/kung_fu_girl.json. Damage
    // is authored in points as a JSON float (23.0), stored by the loader as
    // hundredths (2300) and converted here exactly once (23) -- Combat.h's rule.
    struct Expected {
        const char*  id;
        std::uint16_t slot;
        std::int32_t startup, active, recovery, hitstun, damage, reachPx;
    };
    const Expected expected[] = {
        // id                slot  su  act  rec  hs  dmg  reach(px)
        { "stand_lp",          1,   3,   2,   6,   9,  23,  57 },
        { "stand_lk_far",      7,   5,   2,  11,  13,  39,  81 },
        { "stand_lk_close",    8,   3,   2,   7,  11,  31,  30 },
        { "crouch_hk",        17,   7,   4,  18,  18,  96,  52 },
        { "shuffle_2",        23,   5,   5,  27,  27,  23, 103 },
        { "super_palm",       25,  11,   3,  87,  32, 219,  52 },
    };

    for (const Expected& e : expected) {
        SCOPED_TRACE(e.id);
        const std::uint16_t slot = m.build.moves[0].Find(e.id);
        ASSERT_EQ(e.slot, slot)
            << "the move landed in a slot other than its file position + 1, so a "
               "kernel moveId and a ProverAdapter MoveIndex no longer name the "
               "same move";
        const cse::kernel::MoveDef& mv = d.moves[slot];
        EXPECT_EQ(e.startup,  mv.startup);
        EXPECT_EQ(e.active,   mv.active);
        EXPECT_EQ(e.recovery, mv.recovery);
        EXPECT_EQ(e.hitstun,  mv.hitstun);
        EXPECT_EQ(e.damage,   mv.damage);

        // The box construction, asserted rather than assumed: it starts at the
        // front of the body and extends the authored reach, plus the one
        // sub-unit that turns the file's INCLUSIVE maximum gap into the kernel's
        // half-open bound. See MatchBuilder.cpp.
        EXPECT_EQ(kHalfWidth, mv.hitbox.x0);
        EXPECT_EQ(kHalfWidth + px(e.reachPx) + 1, mv.hitbox.x1);
        EXPECT_EQ(0,       mv.hitbox.y0);
        EXPECT_EQ(kHeight, mv.hitbox.y1);
        EXPECT_EQ(0u, mv.negativeEdge)
            << "the byte that used to be MoveDef::pad_ is negativeEdge now, and "
               "the schema does not author it yet -- so it must still be written "
               "as zero. It is hashed by the connect handshake either way, and an "
               "unwritten byte is a byte two peers can disagree about.";
    }

    // The body the caller supplied, symmetric about the origin and standing on
    // the floor.
    EXPECT_EQ(-kHalfWidth, d.hurtbox.x0);
    EXPECT_EQ(0,           d.hurtbox.y0);
    EXPECT_EQ(kHalfWidth,  d.hurtbox.x1);
    EXPECT_EQ(kHeight,     d.hurtbox.y1);
}

TEST(MatchBridgeTranscription, DamageIsConvertedFromHundredthsExactlyOnce) {
    KfgMatch m{};
    buildKungFuGirl(m, sixNormals());
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    // Not a spot check: every one of her moves. A second rounding rule anywhere
    // would show up as a move that is off by one point, and one point of damage
    // is the sort of thing nobody notices until a combo kills or does not.
    for (std::size_t i = 0; i < m.character.moves.size(); ++i) {
        const Move& src = m.character.moves[i];
        SCOPED_TRACE(src.id);
        ASSERT_EQ(0, src.damageHundredths % 100)
            << "this character now authors a fractional damage point, so the "
               "rounding rule in MatchBuilder.cpp is doing real work and this "
               "test's exactness claim needs revisiting";
        EXPECT_EQ(src.damageHundredths / 100,
                  m.build.data.p[0].moves[i + 1].damage);
    }
}

TEST(MatchBridgeTranscription, TheIndexMapAgreesWithCharacterDataForEveryMove) {
    KfgMatch m{};
    buildKungFuGirl(m, sixNormals());
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    const MoveIndexMap& map = m.build.moves[0];
    EXPECT_EQ("kung_fu_girl", map.characterId);

    for (std::size_t i = 0; i < m.character.moves.size(); ++i) {
        const std::string& id = m.character.moves[i].id;
        SCOPED_TRACE(id);
        const std::uint16_t slot = map.Find(id);
        EXPECT_EQ(static_cast<std::uint16_t>(i + 1), slot);
        EXPECT_EQ(id, std::string(map.IdOf(slot)));
        // The round trip that keeps a live match and a prover verdict able to
        // name the same move.
        EXPECT_EQ(m.character.FindMove(id), MoveIndexMap::CharacterMoveOf(slot));
        EXPECT_EQ(slot, MoveIndexMap::KernelMoveIdOf(m.character.FindMove(id)));
    }

    // A name that is not in the table resolves to slot 0, which the kernel reads
    // as idle. Unlike CharacterData::FindMove's 0xFFFF, this sentinel cannot
    // collide with a real move, because slot 0 is reserved.
    EXPECT_EQ(0u, map.Find("no_such_move"));
    EXPECT_TRUE(map.IdOf(0).empty());
    EXPECT_TRUE(map.IdOf(9999).empty());
    EXPECT_EQ(kInvalidMove, MoveIndexMap::CharacterMoveOf(0));
}

// ============================================================================
// 2. She runs, and she rolls back
// ============================================================================

TEST(MatchBridgeSimulation, ARealCharacterDrivesTheKernelDeterministically) {
    KfgMatch m{};
    buildKungFuGirl(m, sixNormals());
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    const auto seq = scriptedMatch(400);

    GameState start{};
    cse::kernel::ResetMatch(start, 0xC0FFEEu);

    Observed observed{};
    const GameState a = runFrom(start, m.build.data, seq, 0, 400, &observed);
    const GameState b = runFrom(start, m.build.data, seq, 0, 400);

    EXPECT_EQ(0, std::memcmp(&a, &b, sizeof(GameState)))
        << "Two runs of the same 400 inputs with the same loaded character "
           "diverged. Something in the match is reading state that is not in "
           "GameState or in MatchData.";
    EXPECT_EQ(cse::kernel::Checksum(a), cse::kernel::Checksum(b));

    // THE VACUITY GUARDS. A match in which nothing happens is deterministic for
    // reasons that have nothing to do with this bridge, so every one of these
    // must be nonzero or the assertion above proved nothing.
    EXPECT_GT(observed.movesStarted[0], 0) << "p0 never started a move";
    EXPECT_GT(observed.movesStarted[1], 0) << "p1 never started a move";
    EXPECT_GT(observed.boxLiveTicks[0], 0) << "p0 never had a live hitbox";
    EXPECT_GT(observed.boxLiveTicks[1], 0) << "p1 never had a live hitbox";
    EXPECT_GT(observed.hitsOnP0, 0) << "p0 was never hit";
    EXPECT_GT(observed.hitsOnP1, 0) << "p1 was never hit";
    EXPECT_LT(a.p[0].health, 1000);
    EXPECT_LT(a.p[1].health, 1000);
}

TEST(MatchBridgeSimulation, TheLoadedDataActuallyReachesTheTick) {
    // The guard behind the guard. If BuildMatchData produced an empty MatchData
    // and Simulate quietly behaved as the no-data overload, every determinism
    // assertion in this file would still pass. It must not: the same inputs run
    // against kNoMoves have to give a different state.
    KfgMatch m{};
    buildKungFuGirl(m, sixNormals());
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    const auto seq = scriptedMatch(400);
    GameState start{};
    cse::kernel::ResetMatch(start, 0xC0FFEEu);

    const GameState withHer = runFrom(start, m.build.data, seq, 0, 400);
    const GameState without = runFrom(start, cse::kernel::kNoMoves, seq, 0, 400);

    EXPECT_NE(0, std::memcmp(&withHer, &without, sizeof(GameState)))
        << "Simulating with Kung Fu Girl's MatchData produced exactly the state "
           "an empty MatchData produces. The character is not reaching the tick.";
}

TEST(MatchBridgeSimulation, SnapshotRestoreAndResimulateReproducesTheStraightRun) {
    // The property the netcode rests on, now with real character data on the
    // read-only side. MatchData is not part of the snapshot -- Combat.h argues
    // that at length -- so this is also the assertion that nothing in the tick
    // is quietly writing through it.
    KfgMatch m{};
    buildKungFuGirl(m, sixNormals());
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    const auto seq = scriptedMatch(400);
    GameState start{};
    cse::kernel::ResetMatch(start, 0x5EED1234u);

    Observed observed{};
    const GameState straight = runFrom(start, m.build.data, seq, 0, 400, &observed);

    const GameState snapshot  = runFrom(start, m.build.data, seq, 0, 250);
    const GameState restored  = snapshot;                 // the snapshot is a copy
    const GameState resimmed  = runFrom(restored, m.build.data, seq, 250, 400);

    EXPECT_EQ(0, std::memcmp(&straight, &resimmed, sizeof(GameState)))
        << "Re-simulating the last 150 ticks from a restored snapshot did not "
           "reproduce the straight-through run.";
    EXPECT_EQ(cse::kernel::Checksum(straight), cse::kernel::Checksum(resimmed));

    EXPECT_GT(observed.hitsOnP0 + observed.hitsOnP1, 0)
        << "nothing connected anywhere in the straight run";

    // Non-vacuity again, and specifically about the RE-SIMULATED window: a
    // snapshot of an idle room replays perfectly and proves nothing.
    Observed tail{};
    (void)runFrom(restored, m.build.data, seq, 250, 400, &tail);
    EXPECT_GT(tail.boxLiveTicks[0] + tail.boxLiveTicks[1], 0)
        << "no hitbox was live anywhere in the re-simulated window";
    // ENTERED, not STARTED, and the difference is a real finding rather than a
    // loosened assertion. Once cancels landed, p1 enters stand_lk_far at tick
    // 217 and never returns to idle for the remaining 183 ticks: her file
    // authors `stand_lk_far -> stand_lk_far, delay 6, on hit`, the script
    // presses LK every 7 ticks, and the loop closes. So `movesStarted` is
    // legitimately 0 here — not because nothing happened, but because a great
    // deal did and none of it passed through idle.
    //
    // That loop is a genuine infinite, produced by an approximation the loss
    // table counts (`cancel.certain`, 103 permissive edges). It is exactly the
    // phenomenon the combo-termination proof exists to find, showing up
    // unprompted in a determinism test.
    EXPECT_GT(tail.movesEntered[0] + tail.movesEntered[1], 0)
        << "no move was entered anywhere in the re-simulated window";
}

TEST(MatchBridgeSimulation, EveryRewindDepthUpToTheBudgetIsExact) {
    // ARCHITECTURE.md D4 budgets 8 ticks of re-simulation per rollback event.
    // Checking every depth rather than only the deepest is what catches an
    // off-by-one that only shows at one specific depth.
    KfgMatch m{};
    buildKungFuGirl(m, sixNormals());
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    const auto seq = scriptedMatch(200);
    GameState start{};
    cse::kernel::ResetMatch(start, 7u);

    for (std::size_t depth = 1; depth <= 8; ++depth) {
        const GameState straight = runFrom(start, m.build.data, seq, 0, 180);
        const GameState mid      = runFrom(start, m.build.data, seq, 0, 180 - depth);
        const GameState redone   = runFrom(mid,   m.build.data, seq, 180 - depth, 180);
        EXPECT_EQ(0, std::memcmp(&straight, &redone, sizeof(GameState)))
            << "rewind depth " << depth << " diverged";
    }
}

// ============================================================================
// 3. Her reach is hers
// ============================================================================

namespace {

// Start `moveId` on tick 0 by pressing its button for exactly one tick, then run
// idle for `ticks` more. One tick of input, not a hold: holding repeats the move
// as soon as it recovers (Combat.h's note on held-not-pressed), which would make
// a single-hit assertion depend on the length of the run.
GameState pokeOnce(const MatchData& data, std::uint16_t button,
                   std::int32_t p0Sub, std::int32_t p1Sub, int ticks) {
    GameState s{};
    cse::kernel::ResetMatch(s, 0xA11CEu);
    s.p[0].posX = p0Sub;
    s.p[1].posX = p1Sub;
    for (int t = 0; t < ticks; ++t)
        cse::kernel::Simulate(s, inputs(t == 0 ? button : 0u, 0u), data);
    return s;
}

} // namespace

TEST(MatchBridgeReach, TwoOfHerOwnMovesAtOneDistanceDisagreeAsTheirFileSays) {
    // stand_lk_far reaches 81 px and stand_lk_close reaches 30 px -- her file's
    // numbers, not invented ones. At a body-to-body gap of 50 px the far kick
    // connects and the close one cannot. The real character picks between them
    // by range (her engine.escape_hatch says so); here they are on two buttons,
    // which is the point: the DISTANCE decides, not the binding.
    KfgMatch m{};
    buildKungFuGirl(m, { bind("stand_lk_far",   cse::kernel::kInputLK),
                         bind("stand_lk_close", cse::kernel::kInputMK) });
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    // Gap = (p1.posX - p0.posX) - 2 * halfWidth.
    const std::int32_t gap  = px(50);
    const std::int32_t p1At = gap + 2 * kHalfWidth;

    const GameState farKick = pokeOnce(m.build.data, cse::kernel::kInputLK, 0, p1At, 10);
    EXPECT_EQ(1000 - 39, farKick.p[1].health)
        << "stand_lk_far (reach 81 px, damage 39) did not connect at a 50 px gap";
    EXPECT_GT(farKick.p[1].hitstun, 0u)
        << "the defender took damage but no hitstun, so the hit is half applied";

    const GameState closeKick = pokeOnce(m.build.data, cse::kernel::kInputMK, 0, p1At, 10);
    EXPECT_EQ(1000, closeKick.p[1].health)
        << "stand_lk_close (reach 30 px) connected at a 50 px gap, which is "
           "further than her file says it reaches";

    // THE GUARD THAT MAKES THE MISS MEAN SOMETHING. "Did not connect" is what a
    // move that never started, a table that never loaded and a frame window off
    // by one all look like. Re-run the close kick to its third frame -- startup
    // 3, active 2, so frames 3 and 4 are live -- and assert the box really was
    // out there and really did fall short.
    const GameState midCloseKick =
        pokeOnce(m.build.data, cse::kernel::kInputMK, 0, p1At, 4);
    ASSERT_EQ(m.build.moves[0].Find("stand_lk_close"), midCloseKick.p[0].moveId)
        << "the close kick never started, so its miss proves nothing";
    ASSERT_EQ(3u, midCloseKick.p[0].moveFrame);

    cse::kernel::Box live{};
    ASSERT_TRUE(cse::kernel::ActiveHitbox(m.build.data.p[0], midCloseKick.p[0], live))
        << "the close kick had no live hitbox on the first active frame";
    const cse::kernel::Box body =
        cse::kernel::Hurtbox(m.build.data.p[1], midCloseKick.p[1]);
    EXPECT_FALSE(cse::kernel::BoxesOverlap(live, body));
    EXPECT_LT(live.x1, body.x0)
        << "the close kick's box fell short by " << (body.x0 - live.x1)
        << " sub-units, which should be exactly the 20 px it is short by";
    EXPECT_EQ(px(20), body.x0 - live.x1 + 1)
        << "reach 30 px against a 50 px gap is 20 px short, and the +1 is the "
           "half-open conversion of the file's inclusive bound";
}

TEST(MatchBridgeReach, TheAuthoredReachIsTheExactMaximumGap) {
    // CharacterData.h calls reachSub "maximum gap at which the move connects".
    // MatchBuilder builds the box to make that sentence literally true, so the
    // boundary is testable to the sub-unit -- 1/256th of a pixel, which is the
    // resolution at which D2 says a mirror rounding error decides whether a
    // combo is Terminating or Infinite.
    KfgMatch m{};
    buildKungFuGirl(m, { bind("stand_lp", cse::kernel::kInputLP) });
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    const std::int32_t reach = px(57);            // stand_lp, from her file
    const std::int32_t atMax = reach + 2 * kHalfWidth;

    const GameState connects = pokeOnce(m.build.data, cse::kernel::kInputLP, 0, atMax, 10);
    EXPECT_EQ(1000 - 23, connects.p[1].health)
        << "stand_lp missed at a gap of exactly its authored reach";

    const GameState misses = pokeOnce(m.build.data, cse::kernel::kInputLP, 0, atMax + 1, 10);
    EXPECT_EQ(1000, misses.p[1].health)
        << "stand_lp connected one sub-unit past its authored reach";
}

TEST(MatchBridgeReach, ReachIsExactlyMirroredForALeftFacingFighter) {
    // The same boundary with the attacker on the right. MirrorBox negates and
    // swaps and never divides, so the left-facing reach must be the SAME integer
    // -- a mirror that loses one sub-unit shortens a left-facing character's
    // combos and nothing else in the engine would ever say so.
    KfgMatch m{};
    buildKungFuGirl(m, { bind("stand_lp", cse::kernel::kInputLP) });
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    const std::int32_t reach = px(57);
    const std::int32_t atMax = reach + 2 * kHalfWidth;

    const GameState connects = pokeOnce(m.build.data, cse::kernel::kInputLP, 0, -atMax, 10);
    EXPECT_EQ(1u, connects.p[0].facing) << "p0 should be facing -X";
    EXPECT_EQ(1000 - 23, connects.p[1].health)
        << "a left-facing stand_lp missed at a gap its right-facing twin "
           "connects at";

    const GameState misses = pokeOnce(m.build.data, cse::kernel::kInputLP, 0, -(atMax + 1), 10);
    EXPECT_EQ(1000, misses.p[1].health)
        << "a left-facing stand_lp reached one sub-unit further than its "
           "right-facing twin";
}

TEST(MatchBridgeReach, AMoveWhoseFileDeclinesToStateAReachConnectsWithNothing) {
    // Kung Fu Man authors `reach: null` on his two projectile moves: a fireball's
    // reach is a function of distance travelled, so the file refuses to invent a
    // constant (CharacterData.h kNoReach). The bridge refuses too, and builds a
    // zero-width box rather than guessing.
    CharacterData kfm{};
    loadShipped("kung_fu_man.json", kfm);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    MatchBuild build{};
    BuildOptions options{};
    options.body = kungFuGirlBody();
    ASSERT_TRUE(BuildMatchData(kfm, options, kfm, options, build))
        << build.report[0].error;

    const BuildLoss* loss = findLoss(build.report[0], "move.reach (absent)");
    ASSERT_NE(nullptr, loss);
    EXPECT_EQ(2, loss->count)
        << "kung_fu_man.json authors exactly two reach-less moves (hasyo and "
           "suiten_hasyo); this count is the file's, not a guess";
    EXPECT_EQ(BuildLossDirection::KernelOmits, loss->direction);

    int zeroWidth = 0;
    for (std::size_t i = 0; i < kfm.moves.size(); ++i) {
        if (kfm.moves[i].reachSub != kNoReach) continue;
        const cse::kernel::MoveDef& mv = build.data.p[0].moves[i + 1];
        SCOPED_TRACE(kfm.moves[i].id);
        EXPECT_EQ(mv.hitbox.x0, mv.hitbox.x1) << "a reach-less move got a real box";
        // Its FRAME DATA still survives: the move runs, it just cannot connect.
        EXPECT_EQ(kfm.moves[i].startup, mv.startup);
        EXPECT_GT(mv.active, 0);
        ++zeroWidth;
    }
    EXPECT_EQ(2, zeroWidth);
}

// ============================================================================
// 4. Too many moves is a refusal
// ============================================================================

TEST(MatchBridgeCapacity, ExactlyTheCapBuilds) {
    // The boundary on the passing side, so the refusal below cannot be passing
    // for the wrong reason -- a builder that refused 30 moves would also refuse
    // 32 and this suite would still be green.
    const CharacterData c = syntheticCharacter(kMaxBuildableMoves);
    cse::kernel::FighterData data{};
    MoveIndexMap map{};
    BuildReport report{};

    ASSERT_TRUE(BuildFighterData(c, kungFuGirlOptions({}), data, map, report))
        << report.error;
    EXPECT_EQ(cse::kernel::kMaxMovesPerFighter, data.moveCount);
    EXPECT_EQ(kMaxBuildableMoves,
              static_cast<std::int32_t>(map.idByMoveId.size()) - 1);
    EXPECT_EQ(static_cast<std::uint16_t>(kMaxBuildableMoves),
              map.Find("m" + std::to_string(kMaxBuildableMoves - 1)));
}

TEST(MatchBridgeCapacity, OneMoveOverTheCapIsRefusedRatherThanTruncated) {
    const CharacterData c = syntheticCharacter(kMaxBuildableMoves + 1);
    cse::kernel::FighterData data{};
    MoveIndexMap map{};
    BuildReport report{};

    EXPECT_FALSE(BuildFighterData(c, kungFuGirlOptions({}), data, map, report));
    ASSERT_FALSE(report.error.empty()) << "a refusal with no error message";

    // The message has to name all three numbers, because the person reading it
    // has to decide which moves to cut and "too many moves" does not help them.
    EXPECT_NE(std::string::npos, report.error.find(std::to_string(kMaxBuildableMoves + 1)));
    EXPECT_NE(std::string::npos, report.error.find(std::to_string(kMaxBuildableMoves)));
    EXPECT_NE(std::string::npos, report.error.find("synthetic"));

    // And nothing must be left behind. A FighterData carrying the first 31 moves
    // is the truncation this decision exists to prevent, wearing a false return
    // value.
    EXPECT_EQ(0, data.moveCount);
    EXPECT_EQ(0, map.moveCount);
    EXPECT_TRUE(map.byId.empty());
}

TEST(MatchBridgeCapacity, AFailedSideLeavesNoHalfBuiltMatch) {
    CharacterData good{};
    loadShipped("kung_fu_girl.json", good);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    const CharacterData tooMany = syntheticCharacter(kMaxBuildableMoves + 1);

    MatchBuild build{};
    EXPECT_FALSE(BuildMatchData(good, kungFuGirlOptions(sixNormals()),
                                tooMany, kungFuGirlOptions({}), build));

    // Both sides are reported even though the first succeeded, so a caller sees
    // every problem at once rather than one per run.
    EXPECT_TRUE(build.report[0].error.empty());
    EXPECT_FALSE(build.report[1].error.empty());

    // A half-built MatchData is worse than none, because it looks like a match.
    EXPECT_EQ(0, build.data.p[0].moveCount);
    EXPECT_EQ(0, build.data.p[1].moveCount);
}

TEST(MatchBridgeCapacity, ACharacterWithNoMovesIsRefused) {
    CharacterData empty{};
    empty.id = "empty";
    empty.RebuildIndices();

    cse::kernel::FighterData data{};
    MoveIndexMap map{};
    BuildReport report{};
    EXPECT_FALSE(BuildFighterData(empty, kungFuGirlOptions({}), data, map, report));
    EXPECT_FALSE(report.error.empty());
}

// ============================================================================
// The decisions that are not about capacity
// ============================================================================

TEST(MatchBridgeOptions, AnUnsuppliedBodyWarnsAndUsesTheNamedDefault) {
    // CharacterData carries no body at all, so this number is the caller's. A
    // silent default would make it look like the character's.
    CharacterData kfg{};
    loadShipped("kung_fu_girl.json", kfg);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    cse::kernel::FighterData data{};
    MoveIndexMap map{};
    BuildReport report{};
    BuildOptions options{};                    // no body, no bindings
    ASSERT_TRUE(BuildFighterData(kfg, options, data, map, report)) << report.error;

    EXPECT_TRUE(warningMentions(report, "halfWidthSub was not supplied"));
    EXPECT_TRUE(warningMentions(report, "heightSub was not supplied"));
    EXPECT_EQ(-kDefaultBodyHalfWidthSub, data.hurtbox.x0);
    EXPECT_EQ(kDefaultBodyHalfWidthSub,  data.hurtbox.x1);
    EXPECT_EQ(kDefaultBodyHeightSub,     data.hurtbox.y1);
}

TEST(MatchBridgeOptions, ABindingThatCanNeverStartIsReportedAndReallyNeverStarts) {
    // StepAttack takes the first move in slot order whose buttons are ALL held.
    // stand_lp is slot 1 with {LP} and crouch_lp is slot 12 with {Down, LP}, so
    // holding Down+LP can only ever produce stand_lp. That is the natural way
    // somebody binds crouching normals and it silently does not work.
    KfgMatch m{};
    buildKungFuGirl(m, { bind("stand_lp",  cse::kernel::kInputLP),
                         bind("crouch_lp", static_cast<std::uint16_t>(
                                  cse::kernel::kInputDown | cse::kernel::kInputLP)) });
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    EXPECT_TRUE(warningMentions(m.build.report[0], "crouch_lp"))
        << "the shadowed binding was not reported";
    EXPECT_TRUE(warningMentions(m.build.report[0], "can never start"));

    // And the warning is checked against the kernel rather than merely believed.
    GameState s{};
    cse::kernel::ResetMatch(s, 1u);
    cse::kernel::Simulate(
        s, inputs(static_cast<std::uint16_t>(cse::kernel::kInputDown |
                                             cse::kernel::kInputLP), 0u),
        m.build.data);
    EXPECT_EQ(m.build.moves[0].Find("stand_lp"), s.p[0].moveId)
        << "holding Down+LP started something other than the earlier of the two "
           "moves, so the warning describes a rule the kernel does not have";
}

TEST(MatchBridgeOptions, ABindingForAMoveThisCharacterLacksIsAWarningNotAFailure) {
    // One binding table shared by two characters with different movesets is a
    // reasonable thing to write, so this must not be fatal -- but it must be
    // visible, because "why can't I punch" is otherwise a long afternoon.
    KfgMatch m{};
    buildKungFuGirl(m, { bind("stand_lp", cse::kernel::kInputLP),
                         bind("hadoken",  cse::kernel::kInputHP) });
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    EXPECT_TRUE(warningMentions(m.build.report[0], "hadoken"));
    EXPECT_EQ(cse::kernel::kInputLP, m.build.data.p[0].moves[1].button);
}

TEST(MatchBridgeOptions, ADuplicateBindingKeepsTheFirstAndSaysSo) {
    KfgMatch m{};
    buildKungFuGirl(m, { bind("stand_lp", cse::kernel::kInputLP),
                         bind("stand_lp", cse::kernel::kInputHK) });
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    EXPECT_TRUE(warningMentions(m.build.report[0], "bound more than once"));
    EXPECT_EQ(cse::kernel::kInputLP, m.build.data.p[0].moves[1].button)
        << "first-wins, so the result does not depend on where in a list "
           "somebody appended";
}

// ============================================================================
// What was dropped, and how loudly
// ============================================================================

TEST(MatchBridgeLosses, EveryDropIsCountedAgainstKungFuGirlsActualFile) {
    KfgMatch m{};
    buildKungFuGirl(m, sixNormals());
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    const BuildReport& r = m.build.report[0];

    struct ExpectedLoss {
        const char*        field;
        std::int32_t       count;
        BuildLossDirection direction;
    };
    // Counted out of kung_fu_girl.json. These are not decorative: a loss table
    // whose numbers nobody checked is a comment with a struct around it.
    const ExpectedLoss expected[] = {
        // These eight replaced the two rows `cancels`(134) and
        // `move.cancel_window`(16) when cancels landed. All 134 of Kung Fu
        // Girl's authored edges are now CARRIED, so what remains is not the
        // edges themselves but the parts of each edge the kernel cannot yet
        // honour — which is a more useful table than "we dropped everything".
        { "cancels (dropped)",             0, BuildLossDirection::KernelOmits   },
        { "cancels (link, not cancel)",    0, BuildLossDirection::KernelPermits },
        { "cancel.contact_frame",        132, BuildLossDirection::KernelPermits },
        { "cancel.on",                     4, BuildLossDirection::KernelPermits },
        { "cancel.certain",              103, BuildLossDirection::KernelPermits },
        { "cancel.guard",                 41, BuildLossDirection::KernelPermits },
        { "cancel.effect",                 0, BuildLossDirection::KernelOmits   },
        { "move.cancel_window (absent)",   8, BuildLossDirection::KernelPermits },
        { "character.walk_speed",      1, BuildLossDirection::Exact         },
        { "move.pushback",            24, BuildLossDirection::KernelOmits   },
        { "move.stance",              25, BuildLossDirection::KernelPermits },
        { "move.guard",                2, BuildLossDirection::Exact         },
        { "move.effect",              24, BuildLossDirection::Exact         },
        { "resources",                 2, BuildLossDirection::Exact         },
        { "move.hit_condition",       17, BuildLossDirection::KernelPermits },
        { "move.escape_hatch",        15, BuildLossDirection::KernelPermits },
        { "scaling",                   6, BuildLossDirection::KernelOmits   },
        { "decay",                     0, BuildLossDirection::KernelOmits   },
        { "gap_actions",               1, BuildLossDirection::KernelOmits   },
        { "starters",                 21, BuildLossDirection::KernelOmits   },
        { "move.engine.hits",          0, BuildLossDirection::KernelOmits   },
        { "move.engine.motion",        0, BuildLossDirection::KernelOmits   },
        { "move.reach (absent)",       0, BuildLossDirection::KernelOmits   },
        { "move.reach (provenance)",  25, BuildLossDirection::KernelPermits },
        { "move.hitbox.y",            25, BuildLossDirection::KernelPermits },
        { "hurtbox",                   1, BuildLossDirection::KernelPermits },
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

    EXPECT_EQ(sizeof(expected) / sizeof(expected[0]), r.losses.size())
        << "the loss table grew or shrank. That is fine, and it has to be "
           "recorded here, because the point of the table is that somebody "
           "counted.";

    // The four zero-count entries are the ones worth having: they record that a
    // check ran and found nothing, which is what tells "this character has no
    // decay" apart from "nobody looked".
    // 16 -> 19 when cancels landed: the two coarse cancel rows became eight
    // finer ones, five of which bite. playsAsAnalysed is still false, and still
    // computed rather than asserted.
    EXPECT_EQ(19, r.lossesThatBite);
    EXPECT_FALSE(r.playsAsAnalysed)
        << "the bridge is claiming the kernel plays the character ProverAdapter "
           "analysed. It does not: she has 134 cancels and the kernel has no "
           "cancel system.";
}

TEST(MatchBridgeLosses, TheDecayEntryRecordsThatItCheckedRatherThanThatItSkipped) {
    // ADR-001's amendment to D8: `decay.kind: "none"` is the truthful
    // transcription for MUGEN 1.0, so there is genuinely nothing to lose here.
    // That has to be distinguishable from an unchecked field.
    KfgMatch m{};
    buildKungFuGirl(m, sixNormals());
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    ASSERT_EQ(DecayKind::None, m.character.decay.kind);
    const BuildLoss* loss = findLoss(m.build.report[0], "decay");
    ASSERT_NE(nullptr, loss);
    EXPECT_EQ(0, loss->count);
    EXPECT_NE(std::string::npos, loss->note.find("inert"))
        << "a zero count with a note that does not say why is indistinguishable "
           "from a field nobody checked";
}

TEST(MatchBridgeLosses, DirectionNamesAreAllDistinctAndSpelled) {
    EXPECT_STRNE(BuildLossDirectionName(BuildLossDirection::Exact),
                 BuildLossDirectionName(BuildLossDirection::KernelPermits));
    EXPECT_STRNE(BuildLossDirectionName(BuildLossDirection::KernelPermits),
                 BuildLossDirectionName(BuildLossDirection::KernelOmits));
    EXPECT_STRNE(BuildLossDirectionName(BuildLossDirection::Exact),
                 BuildLossDirectionName(BuildLossDirection::KernelOmits));
}

TEST(MatchBridgeLosses, AllThreeShippedCharactersBuild) {
    // The bridge is not allowed to work only on the character it was written
    // against. Kung Fu Man brings reach-less projectiles and AOF2 brings a
    // 10-move character with a 20 px half-width body.
    for (const char* file : { "kung_fu_girl.json", "kung_fu_man.json",
                              "aof2_strength_training.json" }) {
        SCOPED_TRACE(file);
        CharacterData c{};
        loadShipped(file, c);
        if (::testing::Test::HasFatalFailure()) return;

        MatchBuild build{};
        BuildOptions options{};
        options.body = kungFuGirlBody();
        EXPECT_TRUE(BuildMatchData(c, options, c, options, build))
            << build.report[0].error;
        EXPECT_EQ(static_cast<std::int32_t>(c.moves.size()) + 1,
                  build.data.p[0].moveCount);
        EXPECT_FALSE(build.report[0].playsAsAnalysed);
    }
}

// --- The edge guard the kernel cannot see ------------------------------------
//
// CancelEdge carries no `guard` of its own. ROADMAP M1.1b measured every
// character in this tree -- fighter_a_infinite's ten and Kung Fu Girl's
// forty-one -- and found that all fifty-one restate the TARGET move's own guard
// exactly, which the cancel scan does enforce. So the field was not added and
// CancelEdge stayed 16 bytes.
//
// That is an observation about today's data, not a property of the schema, and
// an observation load-bearing enough to skip a struct on deserves a test that
// fails when it stops being true. The build warns per offending edge; this is
// the character that offends.
TEST(MatchBridgeLosses, AnEdgeGuardStricterThanItsTargetIsWarnedAbout) {
    CharacterData c = syntheticCharacter(2);
    c.resources.push_back(ResourceDef{ "meter", 0, 0, 300, true });
    c.RebuildIndices();

    // The target asks for 50 and the EDGE into it asks for 100. The kernel
    // checks the target, so at 50 meter this cancel is taken and the file says
    // it should not be.
    c.moves[1].guard.push_back(ResourceAmount{ 0, 50 });

    Cancel e{};
    e.from  = 0;
    e.to    = 1;
    e.delay = 0;
    e.on    = Contact::Hit;
    e.guard.push_back(ResourceAmount{ 0, 100 });
    c.cancels.push_back(e);
    c.RebuildIndices();

    BuildOptions options{};
    options.bindings = { { c.moves[0].id, cse::kernel::kInputLP },
                         { c.moves[1].id, cse::kernel::kInputMP } };

    MatchBuild build{};
    ASSERT_TRUE(BuildMatchData(c, options, c, options, build))
        << build.report[0].error;

    bool warned = false;
    for (const std::string& w : build.report[0].warnings)
        if (w.find("permitted where the file refuses") != std::string::npos)
            warned = true;

    EXPECT_TRUE(warned)
        << "an edge requiring 100 into a move requiring 50 built without a "
           "word. The kernel will take that cancel at 50, so the only thing "
           "standing between this file and a wrong simulation is this warning.";

    // AND THE REDUNDANT CASE STAYS QUIET, because a warning that fires on all
    // fifty-one authored edges is a warning nobody reads.
    CharacterData quiet = c;
    quiet.cancels[0].guard[0].value = 50;      // exactly the target's minimum
    quiet.RebuildIndices();

    MatchBuild quietBuild{};
    ASSERT_TRUE(BuildMatchData(quiet, options, quiet, options, quietBuild))
        << quietBuild.report[0].error;

    for (const std::string& w : quietBuild.report[0].warnings)
        EXPECT_EQ(w.find("permitted where the file refuses"), std::string::npos)
            << "an edge guard equal to its target's was warned about: " << w;
}

// --- The bridge carries what the kernel already implements --------------------
//
// ROADMAP M1.3d. The kernel has applied `pushbackHit` since ADR-005 P2 and every
// shipped move authors a `pushback`, but MatchBuilder populated ten MoveDef
// fields and left the rest at zero -- so a built character's hits moved nobody,
// froze nobody and knocked nobody down, however completely the kernel
// implemented all three.
//
// THIS TEST IS THE ONLY THING THAT PROVES THE WIRE, and that was measured rather
// than assumed: reverting the four lines in MatchBuilder leaves the whole
// 58-test suite green without it. Every other test that noticed pushback was
// FIXED to expect it -- the combo bench moved closer, the mirror match walks
// back in, four sweeps moved to the corner. Each of those fixes is right, and
// collectively they are a suite that stopped being able to tell whether the
// field is carried at all.
//
// Written against a CharacterData for the same reason:
// tests/test_p2_mechanics.cpp proves the kernel's half by assigning MoveDef
// directly, and doing that here would pass against the very gap this exists to
// find.
TEST(MatchBridgeMechanics, PushbackReachesTheKernelAndMovesTheDefender) {
    constexpr std::int32_t kPushback = px(12);

    CharacterData c = syntheticCharacter(1);
    c.moves[0].pushbackSub = kPushback;
    c.RebuildIndices();

    BuildOptions options{};
    options.bindings = { { c.moves[0].id, cse::kernel::kInputLP } };

    MatchBuild build{};
    ASSERT_TRUE(BuildMatchData(c, options, c, options, build))
        << build.report[0].error;

    const cse::kernel::MoveDef& poke = build.data.p[0].moves[1];
    EXPECT_EQ(poke.pushbackHit, static_cast<std::int16_t>(kPushback))
        << "the file authors " << kPushback
        << " sub-units of pushback and the bridge handed the kernel "
        << poke.pushbackHit
        << ". A field the bridge drops is a mechanic the game does not have, "
           "however well the kernel implements it.";

    // AND IT MOVES SOMEBODY, which a field comparison alone cannot say.
    cse::kernel::GameState s{};
    cse::kernel::ResetMatch(s, 0xC0FFEEu);
    s.p[0].posX   = -px(20);
    s.p[1].posX   =  px(20);
    s.p[0].facing = 0;
    s.p[1].facing = 1;
    for (int i = 0; i < 2; ++i) {
        s.p[i].moveId    = 1;
        s.p[i].moveFrame = static_cast<std::uint16_t>(c.moves[0].startup);
    }

    const std::int32_t before = s.p[1].posX;
    cse::kernel::ResolveHits(s, build.data);
    ASSERT_NE(s.p[1].pushX, 0)
        << "the defender was hit and no pushback was queued, so nothing will "
           "move them on any later tick either.";

    cse::kernel::Simulate(s, cse::kernel::InputPair{}, build.data);
    EXPECT_GT(s.p[1].posX, before)
        << "the defender stands to the RIGHT of the attacker and ended at "
        << s.p[1].posX << " from " << before
        << ". Pushback runs AWAY from the attacker, derived from the position "
           "difference rather than from facing.";
}
