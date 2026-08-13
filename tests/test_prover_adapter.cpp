// The combo-prover adapter, against the three characters that actually exist
// and against synthetic ones built to make each soundness check fail.
//
// THE THING TO READ FIRST, because it looks like a discrepancy and is not.
//
// ADR-001 section 2's verdict table has TWO rows, and they disagree:
//
//     verdict, midscreen | INFINITE | TERMINATING | TERMINATING
//     verdict, corner    | INFINITE | INFINITE    | TERMINATING
//
// `comboprover.hpp` is corner-only and says so at :15-24 -- it does not model
// the distance between the players, because against the wall there is no
// distance. So an adapter built on it answers the CORNER row, and Kung Fu Man
// is INFINITE there. The midscreen TERMINATING is a real and different answer
// produced by the Python reference, which models position and walking and takes
// 226 ms to do it; ADR-001 section 5 says both are correct and that a panel
// showing one without naming the stage is showing a coin flip.
//
// This is not an expectation adjusted to make a test pass. Every other number
// ADR-001 measured through the C++ header on these files comes out of this
// adapter unchanged -- 63 / 79 / 10 configurations explored, 123/7, 39/48 and
// 0/26 usable-versus-dead cancels -- and those are asserted below alongside the
// verdicts. A projection that reproduced the counts but not the verdicts would
// be a defect; one that reproduces all of them is answering the corner row,
// which is the row the corner-only procedure has.
//
// The second half of this file builds characters by hand whose only purpose is
// to make a check fail. A check nobody has watched reject anything is not a
// check, and the ceiling clamp in particular exists because ADR-001 section 8
// item 4 made it the adapter's first correctness decision -- so there is a
// character here that is INFINITE only because meter was allowed past three
// bars, and the test asserts that the replay notices.
#include <gtest/gtest.h>

#include "cse/data/CharacterData.h"
#include "cse/data/ProverAdapter.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

using namespace cse::data;

