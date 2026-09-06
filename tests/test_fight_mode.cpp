// The mode with a live session (ROADMAP M2.4; DETERMINISM.md T1, T3).
//
// UntitledFighterMode decides in FixedTick whether a tick runs: pause, frame
// step and slow motion are that decision, and the character swap, reset, the
// stage position and Demonstrate all restart or re-source the match. Every
// one of them is a training feature, and every one is a desync the moment a
// peer is running the same match -- so while a session is ATTACHED the
// session decides the tick count and the controls are inert. This file holds
// the mode itself to that, headlessly, through the seam M2.4 added for it:
// the mode reads and binds its keys on an InputMap the test supplies, so the
// Application and its window are not needed.
//
// The Application's own half -- the gameplay dt and the pad suppression a
// live session overrides -- is tests/test_frame_gate.cpp.
#include <gtest/gtest.h>

#include "Engine.h"

#include "SessionDriver.h"
#include "UntitledFighterMode.h"

#include "cse/data/CharacterData.h"
#include "cse/data/MatchBuilder.h"
#include "cse/game/Catalogue.h"      // CatalogueNormalBindings: the cook's binding table
#include "cse/game/FightSession.h"
#include "cse/game/Replay.h"
#include "cse/kernel/Simulate.h"   // Checksum: the desync test proves frame + 1 is the tick
#include "cse/net/ISession.h"
#include "cse/net/LoopbackTransport.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using cse::game::HashMatchData;
using cse::game::ReplayData;
using cse::game::ReplayInputSource;
using cse::kernel::GameState;
using cse::net::CreateGekkoLocalSession;
using cse::net::CreateGekkoOnlineSession;
using cse::net::DestroySession;
using cse::net::ISession;
using cse::net::LoopbackNetwork;
using cse::net::SessionConfig;
using untitledfighter::FightHudModel;
using untitledfighter::ModeIntent;
using untitledfighter::SessionDriver;
using untitledfighter::UntitledFighterMode;

namespace {

// The shipped replay's path under the content root, and the command that
// remakes it. A replay names exactly one MatchData by hash (DETERMINISM.md
// S9), so a frame-data edit to fighter_a.json, a binding-table change or a
// kernel change makes the committed file stale -- and this test goes red
// with the fix in its message rather than the mode showing a CharacterChanged
// screen to whoever runs it next.
constexpr const char* kShippedReplay = "UntitledFighter/Replays/base.csrp";
constexpr const char* kRegenerateReplay =
    "from out/build/<preset>/build/bin/<Config>/ run "
    "`UntitledFighterCatalogue Exported/Characters <scratch dir>` and copy "
    "<scratch dir>/base.csrp to Games/UntitledFighter/Assets/UntitledFighter/Replays/base.csrp "
    "(Games/UntitledFighter/Assets/UntitledFighter/Replays/CREDITS.md)";

// The content root the mode resolves "Characters/fighter_a.json" against: the
// staged Exported/ beside the tests, or the source assets for a bare IDE run.
std::string contentRoot() {
    namespace fs = std::filesystem;
    fs::path here = fs::current_path();
    for (int i = 0; i < 8; ++i) {
        const fs::path staged = here / "Exported";
        if (fs::exists(staged / "Characters" / "fighter_a.json")) return staged.string();
        if (!here.has_parent_path() || here.parent_path() == here) break;
        here = here.parent_path();
    }
    here = fs::current_path();
    for (int i = 0; i < 8; ++i) {
        const fs::path source = here / "Games" / "UntitledFighter" / "Assets";
        if (fs::exists(source / "Characters" / "fighter_a.json")) return source.string();
        if (!here.has_parent_path() || here.parent_path() == here) break;
        here = here.parent_path();
    }
    return "Exported";
}

// The scripted keyboard from test_input_map.cpp: the poll seams are virtual
// exactly so a test can be the keyboard.
class FakeInput : public MyCoreEngine::InputMap {
public:
    std::unordered_map<int, bool> keys;

protected:
    bool pollKey(GLFWwindow*, int key) const override {
        auto it = keys.find(key);
        return it != keys.end() && it->second;
    }
    bool pollMouseButton(GLFWwindow*, int) const override { return false; }
    bool pollGamepad(GLFWgamepadstate& out) const override {
        (void)out;
        return false;
    }
};

// A mode with a keyboard and no window. The intent is the constructor's, as
// it is in RegisterTitleGameModes: one class, entered as any of the three.
struct ModeHost {
    FakeInput           input;
    UntitledFighterMode mode;

    explicit ModeHost(ModeIntent intent = ModeIntent::Training) : mode(intent) {}

    // `requireMatch` false lets a test Enter a mode whose honest-error screen
    // IS the thing under test (a replay that is not there, a lobby that
    // refused): Enter succeeds on every path, MatchReady says what loaded.
    void BringUp(bool requireMatch = true) { BringUp(contentRoot(), requireMatch); }

    // The same Enter against a content root the TEST built -- a copy of the
    // staged tree with one file made wrong on purpose, so a refusal that
    // depends on what is on disk (a stale replay) can be watched without
    // editing the shipped assets under every other test's feet.
    void BringUp(const std::string& root, bool requireMatch) {
        mode.SetInputMap(&input);
        MyCoreEngine::GameModeContext ctx{};
        ctx.contentRoot = root;
        std::string error;
        ASSERT_TRUE(mode.Enter(ctx, error)) << error;
        if (requireMatch) ASSERT_TRUE(mode.MatchReady()) << mode.SetupError();
    }

    // One rendered frame running `steps` fixed steps: the Application's loop in
    // miniature (poll, phases, latch retirement), as test_press_delivery.cpp
    // mirrors it.
    void Frame(int steps = 1) {
        input.update(nullptr);
        for (int i = 0; i < steps; ++i) {
            input.beginInputPhase();
            mode.FixedTick(1.f / 60.f);
        }
        if (steps > 0) input.clearPressLatches();
    }

    // Press for one frame, release for one.
    void Tap(int key) {
        input.keys[key] = true;
        Frame(1);
        input.keys[key] = false;
        Frame(1);
    }
};

// Exit on the way out of the scope WHATEVER happened in it. A Versus mode
// owns a session, and an ASSERT that returned early past the Exits below
// would leave one of the process's four bridge slots held for the next test
// (GekkoSession.cpp) -- the leak that turns one failure into a cascade.
// A mode a test already Exited (the peer that leaves mid-match) is Exited
// again here, which IGameMode::Exit allows: every member it touches is
// default-constructed by then, and the seam (SetInputMap) survives Exit.
struct ExitBoth {
    ModeHost& a;
    ModeHost& b;
    ~ExitBoth() {
        a.mode.Exit();
        b.mode.Exit();
    }
};

// A directory a test wrote, removed on the way out WHATEVER happened -- an
// artifact directory left by a failed ASSERT would satisfy the next run's
// existence checks with stale files. Declared BEFORE the objects that write
// into it, so the removal runs after they have Exited.
struct RemoveDir {
    std::filesystem::path d;
    ~RemoveDir() {
        std::error_code ec;
        std::filesystem::remove_all(d, ec);
    }
};

// One step of two peers' hosts and the network between them.
void StepPeers(ModeHost& a, ModeHost& b, LoopbackNetwork& net) {
    a.Frame(1);
    b.Frame(1);
    net.Tick();
}

// fighter_a as the MODE builds it: the same loader, the same options, the
// same MatchData bytes -- so a hash taken here is the hash the mode's own
// replay read demands.
void buildFighterAAsTheModeDoes(cse::data::CharacterData& character,
                                cse::data::MatchBuild& build) {
    cse::data::LoadOptions loadOptions{};
    cse::data::LoadReport  loadReport{};
    ASSERT_TRUE(cse::data::LoadCharacterFile(contentRoot(), "Characters/fighter_a.json",
                                             loadOptions, character, loadReport))
        << loadReport.error;
    const cse::data::BuildOptions options = UntitledFighterMode::MatchBuildOptions();
    ASSERT_TRUE(cse::data::BuildMatchData(character, options, character, options, build))
        << build.report[0].error << build.report[1].error;
}

// The state a plain FightSession reaches after `ticks` ticks of `replay`, with
// one ReplayInputSource per slot over the same ReplayData and the replay's own
// MatchStart -- the pattern every replay consumer in the tree uses (the
// catalogue's verify pass, test_catalogue.cpp). What the mode shows must be
// THIS, byte for byte (DETERMINISM.md T5).
void straightReplayState(const ReplayData& replay, const cse::kernel::MatchData& data,
                         std::uint32_t ticks, GameState& out) {
    cse::game::FightSession session;
    cse::game::FightSetup   setup{};
    setup.start = replay.start;
    setup.data  = &data;
    std::string error;
    ASSERT_TRUE(session.Begin(setup, error)) << error;
    ReplayInputSource p0(replay, 0);
    ReplayInputSource p1(replay, 1);
    session.SetInputSource(0, &p0);
    session.SetInputSource(1, &p1);
    for (std::uint32_t t = 0; t < ticks; ++t) session.Tick();
    out = session.State();
}

} // namespace

