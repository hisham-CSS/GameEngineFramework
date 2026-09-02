// THE POSE IS A PURE FUNCTION OF SIM STATE, PROVEN WITHOUT A PICTURE
// (ROADMAP M3.4a; DETERMINISM.md P4; ADR-019 D3 and D9; ADR-011 decision 6).
//
// cse::game::SelectPose decides which clip a fighter wears and at which frame,
// from GameState and MatchData alone. Nothing here draws. What this file pins is
// the half of P4 that can be pinned before a single bone exists: that the
// decision is a function of the state and of nothing else, so that
//
//   * a Restore followed by a re-run reproduces every pose byte for byte
//     (section 1) -- the property a rollback host needs and the one a hidden
//     "when did the move end" memo would break;
//   * while a move runs the frame IS Fighter::moveFrame, from 0 on the tick the
//     move starts (section 2) -- ADR-005 section 4.1's authoritative window, and
//     the reason a clip authored at startup + active + recovery frames lines up
//     with the kernel's live hitbox with no offset anywhere;
//   * hitstop freezes the pose because it freezes the fields the pose reads
//     (section 3), so the mode gets the genre's freeze frame for free;
//   * the precedence is the kernel's own, and the two places it could have
//     guessed -- a released guard mid-blockstun, a winner who keeps acting after
//     the round is decided -- follow the sim rather than a story (sections 4-6);
//   * asking never changes the state (section 7), which is the const-correctness
//     ARCHITECTURE.md's one-way flow asks for, asserted on the bytes.
//
// THE RIG IS THE SHIPPED CHARACTER. fighter_a.json from the staged Exported/
// tree, a mirror match, the body the mode itself supplies -- the same shapes as
// tests/test_training_mode.cpp, for the same reason it gives: two spellings of
// "load fighter_a" is how two tests disagree about what they loaded. Every
// session-driven section below performs a real move on the real kernel; the
// mutation-driven sections start from a state a real session produced and flip
// one field at a time, which is how a precedence is pinned rather than an
// implementation.
#include <gtest/gtest.h>

#include "cse/game/FightSession.h"
#include "cse/game/InputSource.h"
#include "cse/game/PoseSelect.h"

#include "cse/data/CharacterData.h"
#include "cse/data/MatchBuilder.h"

#include "cse/kernel/Combat.h"
#include "cse/kernel/GameState.h"
#include "cse/kernel/Simulate.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace cse::data;
using namespace cse::game;

using cse::kernel::GameState;
using cse::kernel::Input;
using cse::kernel::MatchData;

namespace {

// ============================================================================
// 0. THE RIG
// ============================================================================

constexpr std::int32_t px(std::int32_t pixels) {
    return pixels * cse::kernel::kSubUnitsPerPixel;
}

// The body is the CALLER'S number -- CharacterData carries none. These are the
// shipped file's own engine.constants, as the mode supplies them.
constexpr std::int32_t kHalfWidth = px(13);
constexpr std::int32_t kHeight    = px(60);

// Midscreen, origins 34 px apart: bodies 8 px apart, inside every light's reach,
// and with room on both sides for the walk and the jump section 1 performs.
constexpr std::int32_t kP0X = -px(17);
constexpr std::int32_t kP1X =  px(17);

constexpr std::uint32_t kSeed = 0xC0FFEEu;

const std::vector<std::string> kBuildResources = { "meter", "juggle" };

LoadOptions loadOptions() {
    LoadOptions o;
    o.expectedResources = kBuildResources;
    return o;
}

// The staged shipping directory, never the Phase-0 corpus (see
// tests/test_training_mode.cpp for the two fallbacks and why neither can pass
// vacuously: every load below ASSERTs).
std::string charactersDir() {
#ifdef CSE_CHARACTERS_DIR
    return CSE_CHARACTERS_DIR;
#else
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
#endif
}

void loadShipped(CharacterData& out) {
    LoadReport report{};
    ASSERT_TRUE(LoadCharacterFile(charactersDir(), "fighter_a.json", loadOptions(), out, report))
        << "fighter_a.json did not load from " << charactersDir() << ".\n"
        << "  rule : " << (report.rule.empty() ? "(no load assertion named)" : report.rule)
        << "\n  error: " << report.error;
    ASSERT_FALSE(out.moves.empty()) << "fighter_a.json loaded with no moves";
}

BodySpec body() {
    BodySpec b{};
    b.halfWidthSub = kHalfWidth;
    b.heightSub    = kHeight;
    return b;
}

MoveBinding bind(std::string id, std::uint16_t button) {
    MoveBinding b{};
    b.moveId = std::move(id);
    b.button = button;
    return b;
}

// Four moves are enough to reach every kind this file needs: a light that
// connects (Move, Hitstun*, hitstop), a heavy for damage, a sweep for Knockdown
// (crouch_hk is the stance variant Down decides), and an aerial for the air
// path. Each on its own single bit.
std::vector<MoveBinding> bindings() {
    return {
        bind("stand_lp",  cse::kernel::kInputLP),
        bind("stand_hp",  cse::kernel::kInputHP),
        bind("crouch_hk", cse::kernel::kInputHK),
        bind("air_mp",    cse::kernel::kInputMP),
    };
}

struct Rig {
    CharacterData character;
    MatchBuild    build;

