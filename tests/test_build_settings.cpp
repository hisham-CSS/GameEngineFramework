// tests/test_build_settings.cpp
//
// WHAT SHIPS, AS DATA -- pinned. (Engine/src/core/BuildSettings.{h,cpp})
//
// build.json is the file that answers "which scenes are in my game, and in what
// order". It is written by the editor, read by the editor, and NEVER read by a
// shipped game; the arrow to the runtime contract runs exactly once, through
// `ApplyTo`, at Build time. Every claim below is about that file, and they are
// ordered by how much it would hurt to get one wrong:
//
//   1. ROUND TRIP (BuildSettingsRoundTrip, BuildSettingsList). Settings written
//      and read back are identical, INCLUDING SCENE ORDER. The order is the
//      entire reason `scenes` is a vector rather than a set -- it is Unity's
//      build index, it is what ApplyTo reads to write `startupScene`, and it is
//      what a future load-by-index would mean. A save/load that returned the
//      right three scenes in the wrong order would ship a game that boots the
//      results screen.
//
//   2. A HOSTILE OR STALE FILE IS REFUSED, NOT CRASHED, AND NEVER HALF-APPLIED
//      (BuildSettingsHostile, BuildSettingsFuzz). This is editor-written JSON,
//      which means it gets hand-edited, copied between machines, merged badly
//      and truncated by a full disk. Every refusal must leave the object
//      EXACTLY as it was: the panel's next act after a bad load would otherwise
//      be to Save the two-line default over somebody's twelve-scene list,
//      because one comma was wrong. The fuzz drives that with a seeded
//      xorshift32 rather than <random>, so a failure reproduces from the seed
//      printed beside it -- on both toolchains.
//
//   3. THE MIGRATION (BuildSettingsMigration). Every project that exists today
//      has a `startupScene` in project.json and no build.json at all. Opening
//      the build panel in one of those projects must not show an empty list --
//      that is a project whose Build button produces a game with no scenes in
//      it. `Load` returning Missing followed by `SeedFromProjectSettings` is
//      the whole migration, and it has to survive a Save/Load cycle and the
//      shipped game's own load-modify-save of project.json.
//
//   4. THE OUTPUT DIRECTORY IS CONTAINED (BuildSettingsOutputDirectory). The
//      build WRITES into this directory and MIRRORS it -- files it did not
//      write are removed. An escaping outputDirectory is therefore a delete
//      primitive with a text field in front of it, and it must be refused by
//      the same rule every other authored path in this engine obeys, not by a
//      second hand-rolled one that drifts.
//
// TESTED AGAINST THE ORACLE, NOT AGAINST A TABLE OF VERDICTS. Wherever a claim
// is "obeys the containment rule", the test asks PathIsContained rather than
// asserting a hand-written yes/no. That is the only honest way to test "the
// SAME rule", and it is also the only portable one: "C:/Windows/x.json" is a
// drive-rooted absolute path on Windows and an ordinary three-element relative
// path on Linux, and CI compiles both.
//
// NOTHING HERE TOUCHES BuildPipeline, deliberately. This file is about the
// FILE -- what it says, what it refuses, and what a Build reads out of it --
// and none of that needs a toolchain, a child process or a build tree. What the
// pipeline DOES with these settings (preflight, compile, assemble, validate)
// belongs to its own test, driven through the BuildProcessLauncher seam that
// BuildPipeline.h declares for exactly that purpose. BuildPipeline.h arrives
// here transitively through Engine.h and that is harmless: a declaration nobody
// calls emits nothing, and this file names none of its functions.
#include <gtest/gtest.h>
#include "Engine.h"
#include "../Engine/src/core/PathSandbox.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include <nlohmann/json.hpp>

using namespace MyCoreEngine;
namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

// ============================================================================
// 0a. THE BOUNDS, AS LOCALS
// ============================================================================
// Copied out of the struct once, because a gtest macro binds its arguments to
// `const T&` -- an odr-use of a static constexpr member declared inside a type
// carrying a dll-interface attribute. Reading them into ordinary constants is
// free and sidesteps the question entirely.
constexpr std::size_t kMaxScenes     = BuildSettings::kMaxScenes;
constexpr std::size_t kMaxPathLength = BuildSettings::kMaxPathLength;
constexpr std::size_t kMaxFileBytes  = BuildSettings::kMaxFileBytes;
constexpr int         kVersion       = BuildSettings::kVersion;

// ============================================================================
// 0b. SCRATCH FILES
// ============================================================================
// Named after the test that asked for one, so two tests running under
// `ctest -j` cannot land on the same path, and so a file left behind by a
// crash names its own culprit. Removed by the destructor: a fuzz that leaves a
// thousand files in the system temp directory is a fuzz nobody runs twice.
std::string ScratchPath(const char* stem) {
    const ::testing::TestInfo* info =
        ::testing::UnitTest::GetInstance()->current_test_info();
    const std::string name = info ? info->name() : "unnamed";
    return (fs::temp_directory_path() /
            ("cse_build_settings_" + name + "_" + stem + ".json")).string();
}

class Scratch {
public:
    explicit Scratch(const char* stem = "a") : path_(ScratchPath(stem)) {
        Remove(); // a leftover from a previous crashed run must not be read
    }
    ~Scratch() { Remove(); }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;

    const std::string& path() const { return path_; }

    // BINARY, deliberately. A text-mode stream on Windows turns every '\n' into
    // two bytes, which would move a 1 MiB boundary test off the boundary and
    // make a fuzz truncation offset mean something different on each platform.
    bool WriteRaw(const std::string& text) const {
        std::ofstream out(path_, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        return out.good();
    }

    // The nasty *contents* go through a real JSON writer, so that a path with a
    // quote, a backslash or an embedded NUL in it is escaped the way an editor
    // would escape it rather than the way a test author guessed.
    bool Write(const json& doc) const { return WriteRaw(doc.dump(2)); }

    void Remove() const { std::error_code ec; fs::remove(path_, ec); }

private:
    std::string path_;
};

// ============================================================================
// 0c. THE TEST'S OWN GENERATOR
// ============================================================================
// xorshift32, written out here rather than taken from <random>, for exactly the
// reason tests/test_game_core.cpp gives for its copy: the standard library's
// engines are not specified to produce identical sequences across
// implementations, and libstdc++ and the MSVC STL genuinely differ. A fuzz whose
// sequence depends on the toolchain is a fuzz whose failure cannot be reproduced
// from the seed printed in the log -- which is the only reason to seed one.
class Rng {
public:
    explicit Rng(std::uint32_t seed) : s_(seed != 0u ? seed : 0x9E3779B9u) {}

    std::uint32_t Next() {
        s_ ^= s_ << 13;
        s_ ^= s_ >> 17;
        s_ ^= s_ << 5;
        return s_;
    }

    std::uint32_t Below(std::uint32_t bound) { return bound == 0u ? 0u : Next() % bound; }

private:
    std::uint32_t s_;
};

// ============================================================================
// 0d. THE STATE EVERY REFUSAL IS MEASURED AGAINST
// ============================================================================
// Deliberately NOT the defaults. "The object was untouched" is only a claim
// worth making about an object that had something to lose: three scenes in a
// specific order, an output directory somebody typed, and the one profile whose
// silent replacement by Development would ship a console window to a player.
//
// The scenes are assigned straight into the vector rather than through
// AddScene, which is legitimate ONLY because these three literals are already
// in normalized form -- BuildSettings.cpp's note about storing the approved
// spelling is the invariant being respected here, not dodged.
BuildSettings Primed() {
    BuildSettings s;
    s.scenes = { "Exported/menu.json", "Exported/fight.json", "Exported/results.json" };
    s.outputDirectory = "Builds/Tournament";
    s.profile = BuildProfile::Shipping;
    return s;
}

bool Same(const BuildSettings& a, const BuildSettings& b) {
    return a.scenes == b.scenes
        && a.outputDirectory == b.outputDirectory
        && a.profile == b.profile;
}

std::string Describe(const BuildSettings& s) {
    std::string out = "profile=";
    out += BuildProfileToken(s.profile);
    out += " outputDirectory='" + s.outputDirectory + "' scenes=[";
    for (std::size_t i = 0; i < s.scenes.size(); ++i) {
        if (i) out += ", ";
        out += "'" + s.scenes[i] + "'";
    }
    out += "]";
    return out;
}

// ============================================================================
// 0e. THE CONTAINMENT ORACLE
// ============================================================================
// Containment is NOT re-implemented here. The claim under test is "build
// settings obey the same rule as every other authored path", and the only way
// to test sameness is to ask the rule. Re-deriving the verdict would test that
// two implementations agree today, which is a different and much weaker claim.
bool OracleAccepts(const std::string& raw) {
    const std::string rel = BuildSettings::NormalizeScenePath(raw);
    fs::path full;
    return PathIsContained(std::string(), rel, full);
}

// Load logs every refusal to stderr, which is right for an editor and wrong for
// a sweep of several hundred deliberate refusals: it would bury the CI log in
// the messages this test is already asserting on through `r.message`. Silenced
// for the sweep only -- the dozen targeted refusals stay loud, because their
// text is the evidence that the message is worth showing a human.
class SilencedCerr {
public:
    SilencedCerr() : saved_(std::cerr.rdbuf(sink_.rdbuf())) {}
    ~SilencedCerr() { std::cerr.rdbuf(saved_); }
    SilencedCerr(const SilencedCerr&) = delete;
    SilencedCerr& operator=(const SilencedCerr&) = delete;
private:
    std::ostringstream sink_;   // constructed first: `saved_` reads from it
    std::streambuf*    saved_;
};

// Every refusal makes the same four statements at once, so they are made in one
// place: Malformed, with a reason, unsafe to save over, and the object exactly
// as it was. `what` names the hostility for the failure message.
void ExpectRefusedAndUntouched(const std::string& text, const char* what) {
    Scratch file("hostile");
    ASSERT_TRUE(file.WriteRaw(text)) << "could not write the scratch file";

    const BuildSettings before = Primed();
    BuildSettings s = before;

    const BuildSettingsLoadResult r = s.Load(file.path());

    EXPECT_EQ(r.status, BuildSettingsStatus::Malformed)
        << what << ": this was accepted (or reported Missing) rather than refused.\n"
        << "  file: " << text.substr(0, 400);
    EXPECT_FALSE(r.ok()) << what;
    EXPECT_FALSE(r.safeToSave())
        << what << ": safeToSave() said yes after a refusal -- the panel would "
           "now overwrite the author's real build list with the defaults";
    EXPECT_FALSE(r.message.empty())
        << what << ": refused with no reason, so the panel has nothing to show";

    EXPECT_TRUE(Same(s, before))
        << what << ": the refusal was half-applied.\n"
        << "  before: " << Describe(before) << "\n"
        << "  after:  " << Describe(s) << "\n"
        << "  reason given: " << r.message;
}

// A document with one key replaced, so the hostile cases below differ from a
// good file by exactly the thing being tested.
json GoodDoc() {
    json doc;
    doc["version"] = kVersion;
    doc["scenes"] = json::array({ "Exported/menu.json",
                                  "Exported/fight.json",
                                  "Exported/results.json" });
    doc["outputDirectory"] = "Builds/Game";
    doc["profile"] = "shipping";
    return doc;
}

json GoodDocWith(const char* key, const json& value) {
    json doc = GoodDoc();
    doc[key] = value;
    return doc;
}

} // namespace

