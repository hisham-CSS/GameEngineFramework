// THE SHOWCASE MECHANISM -- one fighter, many patches, a verdict pair per
// entry (docs/adr/ADR-011 section 4, ROADMAP M1.6).
//
// A variant is a small diff on the shipped fighter, and the exhibit is what
// that diff does to the TWO verdicts: the prover's (sound, about the file) and
// ComboSearch's (executed, about the game). The two slice-1 entries are chosen
// to bend the pair in opposite directions:
//
//   hitstun_plus_7    frame data alone hands the GAME an infinite the MODEL
//                     cannot see -- the restart route is not in the prover's
//                     graph, and the ledger's `starters` row says so. The
//                     search finds the loop by performing it.
//   dead_air_window   an authored cancel that can never connect, and the tool
//                     NAMING it: the prover prints both of air_mp's air-to-air
//                     edges dead, the verdict stays TERMINATING, and the
//                     executed worst case must not grow.
//
// The loader's own rules are tested first, because a patch that silently
// patched nothing is the worst kind of green.

#include "cse/data/CharacterData.h"
#include "cse/data/MatchBuilder.h"
#include "cse/data/ProverAdapter.h"
#include "cse/game/ComboSearch.h"
#include "cse/game/WitnessCursor.h"
#include "cse/kernel/Simulate.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using cse::data::AnalyseCharacter;
using cse::data::BuildMatchData;
using cse::data::BuildOptions;
using cse::data::CharacterData;
using cse::data::LoadCharacterVariant;
using cse::data::LoadOptions;
using cse::data::LoadReport;
using cse::data::MatchBuild;
using cse::data::ProverOptions;
using cse::data::ProverReport;
using cse::data::ProverResult;
using cse::data::ProverStatus;
using cse::game::ComboSearchRequest;
using cse::game::ComboSearchResult;
using cse::game::ComboVerdict;
using cse::game::RunComboSearch;

namespace {

constexpr std::int32_t px(std::int32_t v) {
    return v * cse::kernel::kSubUnitsPerPixel;
}
constexpr std::int32_t kStageEdge = 480 * cse::kernel::kSubUnitsPerPixel;
constexpr std::int32_t kP1X       = kStageEdge - px(13);
constexpr std::int32_t kP0X       = kP1X - px(34);

const std::vector<std::string> kBuildResources = { "meter", "juggle" };

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

LoadOptions loadOptions() {
    LoadOptions o;
    o.expectedResources = kBuildResources;
    return o;
}

BuildOptions normalBindings(const CharacterData& c) {
    const char* kButtons[] = { "lp", "mp", "hp", "lk", "mk", "hk" };
    const std::uint16_t kBits[] = {
        cse::kernel::kInputLP, cse::kernel::kInputMP, cse::kernel::kInputHP,
        cse::kernel::kInputLK, cse::kernel::kInputMK, cse::kernel::kInputHK };
    BuildOptions options{};
    for (int b = 0; b < 6; ++b)
        for (const char* prefix : { "stand_", "crouch_", "air_" }) {
            const std::string id = std::string(prefix) + kButtons[b];
            if (c.FindMove(id) != cse::data::kInvalidMove)
                options.bindings.push_back({ id, kBits[b] });
        }
    return options;
}

struct Exhibit {
    CharacterData character{};
    std::string   description;
    ProverResult  verdict{};
    MatchBuild    build{};
    ComboSearchResult searched{};
};

void bringUpVariant(const std::string& variantRel, Exhibit& out,
                    const std::vector<cse::data::MoveBinding>& extraBindings = {}) {
    LoadReport report{};
    ASSERT_TRUE(LoadCharacterVariant(charactersDir(), "fighter_a.json",
                                     variantRel, loadOptions(), out.character,
                                     report, &out.description))
        << variantRel << ": " << report.error;
    ASSERT_FALSE(out.description.empty());

    ProverOptions po;
    po.expectedResources = kBuildResources;
    ProverReport preport{};
    ASSERT_TRUE(AnalyseCharacter(out.character, po, out.verdict, preport));

    BuildOptions options = normalBindings(out.character);
    for (const cse::data::MoveBinding& b : extraBindings)
        options.bindings.push_back(b);
    ASSERT_TRUE(BuildMatchData(out.character, options, out.character, options,
                               out.build))
        << out.build.report[0].error;

    ComboSearchRequest request{};
    request.data         = &out.build.data;
    request.attackerSlot = 0;
    cse::kernel::ResetMatch(request.from, 0x1D7u);
    request.from.p[0].posX = kP0X;
    request.from.p[1].posX = kP1X;
    out.searched = RunComboSearch(request);
}

// A scratch pair of files for the loader's refusals, written under the test's
// own temp dir so nothing authored is touched.
std::string writeScratch(const std::string& name, const std::string& text) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(::testing::TempDir()) / "cse_variants";
    fs::create_directories(dir);
    std::ofstream out(dir / name, std::ios::binary);
    out << text;
    return (dir / name).string().substr(0);
}

} // namespace