    void Load() {
        loadShipped(character);
        BuildOptions options{};
        options.body     = body();
        options.bindings = bindings();
        ASSERT_TRUE(BuildMatchData(character, options, character, options, build))
            << "the mirror match did not build";
        for (const char* id : { "stand_lp", "stand_hp", "crouch_hk", "air_mp" })
            ASSERT_NE(build.moves[0].Find(id), 0)
                << id << " did not get a kernel slot; the bindings above name a move "
                   "fighter_a.json no longer has";
    }

    std::uint16_t Slot(const char* id) const { return build.moves[0].Find(id); }
};

FightSetup setupFor(const Rig& rig) {
    FightSetup s{};
    s.data = &rig.build.data;
    s.start.seed = kSeed;
    s.start.startPosX[0] = kP0X;
    s.start.startPosX[1] = kP1X;
    return s;
}

// A per-tick script, authored by hand: hold `bits` on [from, to).
struct Script {
    std::vector<Input> in;
    explicit Script(std::size_t ticks) : in(ticks) {}
    void Hold(std::size_t from, std::size_t to, std::uint16_t bits) {
        for (std::size_t t = from; t < to && t < in.size(); ++t) in[t].bits |= bits;
    }
};

// Both fighters' poses for one tick, as bytes.
struct TickPoses {
    PoseRequest p[2];
};

bool samePose(const PoseRequest& a, const PoseRequest& b) {
    return std::memcmp(&a, &b, sizeof(PoseRequest)) == 0;
}

// Equal in everything but the tick -- what a frozen pose looks like.
bool samePoseIgnoringTick(PoseRequest a, PoseRequest b) {
    a.tick = 0;
    b.tick = 0;
    return samePose(a, b);
}

std::string describe(const PoseRequest& r) {
    std::string s = PoseKindName(r.kind);
    s += " slot=" + std::to_string(r.moveSlot) + " frame=" + std::to_string(r.frame) +
         " remaining=" + std::to_string(r.remaining) + " tick=" + std::to_string(r.tick) +
         " posX=" + std::to_string(r.posXSub) + " posY=" + std::to_string(r.posYSub) +
         " mirror=" + std::to_string(r.mirror) + " visible=" + std::to_string(r.visible);
    return s;
}

// Runs `ticks` ticks of a session driven by two scripts, recording the pose of
// both slots after every tick. `onTick`, if given, is called after each tick with
// the session so a section can snapshot or inspect.
template <typename F>
std::vector<TickPoses> run(FightSession& session, std::size_t ticks, F&& onTick) {
    std::vector<TickPoses> poses;
    poses.reserve(ticks);
    for (std::size_t t = 0; t < ticks; ++t) {
        session.Tick();
        TickPoses tp{};
        tp.p[0] = SelectPose(session.Data(), session.State(), 0);
        tp.p[1] = SelectPose(session.Data(), session.State(), 1);
        poses.push_back(tp);
        onTick(session, t);
    }
    return poses;
}

std::vector<TickPoses> run(FightSession& session, std::size_t ticks) {
    return run(session, ticks, [](FightSession&, std::size_t) {});
}

// The one script section 1 and section 7 share: a jab, a heavy, a walk, a jump
// with an aerial, a sweep -- every kind this file can reach from the pad. The
// dummy holds nothing and takes it.
constexpr std::size_t kScriptTicks = 300;

Script attackerScript() {
    Script s(kScriptTicks);
    s.Hold(5,   7,   cse::kernel::kInputLP);                              // jab -> Move, hitstop, dummy hitstun
    s.Hold(40,  42,  cse::kernel::kInputHP);                              // heavy
    s.Hold(90,  120, cse::kernel::kInputRight);                           // walk forward
    s.Hold(120, 140, cse::kernel::kInputLeft);                            // walk back
    s.Hold(150, 152, cse::kernel::kInputUp);                              // jump
    s.Hold(158, 160, cse::kernel::kInputMP);                              // air_mp while rising
    s.Hold(200, 230, cse::kernel::kInputDown);                            // crouch
    s.Hold(206, 212, cse::kernel::kInputRight);                           // ... and crouch-walk
    s.Hold(222, 224, cse::kernel::kInputDown | cse::kernel::kInputHK);    // sweep -> knockdown
    return s;
}

// What a FREE fighter (no move, no stun, no knockdown, round in progress) must
// be posed as, read straight off the kernel's fields. Section 1 holds every such
// tick of a real run to this, so an inverted air test or a facing-blind walk
// cannot hide behind "both kinds were seen".
PoseKind freeOracle(const cse::kernel::Fighter& f) {
    if (f.airborne != 0)  return f.velY > 0 ? PoseKind::JumpRise : PoseKind::JumpFall;
    if (f.crouching != 0) return f.velX != 0 ? PoseKind::CrouchWalk : PoseKind::CrouchIdle;
    if (f.velX != 0)      return ((f.velX > 0) == (f.facing == 0)) ? PoseKind::WalkFwd : PoseKind::WalkBack;
    return PoseKind::Idle;
}

bool isFree(const GameState& s, const cse::kernel::Fighter& f) {
    return f.moveId == 0 && f.hitstun == 0 && f.blockstun == 0 && f.knockdown == 0 &&
           s.roundState == cse::kernel::kRoundFighting;
}

bool sawKind(const std::vector<TickPoses>& poses, int slot, PoseKind kind) {
    for (const TickPoses& tp : poses)
        if (tp.p[slot].kind == kind) return true;
    return false;
}

std::string kindsSeen(const std::vector<TickPoses>& poses, int slot) {
    std::string out;
    PoseKind last = PoseKind::None;
    bool first = true;
    for (const TickPoses& tp : poses) {
        if (first || tp.p[slot].kind != last) {
            if (!first) out += " > ";
            out += PoseKindName(tp.p[slot].kind);
            last  = tp.p[slot].kind;
            first = false;
        }
    }
    return out;
}

} // namespace

