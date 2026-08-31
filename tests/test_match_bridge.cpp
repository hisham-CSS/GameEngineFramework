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

// Where THIS PROJECT'S OWN characters live, as distinct from charactersDir()
// above, which finds the MUGEN corpus in tests/fixtures. Two directories because
// they are two kinds of thing: the corpus is evidence that cannot ship, and
// `fighter_a` is game content that is staged beside the executables.
//
// A fifth copy of this walk -- test_gap_extent, test_ground_truth and
// test_one_frame each carry one -- and the same debt ROADMAP M1.6 records
// against the witness driver: the duplication is written down rather than
// pretended away.
std::string ownCharactersDir() {
    namespace fs = std::filesystem;
    fs::path here = fs::current_path();
    for (int i = 0; i < 8; ++i) {
        const fs::path staged = here / "Exported" / "Characters";
        if (fs::exists(staged / "fighter_a.json")) return staged.string();
        const fs::path source =
            here / "Games" / "UntitledFighter" / "Assets" / "Characters";
        if (fs::exists(source / "fighter_a.json")) return source.string();
        if (!here.has_parent_path() || here.parent_path() == here) break;
        here = here.parent_path();
    }
    return "Exported/Characters";
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
    // StepAttack takes the first move in slot order whose buttons are ALL held
    // and whose stance the input selects. Kung Fu Girl's transcription authors
    // `stance: ground` on BOTH stand_lp and crouch_lp -- MUGEN's statetype is
    // not the standing/crouching split -- so stance cannot tell the pair apart,
    // the shadow is real, and holding Down+LP can only ever produce stand_lp
    // (slot 1, {LP}, against crouch_lp's {Down, LP}). A character that DOES
    // author the split gets selection instead of a warning since ROADMAP
    // M1.3e; this test is the case the warning still exists for.
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
        // Exact since M1.3 slice (a) -- the mask carries `on` whole; the count
        // is how many edges the old one-bit collapse used to move.
        { "cancel.on",                     4, BuildLossDirection::Exact         },
        { "cancel.certain",              103, BuildLossDirection::KernelPermits },
        { "cancel.guard",                 41, BuildLossDirection::KernelPermits },
        { "cancel.effect",                 0, BuildLossDirection::KernelOmits   },
        { "move.cancel_window (absent)",   8, BuildLossDirection::KernelPermits },
        { "character.walk_speed",      1, BuildLossDirection::Exact         },
        // Zero because her file authors no engine.movement -- she keeps the
        // placeholder arc, and the zero-count row is the proof a check ran
        // (ROADMAP M1.3(b1), ADR-014).
        { "character.movement",        0, BuildLossDirection::Exact         },
        // Zero for Kung Fu Girl and the zero is the exhibit: her transcript
        // DISABLES MUGEN's juggle system (airjuggle 0, nojugglecheck), the
        // resource is declared only for the positional contract, and no move
        // spends it -- so her gate, correctly, never fires.
        { "resource.juggle (gate)",    0, BuildLossDirection::Exact         },
        { "character.input_buffer_frames", 0, BuildLossDirection::Exact     },
        { "move.pushback",            24, BuildLossDirection::Exact         },
        // Zero: her converted file authors no corner push (the microwalk
        // slice's carry), and the zero records that somebody looked.
        { "move.corner_push",          0, BuildLossDirection::Exact         },
        // Zero for Kung Fu Girl and, like her juggle gate, the zero records
        // that somebody looked: her converted file authors no impact freeze,
        // so M1.3i's carried hitstop leaves every move of hers at 0.
        { "move.hitstop",              0, BuildLossDirection::Exact         },
        // Exact since ROADMAP M1.3e wired both into MoveDef and StanceAllows.
        // Kung Fu Girl authors a stance on all 25 moves and no blocked_as at
        // all -- the zero-count row records that somebody looked.
        { "move.stance",              25, BuildLossDirection::Exact         },
        { "move.blocked_as",           0, BuildLossDirection::Exact         },
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
        // Exact since M1.3(b2): motion keys are carried (she authors none --
        // the zero records the check), and the uncarried pos_add teleports
        // split into their own row.
        { "move.engine.motion",        0, BuildLossDirection::Exact         },
        { "move.engine.motion (pos_add)", 0, BuildLossDirection::KernelOmits },
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

// Knockdown, from the authored `engine.reaction` block. The kernel slot has
// existed since ADR-005 P2 and was zero for every built character.
//
// Hitstop used to be read by the loader and deliberately NOT carried here,
// because the freeze moved every frame-exact count in tests/test_gap_extent.cpp.
// M1.4 made those counts properties, M1.3i carried the freeze, and
// TheAuthoredFreezeReachesBothFightersAndATapInsideItBuffers below is the
// bridge test for it.
TEST(MatchBridgeMechanics, KnockdownReachesTheKernel) {
    CharacterData c = syntheticCharacter(2);

    // A knockdown is a PAIR here -- it knocks down and getting up takes time --
    // because this engine keeps liedown time per MOVE where MUGEN keeps it per
    // CHARACTER. A move that knocks down and gives no duration gets a warning
    // and no knockdown; AOF2's punk_b_kick is the real file that does it.
    c.moves[1].causesKnockdown  = true;
    c.moves[1].fallRecoverTicks = 20;
    c.RebuildIndices();

    BuildOptions options{};
    options.bindings = { { c.moves[0].id, cse::kernel::kInputLP },
                         { c.moves[1].id, cse::kernel::kInputMP } };

    MatchBuild build{};
    ASSERT_TRUE(BuildMatchData(c, options, c, options, build))
        << build.report[0].error;

    EXPECT_EQ(build.data.p[0].moves[2].knockdownTicks, 20u)
        << "the file says this move knocks down and that getting up takes 20 "
           "ticks; the bridge handed the kernel "
        << build.data.p[0].moves[2].knockdownTicks << ".";

    // AND THE MOVE THAT DOES NOT KNOCK DOWN CARRIES NOTHING, which is the half
    // that says `fall_recover_ticks` describes the knockdown rather than the
    // move. Slot 1 authors no knockdown at all.
    EXPECT_EQ(build.data.p[0].moves[1].knockdownTicks, 0u)
        << "a move that does not knock down was given a knockdown duration.";
}

// THE SHIPPED SWEEP ACTUALLY KNOCKS DOWN.
//
// Reported from play (2026-08-20): "if I do crouch hk it seems like it just goes
// into hitstun - doesn't show me the downed training dummy". Every test for this
// so far used a synthetic character, which proves the WIRE and not the FILE --
// and the file is where the answer turned out to be.
TEST(MatchBridgeMechanics, FighterAsSweepCarriesItsKnockdownIntoTheKernel) {
    CharacterData c{};
    LoadReport report{};
    ASSERT_TRUE(LoadCharacterFile(ownCharactersDir(), "fighter_a.json", loadOptions(), c, report))
        << report.error;

    const MoveIndex sweep = c.FindMove("crouch_hk");
    ASSERT_NE(sweep, cse::data::kInvalidMove) << "fighter_a has no crouch_hk";

    EXPECT_TRUE(c.moves[sweep].causesKnockdown)
        << "the loader did not pick up crouch_hk's "
           "engine.reaction.causes_knockdown, so nothing downstream can.";
    EXPECT_GT(c.moves[sweep].fallRecoverTicks, 0)
        << "crouch_hk knocks down and the loader read a recovery of "
        << c.moves[sweep].fallRecoverTicks
        << "; the kernel counts the knockdown down from that number, so zero is "
           "no knockdown at all.";

    BuildOptions options{};
    options.bindings = { { "crouch_hk", cse::kernel::kInputHK } };
    MatchBuild build{};
    ASSERT_TRUE(BuildMatchData(c, options, c, options, build)) << build.report[0].error;

    const std::uint16_t slot = build.moves[0].Find("crouch_hk");
    ASSERT_NE(slot, 0u) << "crouch_hk did not reach the kernel's move table";
    EXPECT_GT(build.data.p[0].moves[slot].knockdownTicks, 0u)
        << "the bridge handed the kernel a knockdown of "
        << build.data.p[0].moves[slot].knockdownTicks
        << " for a move the file says knocks down.";
}

// --- The stance wire (ROADMAP M1.3e) -----------------------------------------
//
// The bridge finding that moved the headline: MoveDef::stance was never
// assigned, so every built move was kStanceAny, stand_hk shadowed crouch_hk on
// their shared button, and 12 of fighter_a's 18 normals could not be performed
// at all. These two prove the wire on the SHIPPED file, end to end through
// BuildMatchData and the real kernel -- the layer every earlier attempt proved
// at the wrong one.

TEST(MatchBridgeMechanics, ADirectionEstablishesTheStanceOnTheTickItIsPressed) {
    CharacterData c{};
    LoadReport report{};
    ASSERT_TRUE(LoadCharacterFile(ownCharactersDir(), "fighter_a.json", loadOptions(), c, report))
        << report.error;

    // Both variants on the ONE button, standing first -- the exact shadowing
    // pair the finding names.
    BuildOptions options{};
    options.bindings = { { "stand_hk",  cse::kernel::kInputHK },
                         { "crouch_hk", cse::kernel::kInputHK } };
    MatchBuild build{};
    ASSERT_TRUE(BuildMatchData(c, options, c, options, build)) << build.report[0].error;

    const std::uint16_t standSlot  = build.moves[0].Find("stand_hk");
    const std::uint16_t crouchSlot = build.moves[0].Find("crouch_hk");
    ASSERT_NE(standSlot,  0u);
    ASSERT_NE(crouchSlot, 0u);

    // Down+HK from idle: the crouching variant, on that very tick.
    cse::kernel::GameState s{};
    cse::kernel::ResetMatch(s, 0x1D7u);
    cse::kernel::InputPair in{};
    in.p[0].bits = cse::kernel::kInputDown | cse::kernel::kInputHK;
    cse::kernel::Simulate(s, in, build.data);
    EXPECT_EQ(s.p[0].moveId, crouchSlot)
        << "Down+HK started move " << s.p[0].moveId << " and not crouch_hk ("
        << crouchSlot << "). Without the stance wire the first slot sharing "
        << "the button shadows the rest, and the sweep cannot be performed.";

    // HK alone: the standing one.
    cse::kernel::GameState s2{};
    cse::kernel::ResetMatch(s2, 0x1D7u);
    cse::kernel::InputPair hk{};
    hk.p[0].bits = cse::kernel::kInputHK;
    cse::kernel::Simulate(s2, hk, build.data);
    EXPECT_EQ(s2.p[0].moveId, standSlot)
        << "HK with no direction started move " << s2.p[0].moveId
        << " and not stand_hk (" << standSlot << ").";
}

// M1.1c's central claim -- "every normal fighter_a authors is reachable" --
// re-tested THROUGH BuildMatchData, which is the layer its original test
// skipped. Each variant is asked for the way a player asks: its button plus
// the stance-establishing direction, from idle, and it must start on that
// tick under its own name.
TEST(MatchBridgeMechanics, EveryAuthoredNormalIsReachableThroughItsButtonAndStance) {
    CharacterData c{};
    LoadReport report{};
    ASSERT_TRUE(LoadCharacterFile(ownCharactersDir(), "fighter_a.json", loadOptions(), c, report))
        << report.error;

    const char* kButtons[] = { "lp", "mp", "hp", "lk", "mk", "hk" };
    const std::uint16_t kBits[] = {
        cse::kernel::kInputLP, cse::kernel::kInputMP, cse::kernel::kInputHP,
        cse::kernel::kInputLK, cse::kernel::kInputMK, cse::kernel::kInputHK };

    BuildOptions options{};
    for (int b = 0; b < 6; ++b) {
        options.bindings.push_back({ std::string("stand_")  + kButtons[b], kBits[b] });
        options.bindings.push_back({ std::string("crouch_") + kButtons[b], kBits[b] });
        options.bindings.push_back({ std::string("air_")    + kButtons[b], kBits[b] });
    }
    MatchBuild build{};
    ASSERT_TRUE(BuildMatchData(c, options, c, options, build)) << build.report[0].error;

    int reachable = 0;
    for (int b = 0; b < 6; ++b) {
        const struct { std::string name; std::uint16_t extra; } variants[] = {
            { std::string("stand_")  + kButtons[b], 0 },
            { std::string("crouch_") + kButtons[b], cse::kernel::kInputDown },
            { std::string("air_")    + kButtons[b], cse::kernel::kInputUp },
        };
        for (const auto& v : variants) {
            const std::uint16_t slot = build.moves[0].Find(v.name);
            ASSERT_NE(slot, 0u) << v.name << " did not reach the move table";

            cse::kernel::GameState s{};
            cse::kernel::ResetMatch(s, 0x1D7u);
            cse::kernel::InputPair in{};
            in.p[0].bits = static_cast<std::uint16_t>(kBits[b] | v.extra);
            cse::kernel::Simulate(s, in, build.data);

            EXPECT_EQ(s.p[0].moveId, slot)
                << v.name << " (slot " << slot << ") did not start; moveId is "
                << s.p[0].moveId << ". A normal the player cannot perform is "
                << "authored frame data the game does not have.";
            if (s.p[0].moveId == slot) ++reachable;
        }
    }
    EXPECT_EQ(reachable, 18)
        << "only " << reachable << " of fighter_a's 18 normals are reachable.";
}

// --- The juggle wiring (ROADMAP M1.1f) ---------------------------------------
//
// The budget gate has sat in ResolveHits since ADR-005 P2 -- "the mechanism
// the prover's ranking certificate is written in" -- and had never fired for a
// built character because MatchBuilder set neither half. Both halves or
// neither: a budget with no cost never depletes, a cost with no budget refuses
// every hit. Fighter::juggle is the MIRROR of the resource the file calls
// juggle: same authored numbers, gating where ApplyEffects only clamps.
TEST(MatchBridgeMechanics, TheJuggleBudgetReachesTheKernelAndRefusesTheOverspendingHit) {
    CharacterData c{};
    LoadReport report{};
    ASSERT_TRUE(LoadCharacterFile(ownCharactersDir(), "fighter_a.json", loadOptions(), c, report))
        << report.error;

    BuildOptions options{};
    options.bindings = { { "air_mp", cse::kernel::kInputMP },
                         { "air_hp", cse::kernel::kInputHP },
                         { "stand_lp", cse::kernel::kInputLP } };
    MatchBuild build{};
    ASSERT_TRUE(BuildMatchData(c, options, c, options, build)) << build.report[0].error;

    // The budget is the juggle resource's own initial, and each cost is the
    // move's authored juggle spend -- the same numbers the certificate ranks.
    EXPECT_EQ(build.data.p[0].juggleMax, 4)
        << "fighter_a's juggle resource authors initial 4 and the bridge "
           "handed the kernel " << build.data.p[0].juggleMax;
    const std::uint16_t airMp = build.moves[0].Find("air_mp");
    const std::uint16_t airHp = build.moves[0].Find("air_hp");
    const std::uint16_t lp    = build.moves[0].Find("stand_lp");
    ASSERT_NE(airMp, 0u); ASSERT_NE(airHp, 0u); ASSERT_NE(lp, 0u);
    EXPECT_EQ(build.data.p[0].moves[airMp].juggleCost, 1);
    EXPECT_EQ(build.data.p[0].moves[airHp].juggleCost, 2);
    EXPECT_EQ(build.data.p[0].moves[lp].juggleCost, 0)
        << "a move that spends no juggle must cost none, or every hit is gated";

    // AND THE GATE FIRES: the same overlapping hit lands with budget left and
    // is REFUSED -- not damaged, not scaled, refused -- one point short.
    for (const std::int16_t budget : { std::int16_t{1}, std::int16_t{0} }) {
        cse::kernel::GameState s{};
        cse::kernel::ResetMatch(s, 0xC0FFEEu);
        s.p[0].posX = -px(10);
        s.p[1].posX =  px(10);
        s.p[0].facing = 0;
        s.p[1].facing = 1;
        s.p[0].airborne  = 1;
        s.p[0].posY      = px(30);
        s.p[0].moveId    = airMp;
        s.p[0].moveFrame = static_cast<std::uint16_t>(
            build.data.p[0].moves[airMp].startup);
        s.p[1].juggle    = budget;
        s.p[1].hitstun   = 30;   // mid-juggle: the restore must not refill

        const std::int32_t before = s.p[1].health;
        cse::kernel::ResolveHits(s, build.data);
        if (budget >= 1) {
            EXPECT_LT(s.p[1].health, before)
                << "a hit within the budget was refused";
            EXPECT_EQ(s.p[1].juggle, budget - 1)
                << "the budget was not spent by the hit that used it";
        } else {
            EXPECT_EQ(s.p[1].health, before)
                << "the budget was overspent: air_mp costs 1 against a "
                   "defender with 0 left, and the gate let it land";
        }
    }
}

// --- Hitstop crosses the bridge (ROADMAP M1.3i) ------------------------------
//
// The kernel has frozen both fighters on impact since ADR-005 P2; the bridge
// held the authored value back until M1.4 turned the frame-exact counts into
// properties, because the freeze moves every wall-clock number while moving no
// frame-data relationship -- startup 5 is still five ticks OF THE MOVE. Two
// halves proved on the shipped file: the authored freeze reaches BOTH
// fighters, and a tap made entirely inside it still buffers -- the M1.3f
// placement rule, exercised with a real authored value instead of a synthetic
// one, which the crossplat golden (no moves, no hitstop) can never do.
TEST(MatchBridgeMechanics, TheAuthoredFreezeReachesBothFightersAndATapInsideItBuffers) {
    CharacterData c{};
    LoadReport report{};
    ASSERT_TRUE(LoadCharacterFile(ownCharactersDir(), "fighter_a.json", loadOptions(), c, report))
        << report.error;

    const MoveIndex lp = c.FindMove("stand_lp");
    ASSERT_NE(lp, cse::data::kInvalidMove);
    ASSERT_EQ(c.moves[lp].hitstopTicks, 8)
        << "the loader no longer reads stand_lp's authored freeze";

    BuildOptions options{};
    options.bindings = { { "stand_lp", cse::kernel::kInputLP },
                         { "stand_mp", cse::kernel::kInputMP } };
    MatchBuild build{};
    ASSERT_TRUE(BuildMatchData(c, options, c, options, build)) << build.report[0].error;

    const std::uint16_t lpSlot = build.moves[0].Find("stand_lp");
    ASSERT_NE(lpSlot, 0u);
    EXPECT_EQ(build.data.p[0].moves[lpSlot].hitstop, 8)
        << "the bridge dropped the freeze; MoveDef::hitstop is "
        << build.data.p[0].moves[lpSlot].hitstop;

    // Both fighters, with the buffer live so the tap-confirm can be watched.
    build.data.p[0].inputBufferFrames = 2;
    build.data.p[1].inputBufferFrames = 2;

    cse::kernel::GameState s{};
    cse::kernel::ResetMatch(s, 0x1D7u);
    s.p[0].posX = -px(10);
    s.p[1].posX =  px(10);
    s.p[0].facing = 0;
    s.p[1].facing = 1;

    cse::kernel::InputPair lpPress{};
    lpPress.p[0].bits = cse::kernel::kInputLP;
    cse::kernel::Simulate(s, lpPress, build.data);          // stand_lp starts
    ASSERT_EQ(s.p[0].moveId, lpSlot);

    // Run to the contact tick: startup 3 means the box first lands on the
    // move's third frame, and the freeze is set by ResolveHits that tick.
    cse::kernel::InputPair none{};
    while (s.p[0].hitstop == 0) {
        cse::kernel::Simulate(s, none, build.data);
        ASSERT_NE(s.p[0].moveId, 0u) << "stand_lp ran out before it connected";
    }
    // Observed on the tick ResolveHits SET it, before any decrement -- the
    // freeze starts counting on the next tick's physics.
    EXPECT_EQ(s.p[0].hitstop, 8)
        << "the ATTACKER'S freeze is not the authored 8";
    EXPECT_EQ(s.p[1].hitstop, 8)
        << "the DEFENDER'S freeze is not the authored 8: hitstop freezes BOTH "
           "fighters, or the impact reads as the defender lagging";
    const std::uint16_t frameAtFreeze = s.p[0].moveFrame;

    // THE TAP-CONFIRM: press and RELEASE stand_mp's button entirely inside
    // the freeze. M1.3f's placement rule says recording runs during hitstop
    // with aging suspended, so the tap must come out as the cancel the moment
    // the move can take it -- eaten, it is the exact input this genre's
    // confirm culture is built on.
    cse::kernel::InputPair mpTap{};
    mpTap.p[0].bits = cse::kernel::kInputMP;
    cse::kernel::Simulate(s, mpTap, build.data);            // pressed, frozen
    cse::kernel::Simulate(s, none, build.data);             // released, frozen
    EXPECT_EQ(s.p[0].moveFrame, frameAtFreeze)
        << "the move advanced during its own freeze";

    const std::uint16_t mpSlot = build.moves[0].Find("stand_mp");
    ASSERT_NE(mpSlot, 0u);
    bool cancelled = false;
    for (int t = 0; t < 30 && !cancelled; ++t) {
        cse::kernel::Simulate(s, none, build.data);
        cancelled = s.p[0].moveId == mpSlot;
    }
    EXPECT_TRUE(cancelled)
        << "the tap made entirely inside the authored freeze never came out: "
           "the freeze ate the confirm, which is the regression the three "
           "P3Input freeze tests pin on synthetic data and this one pins on "
           "the shipped file.";
}

// JUMP PHYSICS, from engine.movement (ROADMAP M1.3(b1), ADR-014 step one).
// FighterData::jumpImpulseSub and gravitySub have been kernel-consulted since
// M1.1b -- `velY = jumpImpulseSub != 0 ? authored : kJumpImpulse` -- and
// nothing could author them until this wire. The full jump-as-move flip is
// ADR-014's step three; THIS test owns step one's claims: the numbers arrive,
// they change the arc the kernel actually flies, silence keeps the placeholder
// byte for byte, and the loader refuses the sentinel zero by name.
namespace {

// Press Up once from idle and count the ticks the fighter is observably
// airborne. The convention matches P2Movement's: the landing tick clears the
// flag, so an arc whose clamp fires on tick n reads as n-1 airborne ticks.
int airTicksUnder(const MatchData& data) {
    cse::kernel::GameState s{};
    cse::kernel::ResetMatch(s, 0xC0FFEEu);

    cse::kernel::InputPair up{};
    up.p[0].bits = cse::kernel::kInputUp;
    cse::kernel::Simulate(s, up, data);
    EXPECT_NE(s.p[0].airborne, 0u) << "the Up press did not take off at all";

    int air = s.p[0].airborne ? 1 : 0;
    for (int t = 0; t < 400 && s.p[0].airborne; ++t) {
        cse::kernel::Simulate(s, cse::kernel::InputPair{}, data);
        if (s.p[0].airborne) ++air;
    }
    return air;
}

}  // namespace

TEST(MatchBridgeMechanics, TheAuthoredJumpPhysicsChangeTheArcAndSilenceKeepsIt) {
    // --- the carry, and the ledger row that records it -----------------------
    CharacterData c = syntheticCharacter(1);
    c.jumpImpulseSub = 2560;   // 10 px/tick, double the kernel placeholder
    c.gravitySub     = 64;     // the placeholder's own gravity, now AUTHORED
    c.RebuildIndices();

    BuildOptions options{};
    options.bindings = { { c.moves[0].id, cse::kernel::kInputLP } };
    MatchBuild build{};
    ASSERT_TRUE(BuildMatchData(c, options, c, options, build))
        << build.report[0].error;

    EXPECT_EQ(build.data.p[0].jumpImpulseSub, 2560)
        << "the authored takeoff velocity did not reach FighterData; the "
           "kernel has consulted this slot since M1.1b and nothing arrived.";
    EXPECT_EQ(build.data.p[0].gravitySub, 64);

    const BuildLoss* row = findLoss(build.report[0], "character.movement");
    ASSERT_NE(row, nullptr) << "the carry has no ledger row; ADR-011's five "
                               "parts are four";
    EXPECT_EQ(row->count, 2);
    EXPECT_EQ(row->direction, BuildLossDirection::Exact);

    // --- the physics: the arc is the AUTHORED parabola -----------------------
    //
    // J = 2560, G = 64: gravity applies on the takeoff tick, so the landing
    // clamp fires on tick 2*(J/G) - 1 = 79 and the fighter reads airborne for
    // 78 ticks -- exactly double-impulse doubling the placeholder's 38 (the
    // arithmetic P2Movement and the crossplat script derive from J = 1280,
    // G = 64). Asserted as the exact integer, because "longer" alone would
    // pass a wire that dropped gravity and carried only the impulse.
    EXPECT_EQ(airTicksUnder(build.data), 78)
        << "the authored jump does not fly the authored parabola";

    // --- silence: an unauthored file keeps the placeholder arc ---------------
    CharacterData silent = syntheticCharacter(1);
    silent.RebuildIndices();
    MatchBuild silentBuild{};
    ASSERT_TRUE(BuildMatchData(silent, options, silent, options, silentBuild))
        << silentBuild.report[0].error;
    EXPECT_EQ(silentBuild.data.p[0].jumpImpulseSub, 0);
    EXPECT_EQ(silentBuild.data.p[0].gravitySub, 0);
    EXPECT_EQ(airTicksUnder(silentBuild.data), 38)
        << "a character that authored nothing lost the placeholder arc every "
           "measured count in this suite was derived on";
    const BuildLoss* silentRow = findLoss(silentBuild.report[0], "character.movement");
    ASSERT_NE(silentRow, nullptr);
    EXPECT_EQ(silentRow->count, 0) << "the zero-count row is the proof a check ran";

    // --- and the shipped file is the silent case, on purpose -----------------
    //
    // ADR-014: base fighter_a does NOT author engine.movement (the M1.1e
    // buffer precedent), so its hash, its 38-tick arc and the whole measured
    // suite stay put; the floaty_jump VARIANT is where the authored arc shows.
    CharacterData fa{};
    LoadReport faReport{};
    ASSERT_TRUE(LoadCharacterFile(ownCharactersDir(), "fighter_a.json",
                                  loadOptions(), fa, faReport))
        << faReport.error;
    EXPECT_EQ(fa.jumpImpulseSub, 0);
    EXPECT_EQ(fa.gravitySub, 0);

    // --- the sentinel is unauthorable, by refusal ----------------------------
    //
    // Zero means "unauthored, use the placeholder" to the kernel's `!= 0`
    // fallback; an author who writes 0 means "no jump", and silently handing
    // them the placeholder would be the scalingReduction incident again.
    const char* kZeroJump =
        R"({"name":"z","stage":"corner","walk_speed":0.5,)"
        R"("resources":[{"name":"meter","initial":0,"floor":0},)"
        R"({"name":"juggle","initial":4,"floor":0}],)"
        R"("scaling":{},"decay":{"kind":"none"},)"
        R"("moves":[{"id":"jab","startup":3,"active":2,"recovery":4,)"
        R"("hitstun":8,"damage":10.0,"stance":"standing"}],"cancels":[],)"
        R"("engine":{"movement":{"jump_impulse_sub":0}}})";
    CharacterData zc{};
    LoadReport zr{};
    EXPECT_FALSE(LoadCharacterJson("zero_jump.json", kZeroJump, loadOptions(),
                                   zc, zr))
        << "an explicit jump_impulse_sub of 0 loaded, and the kernel will "
           "silently replace it with the placeholder";
    EXPECT_NE(zr.error.find("movement"), std::string::npos) << zr.error;
}

// MOTION KEYS, and the ONE sign flip (ROADMAP M1.3(b2), ADR-014 step two).
// Measured on fighter_a's own authored specials rather than a synthetic, so
// the MUGEN Y-down convention in the file is the thing being tested: the
// bridge negates velYSub exactly once, keeps velXSub as authored, sorts by
// tick, and counts what it carried.
TEST(MatchBridgeMechanics, TheAuthoredMotionKeysCrossWithTheirOneSignFlip) {
    CharacterData c{};
    LoadReport report{};
    ASSERT_TRUE(LoadCharacterFile(ownCharactersDir(), "fighter_a.json",
                                  loadOptions(), c, report))
        << report.error;

    const cse::data::MoveIndex up = c.FindMove("special_uppercut");
    ASSERT_NE(up, cse::data::kInvalidMove);
    ASSERT_FALSE(c.moves[up].motion.empty())
        << "fighter_a's special_uppercut no longer authors motion, so this "
           "test is measuring nothing -- pick another authored special.";

    BuildOptions options{};
    options.bindings = { { "special_uppercut", cse::kernel::kInputHP } };
    MatchBuild build{};
    ASSERT_TRUE(BuildMatchData(c, options, c, options, build))
        << build.report[0].error;

    const std::uint16_t slot = build.moves[0].Find("special_uppercut");
    ASSERT_NE(slot, 0u);
    const cse::kernel::MoveDef& m = build.data.p[0].moves[slot];

    ASSERT_EQ(static_cast<std::size_t>(m.motionCount), c.moves[up].motion.size())
        << "the bridge carried a different number of keys than the file "
           "authors (and the file authors fewer than the kernel bound)";
    std::int32_t prevTick = -1;
    for (std::int32_t i = 0; i < m.motionCount; ++i) {
        SCOPED_TRACE(i);
        const cse::data::MotionKey& a = c.moves[up].motion[static_cast<std::size_t>(i)];
        EXPECT_EQ(m.motion[i].fromTick, a.tick);
        EXPECT_EQ(m.motion[i].velXSub, a.velXSub)
            << "velX is forward-positive in BOTH conventions; nothing may "
               "touch it";
        EXPECT_EQ(m.motion[i].velYSub, -a.velYSub)
            << "the MUGEN Y-down to kernel Y-up flip happens exactly once, "
               "here -- an unflipped uppercut DIVES";
        EXPECT_GE(m.motion[i].fromTick, prevTick)
            << "keys must arrive sorted; the kernel takes the last "
               "at-or-before frame and an unsorted table makes that a "
               "different key on two builds of one file";
        prevTick = m.motion[i].fromTick;
    }

    // The row flipped direction the day the keys crossed; the pos_add split
    // records the one teleport the corpus authors (fighter_a_infinite, not
    // this file).
    const BuildLoss* motion = findLoss(build.report[0], "move.engine.motion");
    ASSERT_NE(motion, nullptr);
    EXPECT_EQ(motion->direction, BuildLossDirection::Exact);
    EXPECT_GE(motion->count, 2) << "fighter_a authors motion on two specials";
    const BuildLoss* posAdd =
        findLoss(build.report[0], "move.engine.motion (pos_add)");
    ASSERT_NE(posAdd, nullptr);
    EXPECT_EQ(posAdd->count, 0) << "fighter_a authors no teleport keys";
}