TEST(VariantLoader, ADescriptionIsRequiredAndAnUnknownMoveIsRefused) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(::testing::TempDir()) / "cse_variants";
    fs::create_directories(dir);
    {
        std::ofstream b(dir / "base.json", std::ios::binary);
        b << R"({"name":"b","stage":"corner","walk_speed":0.5,)"
             R"("resources":[{"name":"meter","initial":0,"floor":0},)"
             R"({"name":"juggle","initial":4,"floor":0}],)"
             R"("scaling":{},"decay":{"kind":"none"},)"
             R"("moves":[{"id":"jab","startup":3,"active":2,"recovery":4,)"
             R"("hitstun":8,"damage":10.0,"stance":"standing"}],"cancels":[]})";
    }
    {
        std::ofstream v(dir / "no_desc.json", std::ios::binary);
        v << R"({"patch":{"moves":{"jab":{"hitstun":9}}}})";
    }
    {
        std::ofstream v(dir / "unknown.json", std::ios::binary);
        v << R"({"description":"names a move the base does not author",)"
             R"("patch":{"moves":{"sweep":{"hitstun":9}}}})";
    }

    CharacterData c{};
    LoadReport report{};
    LoadOptions o = loadOptions();

    EXPECT_FALSE(LoadCharacterVariant(dir.string(), "base.json", "no_desc.json",
                                      o, c, report))
        << "a variant with no description loaded; the catalogue rule decayed "
           "into a convention";
    EXPECT_NE(report.error.find("description"), std::string::npos) << report.error;

    EXPECT_FALSE(LoadCharacterVariant(dir.string(), "base.json", "unknown.json",
                                      o, c, report))
        << "a patch naming a move the base does not author loaded -- it would "
           "have silently changed nothing, which is the worst kind of green";
    EXPECT_NE(report.error.find("sweep"), std::string::npos) << report.error;
}