TEST(FightMode, TheCatalogueAndTheModeBuildTheSameMatchData) {
    // The shipped replay's hash is a COINCIDENCE of two hand-kept tables: the
    // catalogue's normalBindings (Games/UntitledFighter/Game/src/Catalogue.cpp)
    // and the mode's MatchBuildOptions (UntitledFighterMode.cpp). The cook
    // records base.csrp against the first; the Replay intent demands the
    // second (ReplayReadOptions::expectedMatchDataHash). MoveDef::button is
    // part of MatchData, so one binding moved in one table and not the other
    // is a replay the mode refuses as "character changed" about a character
    // nobody changed. This pins the coincidence until one function owns both
    // (ROADMAP M2.5's follow-up).
    cse::data::LoadOptions loadOptions{};
    cse::data::LoadReport  loadReport{};
    cse::data::CharacterData character{};
    ASSERT_TRUE(cse::data::LoadCharacterFile(contentRoot(), "Characters/fighter_a.json",
                                             loadOptions, character, loadReport))
        << loadReport.error;

    const cse::data::BuildOptions modeOptions = UntitledFighterMode::MatchBuildOptions();
    const cse::data::BuildOptions cookOptions = cse::game::CatalogueNormalBindings(character);
    cse::data::MatchBuild modeBuild{};
    cse::data::MatchBuild cookBuild{};
    ASSERT_TRUE(cse::data::BuildMatchData(character, modeOptions, character, modeOptions, modeBuild))
        << modeBuild.report[0].error;
    ASSERT_TRUE(cse::data::BuildMatchData(character, cookOptions, character, cookOptions, cookBuild))
        << cookBuild.report[0].error;
    EXPECT_EQ(HashMatchData(cookBuild.data), HashMatchData(modeBuild.data))
        << "the catalogue's normalBindings (Games/UntitledFighter/Game/src/Catalogue.cpp) and "
           "the mode's MatchBuildOptions (Games/UntitledFighter/Modes/src/UntitledFighterMode.cpp) "
           "build different MatchData for fighter_a: the cooked base.csrp cannot play in the "
           "Replay intent until the two tables agree again";
}

TEST(FightMode, ThreeRegistryEntriesShareOneModeAndOnePresentation) {
    // ADR-022 D1: TRAINING, REPLAY and VERSUS are one class constructed three
    // times with three intents, not three modes. The registry is what both
    // hosts fill and what the menus read, so the names are asserted there,
    // through DisplayNameAt -- the registry hands out IGameMode pointers, and
    // the headless seam (SetInputMap) is not reachable through one, which is
    // why the entries below are Entered as three LOCALLY constructed modes.
    MyCoreEngine::GameModeRegistry registry;
    MyCoreEngine::RegisterTitleGameModes(registry);
    ASSERT_EQ(3, registry.Count()) << "the title registers one mode per intent";
    // Registration order is menu order (GameMode.h), and the title menu's
    // typed verbs are wired to slots 0/1/2 in this order (menu.cxml).
    EXPECT_STREQ("Untitled Fighting Game", registry.DisplayNameAt(0));
    EXPECT_STREQ("Replay",                 registry.DisplayNameAt(1));
    EXPECT_STREQ("Versus (online)",        registry.DisplayNameAt(2));

    // The network before the hosts: a mode's Exit must find its transport
    // alive, and NO test Enters a Versus mode without one -- the lobby would
    // otherwise bind versus.json's real UDP port, which tests/two_peers.py is
    // using under `ctest -j 4`.
    LoopbackNetwork net;
    ModeHost training(ModeIntent::Training);
    ModeHost replay(ModeIntent::Replay);
    ModeHost versus(ModeIntent::Versus);
    EXPECT_EQ(ModeIntent::Training, training.mode.Mode());
    EXPECT_EQ(ModeIntent::Replay,   replay.mode.Mode());
    EXPECT_EQ(ModeIntent::Versus,   versus.mode.Mode());

    // All three enter with the match prepared: Versus enters its lobby with
    // the match built, so the offer it sends can name the content; Replay
    // reads the shipped UntitledFighter/Replays/base.csrp against the hash of
    // the match it built, so its readiness IS that file's freshness.
    training.BringUp();
    replay.BringUp();
    versus.mode.SetTransport(&net.Endpoint("V"));
    versus.BringUp();

    // ONE PRESENTATION is a structural claim, and this is the structure: the
    // three intents fill the SAME FightHudModel and are drawn by the same
    // DrawFightHud, and only the words on it differ. Were there a second HUD
    // or a second model type for one of them, these three lines could not
    // compile against one struct. The model borrows the mode's pointers, so
    // each is read here and not held across a Frame.
    const FightHudModel ht = training.mode.HudModel();
    const FightHudModel hr = replay.mode.HudModel();
    const FightHudModel hv = versus.mode.HudModel();
    EXPECT_STREQ("TRAINING", ht.modeWord);
    EXPECT_STREQ("REPLAY",   hr.modeWord);
    EXPECT_STREQ("VERSUS",   hv.modeWord);
    // The two slot labels are the model's, per intent, not the HUD's literal:
    // the dummy is a TRAINING DUMMY only in training.
    EXPECT_STREQ("YOU",            ht.slotLabel[0]);
    EXPECT_STREQ("TRAINING DUMMY", ht.slotLabel[1]);
    EXPECT_STREQ("P1",             hr.slotLabel[0]);
    EXPECT_STREQ("P2",             hr.slotLabel[1]);
    EXPECT_STREQ("YOU",            hv.slotLabel[0]);
    EXPECT_STREQ("PEER",           hv.slotLabel[1]);
    EXPECT_TRUE(ht.matchReady);
    EXPECT_TRUE(hr.matchReady) << replay.mode.SetupError();
    EXPECT_TRUE(hv.matchReady);

    training.mode.Exit();
    replay.mode.Exit();
    versus.mode.Exit();
}

