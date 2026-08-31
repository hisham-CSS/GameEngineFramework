// THE COOKER RECORDING (ROADMAP M1.6, ADR-011 section 4), proven on a SUBSET.
//
// The full catalogue cook runs every exhibit's search and takes the better
// part of a minute; this test cooks a two-row manifest -- the base and the
// hitstun_plus_7 infinite, one row per verdict kind -- and then re-verifies
// every claim the artifacts make FROM THE FILES, not from the report: the
// replay on disk decodes, matches the rebuilt character bytes, re-simulates
// bit-identical under ReplayVerifier, and the graph is a digraph whose loop
// is drawn. The full nine-row manifest ships beside the variants and the
// UntitledFighterCatalogue tool cooks it; what this test owns is that the
// cook's artifacts survive an independent audit, which is the whole reason
// artifacts beat assertions.
#include "cse/game/Catalogue.h"

#include "cse/data/CharacterData.h"
#include "cse/data/MatchBuilder.h"
#include "cse/game/Replay.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using cse::game::CatalogueReport;
using cse::game::CookCatalogue;
using cse::game::CookedEntry;

namespace {

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
    return "Exported/Characters";
}

std::string readAll(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// A two-row manifest written into the STAGED characters directory (beside the
// real one, under a test-owned name), because LoadCatalogueManifest reads
// through the same sandbox as every authored file and a manifest outside the
// characters tree is exactly what it must refuse.
const char* kSubsetManifestRel = "fighter_a/variants/catalogue_test_subset.json";

void writeSubsetManifest() {
    namespace fs = std::filesystem;
    const fs::path path = fs::path(charactersDir()) / kSubsetManifestRel;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << R"({
  "base": "fighter_a.json",
  "entries": [
    { "name": "base" },
    { "name": "hitstun_plus_7", "variant": "fighter_a/variants/hitstun_plus_7.json" }
  ]
})";
}

} // namespace