// ============================================================================
// 1. ROUND TRIP -- AND ORDER IS THE POINT
// ============================================================================

TEST(BuildSettingsRoundTrip, TheOrderedListSurvivesSaveAndLoad) {
    Scratch file;

    BuildSettings out;
    // A deliberately un-sorted order that is also not the order the strings
    // would fall into by any accident: alphabetically this is fight, menu,
    // results, and by length it is menu, fight, results.
    ASSERT_TRUE(out.AddScene("Exported/menu.json"));
    ASSERT_TRUE(out.AddScene("Exported/results.json"));
    ASSERT_TRUE(out.AddScene("Exported/fight.json"));
    out.outputDirectory = "Builds/Tournament";
    out.profile = BuildProfile::Shipping;

    ASSERT_TRUE(out.Save(file.path()));

    BuildSettings in;
    const BuildSettingsLoadResult r = in.Load(file.path());
    ASSERT_EQ(r.status, BuildSettingsStatus::Ok) << r.message;
    EXPECT_TRUE(r.ok());
    EXPECT_TRUE(r.safeToSave());

    const std::vector<std::string> expected = { "Exported/menu.json",
                                                "Exported/results.json",
                                                "Exported/fight.json" };
    EXPECT_EQ(in.scenes, expected)
        << "the build list came back in a different order than it was written -- "
           "the index IS the meaning here";
    EXPECT_EQ(in.outputDirectory, "Builds/Tournament");
    EXPECT_EQ(in.profile, BuildProfile::Shipping);
    EXPECT_EQ(in.StartupScene(), "Exported/menu.json");
    EXPECT_TRUE(Same(in, out));
}

TEST(BuildSettingsRoundTrip, ReversingTheListReversesTheFile) {
    // The other half of "order survives": a test that only ever writes one
    // order cannot tell a list from a set. Write the reverse and require the
    // reverse back.
    Scratch file;

    BuildSettings out;
    out.scenes = { "Exported/results.json", "Exported/fight.json", "Exported/menu.json" };
    ASSERT_TRUE(out.Save(file.path()));

    BuildSettings in;
    ASSERT_EQ(in.Load(file.path()).status, BuildSettingsStatus::Ok);

    const std::vector<std::string> expected = { "Exported/results.json",
                                                "Exported/fight.json",
                                                "Exported/menu.json" };
    EXPECT_EQ(in.scenes, expected);
    EXPECT_EQ(in.StartupScene(), "Exported/results.json")
        << "scenes[0] is what ApplyTo writes into the bundle's project.json, so "
           "an order that survives everything except index 0 still ships the "
           "wrong boot scene";
}

TEST(BuildSettingsRoundTrip, TheOnDiskShapeIsWhatTheLoaderPromises) {
    // Reading the file with a second JSON library-user rather than with Load,
    // because a Save/Load pair that agreed with each other about a wrong shape
    // would round-trip perfectly and still be unreadable by the next version.
    Scratch file;

    BuildSettings out;
    out.scenes = { "Exported/menu.json", "Exported/fight.json" };
    out.outputDirectory = "Builds/Game";
    out.profile = BuildProfile::Shipping;
    ASSERT_TRUE(out.Save(file.path()));

    std::ifstream in(file.path());
    ASSERT_TRUE(in.is_open());
    json doc;
    ASSERT_NO_THROW(doc = json::parse(in));

    ASSERT_TRUE(doc.is_object());
    ASSERT_TRUE(doc.contains("version"));
    EXPECT_TRUE(doc["version"].is_number_integer());
    EXPECT_EQ(doc["version"].get<int>(), kVersion);

    ASSERT_TRUE(doc.contains("scenes"));
    ASSERT_TRUE(doc["scenes"].is_array());
    ASSERT_EQ(doc["scenes"].size(), 2u);
    EXPECT_EQ(doc["scenes"][0].get<std::string>(), "Exported/menu.json");
    EXPECT_EQ(doc["scenes"][1].get<std::string>(), "Exported/fight.json");

    EXPECT_EQ(doc["outputDirectory"].get<std::string>(), "Builds/Game");
    // THE TOKEN IS STABLE -- it is what is written into a user's build.json, so
    // renaming one orphans their file. Lowercase, matching the enum order.
    EXPECT_EQ(doc["profile"].get<std::string>(), "shipping");
}

TEST(BuildSettingsRoundTrip, EveryProfileTokenIsStableAndParsesBack) {
    const BuildProfile all[] = { BuildProfile::Debug,
                                 BuildProfile::Development,
                                 BuildProfile::Shipping };
    const char* tokens[] = { "debug", "development", "shipping" };

    for (int i = 0; i < 3; ++i) {
        EXPECT_STREQ(BuildProfileToken(all[i]), tokens[i])
            << "a renamed token orphans every build.json already on disk";

        BuildProfile parsed = BuildProfile::Development;
        EXPECT_TRUE(ParseBuildProfile(tokens[i], parsed));
        EXPECT_EQ(parsed, all[i]);

        Scratch file("profile");
        BuildSettings out;
        out.profile = all[i];
        ASSERT_TRUE(out.Save(file.path()));

        BuildSettings in;
        ASSERT_EQ(in.Load(file.path()).status, BuildSettingsStatus::Ok);
        EXPECT_EQ(in.profile, all[i]) << "profile '" << tokens[i] << "' did not survive";
    }
}

TEST(BuildSettingsRoundTrip, AProfileIsTwoExistingAxesAndNoThird) {
    // The mapping a Build action reads to decide what to compile. Wrong here is
    // a build that silently produces the wrong configuration under the right
    // filename, which is the failure the whole profile idea exists to prevent.
    EXPECT_STREQ(BuildProfileCMakeConfig(BuildProfile::Debug),       "Debug");
    EXPECT_STREQ(BuildProfileCMakeConfig(BuildProfile::Development), "RelWithDebInfo");
    EXPECT_STREQ(BuildProfileCMakeConfig(BuildProfile::Shipping),    "Release");

    EXPECT_STREQ(BuildProfileCMakeTarget(BuildProfile::Debug),       "PlayerDebug");
    EXPECT_STREQ(BuildProfileCMakeTarget(BuildProfile::Development), "PlayerDebug");
    EXPECT_STREQ(BuildProfileCMakeTarget(BuildProfile::Shipping),    "PlayerShipping");

    // Development is the default, and it is the one that is both playable and
    // diagnosable. A default of Shipping would mean the first build somebody
    // presses has no console to explain itself with.
    EXPECT_EQ(BuildSettings().profile, BuildProfile::Development);
}