TEST(FightMode, AReplayDrivesBothSlotsAndTheTrainingClockStillWorks) {
    // ADR-022 D3, D4: the Replay intent is the training mode with BOTH slots
    // re-sourced from a file -- one ReplayInputSource per slot over one
    // ReplayData, the match begun from the file's own MatchStart -- and the
    // training clock kept, because nothing else is simulating this match:
    // pause, step and slow motion are the host deciding whether to call Tick(),
    // and a replay is the one place a playtester most wants to freeze a tick.
    //
    // THE FILE IS DECODED HERE FIRST, against the hash the mode builds, so a
    // stale replay fails on this line with the regenerate command and not
    // inside BringUp with a CharacterChanged sentence.
    cse::data::CharacterData character{};
    cse::data::MatchBuild    build{};
    buildFighterAAsTheModeDoes(character, build);
    ReplayData              replay{};
    cse::game::ReplayReport report{};
    cse::game::ReplayReadOptions options{};
    options.expectedMatchDataHash = HashMatchData(build.data);
    ASSERT_TRUE(cse::game::ReadReplayFile(contentRoot(), kShippedReplay, options, replay, report))
        << report.error << "\n  the shipped replay is stale or missing: " << kRegenerateReplay;
    ASSERT_GT(replay.TickCount(), 0u);
    const std::uint32_t ticks = replay.TickCount();

    ModeHost h(ModeIntent::Replay);
    h.BringUp();   // MatchReady, or SetupError names the refusal

    // Tick 0 IS the file's opening: the replay's MatchStart, not the training
    // corner. The catalogue records from its own bench, and a match begun from
    // anywhere else diverges at the first checkpoint (the recorder always
    // checkpoints tick 0).
    EXPECT_EQ(0u, h.mode.CurrentTick());
    EXPECT_EQ(replay.start.startPosX[0], h.mode.State().p[0].posX);
    EXPECT_EQ(replay.start.startPosX[1], h.mode.State().p[1].posX);
    {
        const FightHudModel m = h.mode.HudModel();
        // The word is the SOURCE's own name (ReplayInputSource::Name), read off
        // the bound source, not typed by the HUD.
        EXPECT_STREQ("REPLAY", m.speaking);
        EXPECT_TRUE(m.replay);
        EXPECT_EQ(ticks, m.replayTicks);
        EXPECT_FALSE(m.replayOver);
    }

    // The training clock, through the same keys and the same seam as the
    // training test above: 20 steps are 20 ticks, SPACE holds, `.` runs one,
    // SPACE again releases.
    h.Frame(20);
    EXPECT_EQ(20u, h.mode.CurrentTick());
    h.Tap(GLFW_KEY_SPACE);
    EXPECT_TRUE(h.mode.Paused());
    const std::uint32_t tPaused = h.mode.CurrentTick();
    h.Frame(5);
    EXPECT_EQ(tPaused, h.mode.CurrentTick()) << "the pause did not hold the replay";
    h.Tap(GLFW_KEY_PERIOD);
    EXPECT_EQ(tPaused + 1u, h.mode.CurrentTick()) << "a step is exactly one tick";
    EXPECT_TRUE(h.mode.Paused());
    h.Tap(GLFW_KEY_SPACE);
    EXPECT_FALSE(h.mode.Paused());
    EXPECT_GT(h.mode.CurrentTick(), tPaused + 1u) << "SPACE did not release the replay";

    // V and TAB are INERT: both slots are the recording's. A stage change
    // would restart the file from a position it was not recorded at; a
    // demonstration would put a script in front of a slot the file already
    // authors. Neither key resets the tick or re-sources anything.
    //
    // THE OBSERVABLE IS THAT THE PRESS IS NOT CONSUMED. fighter_a has nothing
    // to demonstrate on any intent, so "demoRemaining is 0 after TAB" held
    // with the Replay guard deleted -- an assertion that cannot fail pins
    // nothing. A press the mode did not take is still latched when the NEXT
    // phase asks for it (InputMap::consumePressed: a later phase retires a
    // served press and returns false); so the key is set, the mode's fixed
    // phase runs, a fresh phase is opened and the test consumes -- true only
    // if the mode declined. The Tap helper cannot do this: its frame ends in
    // clearPressLatches, which drops the evidence.
    const auto declined = [&h](int key, const char* action) {
        h.input.keys[key] = true;
        h.input.update(nullptr);
        h.input.beginInputPhase();
        h.mode.FixedTick(1.f / 60.f);
        h.input.beginInputPhase();
        const bool stillLatched = h.input.consumePressed(action);
        h.input.keys[key] = false;
        h.input.clearPressLatches();
        h.Frame(1);   // the release, as Tap would have run it
        return stillLatched;
    };
    const std::uint32_t tBefore = h.mode.CurrentTick();
    EXPECT_TRUE(declined(GLFW_KEY_V, "Fight.StagePosition")) << "the mode consumed V in a replay";
    EXPECT_FALSE(h.mode.HudModel().stageMidscreen);
    EXPECT_GT(h.mode.CurrentTick(), tBefore) << "V restarted the replay";
    EXPECT_TRUE(declined(GLFW_KEY_TAB, "Fight.Demonstrate")) << "the mode consumed TAB in a replay";
    EXPECT_EQ(0u, h.mode.HudModel().demoRemaining);
    EXPECT_STREQ("REPLAY", h.mode.HudModel().speaking);

    // Run to the end. The replay is about eight seconds; the cap is its length
    // plus a margin, so a mode that never says "over" fails here and not by
    // hanging. Over means: the tick index stands at the file's tick count,
    // the mode paused itself, and the host calling on runs nothing more --
    // FightSession would feed NEUTRAL forever past the end, and a fight
    // nobody recorded is not the replay.
    for (std::uint32_t i = 0; i < ticks + 60u && !h.mode.ReplayOver(); ++i) h.Frame(1);
    ASSERT_TRUE(h.mode.ReplayOver())
        << "after " << ticks + 60u << " steps the tick is " << h.mode.CurrentTick()
        << " of " << ticks;
    EXPECT_TRUE(h.mode.Paused());
    EXPECT_EQ(ticks, h.mode.CurrentTick());
    h.Frame(10);
    EXPECT_EQ(ticks, h.mode.CurrentTick()) << "the mode ticked past the end of the file";
    EXPECT_TRUE(h.mode.Fatal().empty()) << h.mode.Fatal();

    // THE VERIFIER'S VERDICT IS CLEAN, and it compared something: a verifier
    // that compared nothing must not read as one that agreed with everything
    // (Replay.h). Clean here is the kernel today agreeing with the kernel that
    // recorded the file -- the cheapest determinism regression test the
    // project owns, and the mode shows it, never corrects it.
    ASSERT_NE(nullptr, h.mode.ReplayVerdict());
    EXPECT_FALSE(h.mode.ReplayVerdict()->diverged)
        << "checkpoint tick " << h.mode.ReplayVerdict()->tick;
    EXPECT_FALSE(h.mode.ReplayVerdict()->inputMismatch)
        << "tick " << h.mode.ReplayVerdict()->inputMismatchTick;
    {
        const FightHudModel m = h.mode.HudModel();
        EXPECT_TRUE(m.replayOver);
        EXPECT_GT(m.replayCheckpointsCompared, 0u);
        EXPECT_EQ(m.replayCheckpointsCompared, m.replayCheckpointsAgreed);
    }

    // T5: what the mode shows at tick N is, byte for byte, what a plain
    // FightSession driven by two ReplayInputSources over the same ReplayData
    // reaches -- the mode adds no state of its own to a replay.
    GameState straight{};
    straightReplayState(replay, build.data, ticks, straight);
    EXPECT_EQ(0, std::memcmp(&straight, &h.mode.State(), sizeof(GameState)))
        << "the mode's replay state at tick " << ticks
        << " is not the straight two-source session's";

    // R RESTARTS THE SAME FILE FROM TICK 0 with the same two sources still
    // bound (FightSession::Begin keeps sources; the verifier is Reset) -- and
    // the second run lands on the same bytes as the first, which is what
    // makes a replay a replay.
    h.Tap(GLFW_KEY_R);
    EXPECT_FALSE(h.mode.ReplayOver());
    EXPECT_FALSE(h.mode.Paused());
    EXPECT_LT(h.mode.CurrentTick(), 5u) << "R did not restart the replay at tick 0";
    EXPECT_STREQ("REPLAY", h.mode.HudModel().speaking);
    for (std::uint32_t i = 0; i < ticks + 60u && !h.mode.ReplayOver(); ++i) h.Frame(1);
    ASSERT_TRUE(h.mode.ReplayOver());
    EXPECT_EQ(ticks, h.mode.CurrentTick());
    EXPECT_EQ(0, std::memcmp(&straight, &h.mode.State(), sizeof(GameState)))
        << "the restarted replay did not reach the same state";
    EXPECT_FALSE(h.mode.ReplayVerdict()->diverged);
    EXPECT_FALSE(h.mode.ReplayVerdict()->inputMismatch);
    EXPECT_TRUE(h.mode.Fatal().empty()) << h.mode.Fatal();

    h.mode.Exit();
}