// The one_frame_link coin-flip, pinned (ROADMAP M1.8). A patch key the loader
// does not read at move level merged silently and changed nothing -- twice on
// one variant: `hitstop_ticks`, then `reaction`, both guesses at the real
// path `engine.reaction` -- so the exhibit measured the BASE character while
// its caption claimed a diff, and the wrong pair of verdicts cost two
// diagnoses before anyone compared the merged doc to the file. The move level
// is a CLOSED key set (the loader reads exactly seventeen names there), so an
// unknown key is a load error naming the key -- NORTHSTAR property (c)'s own
// sentence, applied to the patch format. `engine` deliberately stays open one
// level down: that namespace carries MUGEN transcription and authoring notes
// by documented design.
TEST(VariantLoader, AMovePatchKeyTheLoaderDoesNotReadIsRefusedByName) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(::testing::TempDir()) / "cse_variants";
    fs::create_directories(dir);
    {
        // `scaling` is an ARRAY: unlike the refusal cases above, the accept
        // case below reaches LoadCharacterJson over the merged doc, so this
        // base must actually parse.
        std::ofstream b(dir / "base_parses.json", std::ios::binary);
        b << R"({"name":"b","stage":"corner","walk_speed":0.5,)"
             R"("resources":[{"name":"meter","initial":0,"floor":0},)"
             R"({"name":"juggle","initial":4,"floor":0}],)"
             R"("scaling":[],"decay":{"kind":"none"},)"
             R"("moves":[{"id":"jab","startup":3,"active":2,"recovery":4,)"
             R"("hitstun":8,"damage":10.0,"stance":"standing"}],"cancels":[]})";
    }
    {
        // The incident's first guess.
        std::ofstream v(dir / "typo_hitstop.json", std::ios::binary);
        v << R"({"description":"zeroes hitstop, or so it believes",)"
             R"("patch":{"moves":{"jab":{"hitstop_ticks":0}}}})";
    }
    {
        // The incident's second guess.
        std::ofstream v(dir / "typo_reaction.json", std::ios::binary);
        v << R"({"description":"zeroes hitstop, second guess",)"
             R"("patch":{"moves":{"jab":{"reaction":{"hitstop_ticks":0}}}}})";
    }
    {
        // RFC 7386 would REPLACE the whole move with the scalar.
        std::ofstream v(dir / "scalar.json", std::ios::binary);
        v << R"({"description":"a scalar where a move patch should be",)"
             R"("patch":{"moves":{"jab":3}}})";
    }
    {
        // The correct spelling of the same edit, plus an open-namespace
        // engine key: both must still load, or the refusal has closed a
        // namespace the schema documents as open.
        std::ofstream v(dir / "real_path.json", std::ios::binary);
        v << R"({"description":"zeroes hitstop at the path the loader reads",)"
             R"("patch":{"moves":{"jab":{"hitstun":9,)"
             R"("engine":{"reaction":{"hitstop_ticks":0},"authoring_note":"x"}}}}})";
    }

    CharacterData c{};
    LoadReport report{};
    LoadOptions o = loadOptions();

    EXPECT_FALSE(LoadCharacterVariant(dir.string(), "base_parses.json",
                                      "typo_hitstop.json", o, c, report))
        << "the exact incident key merged silently again";
    EXPECT_NE(report.error.find("hitstop_ticks"), std::string::npos)
        << "the refusal did not name the key: " << report.error;

    EXPECT_FALSE(LoadCharacterVariant(dir.string(), "base_parses.json",
                                      "typo_reaction.json", o, c, report));
    EXPECT_NE(report.error.find("`reaction`"), std::string::npos)
        << "the refusal did not name the key: " << report.error;

    EXPECT_FALSE(LoadCharacterVariant(dir.string(), "base_parses.json",
                                      "scalar.json", o, c, report))
        << "a scalar move patch would replace the whole move under merge";

    ASSERT_TRUE(LoadCharacterVariant(dir.string(), "base_parses.json",
                                     "real_path.json", o, c, report))
        << "the correctly-spelled edit was refused: " << report.error;
    const cse::data::MoveIndex jab = c.FindMove("jab");
    ASSERT_NE(jab, cse::data::kInvalidMove);
    EXPECT_EQ(c.moves[jab].hitstun, 9) << "the valid patch did not land";
}

TEST(VariantExhibits, HitstunAloneHandsTheGameAnInfiniteTheModelCannotSee) {
    Exhibit e{};
    bringUpVariant("fighter_a/variants/hitstun_plus_7.json", e);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    // The diff is the exhibit: one number on one move.
    const cse::data::MoveIndex lp = e.character.FindMove("stand_lp");
    ASSERT_NE(lp, cse::data::kInvalidMove);
    EXPECT_EQ(e.character.moves[lp].hitstun, 19);

    // The MODEL still says TERMINATING -- its graph walks cancels, and the
    // restart route is not in it. That blindness is recorded, not discovered:
    // the ledger's `starters` and `gap_actions` rows have said so since M1.1.
    EXPECT_EQ(e.verdict.status, ProverStatus::Terminating)
        << "the prover's graph grew a restart route; the exhibit's whole "
           "point -- a gap the ledger names -- needs re-deriving";

    // The GAME loops forever, found by performing it.
    ASSERT_EQ(e.searched.verdict, ComboVerdict::Infinite)
        << "the search did not find the restart loop: " << e.searched.note;
    ASSERT_LT(e.searched.loopStart, e.searched.witness.size());
    const std::uint16_t lpSlot = e.build.moves[0].Find("stand_lp");
    for (std::size_t i = e.searched.loopStart; i < e.searched.witness.size(); ++i)
        EXPECT_EQ(e.searched.witness[i], lpSlot)
            << "the loop the search found is not the stand_lp restart loop "
               "this variant authors";

    RecordProperty("variant_description", e.description);
}