namespace {

// The build-wide resource order. ADR-001 section 8 item 7: the prover keys
// resources POSITIONALLY, so index 0 must mean the same resource in every file
// a build loads, and the adapter re-asserts it at its own seam.
const std::vector<std::string> kBuildResources = { "meter", "juggle" };

LoadOptions loadOptions() {
    LoadOptions o;
    o.expectedResources = kBuildResources;
    return o;
}

ProverOptions proverOptions() {
    ProverOptions o;
    o.expectedResources = kBuildResources;
    return o;
}

// Where the shipped characters live. Mirrors test_character_data.cpp: the
// walk-up fallback keeps the test runnable from a shell in the tree and cannot
// make anything pass vacuously, because every load below ASSERTs.
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

void analyseShipped(const char* file, CharacterData& character, ProverResult& result) {
    loadShipped(file, character);
    if (::testing::Test::HasFatalFailure()) return;
    ProverReport report{};
    ASSERT_TRUE(AnalyseCharacter(character, proverOptions(), result, report))
        << file << " failed to project: " << report.error;
}

const ProjectionLoss* findLoss(const ProverResult& result, const char* field,
                               LossDirection direction) {
    for (const ProjectionLoss& loss : result.losses)
        if (loss.field == field && loss.direction == direction) return &loss;
    return nullptr;
}

// --- Synthetic characters ----------------------------------------------------
//
// Built through the same struct the loader fills, then RebuildIndices(), which
// is the documented path for a character assembled by hand (CharacterData.h).

ResourceDef resource(const char* name, std::int32_t initial, bool hasCeiling,
                     std::int32_t ceiling) {
    ResourceDef r{};
    r.name       = name;
    r.initial    = initial;
    r.floor      = 0;
    r.hasCeiling = hasCeiling;
    r.ceiling    = ceiling;
    return r;
}

Move move(const char* id, std::int32_t startup, std::int32_t hitstun) {
    Move m{};
    m.id       = id;
    m.startup  = startup;
    m.active   = 2;
    m.recovery = 5;
    m.hitstun  = hitstun;
    // Explicit, and NOT the struct's default. `Move::reachSub` defaults to
    // kNoReach, which the adapter reads as "the file declined to state a reach
    // because contact happens when a projectile arrives rather than when it is
    // released" -- so a hand-built character that never touches the field is a
    // character made entirely of fireballs, and every test below would trip the
    // conservative alarm for a reason that has nothing to do with what it is
    // testing. Found by writing the test.
    m.reachSub = 100 * 256;
    return m;
}

Cancel cancel(MoveIndex from, MoveIndex to, std::int32_t delay) {
    Cancel c{};
    c.from  = from;
    c.to    = to;
    c.delay = delay;
    c.on    = Contact::Hit;
    return c;
}

// Two moves that cancel into each other with frames to spare. The smallest
// character that is obviously infinite by inspection: jab leaves 12 frames of
// hitstun and short needs 4 of them; short leaves 14 and jab needs 3.
CharacterData twoMoveInfinite() {
    CharacterData c{};
    c.id   = "synthetic_two_move";
    c.name = "Two-Move Loop";
    c.resources.push_back(resource("meter", 0, false, 0));
    c.resources.push_back(resource("juggle", 0, false, 0));
    c.moves.push_back(move("jab",   3, 12));
    c.moves.push_back(move("short", 4, 14));
    c.cancels.push_back(cancel(0, 1, 0));
    c.cancels.push_back(cancel(1, 0, 0));
    c.starters.push_back(0);
    c.RebuildIndices();
    return c;
}

// A character whose only cycle passes through a move nobody can afford once the
// meter ceiling is honoured. Without the ceiling, three chained gains reach 450
// and `big` opens; with it, meter never leaves 300 and the cycle is unreachable.
//
// This is the shape ADR-001 section 5 is about: the editor and the offline run
// searching different state spaces. It errs PERMISSIVE -- the unclamped run
// invents an infinite the game cannot have -- which is why the verdict is still
// INFINITE below and it is `loopHoldsUnderCeilings` that carries the truth.
CharacterData ceilingOnlyLoop(bool withCeiling) {
    CharacterData c{};
    c.id   = "synthetic_ceiling";
    c.name = "Ceiling-Only Loop";
    c.resources.push_back(resource("meter", 300, withCeiling, 300));
    c.resources.push_back(resource("juggle", 0, false, 0));

    c.moves.push_back(move("jab",   3, 12));   // 0
    c.moves.push_back(move("jab2",  3, 12));   // 1
    c.moves.push_back(move("jab3",  3, 12));   // 2
    c.moves.push_back(move("big",   5, 20));   // 3
    for (MoveIndex m = 0; m < 3; ++m) {
        ResourceAmount gain{};
        gain.resource = 0;
        gain.value    = 50;
        c.moves[m].effect.push_back(gain);
    }
    ResourceAmount spend{};
    spend.resource = 0;
    spend.value    = -100;
    c.moves[3].effect.push_back(spend);
    ResourceAmount need{};
    need.resource = 0;
    need.value    = 400;              // above the ceiling: only overflow reaches it
    c.moves[3].guard.push_back(need);

    c.cancels.push_back(cancel(0, 1, 0));   // jab  -> jab2
    c.cancels.push_back(cancel(1, 2, 0));   // jab2 -> jab3
    c.cancels.push_back(cancel(2, 3, 0));   // jab3 -> big
    c.cancels.push_back(cancel(3, 1, 0));   // big  -> jab2, closing the cycle
    c.starters.push_back(0);
    c.RebuildIndices();
    return c;
}

} // namespace

// ---------------------------------------------------------------------------
// The three real characters
// ---------------------------------------------------------------------------

TEST(ProverAdapter, AllThreeShippedCharactersProject) {
    const char* files[] = { "kung_fu_girl.json", "kung_fu_man.json",
                            "aof2_strength_training.json" };
    for (const char* file : files) {
        CharacterData character{};
        ProverResult  result{};
        ASSERT_NO_FATAL_FAILURE(analyseShipped(file, character, result)) << file;
        EXPECT_GT(character.moves.size(), 0u) << file;
        EXPECT_GT(character.cancels.size(), 0u) << file;
        EXPECT_FALSE(DescribeVerdict(character, result).empty()) << file;
    }
}

// The measured verdicts. See the note at the top of this file about which row
// of ADR-001 section 2's table a corner-only procedure answers.
TEST(ProverAdapter, KungFuGirlIsInfinite) {
    CharacterData character{};
    ProverResult  result{};
    ASSERT_NO_FATAL_FAILURE(analyseShipped("kung_fu_girl.json", character, result));

    EXPECT_EQ(result.status, ProverStatus::Infinite)
        << DescribeVerdict(character, result);
    // ADR-001 section 5 identifies the loop from the CNS: stand_lp into itself,
    // authored at `Kung Fu Girl.cmd:756`, 9 frames of hitstun against delay 2
    // and startup 3. The witness must actually name a move, or "infinite" is a
    // word with nothing behind it.
    ASSERT_FALSE(result.loop.empty());
    EXPECT_EQ(character.moves[result.loop.back()].id, "stand_lp");
}