TEST(FightMode, AStaleReplayIsRefusedByNameWithTheRegenerateCommand) {
    // DETERMINISM.md S9: a replay names exactly one MatchData by hash, and a
    // file whose hash is not the match this mode builds is REFUSED -- the
    // honest-error screen with the regenerate command in the sentence --
    // never a match played against different data with the verifier calling
    // the kernel the culprit. ROADMAP and fighting-core.md claimed the screen;
    // until this test nothing held it.
    //
    // The stale file is MADE, not found: a copy of the staged tree under a
    // root of this test's own, with one byte of base.csrp's matchDataHash
    // (header offset 8, Replay.h) flipped. The character loader opens the
    // character file and the model's `.clips.json` sidecar (CharacterData.cpp,
    // engine.anim3d.model; the glTF itself is the presentation's, and
    // headless there is none), so those two are copied and nothing else -- a
    // loader that grows a read fails the load, and the assertion below shows
    // its sentence instead of the replay's.
    namespace fs = std::filesystem;
    const fs::path root = "stale_replay_root";
    {
        std::error_code ec;
        fs::remove_all(root, ec);   // a stale run's files
    }
    RemoveDir cleanup{ root };
    const fs::path staged = contentRoot();
    std::error_code ec;
    fs::create_directories(root / "Characters" / "fighter_a" / "model", ec);
    fs::create_directories(root / "UntitledFighter" / "Replays", ec);
    ASSERT_TRUE(fs::copy_file(staged / "Characters" / "fighter_a.json",
                              root / "Characters" / "fighter_a.json",
                              fs::copy_options::overwrite_existing, ec)) << ec.message();
    ASSERT_TRUE(fs::copy_file(staged / "Characters" / "fighter_a" / "model" / "fighter_a.clips.json",
                              root / "Characters" / "fighter_a" / "model" / "fighter_a.clips.json",
                              fs::copy_options::overwrite_existing, ec)) << ec.message();
    {
        std::ifstream in(staged / kShippedReplay, std::ios::binary);
        ASSERT_TRUE(in.good()) << (staged / kShippedReplay).string() << " is not staged";
        std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        ASSERT_GT(bytes.size(), 12u) << "shorter than the CSRP header's matchDataHash";
        bytes[8] = static_cast<char>(static_cast<unsigned char>(bytes[8]) ^ 0xFFu);
        std::ofstream out(root / kShippedReplay, std::ios::binary);
        ASSERT_TRUE(out.good());
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    ModeHost h(ModeIntent::Replay);
    h.BringUp(root.string(), /*requireMatch*/ false);
    EXPECT_FALSE(h.mode.MatchReady()) << "a replay whose matchDataHash is not this match's was played";
    const std::string& error = h.mode.SetupError();
    // Refused BY NAME: the file, the field, and the way out.
    EXPECT_NE(std::string::npos, error.find("base.csrp")) << error;
    EXPECT_NE(std::string::npos, error.find("matchDataHash")) << error;
    EXPECT_NE(std::string::npos, error.find("regenerate")) << error;
    EXPECT_NE(std::string::npos, error.find("UntitledFighterCatalogue")) << error;
    // Nothing behind the screen: no tick runs for a match that never began.
    h.Frame(5);
    EXPECT_EQ(0u, h.mode.CurrentTick());
    EXPECT_FALSE(h.mode.HudModel().matchReady);
    h.mode.Exit();
}

TEST(FightMode, PauseStepAndSlowMotionAreInertWhileASessionIsLive) {
    ModeHost h;
    h.BringUp();

    // Without a session the controls do what they say, through the same seam
    // -- so the inertness below is the session's doing and not a dead key.
    const std::uint32_t t0 = h.mode.CurrentTick();
    h.Frame(5);
    EXPECT_EQ(t0 + 5, h.mode.CurrentTick());
    h.Tap(GLFW_KEY_SPACE);
    EXPECT_TRUE(h.mode.Paused());
    const std::uint32_t tPaused = h.mode.CurrentTick();
    h.Frame(5);
    EXPECT_EQ(tPaused, h.mode.CurrentTick()) << "the training pause did not pause";
    h.Tap(GLFW_KEY_SPACE);
    EXPECT_FALSE(h.mode.Paused());

    // A live LOCAL session -- both slots local, the dummy neutral -- is enough:
    // T3 is about who decides, not about the wire.
    ISession* s = CreateGekkoLocalSession(SessionDriver::WireConfig(2));
    ASSERT_NE(nullptr, s);
    std::string error;
    ASSERT_TRUE(h.mode.AttachSession(s, /*padSlot*/ 0, /*localSlots*/ 0b11, error)) << error;
    ASSERT_TRUE(h.mode.SessionLive());

    const std::uint32_t t1 = h.mode.CurrentTick();
    h.Tap(GLFW_KEY_SPACE);                       // pause
    EXPECT_FALSE(h.mode.Paused());
    h.Tap(GLFW_KEY_PERIOD);                      // frame step
    EXPECT_EQ(0u, h.mode.PendingSteps());
    h.Tap(GLFW_KEY_COMMA);                       // slow motion
    EXPECT_EQ(1, h.mode.SlowDivisor());
    h.Frame(10);
    const std::uint32_t ran = h.mode.CurrentTick() - t1;
    EXPECT_EQ(h.mode.Driver().Counts().ticksRun, ran)
        << "a tick ran that the session did not advance";
    EXPECT_GE(ran, 10u) << "the taps above stopped or slowed the match";

    // Reset, the character swap and the stage position restart the match;
    // live, they do nothing -- the tick index never goes back.
    const std::uint32_t t2 = h.mode.CurrentTick();
    h.Tap(GLFW_KEY_R);
    h.Tap(GLFW_KEY_C);
    h.Tap(GLFW_KEY_V);
    EXPECT_GT(h.mode.CurrentTick(), t2);
    EXPECT_TRUE(h.mode.Fatal().empty()) << h.mode.Fatal();

    // Detached, the controls are the host's again.
    h.mode.DetachSession();
    EXPECT_FALSE(h.mode.SessionLive());
    DestroySession(s);
    h.Tap(GLFW_KEY_SPACE);
    EXPECT_TRUE(h.mode.Paused());

    h.mode.Exit();
}

TEST(FightMode, AttachSessionRefusesASlotItCannotPlayAndAReplay) {
    // AttachSession took any padSlot and any mask. SessionDriver::Frame offers
    // the pad only to the slot that is both in the mask and the pad's, so a
    // padSlot of 2 in a two-player match, or a mask without the pad's bit,
    // was a keyboard offered to NOBODY: the match ran on under the session
    // with a dead pad and nothing on screen said so. And a replay's two slots
    // are the file's (ADR-022 D3): a session bound over them would feed the
    // kernel bits the recording did not author, and the verifier would call
    // the kernel the culprit. Each is refused with its value in the sentence,
    // BEFORE any member is written -- the tick index is the witness that the
    // refused attach did not re-Begin the match.
    ISession* s = CreateGekkoLocalSession(SessionDriver::WireConfig(2));
    ASSERT_NE(nullptr, s);
    struct DestroyOne {
        ISession*& s;
        ~DestroyOne() { if (s) DestroySession(s); }
    } destroy{ s };

    {
        ModeHost h;
        h.BringUp();
        h.Frame(5);
        const std::uint32_t t = h.mode.CurrentTick();
        std::string error;
        EXPECT_FALSE(h.mode.AttachSession(s, /*padSlot*/ 2, /*localSlots*/ 0b11, error));
        EXPECT_NE(std::string::npos, error.find("padSlot 2")) << error;
        error.clear();
        EXPECT_FALSE(h.mode.AttachSession(s, -1, 0b11, error));
        EXPECT_NE(std::string::npos, error.find("padSlot -1")) << error;
        error.clear();
        // The pad's slot is one this host does not supply.
        EXPECT_FALSE(h.mode.AttachSession(s, 1, 0b01, error));
        EXPECT_NE(std::string::npos, error.find("padSlot 1")) << error;
        EXPECT_FALSE(h.mode.SessionLive());
        EXPECT_EQ(t, h.mode.CurrentTick()) << "a refused attach re-Began the match";
        // The same session, legally seated, still attaches -- so the refusals
        // above are the arguments' and not the session's.
        error.clear();
        ASSERT_TRUE(h.mode.AttachSession(s, 0, 0b11, error)) << error;
        EXPECT_TRUE(h.mode.SessionLive());
        h.mode.DetachSession();
        h.mode.Exit();
    }
    {
        ModeHost h(ModeIntent::Replay);
        h.BringUp();
        h.Frame(5);
        const std::uint32_t t = h.mode.CurrentTick();
        std::string error;
        EXPECT_FALSE(h.mode.AttachSession(s, 0, 0b11, error));
        EXPECT_NE(std::string::npos, error.find("replay")) << error;
        EXPECT_NE(std::string::npos, error.find("D3")) << error;
        EXPECT_FALSE(h.mode.SessionLive());
        EXPECT_EQ(t, h.mode.CurrentTick()) << "a refused attach re-Began the replay";
        h.mode.Exit();
    }
}

TEST(FightMode, AFrameTheSessionDoesNotAdvanceRunsNoTickAndIsNotAnError) {
    // Two modes, two peers, one loopback with latency: the first fixed steps
    // run no tick at all, because nothing has connected -- and that is a
    // legal frame, not a fatal one (T1).
    ModeHost a, b;
    a.BringUp();
    b.BringUp();

    LoopbackNetwork net;
    net.SetLatencyFrames(2);
    SessionConfig ca = SessionDriver::WireConfig(2);
    ca.localDelay    = 2;
    ca.peerAddresses = { std::string(), "B" };
    SessionConfig cb = ca;
    cb.peerAddresses = { "A", std::string() };
    ISession* sa = CreateGekkoOnlineSession(ca, &net.Endpoint("A"));
    ISession* sb = CreateGekkoOnlineSession(cb, &net.Endpoint("B"));
    ASSERT_NE(nullptr, sa);
    ASSERT_NE(nullptr, sb);

    std::string error;
    ASSERT_TRUE(a.mode.AttachSession(sa, 0, 1u << 0, error)) << error;
    ASSERT_TRUE(b.mode.AttachSession(sb, 1, 1u << 1, error)) << error;

    const std::uint32_t ta = a.mode.CurrentTick();
    a.Frame(1);
    b.Frame(1);
    net.Tick();
    EXPECT_EQ(ta, a.mode.CurrentTick()) << "a tick ran before the peers connected";
    EXPECT_TRUE(a.mode.Fatal().empty()) << a.mode.Fatal();
    EXPECT_TRUE(a.mode.MatchReady());

    // Held Right on A for a stretch: the match gets going and both kernels
    // follow the session, never the fixed step.
    for (int f = 0; f < 240; ++f) {
        a.input.keys[GLFW_KEY_D] = (f >= 40 && f < 80);
        a.Frame(1);
        b.Frame(1);
        net.Tick();
    }
    EXPECT_TRUE(a.mode.Fatal().empty()) << a.mode.Fatal();
    EXPECT_TRUE(b.mode.Fatal().empty()) << b.mode.Fatal();
    EXPECT_GT(a.mode.CurrentTick() - ta, 150u) << "the peers never got going";
    EXPECT_EQ(a.mode.Driver().Counts().ticksRun, a.mode.Driver().Counts().advances);
    EXPECT_EQ(b.mode.Driver().Counts().ticksRun, b.mode.Driver().Counts().advances);
    EXPECT_LE(a.mode.CurrentTick() > b.mode.CurrentTick() ? a.mode.CurrentTick() - b.mode.CurrentTick()
                                                          : b.mode.CurrentTick() - a.mode.CurrentTick(),
              8u) << "the peers drifted apart by more than the prediction window";
    // The held walk reached BOTH kernels: A's fighter is where B says it is.
    EXPECT_EQ(a.mode.State().p[0].posX, b.mode.State().p[0].posX);
    EXPECT_NE(a.mode.State().p[0].posX, a.mode.State().p[1].posX);

    a.mode.DetachSession();
    b.mode.DetachSession();
    DestroySession(sa);
    DestroySession(sb);
    a.mode.Exit();
    b.mode.Exit();
}

TEST(FightMode, VersusReachesLiveThroughTheHandshakeOverALoopback) {
    // ADR-022 D2: the Versus intent owns its transport, its handshake and the
    // session that follows, and reaches LIVE with no help from the host. The
    // network is declared FIRST so the modes' Exits (and destructors) run
    // while their endpoints exist -- the bridge holds the transport pointer
    // until DestroySession (GekkoSession.cpp).
    LoopbackNetwork net;
    ModeHost a(ModeIntent::Versus), b(ModeIntent::Versus);
    ExitBoth guard{ a, b };
    a.mode.SetTransport(&net.Endpoint("A"));
    b.mode.SetTransport(&net.Endpoint("B"));
    // The peer strings are the loopback's endpoint names; the port is unused
    // with an injected transport. Both copies read the same shipped
    // versus.json and differ ONLY by these overrides -- two copies on one
    // machine, headless.
    a.mode.SetCommandLine({ "--slot", "0", "--peer", "B" });
    b.mode.SetCommandLine({ "--slot", "1", "--peer", "A" });
    a.BringUp();
    b.BringUp();

    // Enter leaves both in the lobby with the match BUILT (the offer names its
    // content hash) and nothing live; the screen says who it is waiting for.
    ASSERT_TRUE(a.mode.InLobby());
    EXPECT_FALSE(a.mode.SessionLive());
    {
        const FightHudModel h = a.mode.HudModel();
        EXPECT_TRUE(h.lobby);
        EXPECT_FALSE(h.lobbyEnded);
        ASSERT_NE(nullptr, h.sessionNote);
        EXPECT_NE(std::string::npos, h.sessionNote->find("waiting")) << *h.sessionNote;
        ASSERT_NE(nullptr, h.peer);
        EXPECT_EQ("B", *h.peer);
        EXPECT_EQ(0, h.playerSlot);
        EXPECT_TRUE(h.matchReady);
    }

    // The lobby must not tick the local match: a tick behind the lobby screen
    // is a tick the peer never ran.
    const std::uint32_t lobbyTick = a.mode.CurrentTick();
    bool tickedInLobby = false;
    int steps = 0;
    for (; steps < 600 && !(a.mode.SessionLive() && b.mode.SessionLive()); ++steps) {
        StepPeers(a, b, net);
        if (!a.mode.SessionLive() && a.mode.CurrentTick() != lobbyTick) tickedInLobby = true;
    }
    ASSERT_TRUE(a.mode.SessionLive() && b.mode.SessionLive())
        << "after " << steps << " steps A says `" << *a.mode.HudModel().sessionNote
        << "` and B says `" << *b.mode.HudModel().sessionNote << "`";
    EXPECT_FALSE(tickedInLobby) << "the local match ticked behind the lobby screen";
    EXPECT_FALSE(a.mode.InLobby());
    {
        const FightHudModel h = a.mode.HudModel();
        EXPECT_FALSE(h.lobby);
        EXPECT_NE(std::string::npos, h.sessionNote->find("LIVE")) << *h.sessionNote;
        EXPECT_STREQ("NET", h.speaking);
        // B plays slot 1, so B's YOU is p[1]: the labels follow the local
        // slot, not a constant.
        const FightHudModel hb = b.mode.HudModel();
        EXPECT_EQ(1, hb.playerSlot);
        EXPECT_STREQ("YOU",  hb.slotLabel[1]);
        EXPECT_STREQ("PEER", hb.slotLabel[0]);
    }

    // ConnectedPeers is GekkoNet's own sync and arrives well after Agreed.
    int more = 0;
    for (; more < 300 && !(a.mode.HudModel().connectedPeers >= 1 &&
                           b.mode.HudModel().connectedPeers >= 1); ++more)
        StepPeers(a, b, net);
    EXPECT_GE(a.mode.HudModel().connectedPeers, 1)
        << "after " << more << " more steps A counts " << a.mode.HudModel().connectedPeers;
    EXPECT_GE(b.mode.HudModel().connectedPeers, 1)
        << "after " << more << " more steps B counts " << b.mode.HudModel().connectedPeers;

    // Both kernels advance together under the session, from the tick 0 the
    // attach put them on, and never off the session's word.
    const std::uint32_t ta = a.mode.CurrentTick();
    const std::uint32_t tb = b.mode.CurrentTick();
    for (int f = 0; f < 120; ++f) StepPeers(a, b, net);
    EXPECT_GT(a.mode.CurrentTick() - ta, 60u) << "A never got going";
    EXPECT_GT(b.mode.CurrentTick() - tb, 60u) << "B never got going";
    EXPECT_LE(a.mode.CurrentTick() > b.mode.CurrentTick() ? a.mode.CurrentTick() - b.mode.CurrentTick()
                                                          : b.mode.CurrentTick() - a.mode.CurrentTick(),
              8u) << "the peers drifted apart by more than the prediction window";
    EXPECT_EQ(a.mode.Driver().Counts().ticksRun, a.mode.Driver().Counts().advances);
    EXPECT_TRUE(a.mode.Fatal().empty()) << a.mode.Fatal();
    EXPECT_TRUE(b.mode.Fatal().empty()) << b.mode.Fatal();

    // A HOST'S DetachSession ON THE OWNED SESSION ENDS THE VISIT. The session
    // is the lobby's, and a Versus match without it is a fight the peer has
    // left -- so the lobby is Ended, sticky, and the local kernel does not
    // resume under the pad with the training controls back (which is what it
    // did before: lobby_ stayed Live with the session and handshake alive).
    a.mode.DetachSession();
    EXPECT_FALSE(a.mode.SessionLive());
    EXPECT_TRUE(a.mode.InLobby());
    EXPECT_TRUE(a.mode.HudModel().lobbyEnded);
    EXPECT_NE(std::string::npos, a.mode.HudModel().sessionNote->find("detached"))
        << *a.mode.HudModel().sessionNote;
    const std::uint32_t tDetached = a.mode.CurrentTick();
    for (int f = 0; f < 10; ++f) StepPeers(a, b, net);
    EXPECT_EQ(tDetached, a.mode.CurrentTick()) << "A ticked on after its session was detached";
    a.Tap(GLFW_KEY_SPACE);
    EXPECT_FALSE(a.mode.Paused()) << "the training pause came back behind an ended lobby";
}

TEST(FightMode, APeerThatLeavesEndsTheMatchAndSaysSo) {
    // GekkoNet marks a silent peer disconnected after the session's silence
    // timeout (backend.cpp HandleTooFarBehindActors;
    // SessionConfig::disconnectTimeoutMs) and then KEEPS emitting Advance
    // events with neutral inputs for the gone slot. A kernel that followed
    // them ticked a match nobody else simulated, under a HUD that said LIVE
    // and counted 0 peers. The mode ends the match on the count's drop
    // instead, and the sentence says who stopped answering and at which frame
    // the session gave up.
    //
    // A WALL CLOCK IN A TEST, ON PURPOSE. The timeout is GekkoNet's
    // steady_clock, not a frame count, and nothing this test can call
    // advances it -- so the copy that stays sleeps a few real milliseconds per
    // step. tests/two_peers.py waits on real time for the same reason. The
    // no-wall-clock rule (DETERMINISM.md) binds the MODE, which counts steps;
    // the test is the thing with the clock. And the clock is SHORTENED:
    // SetDisconnectTimeoutMs exists so this test holds the drop in ~100 ms
    // instead of the five seconds a player gets -- a test that slept five
    // real seconds on every run is the one that stops being run. Both copies
    // get the same value: the timeout is per session, and a mismatch would
    // only blur which copy dropped whom. The cap (600 steps) is slack for
    // Windows' sleep granularity, not a wait anyone expects to see.
    LoopbackNetwork net;
    ModeHost a(ModeIntent::Versus), b(ModeIntent::Versus);
    ExitBoth guard{ a, b };   // B is Exited early below and again here; both are legal
    a.mode.SetTransport(&net.Endpoint("A"));
    b.mode.SetTransport(&net.Endpoint("B"));
    a.mode.SetCommandLine({ "--slot", "0", "--peer", "B" });
    b.mode.SetCommandLine({ "--slot", "1", "--peer", "A" });
    a.mode.SetDisconnectTimeoutMs(100);
    b.mode.SetDisconnectTimeoutMs(100);
    a.BringUp();
    b.BringUp();

    int steps = 0;
    for (; steps < 600 && !(a.mode.SessionLive() && b.mode.SessionLive()); ++steps)
        StepPeers(a, b, net);
    ASSERT_TRUE(a.mode.SessionLive() && b.mode.SessionLive())
        << "after " << steps << " steps A says `" << *a.mode.HudModel().sessionNote
        << "` and B says `" << *b.mode.HudModel().sessionNote << "`";
    // The count must have been >= 1 for its drop to mean anything: an online
    // session counts 0 before the peers have found each other, and a mode
    // that ended the match on THAT zero would never reach LIVE.
    int more = 0;
    for (; more < 300 && !(a.mode.HudModel().connectedPeers >= 1 &&
                           b.mode.HudModel().connectedPeers >= 1); ++more)
        StepPeers(a, b, net);
    ASSERT_GE(a.mode.HudModel().connectedPeers, 1)
        << "after " << more << " more steps A counts " << a.mode.HudModel().connectedPeers;
    for (int f = 0; f < 30; ++f) StepPeers(a, b, net);
    EXPECT_TRUE(a.mode.Fatal().empty()) << a.mode.Fatal();

    // THE PEER LEAVES: its session destroyed, its handshake with it. Nothing
    // more arrives at A from B.
    b.mode.Exit();
    const auto saysDisconnected = [](const ModeHost& h) {
        const FightHudModel m = h.mode.HudModel();
        return m.sessionNote != nullptr &&
               m.sessionNote->find("stopped answering") != std::string::npos;
    };
    constexpr int kStepSleepMs = 3;
    const auto    left         = std::chrono::steady_clock::now();
    int alone = 0;
    for (; alone < 600 && !saysDisconnected(a); ++alone) {
        a.Frame(1);
        net.Tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(kStepSleepMs));
    }
    const long long waitedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - left).count();
    ASSERT_TRUE(saysDisconnected(a))
        << "after " << alone << " steps (" << waitedMs << " ms) A still says `"
        << *a.mode.HudModel().sessionNote << "` with " << a.mode.HudModel().connectedPeers
        << " peers and is " << (a.mode.SessionLive() ? "LIVE" : "not live");
    // What exists, for the human reading a green run: how long the 100 ms
    // timeout really took to land under this machine's sleep granularity.
    std::printf("[   note   ] the session dropped the peer after %d steps, %lld ms\n", alone,
                waitedMs);
    // And that the SHORTENED timeout is the one in charge: without the seam
    // reaching the SessionConfig the drop still lands, five seconds later,
    // inside the step cap -- so the cap alone would not notice. The slack is
    // wide (a loaded machine, a coarse sleep); the default is wider still.
    EXPECT_LT(waitedMs, 2500)
        << "the 100 ms timeout did not reach the session; the library's default is in charge";
    const std::string note = *a.mode.HudModel().sessionNote;
    // The sentence names the peer as the wire spells it and the frame the
    // session gave up at -- and no duration, because the timeout is a config
    // field and a number in the sentence would be one no test asserts.
    EXPECT_NE(std::string::npos, note.find("the peer at B")) << note;
    EXPECT_NE(std::string::npos, note.find("dropped it at frame")) << note;
    EXPECT_NE(std::string::npos, note.find("; the match stopped")) << note;
    EXPECT_FALSE(a.mode.SessionLive());
    EXPECT_FALSE(a.mode.Fatal().empty()) << "THE MATCH STOPPED does not show the disconnect";
    EXPECT_EQ(note, a.mode.Fatal());
    EXPECT_TRUE(a.mode.InLobby());
    EXPECT_TRUE(a.mode.HudModel().lobbyEnded);
    // Ended: no tick for 30 more frames of the host calling.
    const std::uint32_t t = a.mode.CurrentTick();
    for (int f = 0; f < 30; ++f) {
        a.Frame(1);
        net.Tick();
    }
    EXPECT_EQ(t, a.mode.CurrentTick()) << "A ticked on after the peer left";
}