TEST(BuildSettingsRoundTrip, TheShippingPlayerIsNotNamedAfterItsTarget) {
    // PlayerShipping sets OUTPUT_NAME "Player", so a copy step that assumed
    // target == filename would look for PlayerShipping.exe, which is never
    // written. That is precisely why BuildPlayerExecutableName exists as a
    // separate accessor rather than as target + extension.
#if defined(_WIN32)
    const std::string debugExe    = "PlayerDebug.exe";
    const std::string shippingExe = "Player.exe";
#else
    const std::string debugExe    = "PlayerDebug";
    const std::string shippingExe = "Player";
#endif
    EXPECT_EQ(BuildPlayerExecutableName(BuildProfile::Debug),       debugExe);
    EXPECT_EQ(BuildPlayerExecutableName(BuildProfile::Development), debugExe);
    EXPECT_EQ(BuildPlayerExecutableName(BuildProfile::Shipping),    shippingExe);

    EXPECT_NE(BuildPlayerExecutableName(BuildProfile::Shipping),
              std::string(BuildProfileCMakeTarget(BuildProfile::Shipping)))
        << "if these ever become equal, delete the accessor -- but until then, "
           "assuming they are equal looks for a file the build never writes";
}

TEST(BuildSettingsRoundTrip, APanelSessionSurvives) {
    // The list as a person actually produces it: add four, drop one, drag one
    // up, then declare a startup scene. If any of those verbs disagrees with
    // what Load will accept next launch, the symptom is a list that saves fine
    // and refuses to load -- which is why the panel is meant to use these and
    // not roll its own.
    Scratch file;

    BuildSettings s;
    std::string why;
    ASSERT_TRUE(s.AddScene("Exported/boot.json", &why)) << why;
    ASSERT_TRUE(s.AddScene("Exported/menu.json", &why)) << why;
    ASSERT_TRUE(s.AddScene("Exported/scratch.json", &why)) << why;
    ASSERT_TRUE(s.AddScene("Exported/fight.json", &why)) << why;

    ASSERT_TRUE(s.RemoveSceneAt(2));                 // boot, menu, fight
    ASSERT_TRUE(s.MoveScene(2, 1));                  // boot, fight, menu
    ASSERT_TRUE(s.SetStartupScene("Exported/menu.json", &why)) << why;  // menu, boot, fight

    const std::vector<std::string> expected = { "Exported/menu.json",
                                                "Exported/boot.json",
                                                "Exported/fight.json" };
    ASSERT_EQ(s.scenes, expected);

    ASSERT_TRUE(s.Save(file.path()));
    BuildSettings in;
    ASSERT_EQ(in.Load(file.path()).status, BuildSettingsStatus::Ok);
    EXPECT_EQ(in.scenes, expected);
}

TEST(BuildSettingsRoundTrip, AnEmptyBuildListIsAFileToo) {
    // A project whose author has not chosen any scenes yet. Saving must work
    // (there is nothing wrong with it), loading must come back Ok and empty,
    // and ApplyTo must REFUSE rather than write an empty startupScene -- which
    // would fall through to the struct's default and ship the engine demo.
    Scratch file;

    BuildSettings out;
    out.outputDirectory = "Builds/Empty";
    ASSERT_TRUE(out.scenes.empty());
    ASSERT_TRUE(out.Save(file.path()));

    BuildSettings in;
    in.scenes = { "Exported/stale.json" }; // whatever the object held before
    ASSERT_EQ(in.Load(file.path()).status, BuildSettingsStatus::Ok);
    EXPECT_TRUE(in.scenes.empty());
    EXPECT_EQ(in.StartupScene(), "");
    EXPECT_EQ(in.IndexOf("Exported/stale.json"), -1);

    ProjectSettings ps;
    const std::string sceneBefore = ps.startupScene;
    EXPECT_FALSE(in.ApplyTo(ps))
        << "an empty build list produced a startup scene out of nothing";
    EXPECT_EQ(ps.startupScene, sceneBefore);
    EXPECT_FALSE(ps.startupSceneFromBuild)
        << "a refused ApplyTo still marked the file as a build manifest, which "
           "would make an unrelated startupScene outrank the title's front end";
}

TEST(BuildSettingsRoundTrip, AFullSceneListSurvivesInOrder) {
    // kMaxScenes is a REFUSAL boundary, so both sides of it are worth pinning.
    // This is the accepting side: exactly the maximum, in order, through a real
    // Save/Load. The refusing side is in BuildSettingsHostile.
    Scratch file;

    BuildSettings out;
    for (std::size_t i = 0; i < kMaxScenes; ++i) {
        std::string why;
        ASSERT_TRUE(out.AddScene("Exported/s" + std::to_string(i) + ".json", &why))
            << "scene " << i << ": " << why;
    }
    ASSERT_EQ(out.scenes.size(), kMaxScenes);

    std::string why;
    EXPECT_FALSE(out.AddScene("Exported/one_too_many.json", &why));
    EXPECT_FALSE(why.empty());
    EXPECT_EQ(out.scenes.size(), kMaxScenes) << "the refused add still grew the list";

    ASSERT_TRUE(out.Save(file.path()));
    BuildSettings in;
    ASSERT_EQ(in.Load(file.path()).status, BuildSettingsStatus::Ok);
    ASSERT_EQ(in.scenes.size(), kMaxScenes);
    for (std::size_t i = 0; i < kMaxScenes; ++i) {
        ASSERT_EQ(in.scenes[i], "Exported/s" + std::to_string(i) + ".json")
            << "entry " << i << " moved";
    }
}

// ============================================================================
// 2. THE LIST, AS THE PANEL EDITS IT
// ============================================================================
// Same claim as section 1 -- the order is the meaning -- exercised at the verb
// level, where the off-by-ones live.

TEST(BuildSettingsList, MoveSceneTakesTheIndexAfterRemoval) {
    // `to` is where the entry ENDS UP, which is what a drop target between two
    // rows means. The other reading (an index in the pre-removal list) produces
    // an off-by-one that is invisible when dragging UP and wrong every time
    // when dragging DOWN -- so both directions are tested.
    BuildSettings s;
    s.scenes = { "a", "b", "c", "d" };

    ASSERT_TRUE(s.MoveScene(0, 2));
    EXPECT_EQ(s.scenes, (std::vector<std::string>{ "b", "c", "a", "d" }))
        << "dragging downwards landed in the wrong slot";

    s.scenes = { "a", "b", "c", "d" };
    ASSERT_TRUE(s.MoveScene(3, 0));
    EXPECT_EQ(s.scenes, (std::vector<std::string>{ "d", "a", "b", "c" }));

    // The last position is reachable, and a no-op move is a success.
    s.scenes = { "a", "b", "c", "d" };
    ASSERT_TRUE(s.MoveScene(0, 3));
    EXPECT_EQ(s.scenes, (std::vector<std::string>{ "b", "c", "d", "a" }));
    ASSERT_TRUE(s.MoveScene(2, 2));
    EXPECT_EQ(s.scenes, (std::vector<std::string>{ "b", "c", "d", "a" }));

    // Out of range fails and changes NOTHING -- a drag released off the end of
    // the panel must not eat an entry.
    const std::vector<std::string> before = s.scenes;
    EXPECT_FALSE(s.MoveScene(4, 0));
    EXPECT_FALSE(s.MoveScene(0, 4));
    EXPECT_FALSE(s.MoveScene(99, 99));
    EXPECT_EQ(s.scenes, before);

    EXPECT_FALSE(s.RemoveSceneAt(4));
    EXPECT_EQ(s.scenes, before);
}

TEST(BuildSettingsList, SetStartupSceneMovesOrAdds) {
    BuildSettings s;
    s.scenes = { "Exported/a.json", "Exported/b.json", "Exported/c.json" };

    // Already present: moved to 0, everything else keeps its relative order.
    ASSERT_TRUE(s.SetStartupScene("Exported/c.json"));
    EXPECT_EQ(s.scenes, (std::vector<std::string>{ "Exported/c.json",
                                                   "Exported/a.json",
                                                   "Exported/b.json" }));
    EXPECT_EQ(s.StartupScene(), "Exported/c.json");

    // Absent: added, then moved to 0. This is what "File > Set Current Scene as
    // Player Startup" becomes, and it must work on a scene not yet in the list.
    ASSERT_TRUE(s.SetStartupScene("Exported/d.json"));
    EXPECT_EQ(s.scenes, (std::vector<std::string>{ "Exported/d.json",
                                                   "Exported/c.json",
                                                   "Exported/a.json",
                                                   "Exported/b.json" }));

    // Refused: nothing added, nothing moved. An escaping path must not leave
    // the list reordered on its way to failing.
    const std::vector<std::string> before = s.scenes;
    std::string why;
    EXPECT_FALSE(s.SetStartupScene("../outside.json", &why));
    EXPECT_FALSE(why.empty());
    EXPECT_EQ(s.scenes, before);
}