TEST(Catalogue, TheCookedArtifactsSurviveAnIndependentAudit) {
    namespace fs = std::filesystem;
    writeSubsetManifest();
    const std::string outDir =
        (fs::path(::testing::TempDir()) / "cse_catalogue").string();

    CatalogueReport report{};
    ASSERT_TRUE(CookCatalogue(charactersDir(), kSubsetManifestRel, outDir,
                              report))
        << report.error
        << (report.entries.empty() ? "" : report.entries.back().error);
    ASSERT_EQ(report.entries.size(), 2u);

    // --- the base row: the pair agrees, TERMINATING both sides --------------
    const CookedEntry& base = report.entries[0];
    EXPECT_EQ(base.name, "base");
    EXPECT_EQ(base.proverStatus, "TERMINATING");
    EXPECT_EQ(base.searchVerdict, "TERMINATING");
    EXPECT_TRUE(base.verdictsAgree);
    EXPECT_GT(base.maxHits, 0);
    EXPECT_FALSE(base.replayRel.empty())
        << "the base row demonstrated nothing; its longest string is the "
           "executed worst case and must be watchable";

    // --- the infinite row: the pair DISAGREES, and that is the exhibit ------
    const CookedEntry& inf = report.entries[1];
    EXPECT_EQ(inf.proverStatus, "TERMINATING")
        << "the restart route is not in the model's graph; if this moved, the "
           "exhibit's premise did";
    EXPECT_EQ(inf.searchVerdict, "INFINITE");
    EXPECT_FALSE(inf.verdictsAgree);
    EXPECT_FALSE(inf.description.empty());

    // --- the audit: every claim re-verified FROM THE FILES ------------------
    for (const CookedEntry& e : report.entries) {
        SCOPED_TRACE(e.name);
        ASSERT_TRUE(e.error.empty()) << e.error;

        // The graph is a digraph and mentions the exhibit's own move.
        const std::string dot = readAll(fs::path(outDir) / e.dotRel);
        EXPECT_NE(dot.find("digraph"), std::string::npos);
        EXPECT_NE(dot.find("stand_lp"), std::string::npos);

        // The replay decodes from DISK, names the character, and matches the
        // REBUILT bytes -- the hash check that catches a catalogue cooked
        // against one build and shipped against another.
        cse::game::ReplayData    replay{};
        cse::game::ReplayReport  replayReport{};
        cse::game::ReplayReadOptions readOptions{};
        ASSERT_TRUE(cse::game::ReadReplayFile(outDir, e.replayRel, readOptions,
                                              replay, replayReport))
            << replayReport.error;
        EXPECT_GT(replay.TickCount(), 0u);

        // Re-simulate the whole replay and demand bit-identity: the cook
        // already verified before writing, and this asserts the FILE carries
        // the same guarantee -- decode, Begin from its own header, drive both
        // players from its own inputs, compare every checkpoint.
        cse::data::CharacterData c{};
        cse::data::LoadReport    lr{};
        cse::data::LoadOptions   lo;
        lo.expectedResources = { "meter", "juggle" };
        if (e.name == "base") {
            ASSERT_TRUE(cse::data::LoadCharacterFile(
                charactersDir(), "fighter_a.json", lo, c, lr))
                << lr.error;
        } else {
            ASSERT_TRUE(cse::data::LoadCharacterVariant(
                charactersDir(), "fighter_a.json",
                "fighter_a/variants/hitstun_plus_7.json", lo, c, lr, nullptr))
                << lr.error;
        }
        // The same bindings the manifest records for these two rows: normals.
        cse::data::BuildOptions bo{};
        {
            const char* kButtons[] = { "lp", "mp", "hp", "lk", "mk", "hk" };
            const std::uint16_t kBits[] = {
                cse::kernel::kInputLP, cse::kernel::kInputMP,
                cse::kernel::kInputHP, cse::kernel::kInputLK,
                cse::kernel::kInputMK, cse::kernel::kInputHK };
            for (int b = 0; b < 6; ++b)
                for (const char* prefix : { "stand_", "crouch_", "air_" }) {
                    const std::string id = std::string(prefix) + kButtons[b];
                    if (c.FindMove(id) != cse::data::kInvalidMove)
                        bo.bindings.push_back({ id, kBits[b] });
                }
        }
        cse::data::MatchBuild build{};
        ASSERT_TRUE(cse::data::BuildMatchData(c, bo, c, bo, build))
            << build.report[0].error;
        std::string matchError;
        ASSERT_TRUE(cse::game::ReplayMatchesData(replay, build.data, matchError))
            << matchError;

        cse::game::FightSetup setup{};
        setup.start = replay.start;
        setup.data  = &build.data;
        cse::game::FightSession session{};
        std::string sessionError;
        ASSERT_TRUE(session.Begin(setup, sessionError)) << sessionError;
        cse::game::ReplayInputSource p0(replay, 0);
        cse::game::ReplayInputSource p1(replay, 1);
        session.SetInputSource(0, &p0);
        session.SetInputSource(1, &p1);
        cse::game::ReplayVerifier verifier(replay);
        session.AddObserver(&verifier);
        for (std::uint32_t t = 0; t < replay.TickCount(); ++t) session.Tick();

        EXPECT_FALSE(verifier.Result().diverged)
            << "the written replay does not re-simulate to its own "
               "checkpoints";
        EXPECT_FALSE(verifier.Result().inputMismatch);
        EXPECT_GT(verifier.CheckpointsCompared(), 0u);
        EXPECT_EQ(verifier.CheckpointsCompared(), verifier.CheckpointsAgreed());
    }

    // --- and the summary says what the report says --------------------------
    const std::string summary = readAll(fs::path(outDir) / "catalogue.txt");
    EXPECT_NE(summary.find("base"), std::string::npos);
    EXPECT_NE(summary.find("hitstun_plus_7"), std::string::npos);
    EXPECT_NE(summary.find("THE PAIR DISAGREES"), std::string::npos)
        << "the infinite row's disagreement is the exhibit and the summary "
           "must say so";
}