TEST(ProverAdapter, KungFuManIsInfiniteInTheCornerAndThatIsTheOtherRow) {
    CharacterData character{};
    ProverResult  result{};
    ASSERT_NO_FATAL_FAILURE(analyseShipped("kung_fu_man.json", character, result));

    // ADR-001 section 2, `verdict, corner`. The midscreen row says TERMINATING
    // and is answered by a model this procedure does not implement. ADR-001
    // section 5: "Midscreen it dies to its own pushback; against the wall
    // nothing stops it", from the character's own 4-way light chain.
    EXPECT_EQ(result.status, ProverStatus::Infinite)
        << DescribeVerdict(character, result);
    ASSERT_FALSE(result.loop.empty());
    EXPECT_EQ(character.moves[result.loop.back()].id, "stand_lp");

    // The stage must be on the result, not in a comment, or a panel cannot tell
    // a designer which of the two answers they are looking at.
    EXPECT_EQ(result.stage, ProverStage::Corner);
    EXPECT_EQ(result.fileStage, "midscreen");
}

TEST(ProverAdapter, Aof2Terminates) {
    CharacterData character{};
    ProverResult  result{};
    ASSERT_NO_FATAL_FAILURE(analyseShipped("aof2_strength_training.json", character, result));

    EXPECT_EQ(result.status, ProverStatus::Terminating)
        << DescribeVerdict(character, result);
    EXPECT_TRUE(result.loop.empty());
    // Terminating with a worst case, which is what the panel renders. One hit,
    // because not one cancel in the character can connect.
    EXPECT_EQ(result.maxHits, 1);
    EXPECT_GT(result.maxFrames, 0);
    EXPECT_GT(result.maxDamageHundredths, 0);
}

// ADR-001 section 2 and section 3's C++ measurements, reproduced through this
// adapter. These are the guard against a verdict that is right by accident: a
// projection that dropped half the cancel table would still call Kung Fu Girl
// infinite, and would not reproduce 123 usable edges and 63 configurations.
TEST(ProverAdapter, TheAdr001CppMeasurementsReproduce) {
    struct Expected {
        const char*  file;
        std::int32_t explored;
        std::int32_t usable;
        std::size_t  dead;
    };
    const Expected expected[] = {
        { "kung_fu_girl.json",           63, 123,  7 },
        { "kung_fu_man.json",            79,  39, 48 },
        { "aof2_strength_training.json", 10,   0, 26 },
    };

    for (const Expected& e : expected) {
        CharacterData character{};
        ProverResult  result{};
        ASSERT_NO_FATAL_FAILURE(analyseShipped(e.file, character, result)) << e.file;

        EXPECT_EQ(result.explored, e.explored) << e.file;
        EXPECT_EQ(result.usableCancels, e.usable) << e.file;
        EXPECT_EQ(result.deadCancels.size(), e.dead) << e.file;
        // `capped` false everywhere is gate 2 of ADR-001: Status::Unknown was
        // never produced across three characters and two implementations.
        EXPECT_FALSE(result.capped) << e.file;
        EXPECT_NE(result.status, ProverStatus::Unknown) << e.file;
        // Every usable + dead edge must be accounted for, so that a projection
        // which silently dropped edges cannot satisfy the two counts above.
        const std::size_t onHitEdges = result.usableCancels + result.deadCancels.size();
        std::size_t authoredOnHit = 0;
        for (const Cancel& edge : character.cancels)
            if (edge.on == Contact::Hit || edge.on == Contact::Always) ++authoredOnHit;
        EXPECT_EQ(onHitEdges, authoredOnHit) << e.file;
    }
}

// ADR-001 section 3, gate 3: `hasRanking` is false in both terminating cases,
// for two different reasons, and the panel must not render them identically.
// The prediction the ADR corrects is that `spendOnly` explains it; AOF2 is
// spend-only and has no certificate anyway, because nothing loops at all.
TEST(ProverAdapter, TheMissingRankingCertificateHasMoreThanOneCause) {
    CharacterData kfm{}, aof2{};
    ProverResult  kfmResult{}, aofResult{};
    ASSERT_NO_FATAL_FAILURE(analyseShipped("kung_fu_man.json", kfm, kfmResult));
    ASSERT_NO_FATAL_FAILURE(analyseShipped("aof2_strength_training.json", aof2, aofResult));

    EXPECT_FALSE(aofResult.hasRanking);
    EXPECT_TRUE(aofResult.spendOnly);
    EXPECT_EQ(aofResult.rankingAbsence, RankingAbsence::NothingLoops);
    EXPECT_EQ(aofResult.usableCancels, 0);

    // Kung Fu Man clears spendOnly because `stand_lp` carries effect.meter = +5,
    // which ADR-001 section 5.3 calls the COMMON case: any character that builds
    // meter on hit lands here.
    EXPECT_FALSE(kfmResult.spendOnly);
    EXPECT_EQ(kfmResult.rankingAbsence, RankingAbsence::CharacterGainsAResource);

    // The point of the enum: these two must not be the same value.
    EXPECT_NE(kfmResult.rankingAbsence, aofResult.rankingAbsence);
}