TEST(BuildSettingsList, OneFileIsOneEntryWhateverItsSpelling) {
    BuildSettings s;
    std::string why;
    ASSERT_TRUE(s.AddScene("Exported/menu.json", &why)) << why;

    // Normalization is applied to every path that ENTERS the list and to every
    // LOOKUP, so the panel can pass whatever spelling it happens to hold.
    EXPECT_EQ(s.IndexOf("./Exported/menu.json"), 0);
    EXPECT_EQ(s.IndexOf("Exported//menu.json"), 0);
    EXPECT_EQ(s.IndexOf("Exported/./menu.json"), 0);
    EXPECT_EQ(s.IndexOf("Exported/nested/../menu.json"), 0);
    EXPECT_TRUE(s.Contains("./Exported/menu.json"));
    EXPECT_EQ(s.IndexOf("Exported/other.json"), -1);
    EXPECT_FALSE(s.Contains("Exported/other.json"));

    // ...and therefore a second spelling is a DUPLICATE, not a second entry.
    EXPECT_FALSE(s.AddScene("./Exported/menu.json", &why));
    EXPECT_FALSE(why.empty()) << "refused without telling the panel why";
    EXPECT_EQ(s.scenes.size(), 1u);

#if defined(_WIN32)
    // Backslashes are a separator on Windows only: on Linux this is a single
    // filename containing a backslash, which is a different file and correctly
    // a different entry.
    EXPECT_EQ(s.IndexOf("Exported\\menu.json"), 0);
    EXPECT_EQ(BuildSettings::NormalizeScenePath("Exported\\menu.json"),
              "Exported/menu.json");
#endif

    // The spelling rule itself, in the forms that mean the same file on both
    // platforms. Stored normalized, so the path that was approved is the path
    // that will later be opened.
    EXPECT_EQ(BuildSettings::NormalizeScenePath("./Exported/menu.json"), "Exported/menu.json");
    EXPECT_EQ(BuildSettings::NormalizeScenePath("Exported//menu.json"),  "Exported/menu.json");
    EXPECT_EQ(BuildSettings::NormalizeScenePath("Exported/menu.json/"),  "Exported/menu.json");
    EXPECT_EQ(BuildSettings::NormalizeScenePath("Exported/a/./b/../menu.json"),
              "Exported/a/menu.json");
    EXPECT_EQ(BuildSettings::NormalizeScenePath(""), "");
}

TEST(BuildSettingsList, AddSceneRefusesWithoutMutating) {
    BuildSettings s;
    s.scenes = { "Exported/menu.json" };
    const std::vector<std::string> before = s.scenes;

    std::string why;
    EXPECT_FALSE(s.AddScene("", &why));
    EXPECT_FALSE(why.empty());
    EXPECT_EQ(s.scenes, before);

    why.clear();
    EXPECT_FALSE(s.AddScene(std::string(kMaxPathLength + 1, 'a'), &why));
    EXPECT_FALSE(why.empty());
    EXPECT_EQ(s.scenes, before);

    why.clear();
    EXPECT_FALSE(s.AddScene("../outside.json", &why));
    EXPECT_FALSE(why.empty());
    EXPECT_EQ(s.scenes, before);

    why.clear();
    EXPECT_FALSE(s.AddScene("/etc/passwd", &why));
    EXPECT_FALSE(why.empty());
    EXPECT_EQ(s.scenes, before);

    // A whyNot of nullptr is the panel calling this from a context with nothing
    // to show; it must not be a crash.
    EXPECT_FALSE(s.AddScene("../outside.json", nullptr));
    EXPECT_EQ(s.scenes, before);

    // Exactly at the bound is allowed -- the refusal is "longer than", and an
    // off-by-one here would reject a legal path on somebody's deep asset tree.
    //
    // BUILT FROM MANY SHORT COMPONENTS RATHER THAN ONE LONG ONE, and that is not
    // cosmetic. This was `std::string(kMaxPathLength - 6, 'a') + ".json"`: a
    // single 1023-character FILENAME, which is inside kMaxPathLength and outside
    // what a filesystem will accept. Linux caps one path COMPONENT at 255 bytes
    // (NAME_MAX), so weakly_canonical inside PathIsContained failed with
    // ENAMETOOLONG and the sandbox refused a path this test called legal.
    //
    // It passed on Windows and failed the Linux CI leg, which is the shape worth
    // remembering: the bound under test is on the WHOLE PATH, so the fixture has
    // to be a legal deep path rather than an illegal shallow one. A test that can
    // only be satisfied on one platform is testing the platform.
    std::string deep;
    while (deep.size() + 64 < kMaxPathLength - 5) deep += std::string(63, 'a') + "/";
    deep += std::string(kMaxPathLength - 5 - deep.size(), 'a') + ".json";
    ASSERT_EQ(deep.size(), kMaxPathLength);
    EXPECT_TRUE(s.AddScene(deep, &why)) << why;
    EXPECT_EQ(s.scenes.size(), 2u);
}

TEST(BuildSettingsList, StartupSceneOfAnEmptyListIsEmptyAndStable) {
    BuildSettings s;
    EXPECT_TRUE(s.scenes.empty());
    // Returns a reference, so an empty list must have something real to refer
    // to rather than a dangling temporary.
    const std::string& a = s.StartupScene();
    const std::string& b = s.StartupScene();
    EXPECT_EQ(a, "");
    EXPECT_EQ(&a, &b);

    s.scenes = { "Exported/menu.json" };
    EXPECT_EQ(s.StartupScene(), "Exported/menu.json");
    s.Clear();
    EXPECT_TRUE(s.scenes.empty());
    EXPECT_EQ(s.StartupScene(), "");
}

// ============================================================================
// 3. A HOSTILE OR STALE FILE IS REFUSED, NOT CRASHED
// ============================================================================

TEST(BuildSettingsHostile, TheThreeValuedResultIsThreeValued) {
    // Missing and Malformed are not the same answer, and collapsing them into a
    // bool is the version that EATS THE FILE: the panel's correct response to
    // Missing is to seed and save, and its correct response to Malformed is to
    // refuse to save until a human looks.
    BuildSettings s = Primed();
    const BuildSettings before = s;

    const BuildSettingsLoadResult missing =
        s.Load((fs::temp_directory_path() / "cse_no_such_build_9e3f.json").string());
    EXPECT_EQ(missing.status, BuildSettingsStatus::Missing);
    EXPECT_FALSE(missing.ok());
    EXPECT_TRUE(missing.safeToSave())
        << "a first-run project reported unsafe to save, so the panel can never "
           "write its first build.json";
    EXPECT_TRUE(Same(s, before)) << "a missing file changed the settings";

    Scratch file("good");
    ASSERT_TRUE(file.Write(GoodDoc()));
    const BuildSettingsLoadResult ok = s.Load(file.path());
    EXPECT_EQ(ok.status, BuildSettingsStatus::Ok);
    EXPECT_TRUE(ok.ok());
    EXPECT_TRUE(ok.safeToSave());
}

TEST(BuildSettingsHostile, BrokenJsonIsRefused) {
    ExpectRefusedAndUntouched("", "an empty file");
    ExpectRefusedAndUntouched("   \n\t  ", "whitespace only");
    ExpectRefusedAndUntouched(R"({"version":1,"scenes":["Exported/a.json")",
                              "a file truncated mid-array (a full disk)");
    ExpectRefusedAndUntouched(R"({"version":1,"scenes":["a.json"],})",
                              "a trailing comma");
    ExpectRefusedAndUntouched("[]", "a top-level array");
    ExpectRefusedAndUntouched("7", "a top-level number");
    ExpectRefusedAndUntouched("null", "a top-level null");
    ExpectRefusedAndUntouched("\"Exported/menu.json\"", "a top-level string");
    ExpectRefusedAndUntouched("<!DOCTYPE html>", "an HTML error page saved over it");
    ExpectRefusedAndUntouched(R"({"version":1} and then some)",
                              "text after the end of the document");
}

TEST(BuildSettingsHostile, AFileFromANewerEditorIsRefusedNotGuessedAt) {
    // Refuse forward, do not guess: a newer editor may have MOVED a meaning
    // rather than only added a key, and reading it with these rules would
    // produce a plausible build list that is not the one somebody authored.
    Scratch file("future");
    ASSERT_TRUE(file.Write(GoodDocWith("version", kVersion + 1)));
    BuildSettings s = Primed();
    const BuildSettingsLoadResult r = s.Load(file.path());

    EXPECT_EQ(r.status, BuildSettingsStatus::Malformed);
    EXPECT_NE(r.message.find("newer editor"), std::string::npos)
        << "the refusal must say WHY, so the reader upgrades instead of "
           "hand-editing the version down: " << r.message;

    ExpectRefusedAndUntouched(GoodDocWith("version", kVersion + 1).dump(2),
                              "version from a newer editor");
    ExpectRefusedAndUntouched(GoodDocWith("version", 99999).dump(2),
                              "a wildly future version");
    ExpectRefusedAndUntouched(GoodDocWith("version", "1").dump(2),
                              "version as a string");
    ExpectRefusedAndUntouched(GoodDocWith("version", 1.5).dump(2),
                              "version as a float");
    ExpectRefusedAndUntouched(GoodDocWith("version", nullptr).dump(2),
                              "version as null");
}

TEST(BuildSettingsHostile, AWrongShapedSceneListIsRefused) {
    ExpectRefusedAndUntouched(GoodDocWith("scenes", "Exported/menu.json").dump(2),
                              "scenes as a bare string");
    ExpectRefusedAndUntouched(GoodDocWith("scenes", json::object()).dump(2),
                              "scenes as an object");
    ExpectRefusedAndUntouched(GoodDocWith("scenes", nullptr).dump(2),
                              "scenes as null");
    ExpectRefusedAndUntouched(
        GoodDocWith("scenes", json::array({ "a.json", "b.json", "c.json",
                                            json::array({ "d.json" }) })).dump(2),
        "a nested array where a path should be");
    ExpectRefusedAndUntouched(
        GoodDocWith("scenes", json::array({ "a.json", "b.json", "c.json", nullptr })).dump(2),
        "a null path");
    ExpectRefusedAndUntouched(
        GoodDocWith("scenes", json::array({ "a.json", "" })).dump(2),
        "an empty path");
    ExpectRefusedAndUntouched(
        GoodDocWith("scenes", json::array({ std::string(kMaxPathLength + 1, 'a') })).dump(2),
        "a path past kMaxPathLength");
}