TEST(VariantExhibits, AnAuthoredCancelThatCanNeverConnectIsNamedDead) {
    // The base pair, for the deltas.
    CharacterData base{};
    LoadReport lreport{};
    ASSERT_TRUE(cse::data::LoadCharacterFile(charactersDir(), "fighter_a.json",
                                             loadOptions(), base, lreport))
        << lreport.error;
    ProverOptions po;
    po.expectedResources = kBuildResources;
    ProverResult baseVerdict{};
    ProverReport preport{};
    ASSERT_TRUE(AnalyseCharacter(base, po, baseVerdict, preport));

    Exhibit e{};
    bringUpVariant("fighter_a/variants/dead_air_window.json", e);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    // THE TOOL THAT NAMES THE DEAD EDGES IS MATCHBUILDER, not the prover: the
    // prover's model has no cancel windows at all (the one_frame boundary
    // tests measure that blindness from the other side), so its dead list must
    // NOT move -- and both of air_mp's air-to-air edges, which first fire on
    // frame 11, must resolve to an EMPTY kernel window against the close at 10.
    EXPECT_EQ(e.verdict.deadCancels.size(), baseVerdict.deadCancels.size())
        << "the prover's dead list moved on a window-only patch, so it has "
           "grown a window model and this exhibit's caption is stale";

    const std::uint16_t airMp = e.build.moves[0].Find("air_mp");
    ASSERT_NE(airMp, 0u);
    int emptied = 0;
    for (std::int32_t i = 0; i < e.build.data.p[0].cancelCount; ++i) {
        const cse::kernel::CancelEdge& ke = e.build.data.p[0].cancels[i];
        if (ke.from != airMp) continue;
        const std::uint16_t to = ke.to;
        const bool airToAir = to == airMp || to == e.build.moves[0].Find("air_hk");
        if (!airToAir) continue;
        EXPECT_GT(ke.earliestFrame, ke.latestFrame)
            << "an air-to-air edge out of air_mp still has a live window "
               "[" << ke.earliestFrame << ", " << ke.latestFrame << "]";
        ++emptied;
    }
    EXPECT_EQ(emptied, 2)
        << "the closed window should reach exactly the self-cancel and the "
           "edge into air_hk";

    // The verdict does not move, and the executed worst case does not grow.
    EXPECT_EQ(e.verdict.status, ProverStatus::Terminating);
    ASSERT_EQ(e.searched.verdict, ComboVerdict::Terminating)
        << e.searched.note;
    EXPECT_LE(e.searched.maxHits, e.verdict.maxHits)
        << "the executed worst case outran the model's on a variant that only "
           "REMOVED capability";

    RecordProperty("variant_description", e.description);
    RecordProperty("dead_cancels", static_cast<int>(e.verdict.deadCancels.size()));
    RecordProperty("executed_max_hits", e.searched.maxHits);
}