// ADR-001 section 5, D8: both implementations evaluate every edge at the SETTLED
// hitstun, so a decay curve reports real cancels as dead. Kung Fu Man ships the
// house rule and Kung Fu Girl does not, so the two lists must differ on one and
// agree on the other -- which is exactly the information a designer staring at a
// dead-cancel list needs.
TEST(ProverAdapter, TheDeadCancelListIsAlsoReportedBeforeDecay) {
    CharacterData kfg{}, kfm{};
    ProverResult  kfgResult{}, kfmResult{};
    ASSERT_NO_FATAL_FAILURE(analyseShipped("kung_fu_girl.json", kfg, kfgResult));
    ASSERT_NO_FATAL_FAILURE(analyseShipped("kung_fu_man.json", kfm, kfmResult));

    ASSERT_EQ(kfg.decay.kind, DecayKind::None);
    EXPECT_EQ(kfgResult.deadCancels.size(), kfgResult.deadCancelsPreDecay.size())
        << "Kung Fu Girl ships decay `none`, so switching decay off must change nothing";

    ASSERT_EQ(kfm.decay.kind, DecayKind::Linear);
    ASSERT_GT(kfm.decay.step, 0);
    EXPECT_GT(kfmResult.deadCancels.size(), kfmResult.deadCancelsPreDecay.size())
        << "Kung Fu Man ships linear decay, so some of its 48 dead cancels are the "
           "decay curve talking and the pre-decay list must be shorter";
    EXPECT_GT(kfmResult.deadCancelsPreDecay.size(), 0u);
}

// Not one of the three shipped characters contains data whose projection can
// hide a real infinite. Asserted as a property of each Conservative row rather
// than of the summary flag, so a row that stopped being computed would show up.
TEST(ProverAdapter, NoConservativeLossIsLiveOnAnyShippedCharacter) {
    const char* files[] = { "kung_fu_girl.json", "kung_fu_man.json",
                            "aof2_strength_training.json" };
    for (const char* file : files) {
        CharacterData character{};
        ProverResult  result{};
        ASSERT_NO_FATAL_FAILURE(analyseShipped(file, character, result)) << file;

        EXPECT_FALSE(result.soundnessAlarm) << file << "\n" << DescribeVerdict(character, result);
        int conservativeRows = 0;
        for (const ProjectionLoss& loss : result.losses) {
            if (loss.direction != LossDirection::Conservative) continue;
            ++conservativeRows;
            EXPECT_EQ(loss.count, 0) << file << ": conservative loss `" << loss.field
                                     << "` is live: " << loss.note;
        }
        // Vacuity guard: the loop above passes trivially if no conservative row
        // is ever produced. There are four of them.
        EXPECT_EQ(conservativeRows, 4) << file;

        // And the permissive rows must not all be zero either, or the loss table
        // is not being filled in from this character's data at all.
        int livePermissive = 0;
        for (const ProjectionLoss& loss : result.losses)
            if (loss.direction == LossDirection::Permissive && loss.count > 0) ++livePermissive;
        EXPECT_GT(livePermissive, 0) << file;
    }
}