TEST(BuildSettingsHostile, TheRefusalNamesTheOffendingIndex) {
    // "could not read build.json" sends the reader hunting through the whole
    // file. The header promises "scenes[3] is not a string", so that is what is
    // pinned -- a panel showing this message is showing something actionable.
    Scratch file("index");
    ASSERT_TRUE(file.Write(GoodDocWith(
        "scenes", json::array({ "a.json", "b.json", "c.json", 7 }))));

    BuildSettings s = Primed();
    const BuildSettingsLoadResult r = s.Load(file.path());
    ASSERT_EQ(r.status, BuildSettingsStatus::Malformed);
    EXPECT_NE(r.message.find("scenes[3]"), std::string::npos)
        << "the message does not say which entry is wrong: " << r.message;
}

TEST(BuildSettingsHostile, DuplicatesAreRefusedNotDeduped) {
    // With the same scene at 0 and at 4 there is no answer to "what is this
    // scene's build index", which is the question the panel and any future
    // load-by-index both ask. Silently deduping would also change the ORDER of
    // everything after it.
    ExpectRefusedAndUntouched(
        GoodDocWith("scenes", json::array({ "Exported/menu.json",
                                            "Exported/menu.json" })).dump(2),
        "the same path twice");

    Scratch file("dup");
    ASSERT_TRUE(file.Write(GoodDocWith(
        "scenes", json::array({ "Exported/menu.json", "./Exported/menu.json" }))));
    BuildSettings s;
    const BuildSettingsLoadResult r = s.Load(file.path());
    EXPECT_EQ(r.status, BuildSettingsStatus::Malformed)
        << "two spellings of one file were accepted as two build entries";
    EXPECT_NE(r.message.find("index 0"), std::string::npos) << r.message;
}

TEST(BuildSettingsHostile, AnEscapingScenePathIsRefused) {
    // The scene paths are OPENED by a build. Assimp's importers have shipped
    // heap-overflow bugs on malformed input, so containment here is a
    // memory-safety boundary and not merely a tidiness rule.
    ExpectRefusedAndUntouched(
        GoodDocWith("scenes", json::array({ "../outside.json" })).dump(2),
        "a scene path escaping with ..");
    ExpectRefusedAndUntouched(
        GoodDocWith("scenes", json::array({ "Exported/../../outside.json" })).dump(2),
        "a scene path escaping after a cancelling ..");
    ExpectRefusedAndUntouched(
        GoodDocWith("scenes", json::array({ "/etc/passwd" })).dump(2),
        "an absolute scene path");
    ExpectRefusedAndUntouched(
        GoodDocWith("scenes", json::array({ "//host/share/x.json" })).dump(2),
        "a UNC scene path");
    ExpectRefusedAndUntouched(
        GoodDocWith("scenes", json::array({ "Exported/menu.json",
                                            "Exported/fight.json",
                                            "../outside.json" })).dump(2),
        "one escaping path at the END of an otherwise good list");

    // ...and the ".." that CANCELS is a spelling, not an escape. Refusing it
    // would reject a legal path; accepting the original spelling while opening
    // the collapsed one would be the escape. It is normalized, then checked,
    // then STORED IN THE NORMALIZED FORM, so the path that was approved is the
    // path that is later opened.
    Scratch file("cancel");
    ASSERT_TRUE(file.Write(GoodDocWith(
        "scenes", json::array({ "Exported/deep/../menu.json" }))));
    BuildSettings s;
    ASSERT_EQ(s.Load(file.path()).status, BuildSettingsStatus::Ok);
    EXPECT_EQ(s.scenes, (std::vector<std::string>{ "Exported/menu.json" }));
}

TEST(BuildSettingsHostile, AnUnknownProfileIsRefusedNotDefaulted) {
    // A typo'd profile that silently became Development would ship a console
    // window and a debug-named exe to somebody's players.
    ExpectRefusedAndUntouched(GoodDocWith("profile", "release").dump(2),
                              "a plausible-but-wrong profile token");
    ExpectRefusedAndUntouched(GoodDocWith("profile", "Development").dump(2),
                              "the right token in the wrong case");
    ExpectRefusedAndUntouched(GoodDocWith("profile", "").dump(2),
                              "an empty profile");
    ExpectRefusedAndUntouched(GoodDocWith("profile", 3).dump(2),
                              "a numeric profile");
    ExpectRefusedAndUntouched(GoodDocWith("profile", nullptr).dump(2),
                              "a null profile");

    BuildProfile parsed = BuildProfile::Debug;
    EXPECT_FALSE(ParseBuildProfile("", parsed));
    EXPECT_FALSE(ParseBuildProfile("Shipping", parsed));
    EXPECT_FALSE(ParseBuildProfile("shipping ", parsed));
    EXPECT_FALSE(ParseBuildProfile("dev", parsed));
    EXPECT_EQ(parsed, BuildProfile::Debug) << "a failed parse wrote to `out`";
}

TEST(BuildSettingsHostile, AnEnormousSceneCountIsRefusedNotTruncated) {
    // REFUSED rather than clamped, because a silently truncated build list is a
    // game that ships without its last levels and says nothing about it.
    json tooMany = json::array();
    for (std::size_t i = 0; i <= kMaxScenes; ++i) {
        tooMany.push_back("Exported/s" + std::to_string(i) + ".json");
    }
    ASSERT_EQ(tooMany.size(), kMaxScenes + 1);
    ExpectRefusedAndUntouched(GoodDocWith("scenes", tooMany).dump(2),
                              "one scene past kMaxScenes");

    // And an absurd one: 5000 entries is well past the bound and still well
    // under kMaxFileBytes, so this reaches the scene bound rather than the byte
    // bound -- the check must not be an allocation of 5000 strings first.
    json absurd = json::array();
    for (int i = 0; i < 5000; ++i) absurd.push_back("Exported/s" + std::to_string(i) + ".json");
    Scratch file("absurd");
    ASSERT_TRUE(file.Write(GoodDocWith("scenes", absurd)));
    BuildSettings s = Primed();
    const BuildSettings before = s;
    const BuildSettingsLoadResult r = s.Load(file.path());
    EXPECT_EQ(r.status, BuildSettingsStatus::Malformed);
    EXPECT_NE(r.message.find("5000"), std::string::npos)
        << "the refusal should name the count it saw: " << r.message;
    EXPECT_TRUE(Same(s, before));
}

TEST(BuildSettingsHostile, AFileOverTheByteBoundIsRefusedBeforeTheParse) {
    // nlohmann materializes the WHOLE document before a single other bound is
    // consulted, so without a byte check a 2 GB build.json is an out-of-memory
    // rather than a refusal. The padding lives in an UNKNOWN key, which means
    // this document would load perfectly well if the byte bound were removed --
    // so a failure here is unambiguous about which check went missing.
    {
        std::string text = R"({"version":1,"scenes":["Exported/menu.json"],"padding":")";
        text.append(static_cast<std::size_t>(kMaxFileBytes) + 4096, 'x');
        text += "\"}";
        ASSERT_GT(text.size(), kMaxFileBytes);

        Scratch file("huge");
        ASSERT_TRUE(file.WriteRaw(text));
        BuildSettings s = Primed();
        const BuildSettings before = s;
        const BuildSettingsLoadResult r = s.Load(file.path());
        EXPECT_EQ(r.status, BuildSettingsStatus::Malformed);
        EXPECT_NE(r.message.find("bytes"), std::string::npos) << r.message;
        EXPECT_TRUE(Same(s, before));
    }

    // The other side of the bound: a large-but-legal file still loads, and the
    // unknown key is ignored rather than treated as a corruption. A version
    // that refused every file with a key it did not recognise could never add
    // one.
    {
        std::string text = R"({"version":1,"scenes":["Exported/menu.json"],"padding":")";
        text.append(static_cast<std::size_t>(kMaxFileBytes) / 2, 'x');
        text += "\"}";
        ASSERT_LT(text.size(), kMaxFileBytes);

        Scratch file("large");
        ASSERT_TRUE(file.WriteRaw(text));
        BuildSettings s;
        EXPECT_EQ(s.Load(file.path()).status, BuildSettingsStatus::Ok);
        EXPECT_EQ(s.scenes, (std::vector<std::string>{ "Exported/menu.json" }));
    }
}

