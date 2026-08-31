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

void bringUpVariant(const std::string& variantRel, Exhibit& out) {
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

    const BuildOptions options = normalBindings(out.character);
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