// ADR-001 section 4 group F, re-derived rather than cited. The claim is that the
// projectile contact frame is a CONSERVATIVE loss -- it can hide an infinite --
// and that it is inert across the corpus because those moves have no outgoing
// cancels. Both halves are checked here from the data.
TEST(ProverAdapter, TheProjectileContactFrameIsConservativeAndInertOnKungFuMan) {
    CharacterData kfm{};
    ProverResult  result{};
    ASSERT_NO_FATAL_FAILURE(analyseShipped("kung_fu_man.json", kfm, result));

    // The moves in question are the ones whose file declines to state a reach,
    // because a fireball's reach is a function of distance travelled.
    std::vector<MoveIndex> reachless;
    for (std::size_t i = 0; i < kfm.moves.size(); ++i)
        if (kfm.moves[i].reachSub == kNoReach) reachless.push_back(static_cast<MoveIndex>(i));

    // Vacuity guard, and the half that would silently rot: if the file ever
    // stops authoring `reach: null` on these two, the inertness below becomes
    // true for the wrong reason.
    ASSERT_EQ(reachless.size(), 2u)
        << "Kung Fu Man's two projectile moves are the only ones with no authored reach";
    EXPECT_EQ(kfm.moves[reachless[0]].id, "hasyo");
    EXPECT_EQ(kfm.moves[reachless[1]].id, "suiten_hasyo");

    // The reasoning: a cancel's delay is counted from the source CONNECTING, and
    // for a projectile the file's delay is counted from release, which is
    // earlier. The modelled delay is therefore too large and real cancels get
    // reported dead -- conservative. It cannot bite if there is nothing to
    // report, and there is not: neither move has an outgoing edge at all.
    for (MoveIndex m : reachless)
        EXPECT_TRUE(kfm.cancelsFrom[m].empty())
            << kfm.moves[m].id << " has outgoing cancels, so ADR-001's inertness "
               "argument no longer holds for this file";

    const ProjectionLoss* outgoing =
        findLoss(result, "projectile contact frame, outgoing", LossDirection::Conservative);
    ASSERT_NE(outgoing, nullptr);
    EXPECT_EQ(outgoing->count, 0);

    // The other half of the same field is PERMISSIVE -- a link into a projectile
    // whose startup means "released", not "connects", looks faster than it is --
    // and Kung Fu Man authors sixteen such edges. It is nonetheless inert too,
    // and for a reason worth writing down rather than discovering later: every
    // one of those sixteen is already reported dead. A fireball needs 14-15
    // frames of startup and no move in this character leaves that much settled
    // hitstun, so the edge the projection would have been too generous about is
    // one the projection never offers.
    std::size_t authoredIntoProjectiles = 0;
    for (const Cancel& edge : kfm.cancels)
        if (kfm.moves[edge.to].reachSub == kNoReach) ++authoredIntoProjectiles;
    ASSERT_EQ(authoredIntoProjectiles, 16u) << "the permissive half must have something to bite on";

    std::size_t deadIntoProjectiles = 0;
    for (const ProverDeadCancel& dead : result.deadCancels)
        if (kfm.moves[dead.to].reachSub == kNoReach) ++deadIntoProjectiles;
    EXPECT_EQ(deadIntoProjectiles, authoredIntoProjectiles);

    const ProjectionLoss* incoming =
        findLoss(result, "projectile contact frame, incoming", LossDirection::Permissive);
    ASSERT_NE(incoming, nullptr);
    EXPECT_EQ(incoming->count, 0)
        << "the count is over USABLE edges, and an edge the search never offers cannot "
           "make the search too generous";
}

// Both corpus infinites survive the ceiling being honoured, so neither verdict
// rests on meter the game would not have handed out.
TEST(ProverAdapter, BothCornerInfinitesSurviveTheCeilingClamp) {
    const char* files[] = { "kung_fu_girl.json", "kung_fu_man.json" };
    for (const char* file : files) {
        CharacterData character{};
        ProverResult  result{};
        ASSERT_NO_FATAL_FAILURE(analyseShipped(file, character, result)) << file;

        ASSERT_EQ(result.status, ProverStatus::Infinite) << file;
        EXPECT_TRUE(result.loopEntryKnown) << file;
        EXPECT_TRUE(result.ceilingReplayRan) << file;
        EXPECT_TRUE(result.loopHoldsUnderCeilings)
            << file << ": the reported loop could not be reproduced with resources capped\n"
            << DescribeVerdict(character, result);
    }
}

// ADR-001 gate 4. The header claims "well under a millisecond" and the ADR
// measured `analyse` at 0.033 / 0.041 / 0.009 ms; a whole AnalyseCharacter call,
// which also builds the projection, computes the loss table, the pre-decay dead
// list and the ceiling replay, measures about 0.07 ms. The bound here is two
// orders of magnitude looser than that, because the claim under test is "an
// editor can run this on every keystroke", not a benchmark.
TEST(ProverAdapter, AnalysisIsFastEnoughToRunOnEveryKeystroke) {
    const char* files[] = { "kung_fu_girl.json", "kung_fu_man.json",
                            "aof2_strength_training.json" };
    for (const char* file : files) {
        CharacterData character{};
        ProverResult  warm{};
        ASSERT_NO_FATAL_FAILURE(analyseShipped(file, character, warm)) << file;

        const int reps = 50;
        const auto begin = std::chrono::steady_clock::now();
        for (int i = 0; i < reps; ++i) {
            ProverResult result{};
            ProverReport report{};
            ASSERT_TRUE(AnalyseCharacter(character, proverOptions(), result, report)) << file;
        }
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin)
                .count() / reps;
        EXPECT_LT(ms, 5.0) << file << " took " << ms << " ms per analysis";
    }
}