TEST(FightMode, ALobbyRefusalIsNamedAndTheMatchIsStillReady) {
    // ADR-022 D5: a refused handshake is a lobby error in the handshake's own
    // words, and the mode stays honest about it -- the match is still built,
    // nothing is live, and the lobby stays ended until Escape.
    LoopbackNetwork net;
    ModeHost a(ModeIntent::Versus), b(ModeIntent::Versus);
    ExitBoth guard{ a, b };
    a.mode.SetTransport(&net.Endpoint("A"));
    b.mode.SetTransport(&net.Endpoint("B"));
    // BOTH claim slot 0: same content, same build, a seating error.
    a.mode.SetCommandLine({ "--slot", "0", "--peer", "B" });
    b.mode.SetCommandLine({ "--slot", "0", "--peer", "A" });
    a.BringUp();
    b.BringUp();

    // "refused: slot", the mode's word and the handshake's field together --
    // not "slot" alone, which the timeout sentence also contains ("--slot"),
    // so a lobby that timed out instead of refusing would have passed.
    const auto refusedNamingTheSlot = [](const ModeHost& h) {
        const FightHudModel m = h.mode.HudModel();
        return m.lobby && m.lobbyEnded && m.sessionNote != nullptr &&
               m.sessionNote->find("refused: slot") != std::string::npos;
    };
    int steps = 0;
    for (; steps < 600 && !(refusedNamingTheSlot(a) && refusedNamingTheSlot(b)); ++steps)
        StepPeers(a, b, net);
    ASSERT_TRUE(refusedNamingTheSlot(a) && refusedNamingTheSlot(b))
        << "after " << steps << " steps A says `" << *a.mode.HudModel().sessionNote
        << "` and B says `" << *b.mode.HudModel().sessionNote << "`";
    // BOTH peers, and neither waited out the clock: a refused handshake sends
    // nothing more, so each side learns of it from its own comparison.
    for (const ModeHost* h : { &a, &b }) {
        const std::string& note = *h->mode.HudModel().sessionNote;
        EXPECT_NE(std::string::npos, note.find("refused: slot")) << note;
        EXPECT_EQ(std::string::npos, note.find("never offered")) << note;
    }
    EXPECT_FALSE(a.mode.SessionLive());
    EXPECT_FALSE(b.mode.SessionLive());
    EXPECT_TRUE(a.mode.MatchReady()) << a.mode.SetupError();
    EXPECT_TRUE(b.mode.MatchReady()) << b.mode.SetupError();
    EXPECT_TRUE(a.mode.Fatal().empty()) << a.mode.Fatal();

    // ENDED IS STICKY within the visit: the local match does not tick, and
    // the training keys are inert -- a reset, a pause, a step or a stage
    // change behind the lobby screen would be a match the peer never joined.
    const std::uint32_t t = a.mode.CurrentTick();
    a.Frame(10);
    EXPECT_EQ(t, a.mode.CurrentTick()) << "the local match ticked behind an ended lobby";
    const std::string note = *a.mode.HudModel().sessionNote;
    a.Tap(GLFW_KEY_R);
    a.Tap(GLFW_KEY_SPACE);
    a.Tap(GLFW_KEY_PERIOD);
    a.Tap(GLFW_KEY_V);
    EXPECT_EQ(note, *a.mode.HudModel().sessionNote);
    EXPECT_EQ(t, a.mode.CurrentTick());
    EXPECT_FALSE(a.mode.Paused());
    EXPECT_EQ(0u, a.mode.PendingSteps());
    EXPECT_FALSE(a.mode.HudModel().stageMidscreen);
    EXPECT_TRUE(a.mode.InLobby());
}

