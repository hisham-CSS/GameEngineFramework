// Character hot reload (ROADMAP M1.5, ADR-016): a frame-data edit lands in a
// running match by RESTART, and a broken edit keeps the last good match.
//
// Pinned at the seam the training mode mirrors rather than through the mode
// itself, which cannot be constructed headlessly: real files on disk, the real
// CharacterFileWatch, the real loader and bridge, and a real FightSession. The
// mode's pollHotReload_ is thin glue over exactly this sequence and names this
// file as its property test; a human plays the glued version at review point
// R5.
//
// What is deliberately NOT tested here: swapping MatchData under a live
// session. ADR-016 forbids it (rollback across the edit is undefined by
// FightSession.h, `resimulated` never flags the changed ticks, and a replay's
// single matchDataHash would describe a match nobody simulated), so the
// discipline below tears down and re-Begins instead -- the restart protocol
// tests/test_training_mode.cpp already pins.
#include <gtest/gtest.h>

#include "cse/data/CharacterData.h"
#include "cse/data/CharacterFileWatch.h"
#include "cse/data/MatchBuilder.h"

#include "cse/game/FightSession.h"

#include "cse/kernel/Combat.h"
#include "cse/kernel/GameState.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace cse::data;

namespace {

// The minimal valid schema-v2 character, one move, with the field the edits
// below move. Every value of `startup` used here has the same digit count, so
// a pure content edit does not also change the file size -- the mtime half of
// the stamp is what those edits exercise, and the size half gets its own
// same-timestamp write.
std::string characterJson(int startup) {
    return std::string(
               R"({"name":"hot","stage":"corner","walk_speed":0.5,)"
               R"("resources":[{"name":"meter","initial":0,"floor":0},)"
               R"({"name":"juggle","initial":4,"floor":0}],)"
               R"("scaling":[],"decay":{"kind":"none"},)"
               R"("moves":[{"id":"jab","startup":)") +
           std::to_string(startup) +
           R"(,"active":2,"recovery":4,"hitstun":8,"damage":10.0,)"
           R"("stance":"standing"}],"cancels":[]})";
}

// Write and then STAMP with an explicit modification time -- the
// test_ui_hotreload.cpp trick, for the same reason: real edits arrive seconds
// apart, a test's arrive microseconds apart, and on a filesystem with coarse
// mtime granularity two writes can land on the same tick.
void writeAt(const std::string& path, const std::string& text,
             int secondsFromNow) {
    { std::ofstream o(path, std::ios::binary); o << text; }
    std::filesystem::last_write_time(
        path, std::filesystem::file_time_type::clock::now() +
                  std::chrono::seconds(secondsFromNow));
}

class CharacterHotReloadTest : public ::testing::Test {
protected:
    // Relative to the working directory on purpose: Bind runs the same
    // containment gate as the loader, so an absolute temp directory would be
    // (correctly) refused.
    std::string file_ = "test_hot_reload_char.json";

    void SetUp() override { writeAt(file_, characterJson(3), 0); }
    void TearDown() override { std::remove(file_.c_str()); }

    // Poll on every Update, so tests drive the watch directly instead of
    // simulating the passage of time. The interval is tested separately.
    static void pollEveryCall(CharacterFileWatch& w) { w.SetPollInterval(0.0f); }

    // EXACTLY the discipline the mode's reload path runs: load and build into
    // the CALLER'S TEMPORARIES, touching nothing else. Keep-last-good is not a
    // property of these two functions -- LoadCharacterFile zeroes its output
    // even on a failed parse -- it is a property of who you point them at, and
    // that is the fact this helper exists to state.
    bool prepare(CharacterData& outCharacter, cse::data::MatchBuild& outBuild,
                 std::string& error) const {
        LoadReport report{};
        if (!LoadCharacterFile(".", file_, LoadOptions{}, outCharacter,
                               report)) {
            error = report.error;
            return false;
        }
        BuildOptions options{};
        MoveBinding binding{};
        binding.moveId = "jab";
        binding.button = cse::kernel::kInputLP;
        options.bindings.push_back(binding);
        if (!BuildMatchData(outCharacter, options, outCharacter, options,
                            outBuild)) {
            error = outBuild.report[0].error.empty() ? outBuild.report[1].error
                                                     : outBuild.report[0].error;
            return false;
        }
        return true;
    }