TEST(BuildSettingsHostile, AnAbsentSceneListIsAnEmptyBuild) {
    // WORTH KNOWING BEFORE WRITING THE PANEL, because the three keys are not
    // symmetric: an absent `scenes` yields an EMPTY list, while an absent
    // `outputDirectory` or `profile` leaves whatever the object already held.
    // For the panel's real flow -- Load into a freshly constructed object --
    // the difference is invisible; for a panel that reloads into the object it
    // is already showing, it is the difference between "the file says nothing
    // ships" and "keep what you had". The file is the authority on what ships,
    // so empty is the right reading; it is pinned here so nobody has to
    // rediscover it from a bug report.
    Scratch file;
    json doc;
    doc["version"] = kVersion;
    ASSERT_TRUE(file.Write(doc));

    BuildSettings s = Primed();
    ASSERT_EQ(s.Load(file.path()).status, BuildSettingsStatus::Ok);
    EXPECT_TRUE(s.scenes.empty());
    EXPECT_EQ(s.outputDirectory, "Builds/Tournament");
    EXPECT_EQ(s.profile, BuildProfile::Shipping);
}

// ---------------------------------------------------------------------------
// The sweep
// ---------------------------------------------------------------------------
// A seeded generator, written out in this file, damaging a valid build.json
// every way an integer can: single bytes, runs of bytes, truncations, and --
// the shape that actually reaches the semantic checks rather than dying in the
// parser -- whole documents assembled from a menu of typed, plausible and
// hostile values.
//
// THE INVARIANT IS A DISJUNCTION, which is what makes it checkable without
// re-implementing Load:
//
//   Malformed -> the object is EXACTLY as it was, and there is a reason to show.
//   Ok        -> every path in the result is normalized, contained, non-empty,
//                unique and within bounds; the list matches the file's order;
//                and the result SURVIVES ITS OWN WRITER -- Save then Load
//                reproduces it. That last one is the property a normalization
//                bug breaks: a file this editor accepted but cannot re-read is
//                a build list that vanishes on the next launch.
//   Missing   -> impossible. The file is right there.
namespace {

json FuzzVersion(Rng& rng, std::string& note) {
    // `zero` is a named constant rather than a literal 0 because a literal 0 is
    // also a null pointer constant, and handing one to a JSON library with a
    // nullptr_t constructor is a needless overload-resolution question.
    const int zero = 0;
    switch (rng.Below(8)) {
    case 0: note = "version=1";      return kVersion;
    case 1: note = "version=0";      return zero;
    case 2: note = "version=next";   return kVersion + 1;
    case 3: note = "version=99999";  return 99999;
    case 4: note = "version=-1";     return -1;
    case 5: note = "version=\"1\"";  return "1";
    case 6: note = "version=1.5";    return 1.5;
    default: note = "version=null";  return nullptr;
    }
}

json FuzzScenes(Rng& rng, std::string& note) {
    switch (rng.Below(12)) {
    case 0:
        note = "scenes=trio";
        return json::array({ "Exported/menu.json", "Exported/fight.json",
                             "Exported/results.json" });
    case 1: note = "scenes=one";   return json::array({ "Exported/menu.json" });
    case 2: note = "scenes=[]";    return json::array();
    case 3:
        note = "scenes=dup-by-spelling";
        return json::array({ "Exported/menu.json", "./Exported/menu.json" });
    case 4: note = "scenes=escaping"; return json::array({ "../outside.json" });
    case 5: note = "scenes=absolute"; return json::array({ "/etc/passwd" });
    case 6: note = "scenes=unc";      return json::array({ "//host/share/x.json" });
    case 7:
        note = "scenes=non-string";
        return json::array({ "Exported/menu.json", 7 });
    case 8: note = "scenes=empty-entry"; return json::array({ "" });
    case 9:
        note = "scenes=over-long-path";
        return json::array({ std::string(kMaxPathLength + 8, 'a') });
    case 10: {
        note = "scenes=300";
        json a = json::array();
        for (int i = 0; i < 300; ++i) a.push_back("Exported/s" + std::to_string(i) + ".json");
        return a;
    }
    default: note = "scenes=not-an-array"; return "Exported/menu.json";
    }
}

json FuzzOutput(Rng& rng, std::string& note) {
    switch (rng.Below(10)) {
    case 0: note = "out=Builds";        return "Builds";
    case 1: note = "out=Builds/Game";   return "Builds/Game";
    case 2: note = "out=cancelling..";  return "Builds/../Game";
    case 3: note = "out=.";             return ".";
    case 4: note = "out=escaping";      return "../Elsewhere";
    case 5: note = "out=absolute";      return "/tmp/elsewhere";
    case 6: note = "out=empty";         return "";
    case 7: note = "out=over-long";     return std::string(kMaxPathLength + 8, 'b');
    case 8: note = "out=number";        return 5;
    default: note = "out=null";         return nullptr;
    }
}

json FuzzProfile(Rng& rng, std::string& note) {
    switch (rng.Below(8)) {
    case 0: note = "profile=development"; return "development";
    case 1: note = "profile=shipping";    return "shipping";
    case 2: note = "profile=debug";       return "debug";
    case 3: note = "profile=Development"; return "Development";
    case 4: note = "profile=release";     return "release";
    case 5: note = "profile=empty";       return "";
    case 6: note = "profile=number";      return 3;
    default: note = "profile=null";       return nullptr;
    }
}

// Everything an accepted result must satisfy, expressed without reference to
// how Load decided it.
void CheckAcceptedInvariants(const BuildSettings& s, const std::string& text,
                             const std::string& what) {
    EXPECT_LE(s.scenes.size(), kMaxScenes) << what;

    for (std::size_t i = 0; i < s.scenes.size(); ++i) {
        const std::string& p = s.scenes[i];
        EXPECT_FALSE(p.empty()) << what << ": scenes[" << i << "] is empty";
        EXPECT_LE(p.size(), kMaxPathLength) << what << ": scenes[" << i << "] is over the bound";
        EXPECT_EQ(BuildSettings::NormalizeScenePath(p), p)
            << what << ": scenes[" << i << "] was stored in a spelling Load would "
               "not produce, so the approved path is not the path that gets opened";
        EXPECT_TRUE(OracleAccepts(p))
            << what << ": scenes[" << i << "] ('" << p << "') escapes the project";
        for (std::size_t j = 0; j < i; ++j) {
            EXPECT_NE(s.scenes[j], p)
                << what << ": scenes[" << i << "] duplicates scenes[" << j << "]";
        }
    }

    EXPECT_FALSE(s.outputDirectory.empty()) << what << ": an empty output directory";
    EXPECT_LE(s.outputDirectory.size(), kMaxPathLength) << what;
    EXPECT_EQ(BuildSettings::NormalizeScenePath(s.outputDirectory), s.outputDirectory) << what;
    EXPECT_TRUE(OracleAccepts(s.outputDirectory))
        << what << ": the build would write to '" << s.outputDirectory
        << "', which is outside the project";

    BuildProfile back = BuildProfile::Development;
    EXPECT_TRUE(ParseBuildProfile(BuildProfileToken(s.profile), back)) << what;
    EXPECT_EQ(back, s.profile) << what;

    // ORDER, checked against the bytes that were accepted rather than against
    // the generator's intent -- which is what lets the byte-mutation shapes be
    // held to the same standard as the structured ones.
    json doc;
    try {
        doc = json::parse(text);
    } catch (const std::exception& e) {
        ADD_FAILURE() << what << ": Load accepted bytes that do not parse: " << e.what();
        return;
    }
    if (!doc.is_object()) {
        ADD_FAILURE() << what << ": Load accepted a document that is not an object";
        return;
    }
    std::vector<std::string> expected;
    if (doc.contains("scenes")) {
        if (!doc["scenes"].is_array()) {
            ADD_FAILURE() << what << ": Load accepted a non-array `scenes`";
            return;
        }
        for (const auto& e : doc["scenes"]) {
            if (!e.is_string()) {
                ADD_FAILURE() << what << ": Load accepted a non-string scene entry";
                return;
            }
            expected.push_back(BuildSettings::NormalizeScenePath(e.get<std::string>()));
        }
    }
    EXPECT_EQ(s.scenes, expected)
        << what << ": the accepted list is not the file's list in the file's order";
}

} // namespace