// ============================================================================
// 1. RESTORE AND RE-SIMULATE REPRODUCE EVERY POSE
// ============================================================================
//
// The rollback host's question. Run the script, snapshot partway, run to the
// end, restore, run the same ticks again: every PoseRequest must come back byte
// for byte. Nothing in SelectPose may depend on the path taken to a state.
TEST(PoseSelect, RestoreAndResimulateReproduceEveryPose) {
    Rig rig;
    rig.Load();
    if (HasFatalFailure()) return;

    const Script attacker = attackerScript();
    const Script dummy(kScriptTicks);
    ScriptedInputSource src0(attacker.in, 0, "P0");
    ScriptedInputSource src1(dummy.in, 0, "P1");

    FightSession session;
    std::string  error;
    ASSERT_TRUE(session.Begin(setupFor(rig), error)) << error;
    session.SetInputSource(0, &src0);
    session.SetInputSource(1, &src1);

    // Every tick's state is kept, so the sweep below can restore to ANY tick --
    // including the ticks just after a move ends, which is where a hidden
    // "hold the last pose for N ticks" memo would live.
    std::vector<GameState> states;
    states.reserve(kScriptTicks);
    const std::vector<TickPoses> first = run(session, kScriptTicks,
        [&](FightSession& s, std::size_t) { states.push_back(s.State()); });
    ASSERT_EQ(states.size(), kScriptTicks);

    // The script must have reached the kinds this test is about, or the
    // comparison below is over an idle room.
    for (PoseKind k : { PoseKind::Move, PoseKind::WalkFwd, PoseKind::WalkBack,
                        PoseKind::JumpRise, PoseKind::JumpFall, PoseKind::CrouchIdle,
                        PoseKind::CrouchWalk })
        EXPECT_TRUE(sawKind(first, 0, k))
            << "the attacker never wore " << PoseKindName(k) << "; kinds seen: "
            << kindsSeen(first, 0);
    for (PoseKind k : { PoseKind::HitstunStand, PoseKind::Knockdown, PoseKind::Idle })
        EXPECT_TRUE(sawKind(first, 1, k))
            << "the dummy never wore " << PoseKindName(k) << "; kinds seen: "
            << kindsSeen(first, 1);

    // THE ORACLE: on every free tick the kind is the one the kernel's own fields
    // dictate, and the carried numbers are the fighter's, both slots.
    for (std::size_t t = 0; t < kScriptTicks; ++t) {
        for (int slot = 0; slot < 2; ++slot) {
            const cse::kernel::Fighter& f = states[t].p[slot];
            const PoseRequest&          r = first[t].p[slot];
            EXPECT_EQ(r.mirror, f.facing)  << "tick " << t << " slot " << slot << ": " << describe(r);
            EXPECT_EQ(r.posXSub, f.posX)   << "tick " << t << " slot " << slot << ": " << describe(r);
            EXPECT_EQ(r.posYSub, f.posY)   << "tick " << t << " slot " << slot << ": " << describe(r);
            EXPECT_EQ(r.visible, 1)        << "tick " << t << " slot " << slot << ": " << describe(r);
            EXPECT_EQ(r.tick, states[t].tick);
            if (isFree(states[t], f))
                EXPECT_EQ(r.kind, freeOracle(f))
                    << "tick " << t << " slot " << slot << ": free fighter posed " << describe(r)
                    << " but airborne=" << int(f.airborne) << " velY=" << f.velY
                    << " crouching=" << int(f.crouching) << " velX=" << f.velX
                    << " facing=" << int(f.facing);
        }
    }

    // THE SWEEP: restore to every third tick, check the restored tick's own pose
    // at once, then re-run a few ticks and compare. A memo keyed on transitions
    // would be left in whatever state the previous iteration ended in and
    // diverge here.
    for (std::size_t k = 1; k < kScriptTicks; k += 3) {
        session.Restore(states[k - 1]);
        ASSERT_EQ(session.CurrentTick(), k);
        for (int slot = 0; slot < 2; ++slot) {
            const PoseRequest now = SelectPose(session.Data(), session.State(), slot);
            EXPECT_TRUE(samePose(now, first[k - 1].p[slot]))
                << "restored to tick " << (k - 1) << ", slot " << slot
                << " posed differently:\n  first   : " << describe(first[k - 1].p[slot])
                << "\n  restored: " << describe(now);
        }
        const std::size_t ahead = kScriptTicks - k < 8 ? kScriptTicks - k : 8;
        const std::vector<TickPoses> again = run(session, ahead);
        for (std::size_t i = 0; i < again.size(); ++i)
            for (int slot = 0; slot < 2; ++slot)
                EXPECT_TRUE(samePose(again[i].p[slot], first[k + i].p[slot]))
                    << "tick " << (k + i) << " slot " << slot << " after a Restore to tick "
                    << (k - 1) << ":\n  first : " << describe(first[k + i].p[slot])
                    << "\n  second: " << describe(again[i].p[slot])
                    << "\nSelectPose is reading something that is not in GameState.";
    }

    // And the long re-run from one early snapshot, end to end.
    constexpr std::size_t kSnapshotAfter = 60;
    session.Restore(states[kSnapshotAfter - 1]);
    ASSERT_EQ(session.CurrentTick(), kSnapshotAfter);
    const std::vector<TickPoses> second = run(session, kScriptTicks - kSnapshotAfter);
    ASSERT_EQ(second.size(), first.size() - kSnapshotAfter);
    for (std::size_t i = 0; i < second.size(); ++i)
        for (int slot = 0; slot < 2; ++slot)
            EXPECT_TRUE(samePose(first[kSnapshotAfter + i].p[slot], second[i].p[slot]))
                << "tick " << (kSnapshotAfter + i) << " slot " << slot
                << " posed differently after the long Restore:\n  first : "
                << describe(first[kSnapshotAfter + i].p[slot])
                << "\n  second: " << describe(second[i].p[slot]);
}