// ---------------------------------------------------------------------------
// Synthetic characters, built to make a check fail
// ---------------------------------------------------------------------------

TEST(ProverAdapter, AnObviousTwoMoveInfiniteIsCaught) {
    const CharacterData character = twoMoveInfinite();
    ProverResult result{};
    ProverReport report{};
    ASSERT_TRUE(AnalyseCharacter(character, proverOptions(), result, report)) << report.error;

    EXPECT_EQ(result.status, ProverStatus::Infinite) << DescribeVerdict(character, result);
    EXPECT_EQ(result.usableCancels, 2);
    EXPECT_TRUE(result.deadCancels.empty());
    EXPECT_TRUE(result.unreachableMoves.empty());
    ASSERT_FALSE(result.loop.empty());
    for (MoveIndex m : result.loop)
        EXPECT_LT(m, character.moves.size());
    EXPECT_TRUE(result.loopHoldsUnderCeilings)
        << "no resource in this character has a ceiling, so the replay must confirm";
}

// The vacuity guard for the test above: the same character, one frame short.
// If `short` needs 13 frames and `jab` leaves 12, the only way into `short` is
// dead, `short` becomes unreachable, and nothing loops. A test that reports
// INFINITE for every input would fail here.
TEST(ProverAdapter, TheSameCharacterTerminatesWhenTheLinkIsOneFrameShort) {
    CharacterData character = twoMoveInfinite();
    character.moves[1].startup = 13;      // jab leaves 12
    ProverResult result{};
    ProverReport report{};
    ASSERT_TRUE(AnalyseCharacter(character, proverOptions(), result, report)) << report.error;

    EXPECT_EQ(result.status, ProverStatus::Terminating) << DescribeVerdict(character, result);
    EXPECT_TRUE(result.loop.empty());
    ASSERT_EQ(result.deadCancels.size(), 1u);
    EXPECT_EQ(character.moves[result.deadCancels[0].from].id, "jab");
    EXPECT_EQ(character.moves[result.deadCancels[0].to].id, "short");
    EXPECT_EQ(result.deadCancels[0].shortfall(), 1);
    ASSERT_EQ(result.unreachableMoves.size(), 1u);
    EXPECT_EQ(character.moves[result.unreachableMoves[0]].id, "short");
    EXPECT_EQ(result.rankingAbsence, RankingAbsence::NothingLoops);
}

// The ceiling, which is the adapter's own correctness decision (ADR-001 section
// 8 item 4). `comboprover.hpp` has no ceilings, so it lets meter climb to 450
// and take an edge guarded on 400; the real character caps at 300 and can never
// take it. The verdict stays INFINITE -- an unclamped search over-approximates
// the attacker, which can invent an infinite and cannot hide one -- and it is
// `loopHoldsUnderCeilings` that tells the panel the witness did not survive.
TEST(ProverAdapter, TheCeilingReplayRejectsALoopOnlyOverflowCouldReach) {
    const CharacterData capped = ceilingOnlyLoop(/*withCeiling*/ true);
    ProverResult cappedResult{};
    ProverReport cappedReport{};
    ASSERT_TRUE(AnalyseCharacter(capped, proverOptions(), cappedResult, cappedReport))
        << cappedReport.error;

    ASSERT_EQ(cappedResult.status, ProverStatus::Infinite)
        << "the unclamped search must still find the loop; the clamp is what this "
           "adapter adds on top\n" << DescribeVerdict(capped, cappedResult);
    EXPECT_TRUE(cappedResult.ceilingReplayRan);
    EXPECT_FALSE(cappedResult.loopHoldsUnderCeilings)
        << "meter is capped at 300 and `big` is guarded on 400, so the cycle cannot "
           "close once the ceiling is honoured\n" << DescribeVerdict(capped, cappedResult);

    // The clamp must be reported as a live loss for this character, or a panel
    // has no way to explain the refusal.
    const ProjectionLoss* ceiling =
        findLoss(cappedResult, "resource.ceiling", LossDirection::Permissive);
    ASSERT_NE(ceiling, nullptr);
    EXPECT_GT(ceiling->count, 0);

    // The vacuity guard, and the half that proves the replay is testing the
    // ceiling and not merely failing: the identical character with the ceiling
    // removed reaches 450, closes the cycle, and is confirmed.
    const CharacterData uncapped = ceilingOnlyLoop(/*withCeiling*/ false);
    ProverResult uncappedResult{};
    ProverReport uncappedReport{};
    ASSERT_TRUE(AnalyseCharacter(uncapped, proverOptions(), uncappedResult, uncappedReport))
        << uncappedReport.error;
    EXPECT_EQ(uncappedResult.status, ProverStatus::Infinite);
    EXPECT_TRUE(uncappedResult.loopHoldsUnderCeilings)
        << DescribeVerdict(uncapped, uncappedResult);
    const ProjectionLoss* noCeiling =
        findLoss(uncappedResult, "resource.ceiling", LossDirection::Permissive);
    ASSERT_NE(noCeiling, nullptr);
    EXPECT_EQ(noCeiling->count, 0);
}