TEST(FightMode, ADesyncReportEndsTheMatchAndNamesTheFieldOnTheHud) {
    // DETERMINISM.md T4, T6; ADR-002 CHOICE C: a desync ENDS the match -- never
    // a correction -- and the mode's last act is the post-mortem
    // tests/online_peer.cpp performs in a loop with a sleep in it, spread over
    // fixed steps instead: the grace that lets the peer detect too, the two
    // states crossing on the raw transport, the first field they disagree
    // about on the HUD, and an artifact a script can read.
    //
    // THE LOBBY IS BYPASSED -- two Training-intent modes, two sessions the
    // test created on one loopback -- because a lobby refuses unequal content
    // and the two matches here must START unequal: B stands midscreen and A
    // in the corner, so the first checksum GekkoNet compares disagrees and the
    // field it names is a position. The tail's seams are intent-independent
    // for exactly this test (SetTransport, SetPeerAddress,
    // SetArtifactDirectory): a session attached to ANY intent ends this way.
    namespace fs = std::filesystem;
    const fs::path dir = "desync_fight_mode";
    // A stale run's files would satisfy the existence asserts below.
    fs::remove_all(dir);

    // DESTRUCTION ORDER, and every line of it is load-bearing: the modes Exit
    // first (ExitBoth, declared last), then the test's two sessions go
    // (declared before it -- a session destroyed under a still-bound driver
    // is a dangling ISession on the next FixedTick), then the modes, then the
    // network they spoke on, and last the artifact directory (RemoveDir,
    // declared right after the network so both modes have Exited before it
    // goes -- on a failed ASSERT as much as on the way out of a green run).
    LoopbackNetwork net;
    RemoveDir cleanup{ dir };
    ModeHost a, b;
    ISession* sa = nullptr;
    ISession* sb = nullptr;
    struct DestroyBoth {
        ISession*& sa;
        ISession*& sb;
        ~DestroyBoth() {
            if (sa) DestroySession(sa);
            if (sb) DestroySession(sb);
        }
    } sessions{ sa, sb };
    ExitBoth guard{ a, b };
    a.mode.SetTransport(&net.Endpoint("A"));
    b.mode.SetTransport(&net.Endpoint("B"));
    a.mode.SetPeerAddress("B");
    b.mode.SetPeerAddress("A");
    a.mode.SetArtifactDirectory(dir.string());
    b.mode.SetArtifactDirectory(dir.string());
    a.BringUp();
    b.BringUp();

    // B moves to midscreen BEFORE the attach: AttachSession re-Begins at tick
    // 0 with each mode's OWN setup, so the two tick-0 states differ in posX
    // and in nothing else. No offer is involved -- this is the bypass.
    b.Tap(GLFW_KEY_V);
    ASSERT_TRUE(b.mode.HudModel().stageMidscreen);

    SessionConfig ca = SessionDriver::WireConfig(2);
    ca.localDelay    = 2;
    ca.peerAddresses = { std::string(), "B" };
    SessionConfig cb = ca;
    cb.peerAddresses = { "A", std::string() };
    sa = CreateGekkoOnlineSession(ca, &net.Endpoint("A"));
    sb = CreateGekkoOnlineSession(cb, &net.Endpoint("B"));
    ASSERT_NE(nullptr, sa);
    ASSERT_NE(nullptr, sb);
    std::string error;
    ASSERT_TRUE(a.mode.AttachSession(sa, 0, 1u << 0, error)) << error;
    ASSERT_TRUE(b.mode.AttachSession(sb, 1, 1u << 1, error)) << error;
    ASSERT_NE(a.mode.State().p[0].posX, b.mode.State().p[0].posX)
        << "the two matches start equal, so nothing here can desync";

    // The verdict is the sentence on the HUD. A substring, and "field p["
    // rather than a frame number: GekkoNet health-checks EVERY confirmed
    // frame of an online session, so the report can name frame 0.
    const auto named = [](const ModeHost& h) {
        const FightHudModel m = h.mode.HudModel();
        return m.sessionNote != nullptr &&
               m.sessionNote->find("field p[") != std::string::npos;
    };
    // The tick each verdict landed on -- "ends the match" means it never
    // moves again, and that is asserted below against these two numbers.
    // AND the tick each session DETACHED on, read the first step SessionLive()
    // is false: the exchange runs for many steps between the two, and a
    // kernel that ticked behind the Exchanging screen would have moved the
    // tick before the verdict was read -- measured from the verdict alone,
    // those ticks were invisible.
    std::uint32_t tickA = 0, tickB = 0;
    std::uint32_t tickDetachA = 0, tickDetachB = 0;
    bool namedA = false, namedB = false;
    bool detachedA = false, detachedB = false;
    int steps = 0;
    for (; steps < 600 && !(namedA && namedB); ++steps) {
        StepPeers(a, b, net);
        if (!detachedA && !a.mode.SessionLive()) { detachedA = true; tickDetachA = a.mode.CurrentTick(); }
        if (!detachedB && !b.mode.SessionLive()) { detachedB = true; tickDetachB = b.mode.CurrentTick(); }
        if (!namedA && named(a)) { namedA = true; tickA = a.mode.CurrentTick(); }
        if (!namedB && named(b)) { namedB = true; tickB = b.mode.CurrentTick(); }
    }
    ASSERT_TRUE(namedA && namedB)
        << "after " << steps << " steps A says `" << *a.mode.HudModel().sessionNote
        << "` and B says `" << *b.mode.HudModel().sessionNote << "`";
    ASSERT_TRUE(detachedA && detachedB);
    EXPECT_EQ(tickDetachA, tickA) << "A ticked between the detach and the verdict";
    EXPECT_EQ(tickDetachB, tickB) << "B ticked between the detach and the verdict";
    EXPECT_EQ(tickDetachA, a.mode.CurrentTick());
    EXPECT_EQ(tickDetachB, b.mode.CurrentTick());
    const std::string noteA = *a.mode.HudModel().sessionNote;
    const std::string noteB = *b.mode.HudModel().sessionNote;
    // The field is a POSITION, because the two setups differ in startPosX.
    // Not the exact path: the reflection table's order decides whether
    // p[0].posX or p[1].posX comes first -- or, once M3.1 fills ev[], an
    // event -- and this test pins the verdict, not the table.
    EXPECT_NE(std::string::npos, noteA.find("posX")) << noteA;
    EXPECT_NE(std::string::npos, noteB.find("posX")) << noteB;
    EXPECT_NE(std::string::npos, noteA.find("desync at frame")) << noteA;

    // THE MATCH IS OVER. The sessions are detached, the banner carries the
    // same sentence, and the tick index never moves again -- not for 60 more
    // frames of the host calling, and not for the training keys, which stay
    // inert until Escape (an ended lobby is not a training screen).
    EXPECT_FALSE(a.mode.SessionLive());
    EXPECT_FALSE(b.mode.SessionLive());
    EXPECT_EQ(noteA, a.mode.Fatal());
    EXPECT_EQ(noteB, b.mode.Fatal());
    for (int f = 0; f < 60; ++f) StepPeers(a, b, net);
    EXPECT_EQ(tickA, a.mode.CurrentTick()) << "A ticked on after the desync verdict";
    EXPECT_EQ(tickB, b.mode.CurrentTick()) << "B ticked on after the desync verdict";
    a.Tap(GLFW_KEY_R);
    a.Tap(GLFW_KEY_SPACE);
    EXPECT_EQ(tickA, a.mode.CurrentTick());
    EXPECT_EQ(noteA, *a.mode.HudModel().sessionNote);
    EXPECT_FALSE(a.mode.Paused());
    EXPECT_TRUE(a.mode.InLobby());
    EXPECT_TRUE(a.mode.HudModel().lobbyEnded);

    // THE FRAME IS NOT THE TICK (Desync.h): the state the mode compared is
    // the one after the reported frame's ADVANCE, and it is the state GekkoNet
    // checksummed -- the report's own local checksum says so. This is the
    // one place that off-by-one is held at the mode's level.
    ASSERT_NE(nullptr, a.mode.StateAtDesync());
    ASSERT_NE(nullptr, b.mode.StateAtDesync());
    EXPECT_EQ(a.mode.LastDesync().localChecksum, cse::kernel::Checksum(*a.mode.StateAtDesync()));
    EXPECT_EQ(b.mode.LastDesync().localChecksum, cse::kernel::Checksum(*b.mode.StateAtDesync()));
    EXPECT_EQ(static_cast<std::uint32_t>(a.mode.LastDesync().frame) + 1u,
              a.mode.StateAtDesync()->tick);

    // ONE ARTIFACT PER SLOT, in the directory the host named -- so two peers
    // on one machine never write the same file -- and it PARSES: a script
    // reads "divergence"."field" off it, which is why a parser and not a
    // substring is the judge here.
    for (int slot = 0; slot < 2; ++slot) {
        const fs::path path = dir / ("desync_slot" + std::to_string(slot) + ".json");
        ASSERT_TRUE(fs::exists(path)) << path.string() << " was not written";
        std::ifstream in(path, std::ios::binary);
        const nlohmann::json j = nlohmann::json::parse(in, nullptr, /*allow_exceptions*/ false);
        ASSERT_TRUE(j.is_object()) << path.string() << " is not a JSON object";
        ASSERT_TRUE(j.contains("divergence") && j["divergence"].is_object())
            << path.string() << ": " << j.dump();
        ASSERT_TRUE(j["divergence"].contains("field") && j["divergence"]["field"].is_string())
            << j.dump();
        EXPECT_NE(std::string::npos, j["divergence"]["field"].get<std::string>().find("posX"))
            << j.dump();
        EXPECT_TRUE(j.contains("reportedFrame")) << j.dump();
    }
    // The verdict names the file it wrote, so a player can find it.
    EXPECT_NE(std::string::npos, noteA.find((dir / "desync_slot0.json").generic_string())) << noteA;
    EXPECT_NE(std::string::npos, noteB.find((dir / "desync_slot1.json").generic_string())) << noteB;
}
