// COMBO VERDICTS BY EXECUTION -- ComboSearch against the two shipped
// characters (docs/adr/ADR-013, ROADMAP M1.4a+M1.4).
//
// The pair is the instrument: `fighter_a_infinite` carries a deliberate
// infinite and the search must FIND it as a replayable witness;` fighter_a`
// is TERMINATING and the search must exhaust it and measure the kernel's own
// worst case -- the executed counterpart of the prover's maxHits. A budget
// too small must say UNRESOLVED and nothing else, and the whole result must
// be bit-identical on a second run, because a verdict that changes between
// runs cannot gate a cooker.

#include "cse/game/ComboSearch.h"
#include "cse/game/WitnessCursor.h"

#include "cse/data/CharacterData.h"
#include "cse/data/MatchBuilder.h"
#include "cse/kernel/Simulate.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

using cse::data::BuildMatchData;
using cse::data::BuildOptions;
using cse::data::CharacterData;
using cse::data::LoadCharacterFile;
using cse::data::LoadOptions;
using cse::data::LoadReport;
using cse::data::MatchBuild;
using cse::game::ComboSearchRequest;
using cse::game::ComboSearchResult;
using cse::game::ComboVerdict;
using cse::game::RunComboSearch;

namespace {

constexpr std::int32_t px(std::int32_t v) {
    return v * cse::kernel::kSubUnitsPerPixel;
}

// The corner opening every shipped verdict is computed for -- the same
// numbers test_ground_truth derives at length: the defender's BODY against
// the wall (half-width 13 px), origins 34 px apart.
constexpr std::int32_t kStageEdge = 480 * cse::kernel::kSubUnitsPerPixel;
constexpr std::int32_t kP1X       = kStageEdge - px(13);
constexpr std::int32_t kP0X       = kP1X - px(34);

const std::vector<std::string> kBuildResources = { "meter", "juggle" };

// The staged shipping directory, exactly as test_ground_truth documents it.
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
        const fs::path source = here / "Editor" / "src" / "Exported" / "Characters";
        if (fs::exists(source / marker)) return source.string();
        if (!here.has_parent_path() || here.parent_path() == here) break;
        here = here.parent_path();
    }
    return "Exported/Characters";
}

// Bind every normal the character authors to its arcade button -- the same
// (button x stance) binding the mode ships and M1.1c proved reachable, so the
// search explores what a player at the keyboard can actually do.
BuildOptions normalBindings(const CharacterData& c) {
    const char* kButtons[] = { "lp", "mp", "hp", "lk", "mk", "hk" };
    const std::uint16_t kBits[] = {
        cse::kernel::kInputLP, cse::kernel::kInputMP, cse::kernel::kInputHP,
        cse::kernel::kInputLK, cse::kernel::kInputMK, cse::kernel::kInputHK };
    BuildOptions options{};
    for (int b = 0; b < 6; ++b) {
        for (const char* prefix : { "stand_", "crouch_", "air_" }) {
            const std::string id = std::string(prefix) + kButtons[b];
            if (c.FindMove(id) != cse::data::kInvalidMove)
                options.bindings.push_back({ id, kBits[b] });
        }
    }
    return options;
}

struct Bench {
    CharacterData character{};
    MatchBuild    build{};
    ComboSearchRequest request{};
};

void bringUp(const char* file, Bench& out) {
    LoadOptions lo;
    lo.expectedResources = kBuildResources;
    LoadReport report{};
    ASSERT_TRUE(LoadCharacterFile(charactersDir(), file, lo, out.character, report))
        << file << ": " << report.error;

    const BuildOptions options = normalBindings(out.character);
    ASSERT_FALSE(options.bindings.empty()) << file << " authors no normals";
    ASSERT_TRUE(BuildMatchData(out.character, options, out.character, options,
                               out.build))
        << out.build.report[0].error;

    cse::kernel::GameState s{};
    cse::kernel::ResetMatch(s, 0x1D7u);
    s.p[0].posX = kP0X;
    s.p[1].posX = kP1X;
    s.p[0].facing = 0;
    s.p[1].facing = 1;

    out.request.data         = &out.build.data;
    out.request.from         = s;
    out.request.attackerSlot = 0;
}

std::string describe(const MatchBuild& build, const ComboSearchResult& r) {
    std::string s = "\n  verdict  ";
    s += r.verdict == ComboVerdict::Infinite ? "INFINITE"
         : r.verdict == ComboVerdict::Terminating ? "TERMINATING" : "UNRESOLVED";
    s += "\n  note     " + r.note + "\n  witness  ";
    for (std::size_t i = 0; i < r.witness.size(); ++i) {
        if (i == r.loopStart) s += "[loop] ";
        s += std::string(build.moves[0].IdOf(r.witness[i])) + " ";
    }
    s += "\n  longest  ";
    for (const std::uint16_t m : r.longestString)
        s += std::string(build.moves[0].IdOf(m)) + " ";
    s += "\n  budget   " + std::to_string(r.ticksUsed) + " tick(s), " +
         std::to_string(r.nodesExpanded) + " node(s)\n";
    return s;
}

} // namespace