// The conservative alarm, watched failing. A move whose file declines to state a
// reach records that contact happens when the projectile arrives rather than
// when it is released; give one an outgoing cancel and ADR-001's inertness
// argument stops holding, which must be said out loud rather than inferred.
TEST(ProverAdapter, AProjectileWithAnOutgoingCancelRaisesTheSoundnessAlarm) {
    CharacterData character = twoMoveInfinite();
    character.moves[0].reachSub = kNoReach;    // `jab` becomes a projectile
    ProverResult result{};
    ProverReport report{};
    ASSERT_TRUE(AnalyseCharacter(character, proverOptions(), result, report)) << report.error;

    const ProjectionLoss* outgoing =
        findLoss(result, "projectile contact frame, outgoing", LossDirection::Conservative);
    ASSERT_NE(outgoing, nullptr);
    EXPECT_EQ(outgoing->count, 1) << "jab -> short is a usable edge out of a reach-less move";
    EXPECT_TRUE(result.soundnessAlarm);
    EXPECT_NE(DescribeVerdict(character, result).find("SOUNDNESS ALARM"), std::string::npos);
}

// A gap action that spends a RESOURCE rather than space. Gap actions are dropped
// because in the corner model they can only buy distance, which is not modelled;
// one that moves meter would be a way of paying for a follow-up that this
// projection cannot see, and dropping it would hide combos.
TEST(ProverAdapter, AGapActionWithAResourceEffectRaisesTheSoundnessAlarm) {
    CharacterData character = twoMoveInfinite();
    GapAction dash{};
    dash.id      = "meter_dash";
    dash.frames  = 8;
    dash.enabled = true;
    ResourceAmount gain{};
    gain.resource = 0;
    gain.value    = 25;
    dash.effect.push_back(gain);
    character.gapActions.push_back(dash);

    ProverResult result{};
    ProverReport report{};
    ASSERT_TRUE(AnalyseCharacter(character, proverOptions(), result, report)) << report.error;

    const ProjectionLoss* gap =
        findLoss(result, "gap_action resource effects", LossDirection::Conservative);
    ASSERT_NE(gap, nullptr);
    EXPECT_EQ(gap->count, 1);
    EXPECT_TRUE(result.soundnessAlarm);
}

// ARCHITECTURE.md D8's one-frame gap, checked rather than hoped for. Our decay
// table is integer permille and the prover's is float, so `int(base * 0.036f)`
// need not equal `(base * 36) / 1000`. No plausible frame data trips it -- the
// smallest base hitstun that does is 375 frames, over six seconds -- which is
// exactly why the check would rot unnoticed without a test that trips it on
// purpose. 750 * 36 / 1000 is 27 exactly; in float it truncates to 26, one frame
// LOW, which is the direction that reports a real cancel dead.
TEST(ProverAdapter, TableDecayFloatMultiplyIsCheckedAgainstIntegerPermille) {
    CharacterData character = twoMoveInfinite();
    character.decay.kind = DecayKind::Table;
    character.decay.tablePermille = { 1000, 36 };
    character.moves[0].hitstun = 750;
    character.moves[1].hitstun = 750;

    ProverResult result{};
    ProverReport report{};
    ASSERT_TRUE(AnalyseCharacter(character, proverOptions(), result, report)) << report.error;

    const ProjectionLoss* low =
        findLoss(result, "decay.table float multiply", LossDirection::Conservative);
    ASSERT_NE(low, nullptr);
    EXPECT_GT(low->count, 0)
        << "the float table must be compared against the integer permille it came from";
    EXPECT_TRUE(result.soundnessAlarm);

    // Vacuity guard: the same character with a table that multiplies by one must
    // trip nothing, or the check is firing on the table's existence rather than
    // on a disagreement.
    CharacterData faithful = character;
    faithful.decay.tablePermille = { 1000, 1000 };
    ProverResult faithfulResult{};
    ProverReport faithfulReport{};
    ASSERT_TRUE(AnalyseCharacter(faithful, proverOptions(), faithfulResult, faithfulReport));
    const ProjectionLoss* none =
        findLoss(faithfulResult, "decay.table float multiply", LossDirection::Conservative);
    ASSERT_NE(none, nullptr);
    EXPECT_EQ(none->count, 0);
    EXPECT_FALSE(faithfulResult.soundnessAlarm);
}