// ============================================================================
// 2. WHILE A MOVE RUNS THE FRAME IS THE MOVE FRAME
// ============================================================================
TEST(PoseSelect, WhileAMoveRunsTheFrameIsTheMoveFrame) {
    Rig rig;
    rig.Load();
    if (HasFatalFailure()) return;

    Script attacker(60);
    attacker.Hold(5, 7, cse::kernel::kInputLP);
    const Script dummy(60);
    ScriptedInputSource src0(attacker.in, 0, "P0");
    ScriptedInputSource src1(dummy.in, 0, "P1");

    FightSession session;
    std::string  error;
    ASSERT_TRUE(session.Begin(setupFor(rig), error)) << error;
    session.SetInputSource(0, &src0);
    session.SetInputSource(1, &src1);

    const std::uint16_t lp = rig.Slot("stand_lp");
    const cse::kernel::MoveDef* def = cse::kernel::MoveAt(rig.build.data.p[0], lp);
    ASSERT_NE(def, nullptr);
    const std::int32_t duration = cse::kernel::MoveDuration(*def);

    int moveTicks = 0;
    bool sawFrameZero = false;
    for (std::size_t t = 0; t < 60; ++t) {
        session.Tick();
        const cse::kernel::Fighter& f = session.State().p[0];
        const PoseRequest r = SelectPose(session.Data(), session.State(), 0);
        if (f.moveId != 0) {
            ++moveTicks;
            EXPECT_EQ(r.kind, PoseKind::Move) << "tick " << t << ": " << describe(r);
            EXPECT_EQ(r.moveSlot, f.moveId) << "tick " << t << ": " << describe(r);
            EXPECT_EQ(r.frame, f.moveFrame)
                << "tick " << t << ": the pose frame is not the kernel's moveFrame: " << describe(r);
            if (f.moveFrame == 0) sawFrameZero = true;
        } else {
            EXPECT_NE(r.kind, PoseKind::Move)
                << "tick " << t << ": a Move pose with no move running: " << describe(r);
        }
    }
    EXPECT_TRUE(sawFrameZero) << "the move's first tick never posed frame 0 -- a move start "
                                 "would be one frame late on screen";
    // stand_lp is not cancelled and connects once, so it runs its full duration
    // exactly once. Hitstop pauses moveFrame but the kernel keeps moveId set, so
    // the move is WORN for duration + hitstop ticks; assert at least the duration.
    EXPECT_GE(moveTicks, duration)
        << "stand_lp was worn for " << moveTicks << " ticks against a " << duration
        << "-tick duration";
}