TEST(BuildSettingsFuzz, EveryMutationIsARefusalOrASettingsFileThatSurvivesItsOwnWriter) {
    const std::string good = GoodDoc().dump(2);
    ASSERT_FALSE(good.empty());

    Scratch subject("subject");
    Scratch resave("resave");
    SilencedCerr quiet;

    // Four independent starting points rather than one long run: a xorshift32
    // walk explores its own neighbourhood, and four cheap seeds cover more
    // shapes than one expensive one. The product is bounded by wall clock --
    // every iteration is two or three real file writes and a handful of
    // filesystem canonicalizations, which is what a containment check costs.
    constexpr std::uint32_t kSeeds      = 4;
    constexpr std::uint32_t kIterations = 150;

    int refused = 0, accepted = 0;

    for (std::uint32_t seedIndex = 0; seedIndex < kSeeds; ++seedIndex) {
        const std::uint32_t seed = 0xB01DFACEu + seedIndex * 0x9E3779B9u;
        Rng rng(seed);

        for (std::uint32_t iteration = 0; iteration < kIterations; ++iteration) {
            std::string text;
            std::string note;

            // Four shapes, because they fail differently: a single substitution
            // finds a missing type check, a burst finds an off-by-one in a
            // walk, a truncation finds a read past the end, and a document
            // assembled from typed pieces is the only one that reaches the
            // semantic rules at all -- a byte-level fuzz of JSON mostly tests
            // nlohmann's parser, which is not this file's job.
            const std::uint32_t shape = rng.Below(6);
            if (shape == 0) {
                text = good;
                const std::uint32_t at = rng.Below(static_cast<std::uint32_t>(text.size()));
                // Printable ASCII only. A random byte is overwhelmingly likely
                // to be invalid UTF-8, which nlohmann's parser rejects on sight
                // -- so the fuzz would spend its whole budget proving the
                // parser works.
                text[at] = static_cast<char>(32 + rng.Below(95));
                note = "substitute at " + std::to_string(at);
            } else if (shape == 1) {
                text = good;
                const std::uint32_t at = rng.Below(static_cast<std::uint32_t>(text.size()));
                const std::uint32_t run = 1u + rng.Below(8);
                for (std::uint32_t k = 0; k < run && at + k < text.size(); ++k) {
                    text[at + k] = static_cast<char>(32 + rng.Below(95));
                }
                note = "burst of " + std::to_string(run) + " at " + std::to_string(at);
            } else if (shape == 2) {
                const std::uint32_t at = rng.Below(static_cast<std::uint32_t>(good.size()));
                text = good.substr(0, at);
                note = "truncated to " + std::to_string(at);
            } else {
                json doc = json::object();
                std::string part;
                // Each key is present three times in four, so that "absent" is
                // exercised as much as "hostile".
                if (rng.Below(4) != 0) { doc["version"] = FuzzVersion(rng, part); note += part + " "; }
                if (rng.Below(4) != 0) { doc["scenes"] = FuzzScenes(rng, part); note += part + " "; }
                if (rng.Below(4) != 0) { doc["outputDirectory"] = FuzzOutput(rng, part); note += part + " "; }
                if (rng.Below(4) != 0) { doc["profile"] = FuzzProfile(rng, part); note += part + " "; }
                // An unknown key must be IGNORED, not treated as corruption --
                // otherwise version 2 of this format can never add one.
                if (rng.Below(4) == 0) doc["comment"] = "hand-edited";
                text = doc.dump(2);
            }

            std::string what = "seed " + std::to_string(seed) +
                               " iteration " + std::to_string(iteration) +
                               " shape " + std::to_string(shape) + " [" + note + "]";

            ASSERT_TRUE(subject.WriteRaw(text)) << what << ": could not write the scratch file";

            const BuildSettings before = Primed();
            BuildSettings s = before;
            const BuildSettingsLoadResult r = s.Load(subject.path());

            ASSERT_NE(r.status, BuildSettingsStatus::Missing)
                << what << ": a file that exists reported Missing, which would "
                   "invite the panel to write a fresh one over it";

            if (r.status == BuildSettingsStatus::Malformed) {
                ++refused;
                EXPECT_FALSE(r.safeToSave()) << what;
                EXPECT_FALSE(r.message.empty()) << what << ": refused with no reason";
                ASSERT_TRUE(Same(s, before))
                    << what << ": the refusal was half-applied.\n"
                    << "  before: " << Describe(before) << "\n"
                    << "  after:  " << Describe(s) << "\n"
                    << "  reason: " << r.message << "\n"
                    << "  file:   " << text.substr(0, 400);
                continue;
            }

            ++accepted;
            CheckAcceptedInvariants(s, text, what);

            // SURVIVES ITS OWN WRITER. A file this editor accepted but cannot
            // re-read is a build list that disappears at the next launch, and
            // the panel -- correctly refusing to save over a Malformed file --
            // would then be unable to repair it.
            ASSERT_TRUE(s.Save(resave.path())) << what << ": Save failed";
            BuildSettings again;
            const BuildSettingsLoadResult rr = again.Load(resave.path());
            EXPECT_EQ(rr.status, BuildSettingsStatus::Ok)
                << what << ": what Save wrote, Load refuses: " << rr.message
                << "\n  settings: " << Describe(s);
            EXPECT_TRUE(Same(again, s))
                << what << ": the settings changed by being written and read.\n"
                << "  wrote: " << Describe(s) << "\n"
                << "  read:  " << Describe(again);
        }
    }

    std::cout << "[ build.json ] " << (refused + accepted) << " mutations: "
              << refused << " refused, " << accepted << " accepted and stable"
              << std::endl;

    // A fuzz in which everything is refused proves only that the loader can say
    // no, and one in which everything is accepted proves nothing at all.
    EXPECT_GE(refused, 100);
    EXPECT_GE(accepted, 10);
}

// ============================================================================
// 4. THE MIGRATION
// ============================================================================

TEST(BuildSettingsMigration, AProjectWithOnlyAStartupSceneOpensWithThatSceneAtIndexZero) {
    // The file every existing project has: written before build.json existed,
    // so it carries startupScene and nothing else -- no masterVolume, no
    // startupSceneFromBuild.
    Scratch legacy("project");
    ASSERT_TRUE(legacy.WriteRaw(R"({"startupScene":"Exported/UntitledFighter/menu.json"})"));

    ProjectSettings ps;
    ASSERT_TRUE(ps.Load(legacy.path()));
    ASSERT_EQ(ps.startupScene, "Exported/UntitledFighter/menu.json");
    EXPECT_FALSE(ps.startupSceneFromBuild)
        << "an old preference file must not be mistaken for a build manifest";

    // The panel's actual sequence: Load says Missing, so seed.
    Scratch build("build");
    BuildSettings bs;
    const BuildSettingsLoadResult r = bs.Load(build.path());
    ASSERT_EQ(r.status, BuildSettingsStatus::Missing);
    ASSERT_TRUE(r.safeToSave());

    ASSERT_TRUE(bs.SeedFromProjectSettings(ps));
    ASSERT_EQ(bs.scenes.size(), 1u)
        << "the build panel would have opened empty, and its Build button would "
           "have produced a game with no scenes in it";
    EXPECT_EQ(bs.scenes[0], "Exported/UntitledFighter/menu.json");
    EXPECT_EQ(bs.StartupScene(), "Exported/UntitledFighter/menu.json");
    EXPECT_EQ(bs.IndexOf("Exported/UntitledFighter/menu.json"), 0);

    // ...and it survives being written down, which is what makes the migration
    // a one-time event rather than something that happens on every launch.
    ASSERT_TRUE(bs.Save(build.path()));
    BuildSettings reopened;
    ASSERT_EQ(reopened.Load(build.path()).status, BuildSettingsStatus::Ok);
    EXPECT_EQ(reopened.scenes, bs.scenes);
    EXPECT_EQ(reopened.outputDirectory, "Builds") << "the default output directory";
}

TEST(BuildSettingsMigration, SeedingOnlyEverAddsAndOnlyToAnEmptyList) {
    ProjectSettings ps;
    ps.startupScene = "Exported/legacy.json";

    // Runs once.
    BuildSettings bs;
    ASSERT_TRUE(bs.SeedFromProjectSettings(ps));
    ASSERT_EQ(bs.scenes, (std::vector<std::string>{ "Exported/legacy.json" }));

    // ...and never again. A migration that ran twice would put the legacy scene
    // back at the front of a list the author had since reordered.
    EXPECT_FALSE(bs.SeedFromProjectSettings(ps));
    EXPECT_EQ(bs.scenes, (std::vector<std::string>{ "Exported/legacy.json" }));

    BuildSettings authored;
    authored.scenes = { "Exported/fight.json", "Exported/menu.json" };
    const std::vector<std::string> before = authored.scenes;
    EXPECT_FALSE(authored.SeedFromProjectSettings(ps))
        << "seeding ran over a list somebody had already authored";
    EXPECT_EQ(authored.scenes, before);

    // Nothing to migrate is not a migration.
    ProjectSettings blank;
    blank.startupScene.clear();
    BuildSettings fresh;
    EXPECT_FALSE(fresh.SeedFromProjectSettings(blank));
    EXPECT_TRUE(fresh.scenes.empty());
}

TEST(BuildSettingsMigration, AHostileLegacyValueIsRefusedAtTheMigrationNotNextLaunch) {
    // project.json predates the containment rule and is hand-editable, so its
    // startupScene may be something that would not survive a build.json reload.
    // Refusing it HERE means the panel can say so; accepting it means a
    // Malformed file next launch with no history of where it came from.
    const char* hostile[] = { "../outside.json", "/etc/passwd", "//host/share/x.json" };
    for (const char* bad : hostile) {
        ProjectSettings ps;
        ps.startupScene = bad;
        BuildSettings bs;
        EXPECT_FALSE(bs.SeedFromProjectSettings(ps)) << "accepted legacy scene '" << bad << "'";
        EXPECT_TRUE(bs.scenes.empty()) << "legacy scene '" << bad << "'";
    }
}