TEST(VariantExhibits, ALinearDecayBreaksTheSoundnessBoundWithoutTouchingTheGame) {
    // The base pair, for the deltas -- including its executed worst case.
    CharacterData base{};
    LoadReport lreport{};
    ASSERT_TRUE(cse::data::LoadCharacterFile(charactersDir(), "fighter_a.json",
                                             loadOptions(), base, lreport))
        << lreport.error;
    ProverOptions po;
    po.expectedResources = kBuildResources;
    ProverResult baseVerdict{};
    ProverReport preport{};
    ASSERT_TRUE(AnalyseCharacter(base, po, baseVerdict, preport));
    ASSERT_TRUE(baseVerdict.hasRanking)
        << "the base character no longer carries the ranking certificate this "
           "exhibit exists to delete";

    Exhibit e{};
    bringUpVariant("fighter_a/variants/decay_linear.json", e);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    // THE MODEL'S TABLE IS GUTTED. Both implementations evaluate every edge at
    // the SETTLED hitstun, so the floor of 10 kills every edge slower than it:
    // 88 usable edges become 35, and the model's stated worst case collapses
    // from 21 to 5. Measured 2026-08-31; the counts are asserted so the day a
    // prover change moves them, this exhibit's caption moves with it.
    EXPECT_EQ(e.verdict.usableCancels, 35)
        << "the settled-hitstun rule changed; re-measure the exhibit";
    EXPECT_EQ(e.verdict.maxHits, 5);

    // THE CERTIFICATE SURVIVES -- the six-aerial roster keeps enough
    // juggle-spending edges above the floor. The base file's older note
    // (written against the 73-edge two-aerial file) predicted its loss and
    // was corrected against this measurement; if this flips, that note is
    // stale again.
    EXPECT_TRUE(e.verdict.hasRanking);

    // AND THAT IS THE EXHIBIT: the game is UNTOUCHED -- the kernel carries no
    // decay (the ledger row is the record) -- so the executed worst case still
    // stands at 7, ABOVE the mis-authored model's stated 5. One model-only
    // field made the sound half claim a bound the game demonstrably beats. A
    // certificate over a mis-authored model certifies the wrong game.
    ASSERT_EQ(e.searched.verdict, ComboVerdict::Terminating) << e.searched.note;
    EXPECT_GT(e.searched.maxHits, e.verdict.maxHits)
        << "the exhibit's whole point is the executed worst case standing "
           "ABOVE the decayed model's stated bound; if the two agree again, "
           "either the kernel grew a decay wire or the model recovered.";

    MatchBuild baseBuild{};
    const BuildOptions baseOptions = normalBindings(base);
    ASSERT_TRUE(BuildMatchData(base, baseOptions, base, baseOptions, baseBuild))
        << baseBuild.report[0].error;
    ComboSearchRequest request{};
    request.data         = &baseBuild.data;
    request.attackerSlot = 0;
    cse::kernel::ResetMatch(request.from, 0x1D7u);
    request.from.p[0].posX = kP0X;
    request.from.p[1].posX = kP1X;
    const ComboSearchResult baseSearched = RunComboSearch(request);
    EXPECT_EQ(e.searched.maxHits, baseSearched.maxHits)
        << "the executed worst case moved on a model-only patch; the kernel "
           "has grown a decay wire and this exhibit's caption is stale";
    EXPECT_EQ(e.searched.longestString, baseSearched.longestString);

    RecordProperty("variant_description", e.description);
    RecordProperty("base_has_ranking", baseVerdict.hasRanking ? 1 : 0);
    RecordProperty("variant_has_ranking", e.verdict.hasRanking ? 1 : 0);
}

TEST(VariantExhibits, AMeterLoopIsInfiniteOnBothSidesBecauseGainMeetsSpend) {
    // The super is bound as a two-attack-button chord -- the genre's own
    // answer to six buttons and twenty-two moves, and the binding
    // fighter_a_infinite ships. From IDLE the chord is shadowed by stand_lp
    // (a superset mask never wins the press scan), which is fine and true to
    // the genre: the super is reached MID-STRING through its cancel, where
    // FindCancel reads the held chord regardless of scan order.
    Exhibit e{};
    bringUpVariant("fighter_a/variants/meter_loop.json", e,
                   { { "super_beam",
                       static_cast<std::uint16_t>(cse::kernel::kInputLP |
                                                  cse::kernel::kInputMP) } });
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    // THE MODEL: the appended edge closes a cycle through super_beam whose
    // per-turn meter arithmetic is exactly break-even (+100 on stand_hp's hit
    // against the super's guard and cost of 100), so `nonNegative` never
    // refuses and the verdict is INFINITE -- the ranking certificate's own
    // vocabulary, exercised in the direction that keeps a loop alive.
    EXPECT_EQ(e.verdict.status, ProverStatus::Infinite)
        << "the model did not find the meter loop; either the appended edge "
           "did not survive the projection or the resource arithmetic moved";

    // THE GAME: meter is Exact in the kernel (M1.1b) -- the guard refuses on
    // both start routes and ApplyEffects clamps at the ceiling -- so the SAME
    // loop performs forever, its guard exercised every turn and never
    // failing. The witness must actually cycle through the super: a loop that
    // avoided it would be some other infinite wearing this exhibit's name.
    ASSERT_EQ(e.searched.verdict, ComboVerdict::Infinite) << e.searched.note;
    ASSERT_LT(e.searched.loopStart, e.searched.witness.size());
    const std::uint16_t superSlot = e.build.moves[0].Find("super_beam");
    const std::uint16_t hpSlot    = e.build.moves[0].Find("stand_hp");
    ASSERT_NE(superSlot, 0u);
    ASSERT_NE(hpSlot, 0u);
    bool loopHasSuper = false, loopHasHp = false;
    for (std::size_t i = e.searched.loopStart; i < e.searched.witness.size(); ++i) {
        if (e.searched.witness[i] == superSlot) loopHasSuper = true;
        if (e.searched.witness[i] == hpSlot)    loopHasHp    = true;
    }
    EXPECT_TRUE(loopHasSuper)
        << "the executed loop does not pass through super_beam, so the meter "
           "guard was never exercised and this is not the meter-loop exhibit";
    EXPECT_TRUE(loopHasHp);

    RecordProperty("variant_description", e.description);
}

