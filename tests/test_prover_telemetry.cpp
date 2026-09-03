// Authoring telemetry (ROADMAP M1.7, ADR-017): the log grows by one line per
// append and parses back -- the Done-when the ROADMAP rewrite dropped and
// ADR-017 restored, pinned at the writer/reader pair the panel calls.
//
// Pinned here rather than through ComboProverPanel because the panel is ImGui
// and cannot be constructed headlessly; its runIfStale_ is thin glue over
// exactly one AppendProverRun call and names this file as its property test --
// the same division test_character_hotreload.cpp draws for the mode's reload
// poll.
#include <gtest/gtest.h>

#include "cse/data/AuthoringTelemetry.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace cse::data;

namespace {

ProverRunRecord sampleRecord() {
    ProverRunRecord r;
    r.unixTimeSeconds  = 1788185517;
    r.file             = "Characters/fighter_a.json";
    r.character        = "Fighter A";
    // Deliberately above 2^63: the fingerprint is a full uint64 and a record
    // format that round-tripped it through a signed slot would corrupt half
    // the hash space silently.
    r.contentHash      = 0xDEADBEEFCAFEF00Dull;
    r.changedSinceLast = true;
    r.moveCount        = 18;
    r.cancelCount      = 73;
    r.resources.push_back({ "meter", 0, 0 });
    r.resources.push_back({ "juggle", 4, 0 });
    r.explored         = 63;
    r.capped           = false;
    r.runMs            = 12.375;
    r.gapMs            = 11.5;
    r.verdict          = "terminating";
    r.verdictCounter   = "terminating";
    r.verdictAir       = "infinite";
    return r;
}

class ProverTelemetryTest : public ::testing::Test {
protected:
    // Relative to the working directory on purpose (the containment gate
    // refuses absolute paths), and NESTED on purpose: the panel's sink is
    // telemetry/prover_runs.jsonl and the directory does not exist until the
    // first append creates it.
    std::string rel_ = "test_telemetry_dir/prover_runs.jsonl";

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all("test_telemetry_dir", ec);
    }
};

} // namespace

// THE M1.7 PROPERTY: one append, one line; every field comes back.
TEST_F(ProverTelemetryTest, TheLogGrowsByOneAppendAndRoundTripsItsFields) {
    std::string error;
    const ProverRunRecord first = sampleRecord();
    ASSERT_TRUE(AppendProverRun(".", rel_, first, error)) << error;

    ProverRunRecord second = sampleRecord();
    second.changedSinceLast = false;   // the Re-run case: same bytes, new run
    second.verdict          = "infinite";
    second.file.clear();               // the caller-supplied-character case
    ASSERT_TRUE(AppendProverRun(".", rel_, second, error)) << error;

    std::vector<ProverRunRecord> back;
    std::int32_t skipped = -1;
    ASSERT_TRUE(ReadProverRuns(".", rel_, back, skipped, error)) << error;
    EXPECT_EQ(skipped, 0);
    ASSERT_EQ(back.size(), 2u) << "two appends did not make two records";

    const ProverRunRecord& a = back[0];
    EXPECT_EQ(a.unixTimeSeconds, first.unixTimeSeconds);
    EXPECT_EQ(a.file, first.file);
    EXPECT_EQ(a.character, first.character);
    EXPECT_EQ(a.contentHash, first.contentHash)
        << "the uint64 fingerprint did not survive the round trip";
    EXPECT_EQ(a.changedSinceLast, true);
    EXPECT_EQ(a.moveCount, 18);
    EXPECT_EQ(a.cancelCount, 73);
    ASSERT_EQ(a.resources.size(), 2u);
    EXPECT_EQ(a.resources[0].name, "meter");
    EXPECT_EQ(a.resources[1].name, "juggle");
    EXPECT_EQ(a.resources[1].initial, 4);
    EXPECT_EQ(a.explored, 63);
    EXPECT_FALSE(a.capped);
    EXPECT_DOUBLE_EQ(a.runMs, 12.375);
    EXPECT_DOUBLE_EQ(a.gapMs, 11.5)
        << "the resource-check cost must stay its OWN field (ADR-017)";
    EXPECT_EQ(a.verdict, "terminating");
    EXPECT_EQ(a.verdictCounter, "terminating")
        << "the per-opening verdicts (ADR-015) did not round-trip";
    EXPECT_EQ(a.verdictAir, "infinite");

    EXPECT_EQ(back[1].changedSinceLast, false);
    EXPECT_EQ(back[1].verdict, "infinite");
    EXPECT_TRUE(back[1].file.empty());
}

// The sink is an authored-adjacent path, so the same containment gate applies
// to it as to everything else that opens a file by name -- and "no runs yet"
// must stay distinguishable from "the log is unreadable".
TEST_F(ProverTelemetryTest, AnEscapingPathIsRefusedAndAMissingLogIsAnError) {
    std::string error;
    EXPECT_FALSE(AppendProverRun(".", "../evil.jsonl", sampleRecord(), error));
    EXPECT_NE(error.find("refused"), std::string::npos) << error;

    std::vector<ProverRunRecord> back;
    std::int32_t skipped = 0;
    error.clear();
    EXPECT_FALSE(ReadProverRuns(".", "test_telemetry_dir/never_written.jsonl",
                                back, skipped, error))
        << "a log nobody wrote read back as an empty healthy one";
    EXPECT_FALSE(error.empty());
}

// A crash mid-append costs ONE line, not the log -- and the cost is COUNTED,
// because a reader that skipped silently would report a half-eaten file as a
// short healthy one.
TEST_F(ProverTelemetryTest, AMalformedLineIsSkippedAndCountedNotFatal) {
    std::string error;
    ASSERT_TRUE(AppendProverRun(".", rel_, sampleRecord(), error)) << error;

    {
        // The torn tail a crash leaves: a line that is not JSON.
        std::ofstream out("test_telemetry_dir/prover_runs.jsonl",
                          std::ios::app | std::ios::binary);
        out << "{\"t\":178818, torn-off-mid-wri\n";
    }
    ASSERT_TRUE(AppendProverRun(".", rel_, sampleRecord(), error)) << error;

    std::vector<ProverRunRecord> back;
    std::int32_t skipped = 0;
    ASSERT_TRUE(ReadProverRuns(".", rel_, back, skipped, error)) << error;
    EXPECT_EQ(back.size(), 2u) << "a torn line took its neighbours with it";
    EXPECT_EQ(skipped, 1) << "the torn line was not counted";
}