// ============================================================================
// 3. HITSTOP FREEZES THE POSE
// ============================================================================
//
// The kernel freezes moveFrame, the stun clocks and the physics while hitstop
// runs (Simulate.cpp). The pose reads those fields and nothing else, so it must
// freeze with them -- both the attacker's frame and the defender's reaction.
TEST(PoseSelect, HitstopFreezesThePose) {
    Rig rig;
    rig.Load();
    if (HasFatalFailure()) return;

    Script attacker(60);
    attacker.Hold(5, 7, cse::kernel::kInputLP);
    const Script dummy(60);
    ScriptedInputSource src0(attacker.in, 0, "P0");
    ScriptedInputSource src1(dummy.in, 0, "P1");

    FightSession session;
    std::string  error;
    ASSERT_TRUE(session.Begin(setupFor(rig), error)) << error;
    session.SetInputSource(0, &src0);
    session.SetInputSource(1, &src1);

    int frozenPairs = 0;
    TickPoses prev{};
    bool prevInHitstop = false;
    for (std::size_t t = 0; t < 60; ++t) {
        session.Tick();
        TickPoses now{};
        now.p[0] = SelectPose(session.Data(), session.State(), 0);
        now.p[1] = SelectPose(session.Data(), session.State(), 1);
        const bool inHitstop = session.State().p[0].hitstop > 0 && session.State().p[1].hitstop > 0;
        if (inHitstop && prevInHitstop) {
            ++frozenPairs;
            for (int slot = 0; slot < 2; ++slot)
                EXPECT_TRUE(samePoseIgnoringTick(prev.p[slot], now.p[slot]))
                    << "tick " << t << " slot " << slot << " moved during hitstop:\n  before: "
                    << describe(prev.p[slot]) << "\n  after : " << describe(now.p[slot]);
        }
        prev          = now;
        prevInHitstop = inHitstop;
    }
    EXPECT_GE(frozenPairs, 2)
        << "the jab never put both fighters in hitstop for two consecutive ticks; "
           "fighter_a's stand_lp authors hitstop 8, so the hit did not connect";
}

// ============================================================================
// 4. THE PRECEDENCE IS THE KERNEL'S
// ============================================================================
//
// From a state a real session produced mid-move, flip one field at a time.
TEST(PoseSelect, KnockdownOutranksStunOutranksMoveOutranksFree) {
    Rig rig;
    rig.Load();
    if (HasFatalFailure()) return;

    Script attacker(20);
    attacker.Hold(5, 7, cse::kernel::kInputLP);
    const Script dummy(20);
    ScriptedInputSource src0(attacker.in, 0, "P0");
    ScriptedInputSource src1(dummy.in, 0, "P1");

    FightSession session;
    std::string  error;
    ASSERT_TRUE(session.Begin(setupFor(rig), error)) << error;
    session.SetInputSource(0, &src0);
    session.SetInputSource(1, &src1);
    for (int t = 0; t < 7; ++t) session.Tick();
    ASSERT_NE(session.State().p[0].moveId, 0) << "the jab did not start by tick 7";

    GameState s = session.State();
    const MatchData& data = session.Data();
    cse::kernel::Fighter& f = s.p[0];

    f.knockdown = 5;
    f.hitstun   = 3;
    f.blockstun = 2;
    EXPECT_EQ(SelectPose(data, s, 0).kind, PoseKind::Knockdown);
    EXPECT_EQ(SelectPose(data, s, 0).remaining, 5);

    f.knockdown = 0;
    EXPECT_EQ(SelectPose(data, s, 0).kind, PoseKind::HitstunStand)
        << "grounded hitstun with a move id still set must read as hitstun: the hit "
           "interrupted the move";
    EXPECT_EQ(SelectPose(data, s, 0).remaining, 3);

    f.hitstun = 0;
    EXPECT_EQ(SelectPose(data, s, 0).kind, PoseKind::BlockstunStand);
    EXPECT_EQ(SelectPose(data, s, 0).remaining, 2);

    f.blockstun = 0;
    const PoseRequest move = SelectPose(data, s, 0);
    EXPECT_EQ(move.kind, PoseKind::Move) << describe(move);
    EXPECT_EQ(move.moveSlot, f.moveId);
    EXPECT_EQ(move.frame, f.moveFrame);

    f.moveId    = 0;
    f.moveFrame = 0;
    f.velX      = 0;
    f.crouching = 0;
    EXPECT_EQ(SelectPose(data, s, 0).kind, PoseKind::Idle);

    // An airborne hit reaction reads as the air variant; a launched body keeps
    // reading as airborne hitstun until it lands.
    f.hitstun  = 12;
    f.airborne = 1;
    f.posY     = px(20);
    EXPECT_EQ(SelectPose(data, s, 0).kind, PoseKind::HitstunAir);

    // A slot the match does not simulate is invisible and poseless.
    const PoseRequest none = SelectPose(data, s, static_cast<std::uint8_t>(s.fighterCount));
    EXPECT_EQ(none.kind, PoseKind::None) << describe(none);
    EXPECT_EQ(none.visible, 0);
}