TEST(ComboSearchVerdicts, TheAuthoredInfiniteIsFoundAsAReplayableWitness) {
    Bench b{};
    bringUp("fighter_a_infinite.json", b);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    const ComboSearchResult r = RunComboSearch(b.request);

    ASSERT_EQ(r.verdict, ComboVerdict::Infinite)
        << "the character authored to carry an infinite came back "
        << describe(b.build, r);
    ASSERT_LT(r.loopStart, r.witness.size());
    EXPECT_GE(r.witness.size() - r.loopStart, 1u)
        << "an empty loop is not a witness" << describe(b.build, r);

    // AND THE WITNESS REPLAYS. The search's claim is an execution claim, so
    // it is checked by executing: drive the printed witness through the same
    // WitnessCursor the search performs with and require twice the loop's
    // hits with the defender's stun never reaching zero between them --
    // the direct reading, off the state.
    std::vector<std::string> ids;
    for (const std::uint16_t m : r.witness)
        ids.push_back(std::string(b.build.moves[0].IdOf(m)));
    cse::game::WitnessDriver driver(cse::game::WitnessCursor::FromIds(
        ids, r.loopStart, b.build.moves[0], b.build.data.p[0]));
    std::string why;
    ASSERT_TRUE(driver.Usable(why)) << why;

    cse::kernel::GameState s = b.request.from;
    int hits = 0, freeAfterFirstHit = 0;
    const int wanted = static_cast<int>(2 * (r.witness.size() - r.loopStart) + 4);
    for (int t = 0; t < 4000 && hits < wanted; ++t) {
        const std::uint16_t stunBefore = s.p[1].hitstun;
        const std::int32_t healthBefore = s.p[1].health;
        cse::kernel::InputPair in{};
        in.p[0].bits = driver.Bits();
        cse::kernel::Simulate(s, in, b.build.data);
        driver.Observe(s.p[0].moveId, s.p[0].moveFrame);
        if (s.p[1].health < healthBefore) ++hits;
        else if (hits > 0 && stunBefore <= 1) ++freeAfterFirstHit;
    }
    EXPECT_GE(hits, wanted)
        << "the printed witness performed " << hits << " hit(s) where "
        << wanted << " prove two turns of its loop" << describe(b.build, r);
    EXPECT_EQ(freeAfterFirstHit, 0)
        << "the defender was actionable on " << freeAfterFirstHit
        << " tick(s) inside the witness, so the loop the search printed is "
           "escapable and INFINITE is the wrong word for it"
        << describe(b.build, r);
}

TEST(ComboSearchVerdicts, TheSafeCharacterTerminatesAndTheWorstCaseIsMeasured) {
    Bench b{};
    bringUp("fighter_a.json", b);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    const ComboSearchResult r = RunComboSearch(b.request);

    ASSERT_EQ(r.verdict, ComboVerdict::Terminating)
        << "fighter_a is the TERMINATING character; a budget verdict here "
           "means the defaults no longer resolve the shipped roster and must "
           "be raised with the reason" << describe(b.build, r);

    // The arc string exists (four aerial repetitions per jump, measured in
    // test_ground_truth section 5), so the kernel's own worst case is at
    // least that. The exact number is REPORTED rather than pinned: it is the
    // paper's to quote alongside the prover's maxHits, and gap_extent's
    // properties are where its stability is asserted.
    EXPECT_GE(r.maxHits, 4) << describe(b.build, r);
    RecordProperty("kernel_max_hits", r.maxHits);
    RecordProperty("nodes_expanded", static_cast<int>(r.nodesExpanded));
    RecordProperty("ticks_used", static_cast<int>(r.ticksUsed));

    std::cout << "\n[ COMBO SEARCH ] fighter_a, by execution:"
              << describe(b.build, r) << "\n";

}

TEST(ComboSearchVerdicts, ABudgetProducesUnresolvedAndNeverAVerdict) {
    Bench b{};
    bringUp("fighter_a.json", b);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    b.request.maxTicks = 500;
    const ComboSearchResult r = RunComboSearch(b.request);

    EXPECT_EQ(r.verdict, ComboVerdict::Unresolved)
        << "500 ticks cannot exhaust this character, so any verdict from them "
           "is a guess wearing a uniform" << describe(b.build, r);
    EXPECT_LE(r.ticksUsed, 500u + b.request.maxMacroTicks)
        << "the budget was overshot by more than one in-flight macro";
}

TEST(ComboSearchVerdicts, TheResultIsBitIdenticalOnASecondRun) {
    Bench b{};
    bringUp("fighter_a_infinite.json", b);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    const ComboSearchResult a = RunComboSearch(b.request);
    const ComboSearchResult c = RunComboSearch(b.request);

    EXPECT_EQ(static_cast<int>(a.verdict), static_cast<int>(c.verdict));
    EXPECT_EQ(a.witness, c.witness);
    EXPECT_EQ(a.loopStart, c.loopStart);
    EXPECT_EQ(a.maxHits, c.maxHits);
    EXPECT_EQ(a.longestString, c.longestString);
    EXPECT_EQ(a.ticksUsed, c.ticksUsed);
    EXPECT_EQ(a.nodesExpanded, c.nodesExpanded);
    EXPECT_EQ(a.note, c.note)
        << "two runs of the same request disagreed; a verdict that changes "
           "between runs cannot gate a cooker, golden a test, or be believed.";
}