    static std::int32_t jabStartupOf(const cse::data::MatchBuild& build) {
        const std::uint16_t slot = build.moves[0].Find("jab");
        return slot == 0 ? -1 : build.data.p[0].moves[slot].startup;
    }
};

} // namespace

// Both halves of the stamp earn their keep: an edit that keeps the byte count
// still moves the timestamp, and an editor that preserves the timestamp still
// changes the size.
TEST_F(CharacterHotReloadTest, TheWatchNoticesATimestampEditAndASizeOnlyEdit) {
    CharacterFileWatch watch;
    std::string error;
    ASSERT_TRUE(watch.Bind(".", file_, error)) << error;
    pollEveryCall(watch);

    EXPECT_FALSE(watch.Update(0.0f)) << "reported a change with nothing edited";

    // Same byte count, later timestamp.
    writeAt(file_, characterJson(5), 2);
    EXPECT_TRUE(watch.Update(0.0f)) << "a same-size edit went unnoticed";

    // Different byte count, timestamp forced back to the value just recorded.
    const auto stamp = std::filesystem::last_write_time(file_);
    { std::ofstream o(file_, std::ios::binary); o << characterJson(5) << " "; }
    std::filesystem::last_write_time(file_, stamp);
    EXPECT_TRUE(watch.Update(0.0f)) << "a same-timestamp edit went unnoticed";
}

// One save, one report. The stamps refresh WHEN the change is reported, not
// when a reload succeeds -- otherwise a broken save would re-report every poll
// until fixed, and a caller that logs the failure would spam it forever.
TEST_F(CharacterHotReloadTest, AChangeReportsOnceAndTheFixingSaveReportsAgain) {
    CharacterFileWatch watch;
    std::string error;
    ASSERT_TRUE(watch.Bind(".", file_, error)) << error;
    pollEveryCall(watch);

    writeAt(file_, "{\"name\":\"half-typed", 2);
    EXPECT_TRUE(watch.Update(0.0f)) << "the broken save went unnoticed";
    EXPECT_FALSE(watch.Update(0.0f))
        << "the same broken save was reported twice";

    writeAt(file_, characterJson(3), 4);
    EXPECT_TRUE(watch.Update(0.0f))
        << "hot reload latched off after one bad edit";
}

// Statting a file every fixed step is wasted work in a shipped game where the
// file never changes; the interval is the whole cost model.
TEST_F(CharacterHotReloadTest, TheIntervalThrottlesStatting) {
    CharacterFileWatch watch;
    std::string error;
    ASSERT_TRUE(watch.Bind(".", file_, error)) << error;
    watch.SetPollInterval(0.25f);

    writeAt(file_, characterJson(5), 2);
    // The change exists on disk but must not have been looked for yet.
    EXPECT_FALSE(watch.Update(0.05f));
    EXPECT_FALSE(watch.Update(0.05f));
    EXPECT_TRUE(watch.Update(0.20f))
        << "the interval elapsed and nothing was polled";
}

// The watch opens an authored path, so the same containment gate applies to it
// as to the loader it feeds.
TEST_F(CharacterHotReloadTest, AnEscapingPathIsRefusedAtBind) {
    CharacterFileWatch watch;
    std::string error;
    EXPECT_FALSE(watch.Bind(".", "../evil.json", error));
    EXPECT_FALSE(watch.Bound());
    EXPECT_NE(error.find("refused"), std::string::npos)
        << "expected a containment refusal, got: " << error;
    // Unbound is quiet, not undefined: the mode calls Update unconditionally.
    EXPECT_FALSE(watch.Update(1.0f));
}