// ---------------------------------------------------------------------------
// The seam's own assertions
// ---------------------------------------------------------------------------

// A03, at the prover seam this time. The loader checks it too, but a
// CharacterData assembled in memory never met the loader, and this is the
// failure that says nothing: comboprover keys its ResourceVec by index, so a
// character whose index 0 is `juggle` compares juggle against meter.
TEST(ProverAdapter, ResourceOrderIsAssertedAtTheSeam) {
    CharacterData character = twoMoveInfinite();
    std::swap(character.resources[0], character.resources[1]);

    ProverResult result{};
    ProverReport report{};
    EXPECT_FALSE(AnalyseCharacter(character, proverOptions(), result, report));
    EXPECT_EQ(report.rule, "A03");
    EXPECT_NE(report.error.find("juggle"), std::string::npos);
    EXPECT_NE(report.error.find("POSITIONALLY"), std::string::npos);

    // Unswapped, the same character must project, or the test above proves only
    // that AnalyseCharacter can fail.
    const CharacterData ordered = twoMoveInfinite();
    ProverResult ok{};
    ProverReport okReport{};
    EXPECT_TRUE(AnalyseCharacter(ordered, proverOptions(), ok, okReport)) << okReport.error;
    EXPECT_TRUE(okReport.rule.empty());
}

// A caller who supplies no expected order gets a warning, never a silent pass.
// A build that never sets it has no A03 at all and that should be visible.
TEST(ProverAdapter, NotCheckingA03IsRecordedRatherThanIgnored) {
    const CharacterData character = twoMoveInfinite();
    ProverResult result{};
    ProverReport report{};
    ASSERT_TRUE(AnalyseCharacter(character, ProverOptions{}, result, report));
    ASSERT_FALSE(report.warnings.empty());
    bool mentioned = false;
    for (const std::string& warning : report.warnings)
        if (warning.find("A03") != std::string::npos) mentioned = true;
    EXPECT_TRUE(mentioned);
}

// An amount naming a resource the character does not declare must be refused,
// not dropped. Dropping it is how index 0 stops meaning what everyone believes.
TEST(ProverAdapter, AnEffectNamingAnUndeclaredResourceIsRefused) {
    CharacterData character = twoMoveInfinite();
    ResourceAmount rogue{};
    rogue.resource = 7;               // the character declares two
    rogue.value    = -50;
    character.moves[0].effect.push_back(rogue);

    ProverResult result{};
    ProverReport report{};
    EXPECT_FALSE(AnalyseCharacter(character, proverOptions(), result, report));
    EXPECT_NE(report.error.find("does not declare"), std::string::npos);
}

TEST(ProverAdapter, ACharacterWithNoMovesIsRefusedRatherThanDecided) {
    CharacterData character{};
    character.id = "empty";
    character.resources.push_back(resource("meter", 0, false, 0));
    character.resources.push_back(resource("juggle", 0, false, 0));
    character.RebuildIndices();

    ProverResult result{};
    ProverReport report{};
    EXPECT_FALSE(AnalyseCharacter(character, proverOptions(), result, report));
    EXPECT_FALSE(report.error.empty());
}

// Every verdict must name the stage it answered, whatever the file claims. All
// three shipped characters declare `midscreen` and are answered in the corner.
TEST(ProverAdapter, EveryVerdictNamesTheStageItAnswered) {
    const char* files[] = { "kung_fu_girl.json", "kung_fu_man.json",
                            "aof2_strength_training.json" };
    for (const char* file : files) {
        CharacterData character{};
        ProverResult  result{};
        ASSERT_NO_FATAL_FAILURE(analyseShipped(file, character, result)) << file;

        EXPECT_EQ(result.stage, ProverStage::Corner) << file;
        EXPECT_EQ(result.fileStage, "midscreen") << file;
        const std::string described = DescribeVerdict(character, result);
        EXPECT_NE(described.find("corner-pinned defender"), std::string::npos) << file;
        EXPECT_NE(described.find("midscreen"), std::string::npos) << file;

        // And the disagreement must reach the caller as a warning, not only as a
        // field somebody might forget to read.
        ProverResult again{};
        ProverReport report{};
        ASSERT_TRUE(AnalyseCharacter(character, proverOptions(), again, report)) << file;
        bool warned = false;
        for (const std::string& warning : report.warnings)
            if (warning.find("answers the corner") != std::string::npos) warned = true;
        EXPECT_TRUE(warned) << file;
    }
}