TEST(VariantExhibits, TheBufferTurnsAOneFrameLinkFromACoinIntoACertainty) {
    // The pair differs by ONE field -- input_buffer_frames 2 -- on top of the
    // same one-tick link (stand_lp hitstun 15 against its 14-tick duration:
    // the re-press must land on exactly the tick the move ends). This is
    // ROADMAP M1.1e's feature as a catalogue row, in the author's own words:
    // "a 3 frame buffer that makes a tightly timed link much easier."
    Exhibit coin{};
    bringUpVariant("fighter_a/variants/one_frame_link.json", coin);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    Exhibit certain{};
    bringUpVariant("fighter_a/variants/one_frame_link_buffered.json", certain);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    // Without the buffer, the search's held early press never has an edge on
    // the one tick the link needs: the loop never closes.
    const auto spell = [](const Exhibit& e) {
        std::string s;
        for (std::size_t i = 0; i < e.searched.witness.size(); ++i) {
            if (i == e.searched.loopStart) s += "[loop] ";
            const std::string_view id =
                e.build.moves[0].IdOf(e.searched.witness[i]);
            s += id.empty() ? std::string("?") : std::string(id);
            s += ' ';
        }
        return s;
    };
    EXPECT_EQ(coin.searched.verdict, ComboVerdict::Terminating)
        << "the unbuffered one-frame link closed after all: " << coin.searched.note
        << "\n  witness: " << spell(coin);

    // With the two-tick window, the same press is consumed on exactly that
    // tick, and the restart loop is found by performing it.
    ASSERT_EQ(certain.searched.verdict, ComboVerdict::Infinite)
        << certain.searched.note;
    const std::uint16_t lpSlot = certain.build.moves[0].Find("stand_lp");
    ASSERT_LT(certain.searched.loopStart, certain.searched.witness.size());
    for (std::size_t i = certain.searched.loopStart;
         i < certain.searched.witness.size(); ++i)
        EXPECT_EQ(certain.searched.witness[i], lpSlot)
            << "the buffered loop is not the stand_lp restart link this pair "
               "exists to demonstrate";

    // The MODEL is blind to the whole story twice over: the restart route is
    // not in its graph and the buffer is deliberately not in its vocabulary
    // (edgeUsable already assumes an ideal first-frame player). Both files
    // must therefore read TERMINATING to the prover -- the exhibit's gap is
    // the kernel's alone, and the ledger's `starters` row names the blindness.
    EXPECT_EQ(coin.verdict.status, ProverStatus::Terminating);
    EXPECT_EQ(certain.verdict.status, ProverStatus::Terminating);

    RecordProperty("coin_description", coin.description);
    RecordProperty("certain_description", certain.description);
}

TEST(VariantExhibits, AFloatyJumpHandsTheStringToTheBudgetAlone) {
    // The first authored engine.movement (M1.3(b1), ADR-014): fighter_a's own
    // MUGEN-provenance 11 px/tick impulse, finally loaded, against placeholder
    // gravity. On the BASE file the arc and the juggle budget end the aerial
    // string IN AGREEMENT at four (M1.1f's measurement); here the arc holds
    // far more than four, so termination rests on the budget ALONE -- and the
    // certificate must survive on that single authority.
    Exhibit e{};
    bringUpVariant("fighter_a/variants/floaty_jump.json", e);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    ASSERT_EQ(e.character.jumpImpulseSub, 2816);
    ASSERT_EQ(e.character.gravitySub, 64);
    ASSERT_EQ(e.build.data.p[0].jumpImpulseSub, 2816)
        << "the variant authored a jump and the bridge dropped it";

    // The authored parabola, flown: J/G = 44, landing clamp on tick 2*44-1,
    // airborne observably 86 ticks -- against the base file's 38.
    {
        cse::kernel::GameState s{};
        cse::kernel::ResetMatch(s, 0xC0FFEEu);
        cse::kernel::InputPair up{};
        up.p[0].bits = cse::kernel::kInputUp;
        cse::kernel::Simulate(s, up, e.build.data);
        ASSERT_NE(s.p[0].airborne, 0u);
        int air = 1;
        for (int t = 0; t < 400 && s.p[0].airborne; ++t) {
            cse::kernel::Simulate(s, cse::kernel::InputPair{}, e.build.data);
            if (s.p[0].airborne) ++air;
        }
        EXPECT_EQ(air, 86) << "the moon jump does not fly the authored arc";
    }

    // The MODEL is blind to all of it -- no jump vocabulary -- so its verdict
    // matches the base file's exactly. The blindness is a named ledger row.
    EXPECT_EQ(e.verdict.status, ProverStatus::Terminating)
        << "the prover's verdict moved on a field it cannot see";

    // And the GAME still terminates: with the arc no longer running out of
    // air at four, the wired juggle budget (M1.1f) refuses the fifth aerial
    // by itself. If this ever reads INFINITE, the budget stopped holding the
    // line the arc used to share -- which would be this exhibit's finding.
    EXPECT_EQ(e.searched.verdict, ComboVerdict::Terminating)
        << "the floaty jump opened an infinite the budget was supposed to "
           "refuse: " << e.searched.note;

    RecordProperty("variant_description", e.description);
}