// The mode binds the watch after a load that may have FAILED -- content not
// staged is the honest-error screen. The file appearing is the change that
// revives it, which previously only the C key (advancing to the NEXT
// character) could do.
TEST_F(CharacterHotReloadTest, AMissingFileBindsAndItsAppearanceIsAChange) {
    const std::string missing = "test_hot_reload_missing.json";
    std::remove(missing.c_str());

    CharacterFileWatch watch;
    std::string error;
    ASSERT_TRUE(watch.Bind(".", missing, error))
        << "a missing file refused to bind, so a dead match screen can never "
           "recover by staging the content: "
        << error;
    pollEveryCall(watch);
    EXPECT_FALSE(watch.Update(0.0f)) << "a still-missing file reported a change";

    writeAt(missing, characterJson(3), 2);
    EXPECT_TRUE(watch.Update(0.0f)) << "the file appearing went unnoticed";
    std::remove(missing.c_str());
}

// THE M1.5 PROPERTY, end to end: NORTHSTAR (c)'s last clause with ADR-016's
// semantics. An edited file is noticed and lands in a running FightSession as
// a restart with the freshly built data; a broken edit -- the normal state
// while typing -- keeps the last good match running.
TEST_F(CharacterHotReloadTest,
       AFrameDataEditLandsInARunningMatchAndABrokenEditKeepsTheLastGoodData) {
    CharacterData character{};
    cse::data::MatchBuild build{};
    std::string error;
    ASSERT_TRUE(prepare(character, build, error)) << error;
    ASSERT_EQ(jabStartupOf(build), 3);

    cse::game::FightSession session;
    cse::game::FightSetup setup{};
    setup.data = &build.data;   // borrowed for the session's whole life
    std::string beginError;
    ASSERT_TRUE(session.Begin(setup, beginError)) << beginError;
    for (int i = 0; i < 5; ++i) session.Tick();
    ASSERT_EQ(session.CurrentTick(), 5u);

    CharacterFileWatch watch;
    ASSERT_TRUE(watch.Bind(".", file_, error)) << error;
    pollEveryCall(watch);
    EXPECT_FALSE(watch.Update(0.0f)) << "reported a change with nothing edited";

    // --- the edit lands ------------------------------------------------------
    writeAt(file_, characterJson(7), 2);
    ASSERT_TRUE(watch.Update(0.0f))
        << "an edited character file went unnoticed";

    CharacterData newCharacter{};
    cse::data::MatchBuild newBuild{};
    ASSERT_TRUE(prepare(newCharacter, newBuild, error)) << error;

    // ADOPT, then RESTART. The build the session borrows is replaced and the
    // session is immediately re-Begun -- the mode does this with its sources
    // and observers detached first; nothing ticks in between in either place.
    // Begin IS the restart: state memset, tick index and high-water reset
    // (test_training_mode.cpp pins that protocol).
    character = newCharacter;
    build     = newBuild;
    ASSERT_TRUE(session.Begin(setup, beginError)) << beginError;
    EXPECT_EQ(session.CurrentTick(), 0u);

    const std::uint16_t slot = build.moves[0].Find("jab");
    ASSERT_NE(slot, 0u);
    EXPECT_EQ(session.Data().p[0].moves[slot].startup, 7)
        << "the frame-data edit did not land in the running match";

    for (int i = 0; i < 3; ++i) session.Tick();
    ASSERT_EQ(session.CurrentTick(), 3u);

    // --- a broken edit keeps the last good data ------------------------------
    writeAt(file_, "{\"name\":\"broken\"", 4);   // half-typed, mid-save
    ASSERT_TRUE(watch.Update(0.0f)) << "the broken save went unnoticed";

    CharacterData scratchCharacter{};
    cse::data::MatchBuild scratchBuild{};
    std::string reloadError;
    ASSERT_FALSE(prepare(scratchCharacter, scratchBuild, reloadError))
        << "a half-typed file parsed, so this test is exercising nothing";
    EXPECT_FALSE(reloadError.empty())
        << "a failed reload said nothing about why";

    // Nothing was adopted: the running match still runs the last good data,
    // at the tick it had reached, and keeps ticking.
    EXPECT_EQ(session.Data().p[0].moves[slot].startup, 7)
        << "a broken edit reached the live MatchData";
    session.Tick();
    EXPECT_EQ(session.CurrentTick(), 4u)
        << "a broken edit stopped the running match";

    // And the fixing save reports again, because the stamps refreshed on the
    // broken report rather than latching off.
    writeAt(file_, characterJson(9), 6);
    EXPECT_TRUE(watch.Update(0.0f))
        << "hot reload latched off after one bad edit";
}