// ============================================================================
// 5. A RELEASED GUARD MID-BLOCKSTUN FALLS BACK TO THE STANDING BLOCK
// ============================================================================
//
// resolveGuard recomputes guard from held input every tick and blockstun does
// not forbid it, so a defender who releases Down mid-blockstun really is
// standing-blocking the next hit -- and so must the pose. No inference from
// history, no "was crouching" memo.
//
// TWO HALVES, AND WHY THE SECOND IS A MUTATION. The session half performs a real
// crouch-block on the real kernel: the dummy holds Down, then back as the jab
// arrives (holding back is also walking back -- a dummy that backs off from tick
// 0 is out of the jab's reach before its active frames, which is how this test's
// first draft found nothing), and the contact is proven by the hitstop freeze,
// the attacker's BLOCKED flag bit and the absence of hitstun. It does NOT drive
// blockstun, because THE SHIPPED GAME HAS NONE: CharacterData reads
// `blockstun_ticks` (CharacterData.h HitWindow::blockstunTicks) and Combat.cpp
// applies MoveDef::blockstun, but MatchBuilder never carries the one into the
// other and has no loss-ledger row saying so -- found by this test, 2026-09-02,
// recorded as ROADMAP M3.0b. Until that lands, the selector's blockstun rule is
// pinned from a state the session produced with blockstun set to what the file
// authors, so this test keeps meaning the same thing before and after the fix.
TEST(PoseSelect, AReleasedGuardMidBlockstunFallsBackToTheStandingBlock) {
    Rig rig;
    rig.Load();
    if (HasFatalFailure()) return;

    Script attacker(40);
    attacker.Hold(5, 7, cse::kernel::kInputLP);
    Script dummy(40);
    dummy.Hold(0, 12, cse::kernel::kInputDown);    // crouch-block through the hit, then stand
    dummy.Hold(7, 40, cse::kernel::kInputRight);   // back, for a fighter facing -X
    ScriptedInputSource src0(attacker.in, 0, "P0");
    ScriptedInputSource src1(dummy.in, 0, "P1");

    FightSession session;
    std::string  error;
    ASSERT_TRUE(session.Begin(setupFor(rig), error)) << error;
    session.SetInputSource(0, &src0);
    session.SetInputSource(1, &src1);

    // The per-tick trace rides on every failure message: a block test that fails
    // can fail because the jab hit, whiffed, or landed on a tick the dummy was
    // not yet holding back, and the numbers tell which.
    std::string trace;
    bool blockedContact = false;
    GameState atContact{};
    for (std::size_t t = 0; t < 40 && !blockedContact; ++t) {
        session.Tick();
        const cse::kernel::Fighter& a = session.State().p[0];
        const cse::kernel::Fighter& d = session.State().p[1];
        trace += "\n  t=" + std::to_string(t) + " atk move=" + std::to_string(a.moveId) +
                 "/" + std::to_string(a.moveFrame) + " flags=" + std::to_string(a.flags) +
                 " dummy hitstun=" + std::to_string(d.hitstun) +
                 " blockstun=" + std::to_string(d.blockstun) + " guard=" + std::to_string(d.guard) +
                 " crouch=" + std::to_string(d.crouching) +
                 " gapPx=" + std::to_string((d.posX - a.posX) / cse::kernel::kSubUnitsPerPixel);
        if (a.hitstop == 0) continue;
        // Contact. The attacker's blocked-mirror bit for slot 1 says it was STOPPED.
        const bool blockedBit = (a.flags & cse::kernel::kFlagsBlockedBits & (1u << 1)) != 0;
        EXPECT_TRUE(blockedBit) << "the jab connected but the kernel did not record a block" << trace;
        EXPECT_EQ(d.hitstun, 0) << "the jab hit a crouch-blocking dummy clean" << trace;
        EXPECT_EQ(d.guard, cse::kernel::kGuardLow)
            << "the guard the block was resolved with is not the low guard the pad holds" << trace;
        blockedContact = true;
        atContact      = session.State();
    }
    ASSERT_TRUE(blockedContact) << "the jab never made contact with the crouch-blocking dummy" << trace;

    // Now the rule, on the state the block produced, with the blockstun the file
    // authors (fighter_a's stand_lp: blockstun 10). Hitstop is cleared so
    // resolveGuard's own early return does not hide the guard.
    const MatchData& data = session.Data();
    GameState s = atContact;
    cse::kernel::Fighter& d = s.p[1];
    d.hitstop   = 0;
    d.blockstun = 10;

    d.guard = cse::kernel::kGuardLow;
    PoseRequest r = SelectPose(data, s, 1);
    EXPECT_EQ(r.kind, PoseKind::BlockstunCrouch) << describe(r);
    EXPECT_EQ(r.remaining, 10);

    d.guard = cse::kernel::kGuardHigh;   // Down released, back still held
    r = SelectPose(data, s, 1);
    EXPECT_EQ(r.kind, PoseKind::BlockstunStand)
        << "a released Down mid-blockstun is a standing block in the kernel and must be on screen: "
        << describe(r);

    d.guard = cse::kernel::kGuardNone;   // back released too: still in blockstun, still standing
    r = SelectPose(data, s, 1);
    EXPECT_EQ(r.kind, PoseKind::BlockstunStand) << describe(r);

    // And the crouch that IS in the state does not smuggle itself in on an
    // unfrozen tick: when the kernel has resolved a guard, the guard decides the
    // block's height, not the posture bit.
    d.crouching = 1;
    r = SelectPose(data, s, 1);
    EXPECT_EQ(r.kind, PoseKind::BlockstunStand)
        << "on an unfrozen tick the block's height is the GUARD's, not the posture bit's: "
        << describe(r);

    // THE FROZEN TICKS, ON THE KERNEL ITSELF. resolveGuard zeroes guard on every
    // tick that starts in hitstop, without reading the pad; StepPhysics returns
    // on the same ticks before its posture write, so `crouching` survives. From
    // the contact state, give the dummy the blockstun the file authors (what
    // M3.0b will carry), keep the hitstop the kernel set, hold Down + back, and
    // drive Simulate through the freeze: the pose must be BlockstunCrouch on
    // every frozen tick -- guard None, crouching 1 -- and still crouch on the
    // first unfrozen tick, when the pad resolves Low again. A selector reading
    // guard alone flickers crouch -> stand -> crouch here.
    GameState frozen = atContact;
    frozen.p[1].blockstun = 10;
    ASSERT_GT(frozen.p[1].hitstop, 0) << "the contact state carries no hitstop to drive through";
    cse::kernel::InputPair held{};
    held.p[1].bits = cse::kernel::kInputDown | cse::kernel::kInputRight;
    int frozenTicks = 0;
    while (frozen.p[1].hitstop > 0 && frozenTicks < 64) {
        const PoseRequest fr = SelectPose(data, frozen, 1);
        EXPECT_EQ(fr.kind, PoseKind::BlockstunCrouch)
            << "frozen tick " << frozenTicks << " (hitstop " << frozen.p[1].hitstop
            << ", guard " << int(frozen.p[1].guard) << ", crouching " << int(frozen.p[1].crouching)
            << "): " << describe(fr);
        cse::kernel::Simulate(frozen, held, data);
        ++frozenTicks;
    }
    EXPECT_GE(frozenTicks, 2) << "the freeze did not last long enough to test";
    ASSERT_GT(frozen.p[1].blockstun, 0) << "blockstun burned during the freeze; the kernel changed";
    const PoseRequest thawed = SelectPose(data, frozen, 1);
    EXPECT_EQ(frozen.p[1].guard, cse::kernel::kGuardLow) << "the pad is still Down + back";
    EXPECT_EQ(thawed.kind, PoseKind::BlockstunCrouch) << "first unfrozen tick: " << describe(thawed);

    // A STANDING block under the same freeze stays standing: crouching is 0, so
    // the fallback has nothing to say and guard None reads as the standing block.
    GameState frozenStand = atContact;
    frozenStand.p[1].blockstun = 10;
    frozenStand.p[1].crouching = 0;
    frozenStand.p[1].guard     = cse::kernel::kGuardNone;
    ASSERT_GT(frozenStand.p[1].hitstop, 0);
    r = SelectPose(data, frozenStand, 1);
    EXPECT_EQ(r.kind, PoseKind::BlockstunStand) << describe(r);
}