TEST(BuildSettingsMigration, ApplyToWritesTheBootSceneAndKeepsEverythingElse) {
    BuildSettings bs;
    bs.scenes = { "Exported/fight.json", "Exported/menu.json" };

    ProjectSettings ps;
    ps.startupScene = "Exported/whatever_was_there.json";
    ps.masterVolume = 0.42f;
    ASSERT_FALSE(ps.startupSceneFromBuild);

    ASSERT_TRUE(bs.ApplyTo(ps));
    EXPECT_EQ(ps.startupScene, "Exported/fight.json") << "scenes[0] is what the built game boots";
    EXPECT_TRUE(ps.startupSceneFromBuild)
        << "without the marker, a build whose scenes[0] is a level is overruled "
           "at boot by the title's compiled-in front end -- silently";
    EXPECT_FLOAT_EQ(ps.masterVolume, 0.42f)
        << "ApplyTo is a load-modify-save step; clobbering masterVolume would "
           "reset the audio mix in every bundle";

    // Reordering the list changes what boots. That is the whole feature.
    ASSERT_TRUE(bs.SetStartupScene("Exported/menu.json"));
    ASSERT_TRUE(bs.ApplyTo(ps));
    EXPECT_EQ(ps.startupScene, "Exported/menu.json");
}

TEST(BuildSettingsMigration, TheManifestMarkerSurvivesAPlayerTouchingTheVolume) {
    // The reason the scene list is NOT in project.json, tested from the other
    // end: the shipped game load-modify-saves its own settings when a player
    // moves the volume slider, and Save rewrites the file from the fields the
    // struct models. Anything the struct does not model is erased -- inside the
    // bundle, where nobody would look.
    Scratch bundle("bundle_project");

    BuildSettings bs;
    bs.scenes = { "Exported/UntitledFighter/menu.json", "Exported/UntitledFighter/fight.json" };

    ProjectSettings authored;          // what the editor had
    authored.masterVolume = 0.8f;
    ASSERT_TRUE(bs.ApplyTo(authored)); // what the Build writes into the bundle
    ASSERT_TRUE(authored.Save(bundle.path()));

    // ...now the shipped game, on someone else's machine.
    ProjectSettings inGame;
    ASSERT_TRUE(inGame.Load(bundle.path()));
    ASSERT_TRUE(inGame.startupSceneFromBuild);
    ASSERT_EQ(inGame.startupScene, "Exported/UntitledFighter/menu.json");
    inGame.masterVolume = 0.25f;
    ASSERT_TRUE(inGame.Save(bundle.path()));

    ProjectSettings atNextBoot;
    ASSERT_TRUE(atNextBoot.Load(bundle.path()));
    EXPECT_TRUE(atNextBoot.startupSceneFromBuild)
        << "a player moving the volume slider erased the build manifest marker, "
           "so the bundle now boots somewhere the build panel did not say";
    EXPECT_EQ(atNextBoot.startupScene, "Exported/UntitledFighter/menu.json");
    EXPECT_FLOAT_EQ(atNextBoot.masterVolume, 0.25f);
}

// ============================================================================
// 5. THE OUTPUT DIRECTORY IS CONTAINED
// ============================================================================

TEST(BuildSettingsOutputDirectory, TheDefaultIsRelativeAndContained) {
    BuildSettings s;
    EXPECT_EQ(s.outputDirectory, "Builds");
    EXPECT_TRUE(OracleAccepts(s.outputDirectory));
    EXPECT_STREQ(BuildSettings::DefaultPath(), "Exported/build.json");
}

TEST(BuildSettingsOutputDirectory, AnEscapingOutputPathIsRefused) {
    // The sharpest of the refusals: the build WRITES here and MIRRORS what it
    // wrote, so an escaping output directory is a delete primitive with a text
    // field in front of it.
    ExpectRefusedAndUntouched(GoodDocWith("outputDirectory", "../Elsewhere").dump(2),
                              "an output directory escaping with ..");
    ExpectRefusedAndUntouched(GoodDocWith("outputDirectory", "Builds/../../Elsewhere").dump(2),
                              "an output directory escaping after a cancelling ..");
    ExpectRefusedAndUntouched(GoodDocWith("outputDirectory", "/tmp/elsewhere").dump(2),
                              "an absolute output directory");
    ExpectRefusedAndUntouched(GoodDocWith("outputDirectory", "//host/share/Builds").dump(2),
                              "a UNC output directory");
    ExpectRefusedAndUntouched(GoodDocWith("outputDirectory", "").dump(2),
                              "an empty output directory");
    ExpectRefusedAndUntouched(GoodDocWith("outputDirectory", 5).dump(2),
                              "a numeric output directory");
    ExpectRefusedAndUntouched(GoodDocWith("outputDirectory", nullptr).dump(2),
                              "a null output directory");
    ExpectRefusedAndUntouched(
        GoodDocWith("outputDirectory", std::string(kMaxPathLength + 1, 'b')).dump(2),
        "an output directory past kMaxPathLength");
#if defined(_WIN32)
    // A drive-rooted path is absolute on Windows. On Linux "C:/Builds" is an
    // ordinary two-element relative directory named "C:", which is contained
    // and therefore legal -- hence the guard rather than an unconditional
    // assertion that would be wrong on half of CI.
    ExpectRefusedAndUntouched(GoodDocWith("outputDirectory", "C:/Builds").dump(2),
                              "a drive-rooted output directory");
#endif
}

TEST(BuildSettingsOutputDirectory, AContainedOutputPathIsStoredNormalized) {
    Scratch file;
    ASSERT_TRUE(file.Write(GoodDocWith("outputDirectory", "Builds/staging/../Game")));

    BuildSettings s;
    ASSERT_EQ(s.Load(file.path()).status, BuildSettingsStatus::Ok);
    // NORMALIZE, THEN CHECK, THEN STORE THE NORMALIZED FORM. Storing the raw
    // spelling while approving the collapsed one is how a symlinked directory
    // turns an approved path into a different opened path.
    EXPECT_EQ(s.outputDirectory, "Builds/Game");
    EXPECT_EQ(BuildSettings::NormalizeScenePath(s.outputDirectory), s.outputDirectory);
    EXPECT_TRUE(OracleAccepts(s.outputDirectory));
}

TEST(BuildSettingsOutputDirectory, ItIsTheSameRuleTheSceneListObeys) {
    // THE CENTRAL CLAIM OF THIS SECTION, and the reason it is table-driven
    // against an oracle rather than against hand-written verdicts: the output
    // directory must be refused by THE SAME rule every other authored path in
    // this engine obeys. Two rules that agree today are not one rule.
    //
    // Every entry below is non-empty, unique and comfortably under
    // kMaxPathLength, so containment is the only thing left that can decide it.
    struct Case { const char* label; std::string path; };
    const std::vector<Case> cases = {
        { "a plain relative path",        "Builds" },
        { "a nested relative path",       "Builds/Game/Windows" },
        { "a leading ./",                 "./Builds" },
        { "a doubled separator",          "Builds//Game" },
        { "a cancelling ..",              "Builds/staging/../Game" },
        { "a trailing separator",         "Builds/Game/" },
        { "the project root itself",      "." },
        { "an escaping ..",               "../Elsewhere" },
        { "a bare ..",                    ".." },
        { "an escape after a cancel",     "Builds/../../Elsewhere" },
        { "a deep escape",                "a/b/../../../Elsewhere" },
        { "a posix absolute path",        "/tmp/elsewhere" },
        { "a root directory",             "/" },
        { "a drive-rooted path",          "C:/Builds" },
        { "a drive-relative path",        "C:Builds" },
        { "a UNC share",                  "//host/share/Builds" },
        { "a backslash spelling",         "Builds\\Game" },
        { "a name that is only spaces",   "  " },
        { "a dotfile",                    ".hidden/Builds" },
        { "a path with a quote in it",    "Bui\"lds" },
        { "a path with a NUL in it",      std::string("Exported/a\0b.json", 17) },
    };

    Scratch asScene("scene");
    Scratch asOutput("output");

    for (const Case& c : cases) {
        const bool expected = OracleAccepts(c.path);
        const std::string normalized = BuildSettings::NormalizeScenePath(c.path);

        // ...as a scene path.
        {
            json doc = json::object();
            doc["scenes"] = json::array({ c.path });
            ASSERT_TRUE(asScene.Write(doc)) << c.label;

            BuildSettings s;
            const BuildSettingsLoadResult r = s.Load(asScene.path());
            const bool got = (r.status == BuildSettingsStatus::Ok);
            EXPECT_EQ(got, expected)
                << c.label << " as a scene path: the loader and the sandbox "
                   "disagree (loader " << (got ? "accepted" : "refused")
                << ", sandbox " << (expected ? "accepts" : "refuses") << "). "
                << r.message;
            if (got) {
                EXPECT_EQ(s.scenes, (std::vector<std::string>{ normalized })) << c.label;
            }
        }

        // ...and as the output directory. Same verdict, or they are two rules.
        {
            json doc = json::object();
            doc["outputDirectory"] = c.path;
            ASSERT_TRUE(asOutput.Write(doc)) << c.label;

            BuildSettings s;
            const BuildSettingsLoadResult r = s.Load(asOutput.path());
            const bool got = (r.status == BuildSettingsStatus::Ok);
            EXPECT_EQ(got, expected)
                << c.label << " as an output directory: the loader and the "
                   "sandbox disagree (loader " << (got ? "accepted" : "refused")
                << ", sandbox " << (expected ? "accepts" : "refuses") << "). "
                << r.message;
            if (got) {
                EXPECT_EQ(s.outputDirectory, normalized) << c.label;
            }
        }
    }
}