TEST(VariantExhibits, TheMicrowalkInfiniteNeedsTheWalkAndTheSearchWalksIt) {
    // The paper's own vocabulary word, exhibited: an infinite that exists
    // only for a player who can walk. Corner push (this slice's wire) recoils
    // the attacker out of stand_lp's reach on every cornered hit; the wide
    // hitstun leaves time to walk back in; the wall and the pushbox make the
    // loop's state return EXACTLY. Found by ADR-013 decision 6's walks --
    // there is nothing else in the kernel that could find it.
    Exhibit e{};
    bringUpVariant("fighter_a/variants/microwalk.json", e);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    // THE LINK'S OWN BUTTON, BOUND ALONE -- and the caption says so. With the
    // full roster bound the dive prefers whichever long-reach heavy connects
    // from the recoil distance and explores those unrelated strings for the
    // whole budget; the exhibit's claim is this LOOP's existence, so the
    // search is asked about exactly the loop's vocabulary: one button plus
    // the movement macros. (The probe that measured the full-roster drowning
    // and the loop's own exact 255-rep period is in the slice commit.)
    {
        BuildOptions solo{};
        solo.bindings = { { "stand_lp", cse::kernel::kInputLP } };
        MatchBuild soloBuild{};
        ASSERT_TRUE(BuildMatchData(e.character, solo, e.character, solo,
                                   soloBuild))
            << soloBuild.report[0].error;
        e.build = soloBuild;

        ComboSearchRequest req{};
        req.data         = &e.build.data;
        req.attackerSlot = 0;
        cse::kernel::ResetMatch(req.from, 0x1D7u);
        req.from.p[0].posX = kP0X;
        req.from.p[1].posX = kP1X;
        e.searched = RunComboSearch(req);
    }

    ASSERT_EQ(e.character.moves[e.character.FindMove("stand_lp")].cornerPushSub,
              5120)
        << "the variant's corner push did not load; the exhibit is measuring "
           "the hitstun_plus_7 family instead of the microwalk";

    ASSERT_EQ(e.searched.verdict, ComboVerdict::Infinite)
        << "the walked restart loop was not found: " << e.searched.note;

    // THE CLAIM: the loop itself walks. A walkless loop here would mean the
    // recoil failed to open the gap and this is a stationary restart wearing
    // the microwalk's name.
    ASSERT_LT(e.searched.loopStart, e.searched.witness.size());
    bool loopWalks = false, loopHits = false;
    for (std::size_t i = e.searched.loopStart; i < e.searched.witness.size(); ++i) {
        if (cse::game::WitnessCursor::IsMacro(e.searched.witness[i]))
            loopWalks = true;
        else
            loopHits = true;
    }
    EXPECT_TRUE(loopWalks)
        << "the infinite's loop contains no movement macro, so the walk was "
           "not needed and the corner push is not doing its job";
    EXPECT_TRUE(loopHits);

    // The MODEL is blind three ways -- no restart route, no walk, no corner
    // -- and must say so by not moving.
    EXPECT_EQ(e.verdict.status, ProverStatus::Terminating);

    RecordProperty("variant_description", e.description);
}