// ============================================================================
// 6. A FIGHTER WHO ACTS AFTER ROUND OVER IS POSED BY ITS ACTION
// ============================================================================
//
// Kernel fact (Simulate.cpp stepRound): a KO sets roundState = kRoundOver even
// with roundsToWin 0, and that is the ONLY stage the round state gates -- both
// fighters keep walking and attacking, and training never refills health. So Ko
// and Win may only dress a fighter who is doing nothing; the moment either acts,
// the action wins.
TEST(PoseSelect, AFighterWhoActsAfterRoundOverIsPosedByItsAction) {
    Rig rig;
    rig.Load();
    if (HasFatalFailure()) return;

    FightSession session;
    std::string  error;
    ASSERT_TRUE(session.Begin(setupFor(rig), error)) << error;
    session.Tick();

    GameState s = session.State();
    const MatchData& data = session.Data();
    s.roundState  = cse::kernel::kRoundOver;
    s.p[1].health = 0;
    // The shape of a KO in a TIMED round: the timer is still above zero, because
    // stepRound decrements it only while no team is out. The training session
    // is untimed (timer 0 from the start) and can leave kRoundFighting only by a
    // KO, so "round over, timer 0, every team standing" -- the time-out below --
    // is the ONLY state a zero timer can mean once the round is decided.
    s.roundTimer  = 60;

    // Idle loser, idle winner.
    EXPECT_EQ(SelectPose(data, s, 1).kind, PoseKind::Ko) << describe(SelectPose(data, s, 1));
    EXPECT_EQ(SelectPose(data, s, 0).kind, PoseKind::Win) << describe(SelectPose(data, s, 0));

    // A team is out when its LAST body is out, and benched partners count
    // (stepRound's own words). A benched team-1 partner with health left keeps
    // team 1 alive, so slot 0 is NOT a winner -- a selector reading only the
    // opposing SLOT's health would say Win here.
    s.fighterCount  = 3;
    s.p[2]          = s.p[1];
    s.p[2].active   = 0;
    s.p[2].health   = 500;
    EXPECT_EQ(SelectPose(data, s, 0).kind, PoseKind::Idle)
        << "a benched partner with health left keeps the team alive: " << describe(SelectPose(data, s, 0));
    s.p[2].health = 0;
    EXPECT_EQ(SelectPose(data, s, 0).kind, PoseKind::Win) << describe(SelectPose(data, s, 0));
    EXPECT_EQ(SelectPose(data, s, 2).kind, PoseKind::None) << "a benched body is poseless";
    s.fighterCount = 2;

    // The winner walks: the walk wins.
    s.p[0].velX = data.p[0].walkSpeedSub;
    EXPECT_EQ(SelectPose(data, s, 0).kind, PoseKind::WalkFwd)
        << "a winner who walks after the KO must be posed by the walk, not frozen in "
           "a win pose the sim is not performing: " << describe(SelectPose(data, s, 0));
    s.p[0].velX = 0;

    // The loser attacks (training keeps simulating): the move wins.
    s.p[1].moveId    = rig.Slot("stand_lp");
    s.p[1].moveFrame = 2;
    EXPECT_EQ(SelectPose(data, s, 1).kind, PoseKind::Move) << describe(SelectPose(data, s, 1));
    s.p[1].moveId    = 0;
    s.p[1].moveFrame = 0;

    // A crouching or airborne loser is posed by the posture, not by Ko.
    s.p[1].crouching = 1;
    EXPECT_EQ(SelectPose(data, s, 1).kind, PoseKind::CrouchIdle);
    s.p[1].crouching = 0;

    // A TIME-OUT: the round is decided with every team standing and the timer at
    // zero. stepRound gives it to the side with more health, so the pose does
    // too -- and only exactly equal totals are the kernel's draw, which poses
    // nobody.
    s.roundTimer  = 0;
    s.p[0].health = 1000;
    s.p[1].health = 300;
    EXPECT_EQ(SelectPose(data, s, 0).kind, PoseKind::Win)
        << "the kernel scored this time-out for team 0: " << describe(SelectPose(data, s, 0));
    EXPECT_EQ(SelectPose(data, s, 1).kind, PoseKind::Idle)
        << "the time-out loser is alive and idle, not KO'd: " << describe(SelectPose(data, s, 1));
    s.p[0].health = 300;
    EXPECT_EQ(SelectPose(data, s, 0).kind, PoseKind::Idle) << "equal totals are the draw";
    EXPECT_EQ(SelectPose(data, s, 1).kind, PoseKind::Idle) << "equal totals are the draw";

    // While the round is still being fought, a fighter at 0 health who has not
    // been ruled out yet is posed by state, not by health.
    s.roundState  = cse::kernel::kRoundFighting;
    s.p[1].health = 0;
    EXPECT_EQ(SelectPose(data, s, 1).kind, PoseKind::Idle);

    // FACING DECIDES FORWARD AND BACK. The dummy faces -X (mirror 1): walking
    // toward -X is walking forward for it, and toward +X is backing off.
    s.p[1].health = 1000;
    s.p[1].velX   = -data.p[1].walkSpeedSub;
    PoseRequest w = SelectPose(data, s, 1);
    EXPECT_EQ(w.kind, PoseKind::WalkFwd)  << describe(w);
    EXPECT_EQ(w.mirror, 1)                << describe(w);
    s.p[1].velX = data.p[1].walkSpeedSub;
    w = SelectPose(data, s, 1);
    EXPECT_EQ(w.kind, PoseKind::WalkBack) << describe(w);
    s.p[1].crouching = 1;
    w = SelectPose(data, s, 1);
    EXPECT_EQ(w.kind, PoseKind::CrouchWalk) << describe(w);
    s.p[1].velX = 0;
    w = SelectPose(data, s, 1);
    EXPECT_EQ(w.kind, PoseKind::CrouchIdle) << describe(w);
}

// ============================================================================
// 7. ASKING NEVER TOUCHES THE STATE
// ============================================================================
TEST(PoseSelect, NeverTouchesTheChecksum) {
    Rig rig;
    rig.Load();
    if (HasFatalFailure()) return;

    const Script attacker = attackerScript();
    const Script dummy(kScriptTicks);
    ScriptedInputSource a0(attacker.in, 0, "P0"), a1(dummy.in, 0, "P1");
    ScriptedInputSource b0(attacker.in, 0, "P0"), b1(dummy.in, 0, "P1");

    FightSession asked, unasked;
    std::string  error;
    ASSERT_TRUE(asked.Begin(setupFor(rig), error)) << error;
    ASSERT_TRUE(unasked.Begin(setupFor(rig), error)) << error;
    asked.SetInputSource(0, &a0);
    asked.SetInputSource(1, &a1);
    unasked.SetInputSource(0, &b0);
    unasked.SetInputSource(1, &b1);

    for (std::size_t t = 0; t < kScriptTicks; ++t) {
        asked.Tick();
        unasked.Tick();

        GameState before{};
        asked.Snapshot(before);
        (void)SelectPose(asked.Data(), asked.State(), 0);
        (void)SelectPose(asked.Data(), asked.State(), 1);
        (void)SelectPose(asked.Data(), asked.State(), 7);
        GameState after{};
        asked.Snapshot(after);
        ASSERT_EQ(std::memcmp(&before, &after, sizeof(GameState)), 0)
            << "tick " << t << ": SelectPose changed the state's bytes";

        ASSERT_EQ(asked.Checksum(), unasked.Checksum())
            << "tick " << t << ": the session that was asked for poses diverged from "
               "the one that was not";
    }
}
