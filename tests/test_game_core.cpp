// THE HEADLESS GAME CORE, EXERCISED THROUGH THE SEAM RATHER THAN AROUND IT.
//
// Game/ is the layer between "a character file was loaded" and "a fight
// happened": where a tick's bits come from (InputSource.h), the one object that
// calls Simulate (FightSession.h), the recording (Replay.h), and the live
// judgement of what the player just did against what the combo prover said they
// could do (ComboWatcher.h). This file is the proof that each of those does what
// its header says, with no window, no GL context and no Engine on the link line
// -- which is the property Games/UntitledFighter/Game/CMakeLists.txt makes a
// configure-time failure to lose, and the reason every other claim in this
// repository was cheap to re-run.
//
// ---------------------------------------------------------------------------
// THE SEVEN CLAIMS, IN THE ORDER OF HOW MUCH THEY WOULD HURT TO GET WRONG
// ---------------------------------------------------------------------------
//   1. RECORD -> PLAYBACK IS BIT-IDENTICAL, at every checkpoint and at the end.
//      Section 4. The whole feature rests on it and it is only true because the
//      kernel is deterministic -- which is why the checkpoints are compared
//      one by one rather than a single final hash being waved at.
//   2. A TAMPERED REPLAY IS REFUSED, NOT CRASHED. Section 5, including a seeded
//      mutation sweep whose generator is written out here rather than taken
//      from <random>, so that a failure reproduces verbatim on both toolchains.
//   3. A CONTENT-HASH MISMATCH IS REFUSED AND SAYS WHY, naming the character
//      rather than implying corruption. Section 6.
//   4. THE SCRIPTED SOURCE PERFORMS THE PROVER'S PRINTED LOOP -- the same claim
//      tests/test_ground_truth.cpp makes by hand, made here through the reusable
//      seam, and cross-checked against that file's own closed-loop driver tick
//      for tick. Section 3.
//   5. EXHAUSTION HANDS CONTROL BACK. Section 7: "Demonstrate, then you try".
//   6. THE WATCHER SEES A SELF-CANCEL LOOP, and the assertion is the COUNT.
//      Section 8.
//   7. THE WATCHER FLAGS A CYCLE THE PROVER FOUND -- both the printed loop of
//      the infinite character, and one of the cycles fighter_a's ranking
//      certificate retires that the kernel performs anyway. Section 8.
//
// Sections 1 and 2 come before all of them and carry no claim number: they are
// the input-source contract and the session contract, and every claim above is
// stated in terms of one or both.
//
// ---------------------------------------------------------------------------
// WHAT THIS FILE DELIBERATELY DUPLICATES, AND WHY THAT IS NOT WASTE
// ---------------------------------------------------------------------------
// The witness reader and the closed-loop `Driver` below are copied from
// tests/test_ground_truth.cpp on purpose and must NOT be refactored into a
// shared header. That file is the REFERENCE: it drove the kernel by hand,
// before any of this existed, and its trace is the thing the new seam has to
// reproduce. A shared implementation would make section 3 compare the seam
// against itself, which is the shape of a test that cannot fail.
//
// Section 3 asserts the two agree BYTE FOR BYTE -- the same input bits on the
// same ticks and the same final GameState. If they ever disagree, the seam is
// wrong and the failure says so in those words rather than reporting a smaller
// hit count.
//
// ---------------------------------------------------------------------------
// AND WHAT IT DOES NOT ASSERT
// ---------------------------------------------------------------------------
// Nothing here asserts a wall-clock duration, a frame rate, or an allocation
// count. The headers promise no clock in a tick path and this file cannot prove
// a negative by timing it; what it CAN do, and does, is drive every tick through
// FightSession and re-run whole prefixes, so anything that read a clock or a
// global would show up as a checksum that did not reproduce.
#include <gtest/gtest.h>

#include "cse/game/ComboWatcher.h"
#include "cse/game/FightSession.h"
#include "cse/game/InputSource.h"
#include "cse/game/Replay.h"

#include "cse/data/CharacterData.h"
#include "cse/data/MatchBuilder.h"
#include "cse/data/ProverAdapter.h"

#include "cse/kernel/Combat.h"
#include "cse/kernel/GameState.h"
#include "cse/kernel/Simulate.h"

// Included explicitly rather than inherited. gcc is stricter than MSVC about
// transitively-included headers and CI compiles both, so every name used below
// is declared by something named here: <iterator> for istreambuf_iterator,
// <system_error> for the error_code std::filesystem::create_directories takes,
// <cstring> for memcmp and strlen.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

using namespace cse::data;
using namespace cse::game;

using cse::kernel::GameState;
using cse::kernel::Input;
using cse::kernel::InputPair;
using cse::kernel::MatchData;

namespace {

// ============================================================================
// 0. UNITS, THE STAGE, AND FINDING THE CHARACTERS
// ============================================================================

// Authored in pixels and converted once, matching test_combat.cpp,
// test_match_bridge.cpp and test_ground_truth.cpp.
constexpr std::int32_t px(std::int32_t pixels) {
    return pixels * cse::kernel::kSubUnitsPerPixel;
}

// The body is the CALLER'S number -- CharacterData carries none -- and these are
// both shipped files' own `engine.constants`, restated in the unit this test
// reads in. See the long note in test_ground_truth.cpp.
constexpr std::int32_t kHalfWidth = px(13);
constexpr std::int32_t kHeight    = px(60);

// WHERE THE TWO FIGHTERS STAND, AND WHY IT IS NOT MatchStart's DEFAULT.
// MatchStart's own opening is ResetMatch's -100px/+100px, a 174 px body-to-body
// gap -- further than anything either character reaches, so nothing would ever
// connect and every assertion in this file would be about an empty room. The
// header says so in the comment on `startPosX` and points at test_ground_truth
// for the number; these are that number. Origins 34 px apart, bodies 8 px apart,
// which is inside the reach of every move any trace here uses.
constexpr std::int32_t kP0X = -px(17);
constexpr std::int32_t kP1X =  px(17);

// Fixed so that a failing run is reproducible verbatim. It only feeds
// GameState::rng, which nothing in a combat tick reads -- but it IS in the
// checksum, so a replay that reproduced the fight and not the stream would still
// fail every assertion in section 4. That is deliberate: the checksum covers the
// whole state or it covers nothing.
constexpr std::uint32_t kSeed = 0xC0FFEEu;

// ResetMatch's opening health (Simulate.cpp), named so the damage arithmetic
// reads as a subtraction from a known start rather than as a magic 1000.
constexpr std::int32_t kStartingHealth = 1000;

// The build-wide positional resource order (ADR-001 section 8 item 7). Passing
// it is what arms load assertion A03.
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

const char* kSafe     = "fighter_a.json";
const char* kInfinite = "fighter_a_infinite.json";

// THE STAGED SHIPPING DIRECTORY, NOT THE PHASE-0 CORPUS. Same shape and same
// reasons as test_ground_truth.cpp: tests/fixtures/characters holds the MUGEN
// transcriptions, which are EVIDENCE and are deliberately never staged next to
// an executable; these two files are the project's own content and are staged to
// ${CMAKE_CURRENT_BINARY_DIR}/Exported by the shared asset-staging script.
//
// Two fallbacks, in order, and neither can make anything pass vacuously because
// every load below ASSERTs.
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

    here = fs::current_path();
    for (int i = 0; i < 8; ++i) {
        const fs::path source = here / "Editor" / "src" / "Exported" / "Characters";
        if (fs::exists(source / marker)) return source.string();
        if (!here.has_parent_path() || here.parent_path() == here) break;
        here = here.parent_path();
    }

    return "Exported/Characters";
#endif
}

void loadShipped(const char* file, CharacterData& out) {
    LoadReport report{};
    ASSERT_TRUE(LoadCharacterFile(charactersDir(), file, loadOptions(), out, report))
        << file << " did not load from " << charactersDir() << ".\n"
        << "  rule : " << (report.rule.empty() ? "(no load assertion named)" : report.rule)
        << "\n  error: " << report.error
        << "\n\nA character that does not load is the first finding; everything "
           "below this point is about a file that reached the engine.";
    ASSERT_FALSE(out.moves.empty()) << file << " loaded with no moves";
}

void analyseShipped(const CharacterData& character, ProverResult& result) {
    ProverReport report{};
    ASSERT_TRUE(AnalyseCharacter(character, proverOptions(), result, report))
        << character.id << " could not be projected into the decision procedure.\n"
        << "  rule : " << report.rule << "\n  error: " << report.error;
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

// A mirror match: both sides get the same bindings, so which of the two fighters
// acts is decided by input bits and never by data. A defender who could not act
// even if it were actionable would make "the defender never acted" a fact about
// the harness rather than about the combo.
bool buildMirror(const CharacterData& character,
                 const std::vector<MoveBinding>& bindings, MatchBuild& out) {
    BuildOptions options{};
    options.body     = body();
    options.bindings = bindings;
    return BuildMatchData(character, options, character, options, out);
}

// SIX SINGLE BITS, AND SINGLE ON PURPOSE. StepAttack takes the first move in slot
// order all of whose bits are held, so a binding whose mask is a superset of an
// earlier one's can never start. No mask below is a subset of any other, so the
// shadowing rule cannot bite any trace in this file.
const std::uint16_t kButtonPool[] = {
    cse::kernel::kInputLP, cse::kernel::kInputMP, cse::kernel::kInputHP,
    cse::kernel::kInputLK, cse::kernel::kInputMK, cse::kernel::kInputHK,
};
constexpr std::size_t kButtonPoolSize = sizeof(kButtonPool) / sizeof(kButtonPool[0]);

const char* buttonName(std::uint16_t bits) {
    switch (bits) {
        case cse::kernel::kInputLP: return "LP";
        case cse::kernel::kInputMP: return "MP";
        case cse::kernel::kInputHP: return "HP";
        case cse::kernel::kInputLK: return "LK";
        case cse::kernel::kInputMK: return "MK";
        case cse::kernel::kInputHK: return "HK";
        case 0:                     return "--";
        default:                    return "??";
    }
}

Input inputOf(std::uint16_t bits) {
    Input in{};
    in.bits = bits;
    return in;
}

InputPair pairOf(std::uint16_t p0, std::uint16_t p1) {
    InputPair pair{};
    pair.p[0].bits = p0;
    pair.p[1].bits = p1;
    return pair;
}

// ============================================================================
// 0b. THE WITNESS, AND THE CLOSED-LOOP DRIVER IT IS THE REFERENCE FOR
// ============================================================================
//
// COPIED FROM tests/test_ground_truth.cpp AND DELIBERATELY NOT SHARED. See the
// note at the top of this file: section 3 asserts that BuildDemonstration
// reproduces this driver's trace exactly, and a shared implementation would turn
// that into a comparison of the seam with itself.

struct Witness {
    std::vector<std::string> sequence;
    std::size_t              loopStart = 0;

    std::string ToString() const {
        std::ostringstream s;
        for (std::size_t i = 0; i < sequence.size(); ++i) {
            if (i) s << " > ";
            if (i == loopStart) s << "[loop] ";
            s << sequence[i];
        }
        return s.str();
    }
};

Witness witnessOf(const CharacterData& character, const ProverResult& result) {
    Witness w{};
    for (MoveIndex m : result.prefix)
        if (m < character.moves.size()) w.sequence.push_back(character.moves[m].id);
    w.loopStart = w.sequence.size();
    for (MoveIndex m : result.loop)
        if (m < character.moves.size()) w.sequence.push_back(character.moves[m].id);
    return w;
}

// A witness that is a single move repeated -- what a self-cancel produces, and
// what section 10's fighter_a case needs to build from the character's own
// cancel table.
Witness selfLoopWitness(const std::string& moveId) {
    Witness w{};
    w.sequence.push_back(moveId);
    w.loopStart = 0;
    return w;
}

// One button per DISTINCT move in the witness, in first-appearance order. A
// surplus move gets a binding of ZERO rather than being dropped, so a shortfall
// surfaces as a named refusal rather than as a trace that silently stops
// following the witness.
std::vector<MoveBinding> bindingsFor(const Witness& w) {
    std::vector<MoveBinding> out;
    for (const std::string& id : w.sequence) {
        bool already = false;
        for (const MoveBinding& b : out)
            if (b.moveId == id) { already = true; break; }
        if (already) continue;
        const std::uint16_t button =
            out.size() < kButtonPoolSize ? kButtonPool[out.size()] : std::uint16_t{0};
        out.push_back(bind(id, button));
    }
    return out;
}

// THE REFERENCE DRIVER. A cursor over the witness: press the button of the move
// the witness says comes next, and advance the cursor when the attacker actually
// ENTERS that move. Past the end the cursor returns to `loopStart`.
//
// Advancing on `moveFrame == 0` rather than on a change of `moveId` is the trap
// FightSession.h writes out for BuildDemonstration and ComboWatcher.h writes out
// again: the loop is a move cancelling into ITSELF, so the id never changes.
class Driver {
public:
    Driver(const Witness& w, const MoveIndexMap& map,
           const std::vector<MoveBinding>& bindings)
        : loopStart_(w.loopStart) {
        for (const std::string& id : w.sequence) {
            slots_.push_back(map.Find(id));
            std::uint16_t button = 0;
            for (const MoveBinding& b : bindings)
                if (b.moveId == id) { button = b.button; break; }
            buttons_.push_back(button);
            ids_.push_back(id);
        }
    }

    bool Usable(std::string& why) const {
        for (std::size_t i = 0; i < slots_.size(); ++i) {
            if (slots_[i] == 0) {
                why = "this character has no move called `" + ids_[i] + "`";
                return false;
            }
            if (buttons_[i] == 0) {
                why = "`" + ids_[i] + "` was given no button, so nothing can ask for it";
                return false;
            }
        }
        return !slots_.empty();
    }

    // Zero on a release tick -- see BuildDemonstration, which this is the
    // reference for. A witness that cancels a move into ITSELF asks for the same
    // button twice running, and a held bit is ONE press however long it lasts.
    //
    // THIS IS THE THIRD COPY OF THIS RULE (here, test_ground_truth.cpp, and
    // FightSession.cpp) and the duplication is why the seam test exists. It is
    // recorded as work rather than left as a comment -- ROADMAP M1.6.
    std::uint16_t Bits() const {
        if (release_ || buttons_.empty()) return 0;
        return buttons_[cursor_];
    }

    void Observe(std::uint16_t attackerMove, std::uint16_t attackerFrame) {
        if (slots_.empty()) return;
        if (release_) { release_ = false; return; }
        if (attackerMove != slots_[cursor_] || attackerFrame != 0) return;
        const std::uint16_t justUsed = buttons_[cursor_];
        cursor_ = (cursor_ + 1 < slots_.size()) ? cursor_ + 1 : loopStart_;
        release_ = (buttons_[cursor_] == justUsed);
    }

    const std::vector<std::uint16_t>& Slots() const { return slots_; }
    std::size_t LoopStart() const { return loopStart_; }

private:
    std::vector<std::uint16_t> slots_;
    std::vector<std::uint16_t> buttons_;
    std::vector<std::string>   ids_;
    std::size_t                loopStart_ = 0;
    std::size_t                cursor_    = 0;
    bool                       release_   = false;
};

// ============================================================================
// 0c. WATCHING A SESSION FROM OUTSIDE
// ============================================================================

// A tick log kept BY AN OBSERVER rather than by a bespoke loop.
//
// tests/test_ground_truth.cpp and tests/test_gap_extent.cpp each hand-rolled one
// of these because there was no seam; FightSession.h names that as reason 2 for
// the observer list existing at all. So this is both a test helper and the
// fourth watcher the header predicts.
//
// It ALSO re-implements the recorder's overwrite-on-re-simulation rule
// independently, which is what lets section 4 assert the recorder's file against
// something rather than against itself.
struct TickLog final : public ITickObserver {
    struct Sample {
        std::uint32_t tick        = 0;
        InputPair     inputs{};
        GameState     state{};      // AFTER the tick
        std::uint32_t checksum     = 0;
        bool          resimulated  = false;
    };

    std::vector<Sample> samples;

    // Contract violations, counted rather than asserted here: OnTick must not
    // throw and a gtest ASSERT inside an observer would unwind through the 60 Hz
    // path the header says must not throw. The tests assert these are zero.
    int nullViolations    = 0;
    int offByOneViolations = 0;
    int gapViolations     = 0;

    void OnTick(const TickView& view) override {
        if (view.state == nullptr || view.data == nullptr) { ++nullViolations; return; }
        // THE OFF-BY-ONE, ASSERTED ONCE AND FOR EVERY TICK. TickView::tick is the
        // tick that RAN, so the state it hands over is already one ahead. An
        // observer that keys a container on state->tick is off by one on every
        // entry, and this counter is what would catch the day that changes.
        if (view.state->tick != view.tick + 1u) ++offByOneViolations;

        Sample s{};
        s.tick        = view.tick;
        s.inputs      = view.inputs;
        s.state       = *view.state;
        s.checksum    = cse::kernel::Checksum(*view.state);
        s.resimulated = view.resimulated;

        if (view.tick < samples.size())        samples[view.tick] = s;
        else if (view.tick == samples.size())  samples.push_back(s);
        else                                   ++gapViolations;
    }

    void Clear() {
        samples.clear();
        nullViolations = offByOneViolations = gapViolations = 0;
    }

    bool Clean() const {
        return nullViolations == 0 && offByOneViolations == 0 && gapViolations == 0;
    }

    std::size_t Size() const { return samples.size(); }

    const GameState& Final() const { return samples.back().state; }

    // The ticks on which `slot`'s health fell. THE INDEPENDENT HIT COUNT: read
    // off the state the simulation produced, never off what a script asked for
    // and never off ComboWatcher, which is the thing being measured.
    std::vector<std::uint32_t> HitTicks(int slot) const {
        std::vector<std::uint32_t> out;
        std::int32_t previous = kStartingHealth;
        for (const Sample& s : samples) {
            if (s.state.p[slot].health < previous) out.push_back(s.tick);
            previous = s.state.p[slot].health;
        }
        return out;
    }

    // The ticks on which `slot` STARTED a move -- ComboWatcher.h signal 1, spelled
    // the way the header spells it and not as an id transition.
    std::vector<std::uint32_t> MoveStartTicks(int slot) const {
        std::vector<std::uint32_t> out;
        for (const Sample& s : samples)
            if (s.state.p[slot].moveId != 0 && s.state.p[slot].moveFrame == 0)
                out.push_back(s.tick);
        return out;
    }
};

// Two of these prove the notification ORDER the header makes part of the
// contract, and that a duplicate registration is refused rather than
// double-counted.
struct CountingObserver final : public ITickObserver {
    explicit CountingObserver(int id, std::vector<int>* sink) : id_(id), sink_(sink) {}
    void OnTick(const TickView&) override {
        ++calls;
        if (sink_ != nullptr) sink_->push_back(id_);
    }
    int calls = 0;

private:
    int               id_   = 0;
    std::vector<int>* sink_ = nullptr;
};

// A per-tick snapshot of a ComboWatcher's report, taken by an observer
// registered AFTER the watcher. That is a legitimate use of the registration
// order the header makes part of the contract: this sees the watcher's state as
// of the end of the same tick, which is the only way to assert about a flag --
// like `onWitness` -- whose value later in the string is not specified.
struct WatcherProbe final : public ITickObserver {
    explicit WatcherProbe(const ComboWatcher* watcher) : watcher_(watcher) {}

    struct Frame {
        std::uint32_t tick               = 0;
        std::int32_t  hits               = 0;
        std::int32_t  cycleRun           = 0;
        std::int32_t  loopTurnsCompleted = 0;
        bool          onWitness          = false;
        std::size_t   witnessIndex       = 0;
        bool          open               = false;
        bool          stale              = false;
    };
    std::vector<Frame> frames;

    void OnTick(const TickView& view) override {
        if (watcher_ == nullptr) return;
        const ComboReport& r = watcher_->Current();
        Frame f{};
        f.tick               = view.tick;
        f.hits               = r.hits;
        f.cycleRun           = r.cycleRun;
        f.loopTurnsCompleted = r.loopTurnsCompleted;
        f.onWitness          = r.onWitness;
        f.witnessIndex       = r.witnessIndex;
        f.open               = r.open;
        f.stale              = watcher_->Stale();
        frames.push_back(f);
    }

private:
    const ComboWatcher* watcher_ = nullptr;
};

// ============================================================================
// 0d. THE TEST'S OWN GENERATOR
// ============================================================================

// xorshift32, written out here rather than taken from <random>, for exactly the
// reason Simulate.cpp gives for the kernel's own copy: the standard library's
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
// 0e. READING THE REPLAY FORMAT BY HAND
// ============================================================================
//
// The tests below parse the encoded bytes THEMSELVES, with shifts and masks,
// against the offsets Replay.h's table names. Two reasons, and neither is
// paranoia:
//
//   * A round trip through the module's own reader and writer would pass even if
//     both agreed on the wrong layout. The header's table is the specification
//     and this is an independent implementation of it.
//   * Every hostile file in section 5 is built by editing these bytes. A test
//     that could only produce malformed files by asking the writer to misbehave
//     could not produce most of them at all.
//
// Little-endian on every platform, assembled byte by byte, exactly as the header
// requires of the implementation -- no memcpy of a scalar and no cast to a
// header struct, which would be an endianness bug and an alignment bug in one
// line.

constexpr std::size_t kOffMagic           = 0;
constexpr std::size_t kOffVersion         = 4;
constexpr std::size_t kOffStateBytes      = 6;
constexpr std::size_t kOffMatchDataHash   = 8;
constexpr std::size_t kOffSeed            = 12;
constexpr std::size_t kOffStartPosX0      = 16;
constexpr std::size_t kOffStartPosX1      = 20;
constexpr std::size_t kOffTickCount       = 24;
constexpr std::size_t kOffRunCount        = 28;
constexpr std::size_t kOffCheckpointCount = 32;
constexpr std::size_t kOffInterval        = 36;
constexpr std::size_t kOffCharacterId0    = 40;
constexpr std::size_t kOffCharacterId1    = 72;

using Bytes = std::vector<std::uint8_t>;

std::uint16_t readU16(const Bytes& b, std::size_t off) {
    if (off + 2 > b.size()) return 0;
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(b[off]) |
                                      (static_cast<std::uint16_t>(b[off + 1]) << 8));
}

std::uint32_t readU32(const Bytes& b, std::size_t off) {
    if (off + 4 > b.size()) return 0;
    return static_cast<std::uint32_t>(b[off]) |
           (static_cast<std::uint32_t>(b[off + 1]) << 8) |
           (static_cast<std::uint32_t>(b[off + 2]) << 16) |
           (static_cast<std::uint32_t>(b[off + 3]) << 24);
}

std::int32_t readI32(const Bytes& b, std::size_t off) {
    return static_cast<std::int32_t>(readU32(b, off));
}

void writeU16(Bytes& b, std::size_t off, std::uint16_t v) {
    if (off + 2 > b.size()) return;
    b[off]     = static_cast<std::uint8_t>(v & 0xFFu);
    b[off + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
}

void writeU32(Bytes& b, std::size_t off, std::uint32_t v) {
    if (off + 4 > b.size()) return;
    b[off]     = static_cast<std::uint8_t>(v & 0xFFu);
    b[off + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
    b[off + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
    b[off + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
}

std::size_t runOffset(std::uint32_t index) {
    return kReplayHeaderBytes + kReplayRunBytes * static_cast<std::size_t>(index);
}

std::size_t checkpointOffset(const Bytes& b, std::uint32_t index) {
    const std::uint32_t runCount = readU32(b, kOffRunCount);
    return kReplayHeaderBytes + kReplayRunBytes * static_cast<std::size_t>(runCount) +
           kReplayCheckpointBytes * static_cast<std::size_t>(index);
}

std::string idField(const Bytes& b, std::size_t off) {
    std::string out;
    for (std::size_t i = 0; i < kReplayCharacterIdBytes && off + i < b.size(); ++i) {
        if (b[off + i] == 0) break;
        out.push_back(static_cast<char>(b[off + i]));
    }
    return out;
}

// A place to write files. The test's working directory is the build's tests/
// dir, so this sits beside the staged Exported/ tree and is unambiguously ours.
std::string replayDir() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::current_path() / "game_core_replays";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir.string();
}

// ============================================================================
// 0f. THE RIG: a character, its verdict, a built match, and a fight to run
// ============================================================================

struct Rig {
    CharacterData            character;
    ProverResult             verdict;
    Witness                  witness;
    std::vector<MoveBinding> bindings;
    MatchBuild               build;
    FightSetup               setup;

    // The witness as KERNEL move ids, which is what DemonstrationRequest speaks:
    // BuildDemonstration has the MatchData and not the CharacterData, and reads
    // the button straight out of MoveDef::button.
    std::vector<std::uint16_t> kernelWitness;
    std::size_t                loopStart = 0;

    std::uint16_t Slot(const std::string& moveId) const {
        return build.moves[0].Find(moveId);
    }
};

// Fill everything downstream of a loaded character and a chosen witness.
void bringUpFrom(const CharacterData& character, const Witness& witness, Rig& rig) {
    rig.witness   = witness;
    rig.bindings  = bindingsFor(witness);
    ASSERT_FALSE(rig.bindings.empty());
    ASSERT_LE(rig.bindings.size(), kButtonPoolSize)
        << "the witness names " << rig.bindings.size() << " distinct moves and "
           "there are only " << kButtonPoolSize << " single-bit buttons; two "
           "moves sharing a mask would let StepAttack's first-wins rule pick the "
           "wrong one, so the trace would stop being the witness.";

    ASSERT_TRUE(buildMirror(character, rig.bindings, rig.build))
        << "p0: " << rig.build.report[0].error
        << " / p1: " << rig.build.report[1].error;

    rig.kernelWitness.clear();
    for (const std::string& id : witness.sequence) {
        const std::uint16_t slot = rig.build.moves[0].Find(id);
        ASSERT_NE(slot, 0u) << "`" << id << "` did not survive the build into the "
                               "kernel, so the witness cannot be performed at all";
        rig.kernelWitness.push_back(slot);
    }
    rig.loopStart = witness.loopStart;

    rig.setup.start.seed         = kSeed;
    rig.setup.start.startPosX[0] = kP0X;
    rig.setup.start.startPosX[1] = kP1X;
    rig.setup.data               = &rig.build.data;
}

// fighter_a_infinite, driven by its OWN printed loop. This is the ground-truth
// experiment, re-run through the seam.
void bringUpInfinite(Rig& rig) {
    loadShipped(kInfinite, rig.character);
    if (::testing::Test::HasFatalFailure()) return;
    analyseShipped(rig.character, rig.verdict);
    if (::testing::Test::HasFatalFailure()) return;

    ASSERT_EQ(rig.verdict.status, ProverStatus::Infinite)
        << kInfinite << " carries one deliberate infinite and the decision "
           "procedure did not find it. Everything section 3, 9 and 10 claim is "
           "about a printed loop, and there is none.\n"
        << DescribeVerdict(rig.character, rig.verdict);
    ASSERT_FALSE(rig.verdict.loop.empty())
        << "INFINITE with an empty loop is a word with nothing behind it";

    bringUpFrom(rig.character, witnessOf(rig.character, rig.verdict), rig);
}

// fighter_a, driven by the ONE self-cancel the decision procedure keeps.
//
// The move is DERIVED, by the same rule test_ground_truth.cpp section 5 uses:
// every edge from a move back into itself, minus the ones the prover reported as
// dead. What is left is the cycle whose only brake is juggle -- a resource the
// kernel does not have. The move id is not written down here, so a character
// edit moves this test rather than silently making it about the wrong edge.
void bringUpSafeSelfCycle(Rig& rig, std::string& moveIdOut) {
    loadShipped(kSafe, rig.character);
    if (::testing::Test::HasFatalFailure()) return;
    analyseShipped(rig.character, rig.verdict);
    if (::testing::Test::HasFatalFailure()) return;

    ASSERT_EQ(rig.verdict.status, ProverStatus::Terminating)
        << kSafe << " was authored to be SAFE and the decision procedure does not "
           "agree.\n"
        << DescribeVerdict(rig.character, rig.verdict);

    const Cancel* live = nullptr;
    for (std::size_t i = 0; i < rig.character.cancels.size(); ++i) {
        const Cancel& e = rig.character.cancels[i];
        if (e.from != e.to) continue;
        bool reportedDead = false;
        for (const ProverDeadCancel& d : rig.verdict.deadCancels)
            if (d.cancel == static_cast<CancelIndex>(i)) { reportedDead = true; break; }
        if (!reportedDead) { live = &e; break; }
    }
    ASSERT_NE(live, nullptr)
        << kSafe << " no longer has a self-cancel the prover keeps, so there is "
           "no certified-away cycle for section 10 to be about.\n"
        << DescribeVerdict(rig.character, rig.verdict);

    moveIdOut = rig.character.moves[live->from].id;
    bringUpFrom(rig.character, selfLoopWitness(moveIdOut), rig);
}

// ============================================================================
// 0g. SAYING WHAT HAPPENED
// ============================================================================

std::string moveName(const MoveIndexMap& map, std::uint16_t slot) {
    if (slot == 0) return "idle";
    const std::string_view id = map.IdOf(slot);
    return id.empty() ? ("slot" + std::to_string(slot)) : std::string(id);
}

// A per-tick table for a failure message. Same role as the one in
// test_ground_truth.cpp: if something breaks, a reader must be able to see WHICH
// TICK and WHAT THE FIGHTERS WERE DOING without re-running anything.
std::string Table(const TickLog& log, const MoveIndexMap& map,
                  std::uint32_t from, std::uint32_t count) {
    std::ostringstream s;
    s << "\n  tick  p0 in  attacker move        fr  hitbits  def stun  def hp  "
         "resim\n";
    for (const TickLog::Sample& sample : log.samples) {
        if (sample.tick < from) continue;
        if (sample.tick >= from + count) break;
        s << "  " << std::setw(4) << sample.tick
          << "  " << std::setw(5) << buttonName(sample.inputs.p[0].bits)
          << "  " << std::setw(18) << std::left
          << moveName(map, sample.state.p[0].moveId) << std::right
          << "  " << std::setw(2) << sample.state.p[0].moveFrame
          << "  " << std::setw(7) << static_cast<int>(sample.state.p[0].alreadyHitBits)
          << "  " << std::setw(8) << sample.state.p[1].hitstun
          << "  " << std::setw(6) << sample.state.p[1].health
          << "  " << (sample.resimulated ? "yes" : " . ")
          << "\n";
    }
    return s.str();
}

std::string DescribeReport(const ComboReport& r, const MoveIndexMap& map) {
    std::ostringstream s;
    s << "\n  open " << (r.open ? "yes" : "no")
      << "  hits " << r.hits << "  damage " << r.damage
      << "  gapTicks " << r.gapTicks
      << "  whiffs " << r.whiffedStarts
      << "\n  startTick " << r.startTick << "  lastHitTick " << r.lastHitTick
      << "  endTick " << r.endTick
      << "\n  cycleRun " << r.cycleRun << "  turns " << r.loopTurnsCompleted
      << "  onWitness " << (r.onWitness ? "yes" : "no")
      << "  witnessIndex " << r.witnessIndex
      << "\n  completedProverLoop " << (r.completedProverLoop ? "yes" : "no")
      << "  performedDeadCancel " << (r.performedDeadCancel ? "yes" : "no")
      << "  deadEdgeConnected " << (r.deadEdgeConnected ? "yes" : "no")
      << "  witnessIncomplete " << (r.witnessIncomplete ? "yes" : "no")
      << "\n  sequence(" << r.sequence.size() << (r.sequenceTruncated ? ", TRUNCATED" : "")
      << ") ";
    for (std::size_t i = 0; i < r.sequence.size() && i < 16; ++i)
        s << moveName(map, r.sequence[i]) << (i + 1 < r.sequence.size() ? ", " : "");
    if (r.sequence.size() > 16) s << "...";
    s << "\n  edges(" << r.edges.size() << ") ";
    for (std::size_t i = 0; i < r.edges.size() && i < 10; ++i) {
        const PerformedEdge& e = r.edges[i];
        s << "\n    t" << e.tick << " " << moveName(map, e.from) << " -> "
          << moveName(map, e.to) << (e.cancel ? "  CANCEL@" : "  link@")
          << e.sourceFrame << (e.dead ? "  DEAD" : "")
          << (e.deadEdgeConnected ? "  DEAD-CONNECTED" : "");
    }
    s << "\n";
    return s.str();
}

// ============================================================================
// 0h. RUNNING A FIGHT
// ============================================================================

// Drive a session for `ticks` ticks, pulling from whatever sources are bound.
void run(FightSession& session, std::uint32_t ticks) {
    for (std::uint32_t i = 0; i < ticks; ++i) session.Tick();
}

// Build the demonstration the tool-assisted player would perform: the witness,
// rehearsed headlessly from the session's current state.
void demonstrate(const Rig& rig, const GameState& from, std::uint32_t turns,
                 std::uint32_t firstTick, Demonstration& out) {
    DemonstrationRequest request{};
    request.from          = &from;
    request.data          = &rig.build.data;
    request.attackerSlot  = 0;
    request.defenderInput = Input{};      // the silent training dummy
    request.moveIds       = rig.kernelWitness;
    request.loopStart     = rig.loopStart;
    request.turns         = turns;
    request.maxTicks      = 600;
    request.firstTick     = firstTick;

    const bool complete = BuildDemonstration(request, out);
    EXPECT_EQ(complete, out.complete)
        << "BuildDemonstration's return value and Demonstration::complete "
           "disagree; the header says it returns `out.complete`.";
    ASSERT_TRUE(out.complete)
        << "the rehearsal did not finish. THAT IS A FINDING, not a flaky test: a "
           "witness the engine cannot perform is the other publishable outcome "
           "(ARCHITECTURE.md 5.5 item 4).\n"
        << "  witness       " << rig.witness.ToString() << "\n"
        << "  reachedIndex  " << out.reachedIndex << " of " << rig.kernelWitness.size()
        << "\n  turnsDone     " << out.turnsDone << " of " << turns
        << "\n  stalledAt     " << out.stalledAt
        << "\n  error         " << out.error;
    ASSERT_FALSE(out.inputs.empty());
    ASSERT_TRUE(out.error.empty())
        << "a complete demonstration carries an error: " << out.error;
    EXPECT_EQ(out.firstTick, firstTick)
        << "DemonstrationRequest::firstTick is carried through to "
           "Demonstration::firstTick so the caller can build the "
           "ScriptedInputSource without re-deriving it.";
    EXPECT_GE(out.turnsDone, turns);
}

}  // namespace

// ============================================================================
// 1. THE SOURCE SEAM: a pure function of an absolute tick index
// ============================================================================

// `At` is CONST, PURE and TOTAL. Same tick in, same bytes out, for ANY uint32 --
// including 0, including kUnboundedTick, including ticks far past the end. This
// is the property that makes seeking, rollback and replay the same operation,
// and the one an `Input Next()` stream cannot have.
TEST(GameInputSource, ScriptedSourceIsAbsolutelyNumberedTotalAndPure) {
    std::vector<Input> trace;
    for (std::uint16_t i = 0; i < 8; ++i)
        trace.push_back(inputOf(static_cast<std::uint16_t>(cse::kernel::kInputLP + i)));

    const std::uint32_t first = 900;
    ScriptedInputSource source(trace, first, "DEMO");

    EXPECT_EQ(source.FirstTick(), first);
    EXPECT_EQ(source.TickCount(), trace.size());
    EXPECT_FALSE(source.Truncated());
    EXPECT_STREQ(source.Name(), "DEMO");
    EXPECT_EQ(source.AuthoredEndTick(), first + trace.size());

    // ABSOLUTE, NOT RELATIVE. A demonstration pressed at tick 900 produces a
    // trace numbered from 900 -- no offset stored outside the source, which is
    // the thing a seek forgets.
    for (std::uint32_t i = 0; i < trace.size(); ++i) {
        const InputSample s = source.At(first + i);
        EXPECT_TRUE(s.authored) << "tick " << (first + i);
        EXPECT_EQ(s.input.bits, trace[i].bits);
    }

    // Before the trace, after it, and at the far end of the number line: nothing
    // authored, and the bits ZEROED. The flag is the contract and the zero is
    // what keeps a missed check from being a heisenbug rather than neutral.
    const std::uint32_t outside[] = { 0u, 1u, first - 1u,
                                      first + static_cast<std::uint32_t>(trace.size()),
                                      first + 10000u, kMaxMatchTicks, kUnboundedTick };
    for (std::uint32_t t : outside) {
        const InputSample s = source.At(t);
        EXPECT_FALSE(s.authored) << "tick " << t;
        EXPECT_EQ(s.input.bits, 0u)
            << "tick " << t << " is unauthored and its bits are not zeroed. A "
               "caller who ignores the flag must get NEUTRAL, not stale bits.";
        EXPECT_TRUE(source.Exhausted(t));
    }

    // PURITY, asked the way a rollback asks it: the same ticks again, out of
    // order, interleaved with other questions. A source that answered from a
    // cursor would disagree with itself here.
    Rng rng(0x5EED0001u);
    for (int i = 0; i < 512; ++i) {
        const std::uint32_t t = first - 4u + rng.Below(20u);
        const InputSample a = source.At(t);
        (void)source.At(rng.Next());
        const InputSample b = source.At(t);
        ASSERT_EQ(a.authored, b.authored) << "At(" << t << ") changed its mind";
        ASSERT_EQ(a.input.bits, b.input.bits) << "At(" << t << ") changed its mind";
    }
}

// The one container in this module that could grow without a bound is a trace
// somebody hands in, so the constructor CLAMPS rather than trusts, and says it
// did. D4's rule about unbounded growth applies to a scripted trace exactly as it
// applies to a count read out of a file.
TEST(GameInputSource, ScriptedSourceClampsToTheTickCapAndReportsThatItDid) {
    std::vector<Input> trace(10, inputOf(cse::kernel::kInputMP));
    ScriptedInputSource source(trace, kMaxMatchTicks - 4u);

    EXPECT_TRUE(source.Truncated())
        << "the trace ran past kMaxMatchTicks and the source did not report that "
           "it had been clamped";
    EXPECT_EQ(source.TickCount(), 4u);
    EXPECT_EQ(source.AuthoredEndTick(), kMaxMatchTicks);
    EXPECT_TRUE(source.At(kMaxMatchTicks - 1u).authored);
    EXPECT_FALSE(source.At(kMaxMatchTicks).authored);
    EXPECT_EQ(source.At(kMaxMatchTicks).input.bits, 0u);

    // And a trace that fits is not reported as clamped, so `Truncated` is a fact
    // rather than a flag that is always on.
    ScriptedInputSource fits(trace, 0);
    EXPECT_FALSE(fits.Truncated());
    EXPECT_EQ(fits.TickCount(), 10u);
}

// LATCHING IS MONOTONIC AND IMMUTABLE. The moment a source can revise the past,
// "same tick in, same bytes out" stops being true and replay, rollback and the
// desync checksum go with it. A caller that gets `false` back has a sequencing
// bug -- so `false` has to actually arrive, and nothing may change when it does.
TEST(GameInputSource, LatchingIsMonotonicAndImmutable) {
    LatchedInputSource pad(0, "YOU");

    EXPECT_EQ(pad.FirstTick(), 0u);
    EXPECT_EQ(pad.NextTick(), 0u);
    EXPECT_STREQ(pad.Name(), "YOU");
    // Never runs out of script -- it authors whatever it is handed. Exhaustion
    // here is a HOST SEQUENCING BUG rather than an end of input, which is why
    // this is kUnboundedTick and not NextTick().
    EXPECT_EQ(pad.AuthoredEndTick(), kUnboundedTick);
    EXPECT_FALSE(pad.At(0).authored);

    for (std::uint32_t t = 0; t < 5; ++t) {
        ASSERT_TRUE(pad.Latch(t, inputOf(static_cast<std::uint16_t>(1u << t))))
            << "latching tick " << t << " in order was refused";
        EXPECT_EQ(pad.NextTick(), t + 1u);
    }

    for (std::uint32_t t = 0; t < 5; ++t) {
        EXPECT_TRUE(pad.At(t).authored);
        EXPECT_EQ(pad.At(t).input.bits, 1u << t);
    }
    EXPECT_FALSE(pad.At(5).authored);
    EXPECT_EQ(pad.At(5).input.bits, 0u);

    // Out of order in both directions, and a re-latch of a tick already written
    // down. All three are refused, and the history is untouched afterwards.
    EXPECT_FALSE(pad.Latch(0, inputOf(0xFFFFu))) << "the past was rewritable";
    EXPECT_FALSE(pad.Latch(3, inputOf(0xFFFFu)));
    EXPECT_FALSE(pad.Latch(9, inputOf(0xFFFFu))) << "a hole was punched in the log";
    EXPECT_EQ(pad.NextTick(), 5u);
    for (std::uint32_t t = 0; t < 5; ++t)
        EXPECT_EQ(pad.At(t).input.bits, 1u << t)
            << "tick " << t << " changed after a refused latch";

    // Reset is for a NEW MATCH only. It throws the history away and begins again,
    // which is exactly the "revise the past" the class prevents mid-match -- so it
    // is a separate, named call rather than something Latch can be talked into.
    pad.Reset(100);
    EXPECT_EQ(pad.FirstTick(), 100u);
    EXPECT_EQ(pad.NextTick(), 100u);
    EXPECT_FALSE(pad.At(0).authored);
    EXPECT_FALSE(pad.At(100).authored);
    EXPECT_TRUE(pad.Latch(100, inputOf(cse::kernel::kInputHP)));
    EXPECT_EQ(pad.At(100).input.bits, cse::kernel::kInputHP);
    EXPECT_FALSE(pad.Latch(102, inputOf(0)));
}

// The whole "Demonstrate" feature, expressed once: primary if it authors this
// tick, otherwise secondary. `Name()` is always "FALLBACK" -- returning whichever
// child spoke last would need a mutable member written from a const method, which
// would make the one method the interface promises is pure the one with a side
// effect. A HUD asks Active(tick)->Name(), which is a pure question.
TEST(GameInputSource, FallbackHandsControlBackWithoutChangingItsName) {
    std::vector<Input> demoTrace(6, inputOf(cse::kernel::kInputLP));
    ScriptedInputSource demo(demoTrace, 10, "DEMO");

    LatchedInputSource pad(0, "YOU");
    for (std::uint32_t t = 0; t < 20; ++t)
        ASSERT_TRUE(pad.Latch(t, inputOf(cse::kernel::kInputMP)));

    FallbackInputSource both(&demo, &pad);

    EXPECT_STREQ(both.Name(), "FALLBACK");
    // The LARGER of the two ends, because either may still speak up to its own.
    EXPECT_EQ(both.AuthoredEndTick(), kUnboundedTick);

    for (std::uint32_t t = 0; t < 10; ++t) {
        EXPECT_EQ(both.Active(t), static_cast<const IInputSource*>(&pad)) << "tick " << t;
        EXPECT_EQ(both.At(t).input.bits, cse::kernel::kInputMP);
    }
    for (std::uint32_t t = 10; t < 16; ++t) {
        EXPECT_EQ(both.Active(t), static_cast<const IInputSource*>(&demo)) << "tick " << t;
        EXPECT_EQ(both.At(t).input.bits, cse::kernel::kInputLP)
            << "the demonstration did not win at tick " << t;
    }
    for (std::uint32_t t = 16; t < 20; ++t) {
        EXPECT_EQ(both.Active(t), static_cast<const IInputSource*>(&pad))
            << "control was not handed back at tick " << t;
        EXPECT_EQ(both.At(t).input.bits, cse::kernel::kInputMP);
    }
    EXPECT_STREQ(both.Name(), "FALLBACK")
        << "the composition renamed itself after being asked about a tick, which "
           "means At() or Active() wrote to a member.";

    // Null is legal on either side and on both. A null primary means the
    // secondary answers everything; two nulls author nothing.
    FallbackInputSource secondaryOnly(nullptr, &pad);
    EXPECT_EQ(secondaryOnly.At(3).input.bits, cse::kernel::kInputMP);
    EXPECT_EQ(secondaryOnly.Active(3), static_cast<const IInputSource*>(&pad));

    FallbackInputSource primaryOnly(&demo, nullptr);
    EXPECT_TRUE(primaryOnly.At(10).authored);
    EXPECT_FALSE(primaryOnly.At(0).authored);
    EXPECT_EQ(primaryOnly.Active(0), nullptr);
    EXPECT_EQ(primaryOnly.AuthoredEndTick(), 16u);

    FallbackInputSource neither(nullptr, nullptr);
    EXPECT_FALSE(neither.At(0).authored);
    EXPECT_EQ(neither.At(0).input.bits, 0u);
    EXPECT_EQ(neither.Active(0), nullptr);
    EXPECT_STREQ(neither.Name(), "FALLBACK");
}

// ============================================================================
// 2. THE SESSION: the one thing that calls Simulate
// ============================================================================

// Refuse a setup that cannot produce a meaningful match, AS DATA -- so a lobby
// can check before committing and the message is written once.
TEST(GameFightSession, ValidateSetupRefusesWhatCannotProduceAMatch) {
    std::string error;

    FightSetup noData{};
    noData.data = nullptr;
    EXPECT_FALSE(ValidateSetup(noData, error))
        << "a null MatchData was accepted, so the refusal moved to the first tick";
    EXPECT_FALSE(error.empty()) << "refused without saying why";

    MatchData data{};
    FightSetup ok{};
    ok.data = &data;
    error.clear();
    EXPECT_TRUE(ValidateSetup(ok, error)) << error;
    EXPECT_TRUE(error.empty());

    // kMaxWorldCoord is the bound that makes PlaceBox total for ANY state,
    // including one a test or a corrupt packet built by hand. Past it is a
    // refusal here rather than undefined arithmetic later.
    for (int slot = 0; slot < 2; ++slot) {
        FightSetup far{};
        far.data = &data;
        far.start.startPosX[slot] = cse::kernel::kMaxWorldCoord + 1;
        error.clear();
        EXPECT_FALSE(ValidateSetup(far, error))
            << "slot " << slot << " was placed beyond kMaxWorldCoord and accepted";
        EXPECT_FALSE(error.empty());

        FightSetup farNegative{};
        farNegative.data = &data;
        farNegative.start.startPosX[slot] = -(cse::kernel::kMaxWorldCoord + 1);
        error.clear();
        EXPECT_FALSE(ValidateSetup(farNegative, error))
            << "slot " << slot << " was placed beyond -kMaxWorldCoord and accepted";
    }

    // It does NOT refuse a position outside the playable stage: Simulate clamps
    // posX to its own stage half-width on the first tick, that clamp is
    // deterministic and identical on both peers, and refusing it here would be a
    // second, disagreeing definition of where the stage ends.
    FightSetup offstage{};
    offstage.data = &data;
    offstage.start.startPosX[0] = 4000 * cse::kernel::kSubUnitsPerPixel;
    offstage.start.startPosX[1] = -4000 * cse::kernel::kSubUnitsPerPixel;
    error.clear();
    EXPECT_TRUE(ValidateSetup(offstage, error))
        << "a position outside the stage was refused, which is a second "
           "definition of where the stage ends: " << error;
}

// Begin puts the match at its opening position and sets the tick index to 0. A
// refused Begin leaves the session NOT started, and Tick() then does nothing --
// rather than ticking an unbuilt match.
TEST(GameFightSession, BeginPlacesTheFightersAndARefusedBeginStartsNothing) {
    Rig rig{};
    bringUpInfinite(rig);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    FightSession session;
    EXPECT_FALSE(session.Started());

    std::string error;
    FightSetup bad{};
    bad.data = nullptr;
    EXPECT_FALSE(session.Begin(bad, error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(session.Started());
    session.Tick();
    EXPECT_EQ(session.CurrentTick(), 0u)
        << "Tick() advanced a session that was never started";

    error.clear();
    ASSERT_TRUE(session.Begin(rig.setup, error)) << error;
    EXPECT_TRUE(error.empty());
    EXPECT_TRUE(session.Started());

    // ResetMatch memsets the state, so no field survives a Begin -- and then
    // MatchStart::startPosX is applied on top.
    EXPECT_EQ(session.CurrentTick(), 0u);
    EXPECT_EQ(session.State().tick, 0u);
    EXPECT_EQ(session.CurrentTick(), session.State().tick)
        << "CurrentTick() is restated as a method because that equality is a "
           "fact worth being able to assert; it is not true.";
    EXPECT_EQ(session.State().p[0].posX, kP0X);
    EXPECT_EQ(session.State().p[1].posX, kP1X);
    EXPECT_EQ(session.State().p[0].posY, 0);
    EXPECT_EQ(session.State().p[0].health, kStartingHealth);
    EXPECT_EQ(session.State().p[1].health, kStartingHealth);
    EXPECT_EQ(session.State().p[0].moveId, 0u);
    EXPECT_EQ(session.HighWaterTick(), 0u);
    EXPECT_EQ(&session.Data(), rig.setup.data)
        << "the session copied the MatchData instead of borrowing it, which is a "
           "second source of truth for the frame data";

    // The seed is stored AS GIVEN and ResetMatch's zero-substitution is what maps
    // it, so a session begun with the same MatchStart reproduces the same stream.
    EXPECT_EQ(session.State().rng, kSeed);
    EXPECT_EQ(session.Checksum(), cse::kernel::Checksum(session.State()))
        << "FightSession::Checksum is not cse::kernel::Checksum forwarded. The "
           "desync checksum and the replay checkpoint value must stay the same "
           "number.";
}

// TickView::tick is the tick that RAN. After the view is delivered, state->tick
// is tick + 1, because Simulate increments at the bottom. This is asserted for
// every tick of a real fight rather than once, because the failure it prevents is
// an observer that is off by one on every entry.
TEST(GameFightSession, TheTickViewIsTheTickThatRanAndTheStateIsOneAhead) {
    Rig rig{};
    bringUpInfinite(rig);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    FightSession session;
    std::string error;
    ASSERT_TRUE(session.Begin(rig.setup, error)) << error;

    TickLog log;
    ASSERT_TRUE(session.AddObserver(&log));

    std::vector<Input> trace(40, inputOf(rig.bindings[0].button));
    ScriptedInputSource source(trace, 0, "DEMO");
    session.SetInputSource(0, &source);
    EXPECT_EQ(session.InputSourceFor(0), static_cast<const IInputSource*>(&source));
    EXPECT_EQ(session.InputSourceFor(1), nullptr);

    run(session, 40);

    EXPECT_EQ(log.offByOneViolations, 0)
        << log.offByOneViolations << " ticks delivered a TickView whose state was "
           "not one tick ahead of TickView::tick.";
    EXPECT_EQ(log.nullViolations, 0) << "a TickView arrived with a null state or data";
    EXPECT_EQ(log.gapViolations, 0)  << "a tick index was skipped";
    ASSERT_EQ(log.Size(), 40u);
    EXPECT_EQ(log.samples.front().tick, 0u)
        << "the first tick that ran was not tick 0";
    EXPECT_EQ(session.CurrentTick(), 40u);
    EXPECT_EQ(session.HighWaterTick(), 40u);

    for (const TickLog::Sample& s : log.samples) {
        ASSERT_FALSE(s.resimulated)
            << "tick " << s.tick << " was reported as re-simulated on a session "
               "that has only ever moved forwards";
        ASSERT_EQ(s.inputs.p[0].bits, rig.bindings[0].button)
            << "tick " << s.tick << " was fed bits the bound source did not author";
        ASSERT_EQ(s.inputs.p[1].bits, 0u)
            << "the unbound slot contributed something other than neutral";
    }

    // And the session is a thin wrapper around the pure kernel call: the same
    // inputs through cse::kernel::Simulate produce the same bytes. If this ever
    // fails, FightSession has acquired an opinion.
    GameState direct{};
    cse::kernel::ResetMatch(direct, kSeed);
    direct.p[0].posX = kP0X;
    direct.p[1].posX = kP1X;
    for (std::uint32_t t = 0; t < 40; ++t)
        cse::kernel::Simulate(direct, pairOf(rig.bindings[0].button, 0), rig.build.data);

    EXPECT_EQ(0, std::memcmp(&direct, &session.State(), sizeof(GameState)))
        << "FightSession::Tick and a bare Simulate loop disagree, so the session "
           "is doing something to the tick beyond calling the kernel."
        << Table(log, rig.build.moves[0], 0, 16);
}

// Observers are notified in REGISTRATION ORDER; a duplicate is refused rather
// than allowed, because an observer notified twice per tick double-counts every
// hit; and a restart KEEPS them, because a host that rebinds its recorder and its
// judge on every reset drops one of them eventually.
TEST(GameFightSession, ObserversAreOrderedRefusedTwiceAndKeptAcrossARestart) {
    MatchData data{};
    FightSetup setup{};
    setup.data = &data;

    FightSession session;
    std::string error;
    ASSERT_TRUE(session.Begin(setup, error)) << error;

    std::vector<int> order;
    CountingObserver a(1, &order), b(2, &order), c(3, &order);

    EXPECT_FALSE(session.AddObserver(nullptr)) << "a null observer was registered";
    EXPECT_EQ(session.ObserverCount(), 0);

    ASSERT_TRUE(session.AddObserver(&a));
    ASSERT_TRUE(session.AddObserver(&b));
    ASSERT_TRUE(session.AddObserver(&c));
    EXPECT_EQ(session.ObserverCount(), 3);
    EXPECT_FALSE(session.AddObserver(&b))
        << "registering the same observer twice was allowed, so it will be "
           "notified twice per tick and double-count every hit";
    EXPECT_EQ(session.ObserverCount(), 3);

    session.Tick();
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);

    // Removal keeps the remaining relative order, so it cannot silently reorder
    // the notification sequence.
    order.clear();
    EXPECT_TRUE(session.RemoveObserver(&b));
    EXPECT_FALSE(session.RemoveObserver(&b)) << "removing twice reported success";
    EXPECT_EQ(session.ObserverCount(), 2);
    session.Tick();
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 3);
    EXPECT_EQ(b.calls, 1) << "a removed observer was still notified";

    // A restart is not a teardown.
    order.clear();
    ASSERT_TRUE(session.Begin(setup, error)) << error;
    EXPECT_EQ(session.ObserverCount(), 2)
        << "Begin dropped the observers, so a host would have to re-register its "
           "recorder and its combo judge on every reset";
    EXPECT_EQ(session.CurrentTick(), 0u);
    EXPECT_EQ(session.HighWaterTick(), 0u)
        << "the re-simulation high-water mark survived a restart, so the first "
           "tick of the new match will be reported as re-simulated";
    session.Tick();
    EXPECT_EQ(order.size(), 2u);

    session.ClearObservers();
    EXPECT_EQ(session.ObserverCount(), 0);
    order.clear();
    session.Tick();
    EXPECT_TRUE(order.empty());

    // The list is fixed-capacity so registration allocates nothing and the tick
    // path iterates a small array. Past the cap is a refusal, not a reallocation.
    std::vector<CountingObserver> many;
    many.reserve(static_cast<std::size_t>(kMaxTickObservers) + 2u);
    for (int i = 0; i < kMaxTickObservers + 2; ++i) many.emplace_back(100 + i, nullptr);
    for (int i = 0; i < kMaxTickObservers; ++i)
        EXPECT_TRUE(session.AddObserver(&many[static_cast<std::size_t>(i)]))
            << "observer " << i << " of " << kMaxTickObservers << " was refused";
    EXPECT_EQ(session.ObserverCount(), kMaxTickObservers);
    EXPECT_FALSE(session.AddObserver(&many[static_cast<std::size_t>(kMaxTickObservers)]))
        << "the fixed observer array grew past kMaxTickObservers";
    EXPECT_EQ(session.ObserverCount(), kMaxTickObservers);
}

// NEUTRAL, NEVER "REPEAT THE LAST TICK". Repeating would make the input a
// function of the path taken through the match rather than of the tick index --
// and it has a gameplay meaning nobody would choose: a demonstration that ends
// mid-combo would leave the button jammed down rather than handing control back.
TEST(GameFightSession, AnUnauthoredTickIsNeutralAndNeverTheLastOne) {
    Rig rig{};
    bringUpInfinite(rig);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    FightSession session;
    std::string error;
    ASSERT_TRUE(session.Begin(rig.setup, error)) << error;

    TickLog log;
    ASSERT_TRUE(session.AddObserver(&log));

    // A short trace of a HELD button that keeps the self-cancel loop running, and
    // then nothing at all. If the session repeated the last authored tick, the
    // loop would carry on forever; if it feeds neutral, the move in progress
    // finishes and the attacker returns to idle.
    constexpr std::uint32_t kTraceTicks = 40;
    constexpr std::uint32_t kAfterTicks = 60;
    std::vector<Input> trace(kTraceTicks, inputOf(rig.bindings[0].button));
    ScriptedInputSource source(trace, 0, "DEMO");
    session.SetInputSource(0, &source);

    run(session, kTraceTicks + kAfterTicks);
    ASSERT_EQ(log.Size(), kTraceTicks + kAfterTicks);

    for (std::uint32_t t = kTraceTicks; t < kTraceTicks + kAfterTicks; ++t)
        ASSERT_EQ(log.samples[t].inputs.p[0].bits, 0u)
            << "tick " << t << " is past the end of the trace and was fed "
            << log.samples[t].inputs.p[0].bits
            << " rather than neutral. The button is jammed down.";

    bool wentIdle = false;
    for (std::uint32_t t = kTraceTicks; t < kTraceTicks + kAfterTicks; ++t)
        if (log.samples[t].state.p[0].moveId == 0u) { wentIdle = true; break; }
    EXPECT_TRUE(wentIdle)
        << "the attacker never returned to idle after the trace ran out, so "
           "something is still pressing the button."
        << Table(log, rig.build.moves[0], kTraceTicks, 24);

    // Unbinding is the same statement said a different way: null means neutral.
    session.SetInputSource(0, nullptr);
    EXPECT_EQ(session.InputSourceFor(0), nullptr);
    const std::uint32_t before = session.CurrentTick();
    session.Tick();
    EXPECT_EQ(log.samples[before].inputs.p[0].bits, 0u);

    // A player index outside [0, 2) is ignored rather than writing off the end of
    // a two-element array.
    session.SetInputSource(-1, &source);
    session.SetInputSource(2, &source);
    session.SetInputSource(99, &source);
    EXPECT_EQ(session.InputSourceFor(0), nullptr);
    EXPECT_EQ(session.InputSourceFor(1), nullptr);
    EXPECT_EQ(session.InputSourceFor(-1), nullptr);
    EXPECT_EQ(session.InputSourceFor(2), nullptr);
}

// Restore moves the tick index BACKWARDS and does not touch HighWaterTick, so
// every tick run afterwards up to that mark is reported as re-simulated WITHOUT
// the caller saying anything. That is what makes TickView::resimulated impossible
// for a host to forget to set -- and it is the flag the recorder and the combo
// judge both change behaviour on.
TEST(GameFightSession, RestoreMakesTheNextTicksResimulatedAndBitIdentical) {
    Rig rig{};
    bringUpInfinite(rig);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    FightSession session;
    std::string error;
    ASSERT_TRUE(session.Begin(rig.setup, error)) << error;

    TickLog straight;
    ASSERT_TRUE(session.AddObserver(&straight));

    const std::uint16_t held = rig.bindings[0].button;
    std::vector<Input> trace(60, inputOf(held));
    ScriptedInputSource source(trace, 0, "DEMO");
    session.SetInputSource(0, &source);

    GameState atTwenty{};
    run(session, 20);
    session.Snapshot(atTwenty);
    EXPECT_EQ(atTwenty.tick, 20u)
        << "Snapshot did not capture the state at the session's own tick index";
    run(session, 40);

    ASSERT_EQ(session.CurrentTick(), 60u);
    ASSERT_EQ(session.HighWaterTick(), 60u);
    const std::uint32_t finalChecksum = session.Checksum();
    const GameState     finalState    = session.State();

    // Roll back and run the same prefix again.
    session.Restore(atTwenty);
    EXPECT_EQ(session.CurrentTick(), 20u)
        << "Restore did not move the tick index back to the restored state's own "
           "tick";
    EXPECT_EQ(session.HighWaterTick(), 60u)
        << "Restore moved the high-water mark, so the re-simulated ticks that "
           "follow will be reported as fresh";

    // SEEDED TO THE TICK THE SESSION IS STANDING AT, and that is a fact about
    // this log rather than about the session. TickLog is indexed by ABSOLUTE
    // tick -- which is what lets it re-implement the recorder's
    // overwrite-on-re-simulation rule independently -- so a fresh one attached
    // here would meet its first view at tick 20 with an empty vector and take
    // its own GAP branch on every one of the forty re-simulated ticks: nothing
    // appended, forty violations counted, Size() zero. Read off CurrentTick()
    // rather than written as 20, so it cannot drift from the restore point.
    //
    // SEEDED RATHER THAN COPIED from `straight`. A copy would already hold the
    // right checksums at 20..59 and the reproduction loop below would compare
    // equal even if the session had delivered nothing at all; these twenty
    // default-constructed samples can only be matched by ticks that really ran
    // again.
    TickLog again;
    again.samples.resize(session.CurrentTick());
    session.ClearObservers();
    ASSERT_TRUE(session.AddObserver(&again));
    run(session, 40);

    ASSERT_EQ(again.Size(), 60u)
        << "the re-simulated ticks were appended at the wrong indices";
    EXPECT_EQ(again.gapViolations, 0);
    for (std::uint32_t t = 20; t < 60; ++t)
        EXPECT_TRUE(again.samples[t].resimulated)
            << "tick " << t << " is at or below the high-water mark and was not "
               "reported as re-simulated";

    // BIT-IDENTICAL, which is the property everything else in this file rests on.
    EXPECT_EQ(session.Checksum(), finalChecksum)
        << "re-simulating a restored prefix produced a different state, so the "
           "tick is not a pure function of (state, inputs, data).";
    EXPECT_EQ(0, std::memcmp(&finalState, &session.State(), sizeof(GameState)));
    for (std::uint32_t t = 20; t < 60; ++t)
        ASSERT_EQ(straight.samples[t].checksum, again.samples[t].checksum)
            << "tick " << t << " did not reproduce."
            << Table(again, rig.build.moves[0], 20, 12);

    // Past the high-water mark the session is running fresh ticks again, and says
    // so -- the flag is about the index, not about "a Restore happened once".
    run(session, 5);
    ASSERT_EQ(again.Size(), 65u);
    for (std::uint32_t t = 60; t < 65; ++t)
        EXPECT_FALSE(again.samples[t].resimulated)
            << "tick " << t << " is past the high-water mark and was still "
               "reported as re-simulated";
    EXPECT_EQ(session.HighWaterTick(), 65u);
}

// ============================================================================
// 3. THE TOOL-ASSISTED PLAYER: the printed loop, through the seam  (CLAIM 4)
// ============================================================================
//
// tests/test_ground_truth.cpp already executed the prover's printed loop, by
// hand, with a closed-loop driver written inside that file. This section makes
// the SAME claim through the reusable seam, and the headline assertion is not "a
// combo happened" but "the seam produced the reference driver's trace exactly".
//
// That is the strongest available statement, and it is the only one that
// distinguishes a working seam from a seam that happens to land some hits. If
// the two disagree the failure says so in those words: the ground-truth file is
// the reference and BuildDemonstration is the thing under test.

// How many turns of the loop the demonstration performs, and why the number is
// small rather than generous. Fighter::health clamps at zero, and every count
// below -- hits, damage, the watcher's own tally -- stops being measurable once
// it does. `fighter_a_infinite` deals 30 a turn against 1000 health, so 15 turns
// plus the witness's five-move prefix is roughly 600 damage and leaves a wide
// margin. A character whose damage grew past this says so with the KO assertion
// rather than quietly measuring a clamp.
constexpr std::uint32_t kDemoTurns = 15;

// Extra ticks run after the trace ends, so that a move STARTED on the last tick
// of the demonstration still gets to land. Past the trace the session feeds
// neutral, so nothing new begins -- these ticks only let what already started
// finish.
constexpr std::uint32_t kSettleTicks = 12;

TEST(GameDemonstration, TheSeamProducesExactlyTheGroundTruthDriversTrace) {
    Rig rig{};
    bringUpInfinite(rig);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    // The reference: the closed-loop driver copied out of test_ground_truth.cpp,
    // run against a bare Simulate loop exactly as that file runs it.
    Driver reference(rig.witness, rig.build.moves[0], rig.bindings);
    std::string why;
    ASSERT_TRUE(reference.Usable(why)) << "the witness cannot be driven: " << why;

    // The thing under test: the same witness, rehearsed by the module.
    FightSession session;
    std::string error;
    ASSERT_TRUE(session.Begin(rig.setup, error)) << error;

    Demonstration demo{};
    demonstrate(rig, session.State(), kDemoTurns, session.CurrentTick(), demo);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    // --- the traces, tick for tick -----------------------------------------
    //
    // The reference driver is run for exactly as many ticks as the rehearsal
    // produced. Both press the button of the move the witness says comes next and
    // both advance on `moveFrame == 0`; if they were written to the same
    // algorithm they cannot differ, and if they differ the algorithm in
    // FightSession.h was not the one implemented.
    GameState referenceState{};
    cse::kernel::ResetMatch(referenceState, kSeed);
    referenceState.p[0].posX = kP0X;
    referenceState.p[1].posX = kP1X;

    std::vector<std::uint16_t> referenceBits;
    referenceBits.reserve(demo.inputs.size());
    for (std::size_t t = 0; t < demo.inputs.size(); ++t) {
        const std::uint16_t bits = reference.Bits();
        referenceBits.push_back(bits);
        cse::kernel::Simulate(referenceState, pairOf(bits, 0), rig.build.data);
        reference.Observe(referenceState.p[0].moveId, referenceState.p[0].moveFrame);
    }

    ASSERT_EQ(referenceBits.size(), demo.inputs.size());
    std::size_t   firstDisagreement = demo.inputs.size();
    std::uint16_t seamBits = 0, referenceBitsAt = 0;
    for (std::size_t t = 0; t < demo.inputs.size(); ++t)
        if (demo.inputs[t].bits != referenceBits[t]) {
            firstDisagreement = t;
            seamBits          = demo.inputs[t].bits;
            referenceBitsAt   = referenceBits[t];
            break;
        }

    EXPECT_EQ(firstDisagreement, demo.inputs.size())
        << "THE SEAM AND THE GROUND-TRUTH DRIVER DISAGREE at tick "
        << firstDisagreement << ": BuildDemonstration pressed "
        << buttonName(seamBits)
        << " and tests/test_ground_truth.cpp's driver pressed "
        << buttonName(referenceBitsAt)
        << ".\n"
           "The reference is the file that earned ARCHITECTURE.md 5.5 item 4. If "
           "these differ, THE SEAM IS WRONG -- the two-phase rehearsal in "
           "FightSession.h is specified as exactly this cursor, including that it "
           "advances on `moveId == wanted && moveFrame == 0` and wraps to "
           "loopStart.\n"
        << "  witness: " << rig.witness.ToString();

    // --- and performing it reproduces the rehearsal --------------------------
    //
    // The whole point of REHEARSE-then-PERFORM is that what comes back is DATA: a
    // fixed list, a pure function of tick index, replayable and rollback-safe. So
    // the live session driven by a ScriptedInputSource over that list must end up
    // in exactly the state the private rehearsal reached.
    ScriptedInputSource demoSource(demo.inputs, demo.firstTick, "DEMO");
    session.SetInputSource(0, &demoSource);
    // The defender is left UNBOUND rather than given a zero source: the request's
    // `defenderInput` is zero and an unbound slot contributes neutral, so these
    // are the same bits by two routes. Binding a vector of 600 zeroes would only
    // invite a reader to think the dummy's input varies.
    TickLog log;
    ASSERT_TRUE(session.AddObserver(&log));

    run(session, static_cast<std::uint32_t>(demo.inputs.size()));

    EXPECT_EQ(0, std::memcmp(&referenceState, &session.State(), sizeof(GameState)))
        << "the PERFORMANCE of the demonstration reached a different state than "
           "the REHEARSAL that produced it, so the fixed list is not a faithful "
           "recording of the closed loop that generated it."
        << Table(log, rig.build.moves[0], 0, 24);
    EXPECT_EQ(session.Checksum(), cse::kernel::Checksum(referenceState));
    EXPECT_EQ(log.offByOneViolations, 0);
    EXPECT_TRUE(log.Clean());

    RecordProperty("demo_ticks", static_cast<int>(demo.inputs.size()));
    RecordProperty("demo_turns", static_cast<int>(demo.turnsDone));
    RecordProperty("witness", rig.witness.ToString());
}

// And it is a COMBO, not merely a trace: every turn of the loop connects, the
// defender never gets a tick back, and the damage keeps accruing. This is the
// ground-truth payoff restated against the seam's own output.
TEST(GameDemonstration, TheDemonstratedLoopConnectsOnEveryTurn) {
    Rig rig{};
    bringUpInfinite(rig);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    FightSession session;
    std::string error;
    ASSERT_TRUE(session.Begin(rig.setup, error)) << error;

    Demonstration demo{};
    demonstrate(rig, session.State(), kDemoTurns, session.CurrentTick(), demo);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    ScriptedInputSource demoSource(demo.inputs, demo.firstTick, "DEMO");
    session.SetInputSource(0, &demoSource);

    TickLog log;
    ASSERT_TRUE(session.AddObserver(&log));
    run(session, static_cast<std::uint32_t>(demo.inputs.size()) + kSettleTicks);

    const std::vector<std::uint32_t> hits = log.HitTicks(1);
    ASSERT_GE(hits.size(), static_cast<std::size_t>(kDemoTurns))
        << "the demonstration asked for " << kDemoTurns << " turns and landed "
        << hits.size() << " hits. A witness the engine cannot perform is the "
           "OTHER publishable outcome, not a flaky test."
        << Table(log, rig.build.moves[0], 0, 40);

    // The defender is still standing, so every count above is a count of real
    // damage rather than of hits against Fighter::health's clamp at zero.
    EXPECT_GT(log.Final().p[1].health, 0)
        << "the defender was knocked out inside the measured window, so the last "
           "hits were measured against the health clamp. Lower kDemoTurns.";
    EXPECT_EQ(log.Final().p[0].health, kStartingHealth)
        << "the attacker took damage, so this is a trade being read as a combo";

    // A FIXED PERIOD is what "forever" looks like from outside. A loop whose
    // period grew would still land hits and would not be an infinite.
    ASSERT_GE(hits.size(), 3u);
    const std::uint32_t period = hits[1] - hits[0];
    EXPECT_GT(period, 0u);
    for (std::size_t i = 1; i + 1 < hits.size(); ++i)
        ASSERT_EQ(hits[i + 1] - hits[i], period)
            << "turn " << i << " took a different number of ticks than the first, "
               "so the demonstrated loop is not periodic."
            << Table(log, rig.build.moves[0], hits[0], 32);

    // Every hit came from the move the witness names -- the loop is one move, so
    // every connecting move must be it.
    const std::uint16_t loopSlot = rig.kernelWitness.back();
    for (std::uint32_t t : hits)
        ASSERT_EQ(log.samples[t].state.p[0].moveId, loopSlot)
            << "the hit on tick " << t << " came from `"
            << moveName(rig.build.moves[0], log.samples[t].state.p[0].moveId)
            << "` and the witness says `"
            << moveName(rig.build.moves[0], loopSlot) << "`.";

    // Read straight off Fighter::hitstun, which is the DIRECT reading
    // ComboWatcher.h insists on: the two indirect detectors miss the case where
    // stun expires on exactly the tick the next hit lands.
    std::vector<std::uint32_t> freeTicks;
    for (std::uint32_t t = hits.front(); t <= hits.back(); ++t) {
        const TickLog::Sample& s = log.samples[t];
        const bool wasHit = (t == 0) ? false
                                     : s.state.p[1].health < log.samples[t - 1].state.p[1].health;
        if (s.state.p[1].hitstun == 0 && !wasHit) freeTicks.push_back(t);
    }
    EXPECT_TRUE(freeTicks.empty())
        << "the defender was out of hitstun on " << freeTicks.size()
        << " tick(s) inside the demonstration, so it is a long string with gaps "
           "in it rather than an unescapable loop."
        << Table(log, rig.build.moves[0], hits.front(), 32);

    RecordProperty("demo_hits", static_cast<int>(hits.size()));
    RecordProperty("demo_period_ticks", static_cast<int>(period));
}

// The failure modes, which have to be visible rather than silent -- a stalled
// rehearsal is the useful outcome, not a bug to retry.
TEST(GameDemonstration, ARehearsalThatCannotFinishSaysWhereAndWhy) {
    Rig rig{};
    bringUpInfinite(rig);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    GameState from{};
    cse::kernel::ResetMatch(from, kSeed);
    from.p[0].posX = kP0X;
    from.p[1].posX = kP1X;

    DemonstrationRequest base{};
    base.from         = &from;
    base.data         = &rig.build.data;
    base.attackerSlot = 0;
    base.moveIds      = rig.kernelWitness;
    base.loopStart    = rig.loopStart;
    base.turns        = 4;
    base.maxTicks     = 600;

    // --- a null pointer is an error, not a crash ----------------------------
    {
        DemonstrationRequest r = base;
        r.from = nullptr;
        Demonstration out{};
        EXPECT_FALSE(BuildDemonstration(r, out));
        EXPECT_FALSE(out.complete);
        EXPECT_FALSE(out.error.empty()) << "refused a null state without saying so";
    }
    {
        DemonstrationRequest r = base;
        r.data = nullptr;
        Demonstration out{};
        EXPECT_FALSE(BuildDemonstration(r, out));
        EXPECT_FALSE(out.error.empty());
    }

    // --- an empty witness cannot be performed --------------------------------
    {
        DemonstrationRequest r = base;
        r.moveIds.clear();
        Demonstration out{};
        EXPECT_FALSE(BuildDemonstration(r, out));
        EXPECT_FALSE(out.error.empty());
    }

    // --- A MOVE WITH NO BUTTON IS REFUSED BY NAME, NOT PRESSED AS NOTHING ----
    //
    // The binding table here is minimal on purpose -- one move, one button -- so
    // 17 of this character's 18 moves have MoveDef::button == 0. Asking for one
    // of them is a request nothing can satisfy, and holding no bits while waiting
    // forever is the failure mode the header rules out.
    {
        std::uint16_t unbound = 0;
        for (std::uint16_t slot = 1;
             slot < static_cast<std::uint16_t>(rig.build.data.p[0].moveCount); ++slot) {
            const cse::kernel::MoveDef* m = cse::kernel::MoveAt(rig.build.data.p[0], slot);
            if (m != nullptr && m->button == 0) { unbound = slot; break; }
        }
        ASSERT_NE(unbound, 0u)
            << "every move in this build has a button, so the minimal binding "
               "table this test needs is not minimal";

        DemonstrationRequest r = base;
        r.moveIds.push_back(unbound);
        Demonstration out{};
        EXPECT_FALSE(BuildDemonstration(r, out));
        EXPECT_FALSE(out.complete);
        EXPECT_FALSE(out.error.empty())
            << "a witness naming a move with no button was accepted, so the "
               "rehearsal would hold nothing and wait forever";
    }

    // --- a budget that runs out reports where it stalled ---------------------
    {
        DemonstrationRequest r = base;
        r.turns    = 200;      // far more than the budget can produce
        r.maxTicks = 20;
        Demonstration out{};
        EXPECT_FALSE(BuildDemonstration(r, out));
        EXPECT_FALSE(out.complete);
        EXPECT_FALSE(out.error.empty())
            << "the rehearsal ran out of budget and said nothing about it";
        EXPECT_LE(out.inputs.size(), r.maxTicks)
            << "the hard budget was exceeded, so `maxTicks` is a suggestion";
        EXPECT_LE(out.reachedIndex, r.moveIds.size());
        EXPECT_LT(out.turnsDone, r.turns);
    }

    // --- turns == 0 means "perform the prefix and stop" ----------------------
    //
    // GIVEN A PREFIX TO PERFORM. The witness is extended by one entry so that
    // index 0 is a prefix whatever the prover's own loopStart turned out to be,
    // and the extra entry is a copy of one already in the witness, so it is
    // bound to a button the rehearsal can ask for.
    {
        DemonstrationRequest r = base;
        ASSERT_FALSE(r.moveIds.empty());
        const std::uint16_t repeated = r.moveIds.back();
        r.moveIds.push_back(repeated);
        r.loopStart = 1;
        r.turns     = 0;
        Demonstration out{};
        EXPECT_TRUE(BuildDemonstration(r, out)) << out.error;
        EXPECT_TRUE(out.complete);
        EXPECT_TRUE(out.error.empty()) << out.error;
        EXPECT_EQ(out.turnsDone, 0u)
            << "zero turns were asked for and " << out.turnsDone << " were performed";
        EXPECT_EQ(out.reachedIndex, r.loopStart)
            << "the prefix is entries [0, loopStart) and the rehearsal stopped "
               "somewhere else";
        EXPECT_FALSE(out.inputs.empty())
            << "the prefix was reported as performed and no button was pressed";
    }

    // --- ...AND A REQUEST FOR NO TICKS AT ALL IS REFUSED, NOT GRANTED --------
    //
    // `turns == 0` with `loopStart == 0` is the one request that is already
    // satisfied before the first tick: the prefix is entries [0, 0), which is
    // nothing, and no turn was asked for. Granting it hands back complete ==
    // true with an EMPTY trace -- the same nothing the empty-witness refusal
    // above rejects, and on the same ground, because `complete` is what a caller
    // checks before performing a demonstration. A caller that trusted it would
    // press a ScriptedInputSource that hands control back on the tick it was
    // pressed, watch nothing happen, and have been told it worked.
    //
    // THIS IS THE ASSERTION THAT WOULD CATCH IT COMING BACK: the bug is one
    // `satisfied()` evaluated at the top of the loop, so it produces success
    // with no error and no ticks rather than anything that looks wrong.
    {
        DemonstrationRequest r = base;
        r.loopStart = 0;
        r.turns     = 0;
        Demonstration out{};
        EXPECT_FALSE(BuildDemonstration(r, out))
            << "a request for no ticks at all was granted, with "
            << out.inputs.size() << " tick(s) of trace to show for it";
        EXPECT_FALSE(out.complete)
            << "a demonstration of nothing reported itself COMPLETE";
        EXPECT_TRUE(out.inputs.empty());
        EXPECT_FALSE(out.error.empty()) << "refused without saying why";
    }
}

// ============================================================================
// 4. THE RECORDING  (CLAIM 1)
// ============================================================================

namespace {

// Record a fight and hand back the bytes and an independent tick log.
//
// The log is what every checkpoint is checked against. A replay verified only by
// the module's own verifier would be a round trip through one implementation;
// the log is a second reading of the same fight, taken from the states the
// kernel produced.
void recordFight(const Rig& rig, const std::vector<Input>& trace,
                 std::uint32_t checkpointInterval, TickLog& log, Bytes& bytes) {
    FightSession session;
    std::string error;
    ASSERT_TRUE(session.Begin(rig.setup, error)) << error;

    ReplayRecorderOptions options{};
    options.checkpointInterval = checkpointInterval;
    ReplayRecorder recorder(options);

    // Begin() BEFORE the first Tick, always. A recorder that receives a TickView
    // without having been told the initial conditions cannot produce a valid file.
    ASSERT_TRUE(recorder.Begin(rig.setup.start, HashMatchData(rig.build.data),
                               rig.character.id, rig.character.id, error))
        << error;
    EXPECT_TRUE(recorder.Recording());
    EXPECT_EQ(recorder.TickCount(), 0u);

    ASSERT_TRUE(session.AddObserver(&recorder));
    ASSERT_TRUE(session.AddObserver(&log));

    ScriptedInputSource source(trace, 0, "TRACE");
    session.SetInputSource(0, &source);
    run(session, static_cast<std::uint32_t>(trace.size()));

    ASSERT_TRUE(recorder.Error().empty())
        << "the recorder went inert during a straightforward forward-only run: "
        << recorder.Error();
    ASSERT_EQ(recorder.TickCount(), trace.size());
    ASSERT_TRUE(recorder.Encode(bytes, error)) << error;
}

// Play a decoded replay back through a fresh session, watched by both the
// module's verifier and this file's own log.
void playBack(const ReplayData& replay, const MatchData& data,
              TickLog& log, ReplayVerifier& verifier) {
    FightSetup setup{};
    setup.start = replay.start;
    setup.data  = &data;

    FightSession session;
    std::string error;
    ASSERT_TRUE(session.Begin(setup, error)) << error;

    // ONE ReplayData, TWO sources. The pairing is what was recorded, and the
    // decoded stream is a flat vector of InputPair precisely so that a host
    // cannot play one player's input against the other's from a different tick.
    ReplayInputSource p0(replay, 0), p1(replay, 1);
    session.SetInputSource(0, &p0);
    session.SetInputSource(1, &p1);

    ASSERT_TRUE(session.AddObserver(&verifier));
    ASSERT_TRUE(session.AddObserver(&log));
    run(session, replay.TickCount());
}

// Assert the structural invariants a ReplayData in hand is supposed to already
// have proven. Used by the round-trip tests and by the fuzz, so the two cannot
// drift apart in what they consider well-formed.
void expectStructurallySound(const ReplayData& replay, const char* what) {
    EXPECT_EQ(replay.version, kReplayVersion) << what;
    ASSERT_FALSE(replay.inputs.empty()) << what << ": a replay of no ticks";
    ASSERT_FALSE(replay.checkpoints.empty()) << what << ": no checkpoints at all";
    EXPECT_GE(replay.checkpointInterval, 1u) << what;

    for (std::size_t i = 0; i < replay.checkpoints.size(); ++i) {
        ASSERT_LT(replay.checkpoints[i].tick, replay.inputs.size())
            << what << ": checkpoint " << i << " names a tick past the end";
        if (i > 0)
            ASSERT_LT(replay.checkpoints[i - 1].tick, replay.checkpoints[i].tick)
                << what << ": checkpoints " << (i - 1) << " and " << i
                << " are not strictly increasing";
    }
    EXPECT_EQ(replay.checkpoints.back().tick, replay.inputs.size() - 1)
        << what << ": the last checkpoint is not the last tick, so the replay has "
                  "no end-to-end check at all";
}

// The demo trace, materialised once per test that needs it.
void demoTrace(const Rig& rig, std::vector<Input>& out) {
    GameState from{};
    cse::kernel::ResetMatch(from, kSeed);
    from.p[0].posX = kP0X;
    from.p[1].posX = kP1X;

    Demonstration demo{};
    demonstrate(rig, from, kDemoTurns, 0, demo);
    if (::testing::Test::HasFatalFailure()) return;
    out = demo.inputs;
}

}  // namespace

// The hash both a replay and the connect handshake ask their question with. Two
// hashes for one question would eventually disagree, which is why Replay.h says
// this is the function ARCHITECTURE.md 4.8's handshake should use.
TEST(GameReplayFormat, TheContentHashIsStableAndMovesWhenTheCharacterDoes) {
    Rig rig{};
    bringUpInfinite(rig);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    const std::uint32_t first  = HashMatchData(rig.build.data);
    const std::uint32_t second = HashMatchData(rig.build.data);
    EXPECT_EQ(first, second) << "HashMatchData is not a function of its argument";

    // Edit the character the way a designer would -- one number in one move --
    // and rebuild. A hash that did not move would make the CharacterChanged
    // refusal unable to fire on the change it exists for.
    CharacterData edited = rig.character;
    const MoveIndex lp = edited.FindMove(rig.witness.sequence.front());
    ASSERT_NE(lp, kInvalidMove);
    edited.moves[lp].damageHundredths += 100;   // one whole point of damage

    MatchBuild editedBuild{};
    ASSERT_TRUE(buildMirror(edited, rig.bindings, editedBuild))
        << editedBuild.report[0].error;
    EXPECT_NE(HashMatchData(editedBuild.data), first)
        << "one move's damage changed and the content hash did not, so a replay "
           "recorded against the old character would play back against the new "
           "one and say nothing.";
}

// THE FORMAT, BYTE BY BYTE, read back with shifts and masks against the table in
// Replay.h. This is an independent implementation of the specification: a round
// trip through the module's own reader and writer would agree even if both were
// wrong about the layout.
TEST(GameReplayFormat, TheEncodedBytesAreExactlyWhatTheHeaderTableSays) {
    Rig rig{};
    bringUpInfinite(rig);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    std::vector<Input> trace;
    demoTrace(rig, trace);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    ASSERT_FALSE(trace.empty());

    TickLog log;
    Bytes bytes;
    recordFight(rig, trace, kDefaultCheckpointInterval, log, bytes);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    ASSERT_GE(bytes.size(), kReplayHeaderBytes);

    // The magic is compared as BYTES, not as a uint32 constant, so the check is
    // endian-independent and the file is greppable.
    for (std::size_t i = 0; i < 4; ++i)
        EXPECT_EQ(bytes[kOffMagic + i], kReplayMagic[i]) << "magic byte " << i;

    EXPECT_EQ(readU16(bytes, kOffVersion), kReplayVersion);
    EXPECT_EQ(readU16(bytes, kOffStateBytes), sizeof(GameState))
        << "stateBytes is not sizeof(GameState), so the reader cannot tell 'the "
           "state LAYOUT changed' from 'the state VALUES changed'";
    EXPECT_EQ(readU32(bytes, kOffMatchDataHash), HashMatchData(rig.build.data));
    EXPECT_EQ(readU32(bytes, kOffSeed), kSeed)
        << "the seed is stored AS GIVEN, so the file reads against the call site "
           "that produced it";
    EXPECT_EQ(readI32(bytes, kOffStartPosX0), kP0X);
    EXPECT_EQ(readI32(bytes, kOffStartPosX1), kP1X);
    EXPECT_EQ(readU32(bytes, kOffTickCount), trace.size());
    EXPECT_EQ(readU32(bytes, kOffInterval), kDefaultCheckpointInterval);
    EXPECT_EQ(idField(bytes, kOffCharacterId0), rig.character.id);
    EXPECT_EQ(idField(bytes, kOffCharacterId1), rig.character.id);

    const std::uint32_t runCount        = readU32(bytes, kOffRunCount);
    const std::uint32_t checkpointCount = readU32(bytes, kOffCheckpointCount);
    EXPECT_GE(runCount, 1u);
    EXPECT_GE(checkpointCount, 1u);

    // TOTAL FILE SIZE = 104 + 6*runCount + 8*checkpointCount, EXACTLY. Trailing
    // bytes are how a payload rides along inside a file that otherwise validates.
    EXPECT_EQ(bytes.size(),
              kReplayHeaderBytes + kReplayRunBytes * runCount +
                  kReplayCheckpointBytes * checkpointCount)
        << "the encoded size does not match the header's own counts";

    // THE RLE CLAIM, MEASURED -- and it is no longer "one run".
    //
    // This asserted runCount == 1, on the premise that a self-cancel loop's trace
    // is one button held for the whole demonstration. That stopped being true
    // when the trace builder started RELEASING between repeats of the same
    // button: a witness that cancels a move into itself asks for the same bit
    // twice running, and a held bit is one press however long it lasts. The old
    // number was measuring a trace that could not perform its own loop.
    //
    // What the encoding actually has to do is unchanged, so that is what is
    // asserted now: far fewer runs than ticks, and a smaller file than a flat
    // log. Both survive the trace's shape changing again.
    EXPECT_LT(runCount, trace.size() / 2)
        << "a " << trace.size() << "-tick demonstration encoded as " << runCount
        << " runs. Press/release alternation still leaves long stretches of held "
           "bits between moves, so an encoding near one run per tick is not doing "
           "the one thing it exists for.";
    EXPECT_EQ(readU16(bytes, runOffset(0) + 0), trace.front().bits);
    EXPECT_EQ(readU16(bytes, runOffset(0) + 2), 0u) << "the silent dummy's bits";
    EXPECT_LT(bytes.size(), 4u * trace.size())
        << "the run-length encoded file is not smaller than a flat log would be";

    // Run lengths sum to EXACTLY tickCount, and no run is zero-length -- a
    // zero-length run is how a hostile file makes a decoder loop without
    // consuming input.
    std::uint32_t summed = 0;
    for (std::uint32_t i = 0; i < runCount; ++i) {
        const std::uint16_t length = readU16(bytes, runOffset(i) + 4);
        ASSERT_GT(length, 0u) << "run " << i << " has zero length";
        summed += length;
    }
    EXPECT_EQ(summed, readU32(bytes, kOffTickCount));

    // Checkpoints: strictly increasing, in range, last one always the last tick,
    // and every value equal to the checksum this file's own log recorded for the
    // state when state.tick == tick + 1. THE OFF-BY-ONE IS THE POINT: an
    // implementer who keys on state.tick writes every checkpoint one tick late.
    std::uint32_t previous = 0;
    bool first = true;
    for (std::uint32_t i = 0; i < checkpointCount; ++i) {
        const std::size_t   off  = checkpointOffset(bytes, i);
        const std::uint32_t tick = readU32(bytes, off + 0);
        const std::uint32_t sum  = readU32(bytes, off + 4);
        ASSERT_LT(tick, trace.size()) << "checkpoint " << i << " is out of range";
        if (!first) ASSERT_LT(previous, tick) << "checkpoint " << i << " is not after " << (i - 1);
        previous = tick;
        first    = false;

        ASSERT_LT(tick, log.Size());
        ASSERT_EQ(sum, log.samples[tick].checksum)
            << "checkpoint " << i << " at tick " << tick << " does not hold the "
               "checksum of the state after that tick ran. If it holds the "
               "checksum of tick " << (tick == 0 ? 0u : tick - 1u)
            << " instead, the recorder keyed on state.tick rather than "
               "TickView::tick.";
    }
    EXPECT_EQ(previous, trace.size() - 1)
        << "the final tick did not get a checkpoint, so this replay has no "
           "end-to-end check";

    RecordProperty("replay_bytes", static_cast<int>(bytes.size()));
    RecordProperty("replay_ticks", static_cast<int>(trace.size()));
    RecordProperty("replay_runs", static_cast<int>(runCount));
    std::cout << "\n[ REPLAY ] " << trace.size() << " ticks of a demonstration in "
              << bytes.size() << " bytes: a " << kReplayHeaderBytes
              << "-byte header, " << runCount << " run(s) of " << kReplayRunBytes
              << " and " << checkpointCount << " checkpoint(s) of "
              << kReplayCheckpointBytes << ".\n\n";
}

// THE CLAIM THE WHOLE FEATURE RESTS ON. Drive a fight, record it, play it back,
// and compare at EVERY checkpoint and at the end -- not just the final one. It is
// only true because the kernel is deterministic, which is the same fact
// tests/test_determinism_crossplat.cpp asserts with a golden hash.
TEST(GameReplayRoundTrip, PlaybackIsBitIdenticalAtEveryCheckpointAndAtTheEnd) {
    Rig rig{};
    bringUpInfinite(rig);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    std::vector<Input> trace;
    demoTrace(rig, trace);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    // Interval 5 rather than the default 60, so this replay carries MANY
    // checkpoints and "at every checkpoint" is a statement about tens of
    // comparisons rather than about two. The final tick gets one whatever the
    // interval is, which is what makes the last assertion below end-to-end.
    TickLog recordLog;
    Bytes   bytes;
    recordFight(rig, trace, 5, recordLog, bytes);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    ReplayReadOptions options{};
    options.expectedMatchDataHash = HashMatchData(rig.build.data);

    ReplayData   replay{};
    ReplayReport report{};
    ASSERT_TRUE(DecodeReplay("round-trip", bytes.data(), bytes.size(), options,
                             replay, report))
        << "a replay this module just wrote did not read back: " << report.error
        << " (" << ReplayRefusalName(report.refusal) << ")";
    EXPECT_TRUE(report.error.empty());
    EXPECT_EQ(report.refusal, ReplayRefusal::None);
    EXPECT_TRUE(report.warnings.empty())
        << "a clean read produced a warning: " << report.warnings.front();

    expectStructurallySound(replay, "round trip");
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    // THE FILE IS RLE; THE DECODED REPLAY IS FLAT. One entry per tick, which is
    // what keeps ReplayInputSource::At an O(1) pure function of a tick index.
    ASSERT_EQ(replay.TickCount(), trace.size());
    ASSERT_EQ(replay.inputs.size(), trace.size());
    for (std::size_t t = 0; t < trace.size(); ++t) {
        ASSERT_EQ(replay.inputs[t].p[0].bits, trace[t].bits) << "tick " << t;
        ASSERT_EQ(replay.inputs[t].p[1].bits, 0u) << "tick " << t;
    }
    EXPECT_EQ(replay.start.seed, kSeed);
    EXPECT_EQ(replay.start.startPosX[0], kP0X);
    EXPECT_EQ(replay.start.startPosX[1], kP1X);
    EXPECT_EQ(replay.characterId[0], rig.character.id);
    EXPECT_EQ(replay.characterId[1], rig.character.id);
    EXPECT_EQ(replay.checkpointInterval, 5u);
    EXPECT_GT(replay.checkpoints.size(), 10u)
        << "interval 5 over " << trace.size() << " ticks produced only "
        << replay.checkpoints.size() << " checkpoints, so 'every checkpoint' is "
           "not saying much";

    // The source reads what the file says, and answers `authored = false` past
    // the end rather than indexing off a vector.
    ReplayInputSource source(replay, 0);
    EXPECT_EQ(source.AuthoredEndTick(), replay.TickCount());
    EXPECT_TRUE(source.At(0).authored);
    EXPECT_EQ(source.At(0).input.bits, trace.front().bits);
    EXPECT_FALSE(source.At(replay.TickCount()).authored);
    EXPECT_EQ(source.At(replay.TickCount()).input.bits, 0u);
    EXPECT_FALSE(source.At(kUnboundedTick).authored);
    ASSERT_NE(source.Name(), nullptr);
    EXPECT_GT(std::strlen(source.Name()), 0u);

    // --- play it back --------------------------------------------------------
    TickLog        playLog;
    ReplayVerifier verifier(replay);
    playBack(replay, rig.build.data, playLog, verifier);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    ASSERT_EQ(playLog.Size(), trace.size());
    EXPECT_TRUE(playLog.Clean());

    // EVERY CHECKPOINT, compared by this file rather than taken on the verifier's
    // word -- and then the verifier's own count compared against the same number,
    // so a verifier that compared nothing cannot read as one that agreed with
    // everything.
    for (std::size_t i = 0; i < replay.checkpoints.size(); ++i) {
        const ReplayCheckpoint& cp = replay.checkpoints[i];
        ASSERT_LT(cp.tick, playLog.Size());
        ASSERT_EQ(playLog.samples[cp.tick].checksum, cp.checksum)
            << "checkpoint " << i << " at tick " << cp.tick
            << " did not reproduce on playback. RECORD -> PLAYBACK IS NOT "
               "BIT-IDENTICAL, which is the claim the whole feature rests on."
            << Table(playLog, rig.build.moves[0], cp.tick > 4 ? cp.tick - 4 : 0, 10);
        ASSERT_EQ(recordLog.samples[cp.tick].checksum, cp.checksum)
            << "checkpoint " << i << " does not match the RECORDING run either, "
               "so the recorder wrote a checksum of something else.";
    }

    EXPECT_FALSE(verifier.Result().diverged)
        << "the verifier reported a divergence at tick " << verifier.Result().tick
        << ": recorded " << verifier.Result().recordedChecksum << ", live "
        << verifier.Result().liveChecksum;
    EXPECT_FALSE(verifier.Result().inputMismatch)
        << "the verifier saw different inputs than the file records at tick "
        << verifier.Result().inputMismatchTick << ", which is a HOST WIRING BUG "
           "in this test rather than a finding about the engine";
    EXPECT_EQ(verifier.CheckpointsCompared(), replay.checkpoints.size())
        << "the verifier did not compare every checkpoint in the file";
    EXPECT_EQ(verifier.CheckpointsAgreed(), verifier.CheckpointsCompared());
    EXPECT_FALSE(verifier.ShouldStop());

    // AND AT THE END: the whole 80-byte state, not merely its hash.
    ASSERT_EQ(0, std::memcmp(&recordLog.Final(), &playLog.Final(), sizeof(GameState)))
        << "the final states differ although every checkpoint agreed, which means "
           "the checkpoints do not cover the last tick.";

    // Every tick, not only the checkpointed ones. The file only promises the
    // checkpoints; this says the rest came along too, which is what makes the
    // checkpoint interval a cost/precision trade rather than a correctness one.
    for (std::size_t t = 0; t < trace.size(); ++t)
        ASSERT_EQ(recordLog.samples[t].checksum, playLog.samples[t].checksum)
            << "playback first diverged at tick " << t << ", between checkpoints."
            << Table(playLog, rig.build.moves[0],
                     t > 4 ? static_cast<std::uint32_t>(t) - 4u : 0u, 10);

    RecordProperty("checkpoints_compared", static_cast<int>(verifier.CheckpointsCompared()));
}

// The same round trip THROUGH THE FILESYSTEM, because a playtester who validates
// a combo and wants to post it is handling a file rather than a buffer.
TEST(GameReplayRoundTrip, AFileWrittenAndReadBackIsTheSameReplay) {
    Rig rig{};
    bringUpInfinite(rig);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    std::vector<Input> trace;
    demoTrace(rig, trace);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    FightSession session;
    std::string error;
    ASSERT_TRUE(session.Begin(rig.setup, error)) << error;

    ReplayRecorderOptions options{};
    options.checkpointInterval = 12;
    ReplayRecorder recorder(options);
    ASSERT_TRUE(recorder.Begin(rig.setup.start, HashMatchData(rig.build.data),
                               rig.character.id, rig.character.id, error))
        << error;
    ASSERT_TRUE(session.AddObserver(&recorder));

    TickLog log;
    ASSERT_TRUE(session.AddObserver(&log));
    ScriptedInputSource source(trace, 0, "DEMO");
    session.SetInputSource(0, &source);
    run(session, static_cast<std::uint32_t>(trace.size()));
    ASSERT_TRUE(recorder.Error().empty()) << recorder.Error();

    const std::string dir = replayDir();
    ASSERT_TRUE(recorder.Write(dir, "combo.csrp", error)) << error;

    Bytes encoded;
    ASSERT_TRUE(recorder.Encode(encoded, error)) << error;

    // The file on disk is EXACTLY the bytes Encode hands back -- Encode exists so
    // a test can assert the format without touching the filesystem, and that is
    // only worth anything if the two agree.
    Bytes fromDisk;
    {
        std::ifstream in(std::filesystem::path(dir) / "combo.csrp", std::ios::binary);
        ASSERT_TRUE(in.good()) << "the replay this test just wrote is not there";
        fromDisk.assign(std::istreambuf_iterator<char>(in),
                        std::istreambuf_iterator<char>());
    }
    ASSERT_EQ(fromDisk.size(), encoded.size());
    EXPECT_EQ(0, std::memcmp(fromDisk.data(), encoded.data(), encoded.size()))
        << "Write and Encode produced different bytes";

    ReplayReadOptions readOptions{};
    readOptions.expectedMatchDataHash = HashMatchData(rig.build.data);

    ReplayData   replay{};
    ReplayReport report{};
    ASSERT_TRUE(ReadReplayFile(dir, "combo.csrp", readOptions, replay, report))
        << report.error << " (" << ReplayRefusalName(report.refusal) << ")";
    expectStructurallySound(replay, "file round trip");
    ASSERT_EQ(replay.TickCount(), trace.size());
    for (std::size_t t = 0; t < trace.size(); ++t)
        ASSERT_EQ(replay.inputs[t].p[0].bits, trace[t].bits) << "tick " << t;

    TickLog        playLog;
    ReplayVerifier verifier(replay);
    playBack(replay, rig.build.data, playLog, verifier);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    EXPECT_FALSE(verifier.Result().diverged);
    EXPECT_EQ(verifier.CheckpointsAgreed(), verifier.CheckpointsCompared());
    EXPECT_GT(verifier.CheckpointsCompared(), 0u);
    ASSERT_EQ(0, std::memcmp(&log.Final(), &playLog.Final(), sizeof(GameState)));
}

// RE-SIMULATION OVERWRITES, IT DOES NOT APPEND -- and a GAP is a hard error.
//
// The obvious implementation, push_back, produces a plausible-looking file whose
// input log is the union of every predicted and corrected timeline, which then
// replays as something nobody did. This test rolls a session back, feeds
// DIFFERENT inputs the second time, and requires the file to be a recording of
// the second timeline only.
TEST(GameReplayRecorder, ResimulationOverwritesAndAGapIsRefused) {
    Rig rig{};
    bringUpInfinite(rig);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    const std::uint16_t held = rig.bindings[0].button;

    FightSession session;
    std::string error;
    ASSERT_TRUE(session.Begin(rig.setup, error)) << error;

    ReplayRecorderOptions options{};
    options.checkpointInterval = 4;
    ReplayRecorder recorder(options);
    ASSERT_TRUE(recorder.Begin(rig.setup.start, HashMatchData(rig.build.data),
                               rig.character.id, rig.character.id, error))
        << error;
    ASSERT_TRUE(session.AddObserver(&recorder));

    // A watcher too, because the same TickView flag it must react to -- and its
    // reaction is the opposite one: the recorder corrects itself, the judge stops
    // reporting rather than report about a timeline that no longer happened.
    ComboWatcher watcher(0, &rig.build.moves[0], &rig.verdict);
    watcher.Reset();
    ASSERT_TRUE(session.AddObserver(&watcher));

    for (int i = 0; i < 20; ++i) session.Tick(pairOf(held, 0));
    ASSERT_EQ(recorder.TickCount(), 20u);
    EXPECT_FALSE(watcher.Stale()) << "nothing has been re-simulated yet";

    GameState atTen{};
    // Re-run from tick 10 with a DIFFERENT input, which is exactly what a
    // rollback that mispredicted does.
    {
        FightSession probe;
        std::string probeError;
        ASSERT_TRUE(probe.Begin(rig.setup, probeError)) << probeError;
        for (int i = 0; i < 10; ++i) probe.Tick(pairOf(held, 0));
        probe.Snapshot(atTen);
    }
    session.Restore(atTen);
    ASSERT_EQ(session.CurrentTick(), 10u);
    for (int i = 0; i < 10; ++i) session.Tick(pairOf(0, 0));

    EXPECT_TRUE(recorder.Error().empty()) << recorder.Error();
    EXPECT_EQ(recorder.TickCount(), 20u)
        << "the recorder appended the corrected timeline instead of overwriting "
           "it, so the file is the union of two timelines and replays as "
           "something nobody did";

    // The judge went stale on the first re-simulated tick rather than carrying on
    // with a history that no longer describes anything.
    EXPECT_TRUE(watcher.Stale())
        << "the combo judge kept reporting through a rollback, so a playtester "
           "would be shown a verdict about a timeline that no longer happened";

    Bytes bytes;
    ASSERT_TRUE(recorder.Encode(bytes, error)) << error;

    ReplayReadOptions readOptions{};
    readOptions.expectedMatchDataHash = HashMatchData(rig.build.data);
    ReplayData   replay{};
    ReplayReport report{};
    ASSERT_TRUE(DecodeReplay("rollback", bytes.data(), bytes.size(), readOptions,
                             replay, report))
        << report.error;
    ASSERT_EQ(replay.TickCount(), 20u);
    for (std::size_t t = 0; t < 10; ++t)
        EXPECT_EQ(replay.inputs[t].p[0].bits, held) << "tick " << t;
    for (std::size_t t = 10; t < 20; ++t)
        EXPECT_EQ(replay.inputs[t].p[0].bits, 0u)
            << "tick " << t << " still holds the MISPREDICTED input, so the "
               "overwrite did not happen";

    // ...and the corrected file plays back to the state the corrected session
    // actually reached.
    TickLog        playLog;
    ReplayVerifier verifier(replay);
    playBack(replay, rig.build.data, playLog, verifier);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    EXPECT_FALSE(verifier.Result().diverged)
        << "the corrected recording does not reproduce the corrected fight";
    EXPECT_EQ(0, std::memcmp(&session.State(), &playLog.Final(), sizeof(GameState)));

    // --- A GAP IS A HARD ERROR ----------------------------------------------
    //
    // Above the current length means a tick was never delivered. Zero-filling it
    // is refused because NEUTRAL IS A LEGAL THING TO PRESS: a zero-filled gap is
    // indistinguishable from a player who let go, and the file would be a
    // confident recording of a fight that did not happen.
    ReplayRecorder gapped(options);
    std::string gapError;
    ASSERT_TRUE(gapped.Begin(rig.setup.start, HashMatchData(rig.build.data),
                             rig.character.id, rig.character.id, gapError))
        << gapError;

    GameState made{};
    cse::kernel::ResetMatch(made, kSeed);
    made.tick = 6;                      // state->tick == view.tick + 1
    TickView view{};
    view.tick   = 5;                    // nothing has been delivered yet, so this is a gap
    view.inputs = pairOf(held, 0);
    view.state  = &made;
    view.data   = &rig.build.data;
    gapped.OnTick(view);

    EXPECT_FALSE(gapped.Error().empty())
        << "a tick above the recorder's own length was accepted, so the file will "
           "have a hole in it that reads as neutral input";
    EXPECT_FALSE(gapped.Recording());
    Bytes refused;
    std::string encodeError;
    EXPECT_FALSE(gapped.Encode(refused, encodeError))
        << "an inert recorder still produced bytes";
    EXPECT_FALSE(encodeError.empty());
    EXPECT_FALSE(gapped.Write(replayDir(), "should_not_exist.csrp", encodeError));
}

// A RESTARTED SESSION IS THE THIRD HARD ERROR, and it is the one that would have
// survived being shared.
//
// FightSession::Begin resets the tick index and the high-water mark and KEEPS
// its observers -- asserted in section 2, and deliberate, because a host that
// rebinds its recorder and its combo judge on every reset drops one of them
// eventually. So the first tick after a restart arrives at 0 with `resimulated`
// CLEAR while the recorder still holds the previous round, and the two rules
// that could apply to it point opposite ways: by INDEX it looks like the
// overwrite a rollback performs, and by the FLAG -- which is what Replay.h keys
// the rule on -- it is not a re-simulation at all.
//
// Taking the index reading discards the recording one tick at a time while
// `start`, the MatchData hash and both character ids still describe the match
// that was abandoned. THE RESULTING FILE VALIDATES: nothing in the format can
// notice that its header and its input log came from different matches, so it
// decodes, plays back, diverges, and the divergence is blamed on the engine.
// That is a worse outcome than a refusal, and it is the same sin as a
// zero-filled gap arriving by a third route.
TEST(GameReplayRecorder, ARestartedSessionIsRefusedRatherThanOverwritten) {
    Rig rig{};
    bringUpInfinite(rig);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    const std::uint16_t held = rig.bindings[0].button;

    FightSession session;
    std::string error;
    ASSERT_TRUE(session.Begin(rig.setup, error)) << error;

    ReplayRecorder recorder;
    ASSERT_TRUE(recorder.Begin(rig.setup.start, HashMatchData(rig.build.data),
                               rig.character.id, rig.character.id, error))
        << error;
    ASSERT_TRUE(session.AddObserver(&recorder));

    for (int i = 0; i < 12; ++i) session.Tick(pairOf(held, 0));
    ASSERT_EQ(recorder.TickCount(), 12u);
    ASSERT_TRUE(recorder.Error().empty()) << recorder.Error();

    // The round restarts FROM A DIFFERENT OPENING and the recorder is not told.
    // The distance between the fighters is what decides whether the loop
    // connects at all, so this is the single number a file that kept the old
    // header would be most wrong about.
    FightSetup restarted = rig.setup;
    restarted.start.startPosX[0] = rig.setup.start.startPosX[0] + 4 * 256;
    ASSERT_TRUE(session.Begin(restarted, error)) << error;
    ASSERT_EQ(session.CurrentTick(), 0u);
    ASSERT_EQ(session.ObserverCount(), 1)
        << "the restart dropped the observers, so this test is no longer about "
           "the case it was written for";

    session.Tick(pairOf(held, 0));

    EXPECT_FALSE(recorder.Error().empty())
        << "tick 0 of a restarted session was accepted, so the recording now "
           "holds the NEW round's inputs under the OLD round's header";
    EXPECT_FALSE(recorder.Recording());
    EXPECT_NE(recorder.Error().find("Begin"), std::string::npos)
        << "the refusal does not name the call that was missed, which is the one "
           "thing the host can do about it: " << recorder.Error();

    // AND IT REFUSED BEFORE MUTATING. The overwrite this is not doing drops
    // everything above the arriving tick, so an accepted tick 0 would leave one
    // tick where twelve were -- which is the reading that survives a rewording of
    // the message above.
    EXPECT_EQ(recorder.TickCount(), 12u)
        << "the recorder truncated the round it had already recorded";

    Bytes bytes;
    std::string encodeError;
    EXPECT_FALSE(recorder.Encode(bytes, encodeError))
        << "an inert recorder still produced bytes";
    EXPECT_FALSE(encodeError.empty());
}

// Character ids are a FIXED 32-byte field, so no untrusted count ever sizes an
// allocation -- and an over-long id is REFUSED at record time rather than
// truncated, because truncation would produce a file whose error message names
// the wrong character.
TEST(GameReplayRecorder, AnOverLongCharacterIdIsRefusedRatherThanTruncated) {
    MatchStart start{};
    ReplayRecorder recorder;
    std::string error;

    const std::string exact(kReplayCharacterIdBytes, 'a');
    EXPECT_TRUE(recorder.Begin(start, 1u, exact, exact, error)) << error;
    EXPECT_TRUE(error.empty());

    const std::string tooLong(kReplayCharacterIdBytes + 1, 'a');
    ReplayRecorder second;
    error.clear();
    EXPECT_FALSE(second.Begin(start, 1u, tooLong, "ok", error))
        << "a " << tooLong.size() << "-byte character id was accepted into a "
        << kReplayCharacterIdBytes << "-byte field";
    EXPECT_FALSE(error.empty());

    ReplayRecorder third;
    error.clear();
    EXPECT_FALSE(third.Begin(start, 1u, "ok", tooLong, error));
    EXPECT_FALSE(error.empty());

    // And a recorder that was never told the initial conditions refuses every
    // later tick rather than writing a header of zeroes: an unplayable file that
    // validates is worse than no file.
    ReplayRecorder never;
    EXPECT_FALSE(never.Recording());
    Bytes bytes;
    error.clear();
    EXPECT_FALSE(never.Encode(bytes, error))
        << "a recorder that was never begun produced a file";
    EXPECT_FALSE(error.empty());
}

// ============================================================================
// 5. A REPLAY FILE IS UNTRUSTED INPUT  (CLAIM 2)
// ============================================================================
//
// It is the one artifact here a stranger on the internet hands you, and
// docs/MAINTENANCE.md's rule about authored paths admits no exceptions. Every
// count in the header is checked against the bytes actually present BEFORE it is
// used to size or index anything; a truncated, padded or hostile file is a clean
// refusal with a message; and the refusals are DISTINCT because the remedy is
// different every time -- re-download, update the game, get the old character
// back, or file a bug against the engine.
//
// WHAT THIS SECTION CLAIMS, PRECISELY, AND WHERE IT IS STRONGER THAN "EVERYTHING
// IS A REFUSAL".
//
// Some mutations of a valid replay produce a file that is still well-formed: a
// flipped bit inside a run's INPUT BITS is a different, perfectly legal
// recording, and a reader that refused it would be refusing a replay somebody
// could have made on purpose. So the invariant the fuzz asserts is the one a
// format can actually promise:
//
//     every mutation is EITHER a named refusal with a message, OR a
//     structurally sound ReplayData -- and never a crash, never a decoded
//     length that disagrees with the header, never a checkpoint pointing off
//     the end, never an allocation a header field talked the reader into.
//
// and then, separately and concretely, that a tampered INPUT STREAM which
// changes the fight is CAUGHT AT PLAYBACK by the checkpoints rather than
// swallowed. Between them those two say the thing that matters: a tampered file
// can never be silently accepted as the original.

namespace {

// A valid two-run replay and everything needed to decode it.
//
// TWO RUNS RATHER THAN ONE, DELIBERATELY. The demonstration is a single held
// button and therefore a single run, which cannot express "a zero-length run
// among well-formed ones" or "a divergence that begins after some checkpoints
// have already agreed". Sixty ticks of a held button and sixty of nothing gives
// both, and it is a fight a playtester would actually record: perform the combo,
// then let go.
struct Hostile {
    Rig               rig;
    std::vector<Input> trace;
    TickLog           log;
    Bytes             good;
    ReplayReadOptions options;

    std::uint32_t TickCount() const { return readU32(good, kOffTickCount); }
    std::uint32_t RunCount() const { return readU32(good, kOffRunCount); }
    std::uint32_t CheckpointCount() const { return readU32(good, kOffCheckpointCount); }
};

constexpr std::uint32_t kHostileHeldTicks = 60;
constexpr std::uint32_t kHostileFreeTicks = 60;

void buildHostileSubject(Hostile& h) {
    bringUpInfinite(h.rig);
    if (::testing::Test::HasFatalFailure()) return;

    const std::uint16_t held = h.rig.bindings[0].button;
    h.trace.assign(kHostileHeldTicks, inputOf(held));
    h.trace.resize(kHostileHeldTicks + kHostileFreeTicks, inputOf(0));

    recordFight(h.rig, h.trace, 5, h.log, h.good);
    if (::testing::Test::HasFatalFailure()) return;

    h.options.expectedMatchDataHash = HashMatchData(h.rig.build.data);

    ASSERT_EQ(h.RunCount(), 2u)
        << "the hostile-test subject was built to have exactly two runs -- a held "
           "button and a release -- and encoded as " << h.RunCount()
        << ". Several refusals below cannot be isolated with one run.";
    ASSERT_GT(h.CheckpointCount(), 4u);
    ASSERT_EQ(h.good.size(),
              kReplayHeaderBytes + kReplayRunBytes * h.RunCount() +
                  kReplayCheckpointBytes * h.CheckpointCount());
}

// Decode and require a refusal of a NAMED kind. The kind matters: three of the
// five are not the file's fault at all, and a playtester whose replay stops
// working needs to be told which one happened.
void expectRefusal(const Bytes& bytes, const ReplayReadOptions& options,
                   ReplayRefusal expected, const char* what) {
    ReplayData   data{};
    ReplayReport report{};
    const bool ok = DecodeReplay(what, bytes.empty() ? nullptr : bytes.data(),
                                 bytes.size(), options, data, report);
    EXPECT_FALSE(ok) << what << " was ACCEPTED";
    EXPECT_EQ(report.refusal, expected)
        << what << " was refused as `" << ReplayRefusalName(report.refusal)
        << "` and the header says `" << ReplayRefusalName(expected)
        << "`. These are not interchangeable -- the remedy is different for each."
           "\n  message: " << report.error;
    EXPECT_FALSE(report.error.empty())
        << what << " was refused with no message at all, so nobody holding the "
                   "file can act on it";
}

}  // namespace

// EVERY TRUNCATION, at every length. A short file must never be read past its
// end, and it must never be accepted because the header said the missing bytes
// were there.
TEST(GameReplayRefusal, EveryTruncationIsARefusalAndNeverACrash) {
    Hostile h{};
    buildHostileSubject(h);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    // Nothing at all, including a null pointer, which is what a caller with an
    // empty buffer hands over.
    {
        ReplayData   data{};
        ReplayReport report{};
        EXPECT_FALSE(DecodeReplay("empty", nullptr, 0, h.options, data, report));
        EXPECT_NE(report.refusal, ReplayRefusal::None);
        EXPECT_FALSE(report.error.empty());
    }

    for (std::size_t length = 0; length < h.good.size(); ++length) {
        Bytes cut(h.good.begin(), h.good.begin() + static_cast<std::ptrdiff_t>(length));

        ReplayData   data{};
        ReplayReport report{};
        const bool ok = DecodeReplay("truncated", cut.empty() ? nullptr : cut.data(),
                                     cut.size(), h.options, data, report);
        ASSERT_FALSE(ok) << "a file truncated to " << length << " of "
                         << h.good.size() << " bytes was ACCEPTED";
        ASSERT_NE(report.refusal, ReplayRefusal::None) << "at length " << length;
        ASSERT_FALSE(report.error.empty()) << "at length " << length;

        // Once the whole header is present, every field in it is valid and the
        // ONLY complaint left is that the sections do not fit -- which is
        // Malformed and not one of the four refusals that mean something else.
        if (length >= kReplayHeaderBytes)
            ASSERT_EQ(report.refusal, ReplayRefusal::Malformed)
                << "a file truncated to " << length << " bytes was refused as `"
                << ReplayRefusalName(report.refusal)
                << "`; its header is intact and its magic, version and stateBytes "
                   "all match, so the only thing wrong with it is its size.";
    }

    // And the untruncated file is fine, so the sweep above is not passing because
    // nothing ever reads.
    ReplayData   data{};
    ReplayReport report{};
    EXPECT_TRUE(DecodeReplay("intact", h.good.data(), h.good.size(), h.options,
                             data, report))
        << report.error;
}

// PADDING IS A REFUSAL TOO. A lenient reader is how a payload rides along inside
// a file that otherwise validates.
TEST(GameReplayRefusal, TrailingBytesAreARefusalRatherThanIgnored) {
    Hostile h{};
    buildHostileSubject(h);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    for (std::size_t extra : { std::size_t{1}, std::size_t{7}, std::size_t{4096} }) {
        Bytes padded = h.good;
        padded.resize(padded.size() + extra, 0x41);
        expectRefusal(padded, h.options, ReplayRefusal::Malformed, "a padded file");
    }
}

// THE FIVE NAMED REFUSALS, EACH FIRED ON ITS OWN CAUSE.
TEST(GameReplayRefusal, TheNamedRefusalsAreDistinctAndFireOnTheirOwnCause) {
    Hostile h{};
    buildHostileSubject(h);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    // --- NotAReplay: this is not a replay file at all ------------------------
    //
    // Say so, rather than reporting a corrupt replay, because the usual cause is
    // the wrong file being picked.
    for (std::size_t i = 0; i < 4; ++i) {
        Bytes wrong = h.good;
        wrong[kOffMagic + i] ^= 0xFFu;
        expectRefusal(wrong, h.options, ReplayRefusal::NotAReplay,
                      "a file whose magic is wrong");
    }

    // --- Version: name BOTH numbers -----------------------------------------
    {
        Bytes wrong = h.good;
        writeU16(wrong, kOffVersion, 4242u);

        ReplayData   data{};
        ReplayReport report{};
        EXPECT_FALSE(DecodeReplay("version", wrong.data(), wrong.size(), h.options,
                                  data, report));
        EXPECT_EQ(report.refusal, ReplayRefusal::Version)
            << "refused as " << ReplayRefusalName(report.refusal);
        EXPECT_NE(report.error.find("4242"), std::string::npos)
            << "the version refusal does not name the version the FILE carries, "
               "so the reader cannot tell which build wrote it: " << report.error;
        EXPECT_NE(report.error.find(std::to_string(kReplayVersion)), std::string::npos)
            << "the version refusal does not name the version this BUILD expects: "
            << report.error;

        // A version BELOW ours is refused just as hard. There is no forward and
        // no backward compatibility shim, and there will not be one.
        Bytes older = h.good;
        writeU16(older, kOffVersion, 0u);
        expectRefusal(older, h.options, ReplayRefusal::Version, "an older version");
    }

    // --- StateLayout: the checksums hash a differently shaped object ---------
    //
    // Distinct from a value-level divergence and must not be collapsed into it:
    // comparing checksums of two differently shaped structs would produce a
    // divergence report that means nothing.
    {
        Bytes wrong = h.good;
        writeU16(wrong, kOffStateBytes,
                 static_cast<std::uint16_t>(sizeof(GameState) + 4u));
        expectRefusal(wrong, h.options, ReplayRefusal::StateLayout,
                      "a file recorded against a different GameState layout");

        Bytes smaller = h.good;
        writeU16(smaller, kOffStateBytes,
                 static_cast<std::uint16_t>(sizeof(GameState) - 4u));
        expectRefusal(smaller, h.options, ReplayRefusal::StateLayout,
                      "a smaller recorded GameState");
    }

    // --- TooLarge: well-formed, but past a limit the CALLER chose ------------
    //
    // Separate from Malformed on purpose: the file is fine and the limit is a
    // policy. An enormous tickCount is refused BEFORE anything is reserved, which
    // is the rule that keeps a four-byte field from becoming a multi-gigabyte
    // allocation.
    {
        Bytes huge = h.good;
        writeU32(huge, kOffTickCount, 0xFFFFFFFFu);
        expectRefusal(huge, h.options, ReplayRefusal::TooLarge,
                      "a file claiming 4294967295 ticks");

        Bytes overCap = h.good;
        writeU32(overCap, kOffTickCount, kMaxMatchTicks + 1u);
        expectRefusal(overCap, h.options, ReplayRefusal::TooLarge,
                      "a file one tick past kMaxMatchTicks");

        // ...and a caller with a tighter policy gets the same answer about a file
        // that is perfectly legal by the format's own caps.
        ReplayReadOptions tight = h.options;
        tight.maxTicks = h.TickCount() - 1u;
        expectRefusal(h.good, tight, ReplayRefusal::TooLarge,
                      "a valid file over this caller's own tick cap");

        ReplayReadOptions exact = h.options;
        exact.maxTicks = h.TickCount();
        ReplayData   data{};
        ReplayReport report{};
        EXPECT_TRUE(DecodeReplay("at the cap", h.good.data(), h.good.size(), exact,
                                 data, report))
            << "a file exactly at the caller's cap was refused: " << report.error;
    }

    // --- Malformed: the structural rules, one at a time ---------------------
    {
        // A zero count where one is required. Each of these is checked on its own
        // rather than as a lump, because a reader that only rejected them
        // together could be missing two of the three.
        for (std::size_t off : { kOffTickCount, kOffRunCount, kOffCheckpointCount,
                                 kOffInterval }) {
            Bytes zeroed = h.good;
            writeU32(zeroed, off, 0u);
            expectRefusal(zeroed, h.options, ReplayRefusal::Malformed,
                          "a header count of zero");
        }

        // A ZERO-LENGTH RUN, with the total still summing correctly. This is the
        // one that needs two runs: it is how a hostile file makes a decoder loop
        // without consuming input, and "harmless" is not a property to leave to
        // the shape of somebody's for-loop.
        {
            Bytes zeroRun = h.good;
            const std::uint16_t second = readU16(zeroRun, runOffset(1) + 4);
            writeU16(zeroRun, runOffset(0) + 4, 0u);
            writeU16(zeroRun, runOffset(1) + 4,
                     static_cast<std::uint16_t>(second + kHostileHeldTicks));
            // The sum is still exactly tickCount, so only the zero-length rule can
            // catch this.
            std::uint32_t summed = 0;
            for (std::uint32_t i = 0; i < h.RunCount(); ++i)
                summed += readU16(zeroRun, runOffset(i) + 4);
            ASSERT_EQ(summed, h.TickCount())
                << "this test's own hostile file is malformed for a second reason, "
                   "so it cannot isolate the zero-length-run rule";
            expectRefusal(zeroRun, h.options, ReplayRefusal::Malformed,
                          "a zero-length run whose file still sums correctly");
        }

        // Run lengths that do not sum to tickCount. Less is truncation, more is a
        // lie; both are refusals, and neither changes the file's SIZE, so only the
        // sum check can catch them.
        {
            Bytes short_ = h.good;
            writeU16(short_, runOffset(1) + 4,
                     static_cast<std::uint16_t>(kHostileFreeTicks - 1u));
            expectRefusal(short_, h.options, ReplayRefusal::Malformed,
                          "run lengths that sum to less than tickCount");

            Bytes over = h.good;
            writeU16(over, runOffset(1) + 4,
                     static_cast<std::uint16_t>(kHostileFreeTicks + 1u));
            expectRefusal(over, h.options, ReplayRefusal::Malformed,
                          "run lengths that sum to more than tickCount");
        }

        // Checkpoints out of range, out of order, and not ending on the last tick.
        // The section validates on its own, structurally, before any checkpoint is
        // compared against anything.
        {
            const std::uint32_t last = h.CheckpointCount() - 1u;

            Bytes outOfRange = h.good;
            writeU32(outOfRange, checkpointOffset(outOfRange, last) + 0, h.TickCount());
            expectRefusal(outOfRange, h.options, ReplayRefusal::Malformed,
                          "a checkpoint naming a tick past the end");

            Bytes wayOut = h.good;
            writeU32(wayOut, checkpointOffset(wayOut, last) + 0, 0xFFFFFFFFu);
            expectRefusal(wayOut, h.options, ReplayRefusal::Malformed,
                          "a checkpoint naming tick 4294967295");

            Bytes notIncreasing = h.good;
            writeU32(notIncreasing, checkpointOffset(notIncreasing, 1) + 0,
                     readU32(notIncreasing, checkpointOffset(notIncreasing, 0) + 0));
            expectRefusal(notIncreasing, h.options, ReplayRefusal::Malformed,
                          "two checkpoints on the same tick");

            Bytes backwards = h.good;
            writeU32(backwards, checkpointOffset(backwards, 1) + 0, 0u);
            expectRefusal(backwards, h.options, ReplayRefusal::Malformed,
                          "checkpoints that go backwards");

            Bytes noEnd = h.good;
            writeU32(noEnd, checkpointOffset(noEnd, last) + 0, h.TickCount() - 2u);
            expectRefusal(noEnd, h.options, ReplayRefusal::Malformed,
                          "a file whose last checkpoint is not the last tick");
        }

        // A count that makes the sections not fit. runCount is never trusted to
        // describe the file: the reader computes what the file's own length
        // implies and refuses when they disagree.
        {
            Bytes manyRuns = h.good;
            writeU32(manyRuns, kOffRunCount, 0x0FFFFFFFu);
            expectRefusal(manyRuns, h.options, ReplayRefusal::Malformed,
                          "a header claiming 268435455 runs");

            Bytes manyCheckpoints = h.good;
            writeU32(manyCheckpoints, kOffCheckpointCount, 0x0FFFFFFFu);
            expectRefusal(manyCheckpoints, h.options, ReplayRefusal::Malformed,
                          "a header claiming 268435455 checkpoints");

            Bytes oneMoreRun = h.good;
            writeU32(oneMoreRun, kOffRunCount, h.RunCount() + 1u);
            expectRefusal(oneMoreRun, h.options, ReplayRefusal::Malformed,
                          "a header claiming one more run than the file holds");
        }

        // A character id with data after its terminator: 32 fixed bytes is what
        // keeps an untrusted count from sizing an allocation, and a field with
        // junk past its NUL is a field somebody is hiding something in.
        {
            Bytes dirty = h.good;
            const std::size_t idLength = h.rig.character.id.size();
            ASSERT_LT(idLength + 2, kReplayCharacterIdBytes)
                << "this character's id fills the field, so there is no room after "
                   "its terminator to hide anything in";
            dirty[kOffCharacterId0 + idLength + 1] = 'X';
            expectRefusal(dirty, h.options, ReplayRefusal::Malformed,
                          "a character id with data after its terminator");

            Bytes dirty1 = h.good;
            dirty1[kOffCharacterId1 + idLength + 1] = 'X';
            expectRefusal(dirty1, h.options, ReplayRefusal::Malformed,
                          "the second character id with data after its terminator");
        }
    }
}

// A REPLAY PATH IS UNTRUSTED ON BOTH SIDES: a stranger hands you the file, and a
// playtester types the name they are saving it under. Both go through
// MyCoreEngine::PathIsContained, lexically, BEFORE any filesystem access -- so a
// symlink cannot be used to slip past canonicalization.
TEST(GameReplayRefusal, AnUntrustedPathIsRefusedBeforeTheFileIsOpened) {
    Hostile h{};
    buildHostileSubject(h);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    const std::string dir = replayDir();

    // Something valid on disk first, so a refusal below cannot be "the file was
    // not there" wearing a path refusal's clothes.
    {
        std::ofstream out(std::filesystem::path(dir) / "valid.csrp", std::ios::binary);
        ASSERT_TRUE(out.good());
        out.write(reinterpret_cast<const char*>(h.good.data()),
                  static_cast<std::streamsize>(h.good.size()));
    }
    {
        ReplayData   data{};
        ReplayReport report{};
        ASSERT_TRUE(ReadReplayFile(dir, "valid.csrp", h.options, data, report))
            << report.error;
    }

    const std::string absolute = (std::filesystem::path(dir) / "valid.csrp").string();
    const char* const hostile[] = {
        "../valid.csrp",
        "../../valid.csrp",
        "sub/../../valid.csrp",
        "..",
    };

    for (const char* rel : hostile) {
        ReplayData   data{};
        ReplayReport report{};
        EXPECT_FALSE(ReadReplayFile(dir, rel, h.options, data, report))
            << "`" << rel << "` escaped the sandbox";
        EXPECT_EQ(report.refusal, ReplayRefusal::PathRefused)
            << "`" << rel << "` was refused as `"
            << ReplayRefusalName(report.refusal) << "`: " << report.error;
        EXPECT_FALSE(report.error.empty());
    }

    {
        ReplayData   data{};
        ReplayReport report{};
        EXPECT_FALSE(ReadReplayFile(dir, absolute, h.options, data, report))
            << "an absolute path was joined onto the base directory, which "
               "std::filesystem::path::operator/ does by REPLACING the base";
        EXPECT_EQ(report.refusal, ReplayRefusal::PathRefused) << report.error;
    }

    // The write side takes the same route, and writes NOTHING at all on refusal:
    // a partially written replay that fails validation on load is a worse outcome
    // than no file.
    {
        ReplayRecorder recorder;
        std::string error;
        ASSERT_TRUE(recorder.Begin(h.rig.setup.start, HashMatchData(h.rig.build.data),
                                   h.rig.character.id, h.rig.character.id, error))
            << error;

        FightSession session;
        ASSERT_TRUE(session.Begin(h.rig.setup, error)) << error;
        ASSERT_TRUE(session.AddObserver(&recorder));
        run(session, 4);
        ASSERT_TRUE(recorder.Error().empty()) << recorder.Error();

        for (const char* rel : hostile) {
            error.clear();
            EXPECT_FALSE(recorder.Write(dir, rel, error))
                << "`" << rel << "` was written to on the recording side";
            EXPECT_FALSE(error.empty());
        }
        error.clear();
        EXPECT_FALSE(recorder.Write(dir, absolute, error));
        EXPECT_FALSE(error.empty());

        // ...and the ordinary case still works, so the refusals above are not a
        // Write that never writes.
        error.clear();
        EXPECT_TRUE(recorder.Write(dir, "ok.csrp", error)) << error;
    }

    // A file that is not there at all is Unreadable, not PathRefused: the path
    // was fine and the file was the problem, and telling those apart is the whole
    // point of separate refusals.
    {
        ReplayData   data{};
        ReplayReport report{};
        EXPECT_FALSE(ReadReplayFile(dir, "no_such_replay.csrp", h.options, data, report));
        EXPECT_EQ(report.refusal, ReplayRefusal::Unreadable)
            << "a missing file was refused as `"
            << ReplayRefusalName(report.refusal) << "`: " << report.error;
    }

    // A file bigger than the caller's cap costs one stat call rather than a read.
    {
        ReplayReadOptions tiny = h.options;
        tiny.maxFileBytes = 16;
        ReplayData   data{};
        ReplayReport report{};
        EXPECT_FALSE(ReadReplayFile(dir, "valid.csrp", tiny, data, report));
        EXPECT_EQ(report.refusal, ReplayRefusal::Unreadable)
            << "a file over maxFileBytes was refused as `"
            << ReplayRefusalName(report.refusal) << "`: " << report.error;
    }
}

// THE SWEEP. A seeded generator, written out in this file, mutating a valid
// replay every way an integer can -- single bytes, runs of bytes, whole fields
// and arbitrary truncations.
//
// The seed is printed on failure and the generator is xorshift32, so a failing
// iteration reproduces verbatim on both toolchains. That is the only reason to
// seed a fuzz at all, and it is why this does not reach for <random>.
TEST(GameReplayFuzz, EveryMutationIsARefusalOrAStructurallySoundReplay) {
    Hostile h{};
    buildHostileSubject(h);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    constexpr std::uint32_t kSeeds      = 4;
    constexpr std::uint32_t kIterations = 900;

    int refused = 0, accepted = 0, unmodified = 0;
    int refusalCounts[16] = {};

    for (std::uint32_t seedIndex = 0; seedIndex < kSeeds; ++seedIndex) {
        const std::uint32_t seed = 0xA5A50001u + seedIndex * 0x9E3779B9u;
        Rng rng(seed);

        for (std::uint32_t iteration = 0; iteration < kIterations; ++iteration) {
            Bytes mutated = h.good;

            // Four shapes of damage, chosen because they fail differently: a
            // single flipped bit finds a missing bounds check, a smashed field
            // finds a trusted count, a burst finds an off-by-one in a section
            // walk, and a truncation finds a read past the end.
            const std::uint32_t shape = rng.Below(4);
            std::ostringstream what;
            what << "seed " << seed << " iteration " << iteration << " shape " << shape;

            if (shape == 0) {
                const std::uint32_t at = rng.Below(static_cast<std::uint32_t>(mutated.size()));
                mutated[at] ^= static_cast<std::uint8_t>(1u << rng.Below(8));
                what << " bit at " << at;
            } else if (shape == 1) {
                const std::uint32_t at = rng.Below(static_cast<std::uint32_t>(mutated.size()));
                mutated[at] = static_cast<std::uint8_t>(rng.Below(256));
                what << " byte at " << at;
            } else if (shape == 2) {
                const std::uint32_t at = rng.Below(static_cast<std::uint32_t>(mutated.size()));
                const std::uint32_t span = 1u + rng.Below(9u);
                for (std::uint32_t i = 0; i < span && at + i < mutated.size(); ++i)
                    mutated[at + i] = static_cast<std::uint8_t>(rng.Below(256));
                what << " burst of " << span << " at " << at;
            } else {
                const std::uint32_t keep = rng.Below(static_cast<std::uint32_t>(mutated.size()) + 1u);
                mutated.resize(keep);
                what << " truncated to " << keep;
            }

            // A MUTATION THAT CHANGED NOTHING PROVES NOTHING, and three of the
            // four shapes can produce one: `keep` may be the whole file, and a
            // byte or a burst may be rewritten with the value it already had. An
            // unmodified subject decodes, so without this line the `accepted > 0`
            // guard at the bottom could be satisfied entirely by files the round
            // trip already accepts -- and it is there precisely to prove that a
            // GENUINELY damaged file was accepted somewhere in the sweep.
            //
            // Skipped rather than re-rolled, so that every other iteration draws
            // exactly what it drew before this line existed: a failure is
            // reported by seed and iteration index, and that only reproduces
            // while the draw sequence is fixed.
            if (mutated == h.good) { ++unmodified; continue; }

            ReplayData   data{};
            ReplayReport report{};
            const bool ok = DecodeReplay("fuzz",
                                         mutated.empty() ? nullptr : mutated.data(),
                                         mutated.size(), h.options, data, report);

            if (!ok) {
                ++refused;
                ASSERT_NE(report.refusal, ReplayRefusal::None)
                    << what.str() << ": refused with refusal None";
                ASSERT_FALSE(report.error.empty())
                    << what.str() << ": refused with no message";
                const int index = static_cast<int>(report.refusal);
                if (index >= 0 && index < 16) ++refusalCounts[index];
                continue;
            }

            ++accepted;

            // ACCEPTED, SO IT MUST BE SOUND. A ReplayData in hand is supposed to
            // be a replay whose structure has already been proven, and these are
            // the properties the header promises about one.
            ASSERT_EQ(report.refusal, ReplayRefusal::None) << what.str();
            ASSERT_TRUE(report.error.empty()) << what.str() << ": " << report.error;

            const std::uint32_t declaredTicks = readU32(mutated, kOffTickCount);
            ASSERT_EQ(data.inputs.size(), declaredTicks)
                << what.str() << ": the decoded stream is " << data.inputs.size()
                << " ticks long and the header says " << declaredTicks
                << ". The flat expansion and the header must not disagree.";
            ASSERT_LE(data.inputs.size(), h.options.maxTicks)
                << what.str() << ": more ticks were allocated than the caller's cap";
            ASSERT_EQ(data.version, kReplayVersion) << what.str();
            ASSERT_EQ(data.matchDataHash, h.options.expectedMatchDataHash)
                << what.str() << ": the content hash was accepted although the "
                                 "caller armed the CharacterChanged check";

            ASSERT_FALSE(data.checkpoints.empty()) << what.str();
            ASSERT_GE(data.checkpointInterval, 1u) << what.str();
            for (std::size_t i = 0; i < data.checkpoints.size(); ++i) {
                ASSERT_LT(data.checkpoints[i].tick, data.inputs.size())
                    << what.str() << ": checkpoint " << i << " is out of range";
                if (i > 0)
                    ASSERT_LT(data.checkpoints[i - 1].tick, data.checkpoints[i].tick)
                        << what.str() << ": checkpoints are not strictly increasing";
            }
            ASSERT_EQ(data.checkpoints.back().tick, data.inputs.size() - 1)
                << what.str() << ": the last checkpoint is not the last tick";
        }
    }

    // VACUITY GUARDS, BOTH WAYS. A sweep that refused everything would be a sweep
    // whose subject never decoded; one that accepted everything would be a reader
    // that validates nothing. The interesting property is that BOTH happen and
    // neither ever crashes.
    EXPECT_GT(refused, 0)  << "no mutation was refused at all";
    EXPECT_GT(accepted, 0)
        << "every single mutation that actually changed a byte was refused, "
           "which means this sweep is not reaching the bytes a legal edit lives "
           "in (a run's input bits, a character id) and is therefore not testing "
           "acceptance at all";
    EXPECT_LT(unmodified, static_cast<int>(kSeeds * kIterations))
        << "every iteration produced an unmodified file, so the sweep mutated "
           "nothing at all";

    RecordProperty("fuzz_iterations", static_cast<int>(kSeeds * kIterations));
    RecordProperty("fuzz_refused", refused);
    RecordProperty("fuzz_accepted", accepted);
    RecordProperty("fuzz_unmodified", unmodified);
    RecordProperty("fuzz_malformed",
                   refusalCounts[static_cast<int>(ReplayRefusal::Malformed)]);
    RecordProperty("fuzz_not_a_replay",
                   refusalCounts[static_cast<int>(ReplayRefusal::NotAReplay)]);
    RecordProperty("fuzz_character_changed",
                   refusalCounts[static_cast<int>(ReplayRefusal::CharacterChanged)]);
}

// AND THE OTHER HALF OF CLAIM 2: a tampered INPUT STREAM that survives decoding
// is caught at PLAYBACK, by the checkpoints, rather than swallowed.
//
// This is also the only honest way to exercise ReplayDivergence in a test that
// cannot edit the kernel. From the verifier's point of view the two cases are
// identical: the file's structure validated, its character hash matched, its
// state layout matched, the same inputs went in -- and the state at tick T is
// not the state recorded at tick T. Here the cause is a tampered file rather
// than an edited simulation, and the report is the same one an investigator
// would get from the real thing.
TEST(GameReplayFuzz, ATamperedInputStreamIsCaughtAtPlaybackRatherThanSwallowed) {
    Hostile h{};
    buildHostileSubject(h);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    // The second run is the RELEASE. Putting the button back into it is a
    // structurally perfect file describing a fight nobody had.
    const std::uint16_t held = h.rig.bindings[0].button;
    ASSERT_EQ(readU16(h.good, runOffset(0) + 0), held);
    ASSERT_EQ(readU16(h.good, runOffset(1) + 0), 0u);

    Bytes tampered = h.good;
    writeU16(tampered, runOffset(1) + 0, held);

    ReplayData   replay{};
    ReplayReport report{};
    ASSERT_TRUE(DecodeReplay("tampered", tampered.data(), tampered.size(), h.options,
                             replay, report))
        << "the tampered file was refused at load, which is a fine outcome but "
           "not the one this test is about: " << report.error;
    expectStructurallySound(replay, "tampered");
    ASSERT_EQ(replay.TickCount(), h.TickCount());
    for (std::uint32_t t = kHostileHeldTicks; t < h.TickCount(); ++t)
        ASSERT_EQ(replay.inputs[t].p[0].bits, held) << "tick " << t;

    TickLog        playLog;
    ReplayVerifier verifier(replay);
    playBack(replay, h.rig.build.data, playLog, verifier);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    const ReplayDivergence& d = verifier.Result();

    // INPUT MISMATCH FIRST, AND IT IS FALSE HERE. The host fed exactly what the
    // file said; reporting a wiring bug would point the investigator at the one
    // party that is innocent.
    EXPECT_FALSE(d.inputMismatch)
        << "the verifier reported an input mismatch at tick " << d.inputMismatchTick
        << ", although the playback was driven from this very file";

    EXPECT_TRUE(d.diverged)
        << "a file whose input stream was edited replayed with every checkpoint "
           "agreeing, which means the checkpoints are not a check on anything."
        << Table(playLog, h.rig.build.moves[0], kHostileHeldTicks, 16);
    EXPECT_NE(d.recordedChecksum, d.liveChecksum);
    EXPECT_GE(d.tick, kHostileHeldTicks)
        << "the divergence was reported before the tampered run begins";
    EXPECT_TRUE(d.hadPreviousAgreement)
        << "no earlier checkpoint agreed, so the window this reports is the whole "
           "file rather than the second the divergence happened in";
    EXPECT_LT(d.previousAgreeingTick, d.tick)
        << "the reported window (previousAgreeingTick, tick] is empty or "
           "backwards, so it names no interval at all";
    EXPECT_LT(d.previousAgreeingTick, kHostileHeldTicks)
        << "the last agreeing checkpoint is inside the tampered region";

    EXPECT_GT(verifier.CheckpointsCompared(), 0u);
    EXPECT_LT(verifier.CheckpointsAgreed(), verifier.CheckpointsCompared())
        << "the verifier compared checkpoints and agreed with all of them on a "
           "file it should disagree with";

    // PLAYBACK CONTINUES BY DEFAULT: the investigator wants the whole drift
    // profile, not the first disagreement. So the log is complete.
    EXPECT_FALSE(verifier.ShouldStop());
    EXPECT_EQ(playLog.Size(), replay.TickCount())
        << "playback stopped at the divergence although stopOnDivergence was not "
           "set, so the drift profile after the first disagreement is gone";

    // ...and the opt-in says so instead, without touching the session -- an
    // observer that could halt the thing it observes would no longer be one.
    {
        TickLog        stopLog;
        ReplayVerifier stopping(replay, true);
        playBack(replay, h.rig.build.data, stopLog, stopping);
        ASSERT_FALSE(::testing::Test::HasFatalFailure());
        EXPECT_TRUE(stopping.Result().diverged);
        EXPECT_TRUE(stopping.ShouldStop())
            << "stopOnDivergence was set and the verifier did not ask the host to "
               "stop";
        EXPECT_EQ(stopLog.Size(), replay.TickCount())
            << "the verifier stopped the session itself rather than asking the "
               "HOST to; it has no way to stop a session and is not going to grow "
               "one";
    }

    RecordProperty("divergence_tick", static_cast<int>(d.tick));
    RecordProperty("divergence_previous_agreeing", static_cast<int>(d.previousAgreeingTick));
}

// A HOST WIRING BUG IS NOT A DETERMINISM FINDING, and the two are told apart by
// checking the inputs FIRST. Reporting "the engine changed" when the wrong
// source was bound sends an investigator to the one place that is innocent.
TEST(GameReplayFuzz, DifferentInputsAreReportedAsAWiringBugAndNotAsADivergence) {
    Hostile h{};
    buildHostileSubject(h);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    ReplayData   replay{};
    ReplayReport report{};
    ASSERT_TRUE(DecodeReplay("wiring", h.good.data(), h.good.size(), h.options,
                             replay, report))
        << report.error;

    FightSetup setup{};
    setup.start = replay.start;
    setup.data  = &h.rig.build.data;

    FightSession session;
    std::string error;
    ASSERT_TRUE(session.Begin(setup, error)) << error;

    ReplayVerifier verifier(replay);
    ASSERT_TRUE(session.AddObserver(&verifier));

    // Three ticks of the file, and then neutral -- the shape of a host that
    // forgot to bind the replay source, or bound it one tick late.
    constexpr std::uint32_t kWrongFrom = 3;
    for (std::uint32_t t = 0; t < replay.TickCount(); ++t) {
        if (t < kWrongFrom) session.Tick(replay.inputs[t]);
        else                session.Tick(pairOf(0, 0));
    }

    EXPECT_TRUE(verifier.Result().inputMismatch)
        << "the bits fed into the session were not the bits the replay records "
           "and the verifier said nothing about it";
    EXPECT_EQ(verifier.Result().inputMismatchTick, kWrongFrom)
        << "the input mismatch was reported at tick "
        << verifier.Result().inputMismatchTick << " and the first tick fed the "
           "wrong bits was " << kWrongFrom;
}

// ============================================================================
// 6. THE CHARACTER FILE WAS EDITED SINCE  (CLAIM 3)
// ============================================================================
//
// The same inputs against different frame data are a DIFFERENT FIGHT, and
// playing the replay back would show a combo that never happened or drop one
// that did. That is why the analysis is not in the file: if the verdict
// travelled with the replay, a replay of an edited character would play back
// next to the OLD verdict and look correct.
//
// So the refusal is loud, it names the character, and it says "edited since" --
// because that is the fact. "Corrupt" would send the playtester to re-download a
// file that is perfectly intact.

namespace {

std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

bool mentions(const std::string& haystack, const std::string& needle) {
    return lower(haystack).find(lower(needle)) != std::string::npos;
}

}  // namespace

TEST(GameReplayCharacter, AnEditedCharacterIsRefusedAndNamedRatherThanCalledCorrupt) {
    Rig rig{};
    bringUpInfinite(rig);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    std::vector<Input> trace;
    demoTrace(rig, trace);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    TickLog log;
    Bytes   bytes;
    recordFight(rig, trace, 10, log, bytes);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    // ONE NUMBER, IN ONE MOVE. This is what a designer does between recording a
    // combo and posting it -- a balance pass, a hitstun tweak -- and it is
    // exactly the change that makes the recording a recording of another game.
    CharacterData edited = rig.character;
    const MoveIndex loopMove = edited.FindMove(rig.witness.sequence.back());
    ASSERT_NE(loopMove, kInvalidMove);
    edited.moves[loopMove].hitstun += 1;

    MatchBuild editedBuild{};
    ASSERT_TRUE(buildMirror(edited, rig.bindings, editedBuild))
        << editedBuild.report[0].error;

    const std::uint32_t recordedHash = HashMatchData(rig.build.data);
    const std::uint32_t editedHash   = HashMatchData(editedBuild.data);
    ASSERT_NE(recordedHash, editedHash)
        << "one move's hitstun changed and the content hash did not, so this test "
           "cannot arm the refusal it is about";

    // --- refused at load, with the hash the caller is holding ----------------
    {
        ReplayReadOptions options{};
        options.expectedMatchDataHash = editedHash;

        ReplayData   replay{};
        ReplayReport report{};
        EXPECT_FALSE(DecodeReplay("edited", bytes.data(), bytes.size(), options,
                                  replay, report))
            << "a replay recorded against a different character was accepted, so "
               "the playtester is about to watch a fight that never happened";
        EXPECT_EQ(report.refusal, ReplayRefusal::CharacterChanged)
            << "refused as `" << ReplayRefusalName(report.refusal)
            << "`; a value-level mismatch is not a structural one and must not be "
               "collapsed into Malformed: " << report.error;
        ASSERT_FALSE(report.error.empty());

        EXPECT_TRUE(mentions(report.error, rig.character.id))
            << "the refusal does not name the character. ReplayData::characterId "
               "is carried precisely so the message can say which one.\n  message: "
            << report.error;
        EXPECT_TRUE(mentions(report.error, "edited"))
            << "the refusal does not say the character was EDITED SINCE, which is "
               "the fact and the only thing that tells the reader what to do about "
               "it.\n  message: " << report.error;
        EXPECT_FALSE(mentions(report.error, "corrupt"))
            << "the refusal calls a perfectly intact file corrupt, which sends the "
               "playtester to re-download a file that is fine.\n  message: "
            << report.error;
    }

    // --- and the same check, on its own, at the point of use -----------------
    //
    // ReplayMatchesData exists so the play path always states this the same way
    // and the sentence is written once. Called even when the read already checked
    // it, because that is where a mistaken MatchData would actually bite.
    {
        ReplayReadOptions options{};
        options.expectedMatchDataHash = recordedHash;
        ReplayData   replay{};
        ReplayReport report{};
        ASSERT_TRUE(DecodeReplay("original", bytes.data(), bytes.size(), options,
                                 replay, report))
            << report.error;

        std::string error;
        EXPECT_TRUE(ReplayMatchesData(replay, rig.build.data, error))
            << "the replay was refused against the very data it was recorded "
               "against: " << error;
        EXPECT_TRUE(error.empty());

        error.clear();
        EXPECT_FALSE(ReplayMatchesData(replay, editedBuild.data, error))
            << "ReplayMatchesData accepted an edited character, so a host that "
               "relies on it at the point of use has no check at all";
        ASSERT_FALSE(error.empty());
        EXPECT_TRUE(mentions(error, rig.character.id))
            << "the sentence does not name the character: " << error;
        EXPECT_TRUE(mentions(error, std::to_string(recordedHash)) ||
                    mentions(error, "hash"))
            << "the sentence names neither hash, so an investigator cannot tell "
               "which side moved: " << error;
    }

    // --- the id is NOT the check, and must never become one ------------------
    //
    // Two characters can legitimately be renamed, and no character can
    // legitimately have its frame data changed under a replay. So a reader must
    // never accept on a matching id and never refuse on a mismatched one.
    {
        Bytes renamed = bytes;
        for (std::size_t i = 0; i < kReplayCharacterIdBytes; ++i)
            renamed[kOffCharacterId0 + i] = 0;
        const std::string other = "renamed_character";
        for (std::size_t i = 0; i < other.size(); ++i)
            renamed[kOffCharacterId0 + i] = static_cast<std::uint8_t>(other[i]);

        ReplayReadOptions options{};
        options.expectedMatchDataHash = recordedHash;
        ReplayData   replay{};
        ReplayReport report{};
        EXPECT_TRUE(DecodeReplay("renamed", renamed.data(), renamed.size(), options,
                                 replay, report))
            << "a replay whose character was RENAMED was refused, though its frame "
               "data is byte-identical. The hash decides; the id is for the human "
               "reading the error: " << report.error;
        EXPECT_EQ(replay.characterId[0], other);
    }
}

// ZERO SKIPS THE CHECK AND RECORDS A WARNING, NEVER A SILENT PASS. A caller who
// never sets expectedMatchDataHash has no character-change check at all, and
// that should be visible in the report rather than inferred from a green return.
TEST(GameReplayCharacter, SkippingTheContentCheckIsAWarningAndNeverSilent) {
    Rig rig{};
    bringUpInfinite(rig);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    std::vector<Input> trace;
    demoTrace(rig, trace);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    TickLog log;
    Bytes   bytes;
    recordFight(rig, trace, 20, log, bytes);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    ReplayReadOptions unarmed{};
    ASSERT_EQ(unarmed.expectedMatchDataHash, 0u)
        << "the default arms the check, so a caller who forgets gets one by "
           "accident and this test is measuring nothing";

    ReplayData   replay{};
    ReplayReport report{};
    EXPECT_TRUE(DecodeReplay("unarmed", bytes.data(), bytes.size(), unarmed,
                             replay, report))
        << report.error;
    EXPECT_FALSE(report.warnings.empty())
        << "the content check was skipped and the report says nothing. A tool that "
           "dumps replays legitimately has no MatchData; a PLAY path never does, "
           "and the difference has to be visible.";
    EXPECT_TRUE(report.error.empty());
    EXPECT_EQ(report.refusal, ReplayRefusal::None);
    EXPECT_EQ(replay.matchDataHash, HashMatchData(rig.build.data))
        << "the hash from the file is not carried through, so a caller cannot "
           "perform the check later even if it wanted to";
}

// ============================================================================
// 7. DEMONSTRATE, THEN YOU TRY  (CLAIM 5)
// ============================================================================
//
// "The tool-assisted player is PLAYER-FACING first: a Demonstrate the playtester
// presses, which performs the prover's printed loop perfectly and then hands
// control back so they can try it themselves."
//
// That whole interaction is a scripted source running out next to a latched one,
// composed by FallbackInputSource. Running out is DATA -- InputSample::authored
// goes false -- and never a failure, a sentinel or an out-of-band flag.
TEST(GameDemonstrateThenYouTry, TheTraceRunsOutAndTheFightKeepsTicking) {
    Rig rig{};
    bringUpInfinite(rig);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    // A SECOND BINDING, so that "the pad took over" is visible as a different
    // move rather than inferred from an absence. The witness binds one move to
    // LP; this adds `stand_mp` on MP, which is a disjoint single bit and
    // therefore cannot shadow or be shadowed under StepAttack's first-wins rule.
    const std::string padMoveId = "stand_mp";
    ASSERT_NE(rig.character.FindMove(padMoveId), kInvalidMove)
        << "this character has no `" << padMoveId << "`, so the pad has nothing "
           "distinguishable to press";

    std::vector<MoveBinding> bindings = rig.bindings;
    bool alreadyBound = false;
    for (const MoveBinding& b : bindings)
        if (b.moveId == padMoveId) { alreadyBound = true; break; }
    ASSERT_FALSE(alreadyBound)
        << "the witness already binds `" << padMoveId << "`, so the pad's move "
           "cannot be told apart from the demonstration's";
    bindings.push_back(bind(padMoveId, cse::kernel::kInputMP));

    MatchBuild build{};
    ASSERT_TRUE(buildMirror(rig.character, bindings, build)) << build.report[0].error;

    FightSetup setup{};
    setup.start = rig.setup.start;
    setup.data  = &build.data;

    const std::uint16_t demoSlot = build.moves[0].Find(rig.witness.sequence.back());
    const std::uint16_t padSlot  = build.moves[0].Find(padMoveId);
    ASSERT_NE(demoSlot, 0u);
    ASSERT_NE(padSlot, 0u);
    ASSERT_NE(demoSlot, padSlot);

    FightSession session;
    std::string error;
    ASSERT_TRUE(session.Begin(setup, error)) << error;

    // The playtester presses Demonstrate at the session's current tick. The
    // rehearsal starts from the state they are standing in and the trace is
    // numbered from there -- no offset stored outside the source.
    DemonstrationRequest request{};
    request.from         = &session.State();
    request.data         = &build.data;
    request.attackerSlot = 0;
    // The witness as kernel ids of THIS build, which has one binding more than
    // rig.build does -- so the slots must be looked up again rather than reused.
    for (const std::string& id : rig.witness.sequence) {
        const std::uint16_t slot = build.moves[0].Find(id);
        ASSERT_NE(slot, 0u) << "`" << id << "` is not in this build";
        request.moveIds.push_back(slot);
    }
    request.loopStart = rig.loopStart;
    request.turns     = 6;
    request.maxTicks  = 400;
    request.firstTick = session.CurrentTick();

    Demonstration demo{};
    ASSERT_TRUE(BuildDemonstration(request, demo))
        << "the rehearsal failed: " << demo.error;
    ASSERT_FALSE(demo.inputs.empty());
    const std::uint32_t demoEnd =
        demo.firstTick + static_cast<std::uint32_t>(demo.inputs.size());

    ScriptedInputSource demoSource(demo.inputs, demo.firstTick, "DEMO");
    LatchedInputSource  pad(0, "YOU");
    FallbackInputSource attacker(&demoSource, &pad);
    session.SetInputSource(0, &attacker);

    // EXHAUSTION IS A NORMAL STATE, expressed as data and reported before it is
    // reached, so a HUD can draw a progress bar without polling.
    EXPECT_EQ(demoSource.AuthoredEndTick(), demoEnd);
    EXPECT_FALSE(demoSource.Exhausted(demoEnd - 1u));
    EXPECT_TRUE(demoSource.Exhausted(demoEnd));
    EXPECT_EQ(demoSource.At(demoEnd).input.bits, 0u);

    TickLog log;
    ASSERT_TRUE(session.AddObserver(&log));

    constexpr std::uint32_t kYouTryTicks = 90;
    const std::uint32_t total = demoEnd + kYouTryTicks;

    // The host polls its pad once per tick and latches BEFORE asking the session
    // to run that tick. This loop is the Engine-side file, minus the hardware
    // read -- which is the whole reason LatchedInputSource lives in a library
    // that links no Engine.
    for (std::uint32_t t = 0; t < total; ++t) {
        ASSERT_TRUE(pad.Latch(t, inputOf(cse::kernel::kInputMP)))
            << "latching tick " << t << " was refused, which is a host sequencing "
               "bug and means the input log now has a hole in it";
        session.Tick();
    }

    ASSERT_EQ(log.Size(), total);
    EXPECT_TRUE(log.Clean());

    // --- who was speaking, tick by tick -------------------------------------
    for (std::uint32_t t = 0; t < demoEnd; ++t) {
        ASSERT_EQ(attacker.Active(t), static_cast<const IInputSource*>(&demoSource))
            << "tick " << t << " is inside the demonstration and the pad answered "
                              "it";
        ASSERT_EQ(log.samples[t].inputs.p[0].bits, demo.inputs[t - demo.firstTick].bits)
            << "tick " << t << " did not receive the demonstrated bits";
    }
    for (std::uint32_t t = demoEnd; t < total; ++t) {
        ASSERT_EQ(attacker.Active(t), static_cast<const IInputSource*>(&pad))
            << "control was not handed back at tick " << t;
        ASSERT_EQ(log.samples[t].inputs.p[0].bits, cse::kernel::kInputMP)
            << "tick " << t << " is past the demonstration and the pad's bits did "
                              "not arrive";
    }
    EXPECT_STREQ(attacker.Active(0)->Name(), "DEMO");
    EXPECT_STREQ(attacker.Active(demoEnd)->Name(), "YOU")
        << "a HUD asking Active(tick)->Name() would still be drawing DEMO after "
           "the demonstration ended";

    // --- and the FIGHT kept going -------------------------------------------
    //
    // Not merely the tick loop: the playtester's own move came out. Nothing was
    // reset, nothing had to be noticed, and the demonstration did not leave the
    // button jammed down.
    bool demoMoveDuringDemo = false, padMoveAfter = false;
    std::uint32_t firstPadMoveTick = 0;
    for (std::uint32_t t = 0; t < demoEnd; ++t)
        if (log.samples[t].state.p[0].moveId == demoSlot) demoMoveDuringDemo = true;
    for (std::uint32_t t = demoEnd; t < total; ++t)
        if (log.samples[t].state.p[0].moveId == padSlot) {
            padMoveAfter = true;
            firstPadMoveTick = t;
            break;
        }

    EXPECT_TRUE(demoMoveDuringDemo)
        << "the demonstration never performed its own move, so there is nothing "
           "for control to be handed back FROM."
        << Table(log, build.moves[0], 0, 24);
    EXPECT_TRUE(padMoveAfter)
        << "the playtester held " << buttonName(cse::kernel::kInputMP) << " for "
        << kYouTryTicks << " ticks after the demonstration ended and `" << padMoveId
        << "` never came out, so control was not really handed back."
        << Table(log, build.moves[0], demoEnd, 32);

    // The demonstration's own move must NOT keep coming out afterwards -- that
    // would be the jammed button, and it is the exact failure "neutral, never
    // repeat-last" exists to rule out. Anything already in flight is allowed to
    // finish, which is why this starts a full move duration after the handover.
    const cse::kernel::MoveDef* demoMove =
        cse::kernel::MoveAt(build.data.p[0], demoSlot);
    ASSERT_NE(demoMove, nullptr);
    const std::uint32_t settled =
        demoEnd + static_cast<std::uint32_t>(cse::kernel::MoveDuration(*demoMove)) + 1u;
    for (std::uint32_t t = settled; t < total; ++t)
        ASSERT_NE(log.samples[t].state.p[0].moveId, demoSlot)
            << "the demonstration's move started again on tick " << t
            << ", well after the trace ran out."
            << Table(log, build.moves[0], demoEnd, 32);

    RecordProperty("demo_end_tick", static_cast<int>(demoEnd));
    RecordProperty("first_player_move_tick", static_cast<int>(firstPadMoveTick));
}

// ============================================================================
// 8. THE LIVE JUDGE  (CLAIMS 6 AND 7)
// ============================================================================
//
// This is the object that makes the running game VALIDATE the tool rather than
// merely demonstrate it. Every assertion below is about a count or a flag the
// header states a rule for; none is about a rendering.

namespace {

// One fight, watched by a ComboWatcher, an independent tick log, and a probe
// that snapshots the watcher's report every tick.
//
// The probe is registered AFTER the watcher on purpose: observers are notified in
// REGISTRATION ORDER, which the header makes part of the contract, so the probe
// sees the watcher's state as of the end of the same tick. That is the only way
// to assert about a flag whose value later in a string is deliberately left
// unspecified.
void watchFight(const Rig& rig, const std::vector<Input>& trace,
                ComboWatcher& watcher, TickLog& log, WatcherProbe& probe,
                std::uint32_t settleTicks) {
    FightSession session;
    std::string error;
    ASSERT_TRUE(session.Begin(rig.setup, error)) << error;
    watcher.Reset();

    ASSERT_TRUE(session.AddObserver(&watcher));
    ASSERT_TRUE(session.AddObserver(&probe));
    ASSERT_TRUE(session.AddObserver(&log));

    ScriptedInputSource source(trace, 0, "DEMO");
    session.SetInputSource(0, &source);
    run(session, static_cast<std::uint32_t>(trace.size()) + settleTicks);
}

}  // namespace

// A MOVE STARTED == `moveId != 0 && moveFrame == 0`, NOT A TRANSITION OF moveId.
//
// A move that cancels into ITSELF never changes the id: it goes from (id=7, f=9)
// to (id=7, f=0) and a transition detector sees nothing happen at all. Self-
// cancels are exactly what an infinite combo is made of, so a watcher built on id
// transitions would report ONE hit and then sit silently through the twenty-odd
// that follow -- which is precisely the combo it exists to catch.
//
// THE ASSERTION IS THEREFORE THE COUNT.
TEST(GameComboWatcher, ASelfCancelLoopIsCountedRepetitionByRepetition) {
    Rig rig{};
    bringUpInfinite(rig);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    std::vector<Input> trace;
    demoTrace(rig, trace);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    ComboWatcher watcher(0, &rig.build.moves[0], &rig.verdict);
    TickLog      log;
    WatcherProbe probe(&watcher);
    watchFight(rig, trace, watcher, log, probe, kSettleTicks);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    ASSERT_TRUE(log.Clean());

    const ComboReport&               report = watcher.Current();
    const std::vector<std::uint32_t> hits   = log.HitTicks(1);

    // The defender is still standing, so the health-delta reading below is a
    // count of real hits rather than of hits against the clamp at zero.
    ASSERT_GT(log.Final().p[1].health, 0)
        << "the defender was knocked out inside the measured window, so the "
           "independent hit count stopped early and cannot be compared. Lower "
           "kDemoTurns.";

    EXPECT_FALSE(watcher.Stale()) << "nothing was re-simulated";
    ASSERT_GE(hits.size(), static_cast<std::size_t>(kDemoTurns))
        << "the fight itself did not happen, so there is nothing to count."
        << Table(log, rig.build.moves[0], 0, 32);

    // THE HEADLINE. An id-transition detector reports 1 here.
    EXPECT_EQ(report.hits, static_cast<std::int32_t>(hits.size()))
        << "the watcher counted " << report.hits << " hits and the defender's "
           "health fell on " << hits.size() << " ticks.\n"
           "A watcher that reports 1 is detecting a CHANGE OF moveId; a self-"
           "cancel never changes the id, and this loop is one move cancelling "
           "into itself for the whole demonstration."
        << DescribeReport(report, rig.build.moves[0])
        << Table(log, rig.build.moves[0], 0, 32);
    EXPECT_GT(report.hits, 10)
        << "even if the counts agree, this run is too short to distinguish a "
           "working watcher from one that stopped after the first repetition";

    // Fighter::comboHits USED TO BE DEAD STATE, and this block used to assert
    // that: nothing wrote it, so a watcher reading it would report zero forever
    // and look like it worked, and an EXPECT_NE caught exactly that.
    //
    // ADR-005 P2 brought the field to life -- ResolveHits increments it and the
    // hitstun-decay rule reads it. THE OLD GUARD IS RETIRED BECAUSE ITS PREMISE
    // WAS REMOVED ON PURPOSE, not because it became inconvenient: a test that
    // asserts a field is dead has to go when the field is deliberately given a
    // writer, or it forbids the feature.
    //
    // What replaces it is stronger, because the two numbers are now derived
    // separately and can be compared. The attacker is never hit, so ITS counter
    // must stay at zero -- which is what catches a watcher that reads the field
    // off the wrong slot, or one that counts its own swings.
    EXPECT_EQ(log.Final().p[0].comboHits, 0u)
        << "the ATTACKER accumulated combo hits. Either ResolveHits credited the "
           "wrong slot, or something is counting swings rather than connections.";
    EXPECT_EQ(static_cast<std::int32_t>(log.Final().p[1].comboHits), report.hits)
        << "the kernel's own combo counter and the watcher's independently "
           "derived count disagree. They are computed from different signals -- "
           "the kernel from ResolveHits, the watcher from alreadyHitBits plus a "
           "health delta -- so a disagreement means one of them is wrong, and "
           "this is the assertion that can tell.\n"
           "(They are the same number only for a combo short of comboHits' uint8 "
           "saturation, which this one is: " << report.hits << " hits.)"
        << DescribeReport(report, rig.build.moves[0]);

    // The string, its shape, and its arithmetic.
    EXPECT_TRUE(report.open)
        << "the defender got out of a combo the ground-truth test measures as "
           "unescapable" << DescribeReport(report, rig.build.moves[0]);
    EXPECT_EQ(report.startTick, hits.front());
    EXPECT_EQ(report.lastHitTick, hits.back());
    EXPECT_EQ(watcher.CompletedCombos(), 0)
        << "a string ended although the defender never became actionable";

    // DAMAGE IS THE DEFENDER'S HEALTH DELTA, not the sum of MoveDef::damage:
    // health clamps at zero and the authored number would keep accruing against a
    // fighter who is already at nothing.
    EXPECT_EQ(report.damage, kStartingHealth - log.Final().p[1].health)
        << "the reported damage is not the defender's health delta";

    // Every connecting move is the loop's move, and the sequence records each
    // one rather than collapsing the repeats.
    const std::uint16_t loopSlot = rig.kernelWitness.back();
    EXPECT_EQ(report.sequence.size(), static_cast<std::size_t>(report.hits))
        << "the sequence holds " << report.sequence.size() << " connecting moves "
           "for " << report.hits << " hits";
    EXPECT_FALSE(report.sequenceTruncated)
        << "this string is shorter than kMaxComboSequence and was truncated anyway";
    for (std::size_t i = 0; i < report.sequence.size(); ++i)
        ASSERT_EQ(report.sequence[i], loopSlot)
            << "connecting move " << i << " is `"
            << moveName(rig.build.moves[0], report.sequence[i])
            << "` and the loop is one move";

    // A TRUE COMBO: the defender was never actionable while it ran. gapTicks is
    // the DIRECT reading off Fighter::hitstun that ComboWatcher.h insists on --
    // the two indirect detectors miss the case where stun expires on exactly the
    // tick the next hit lands.
    EXPECT_EQ(report.gapTicks, 0)
        << "the watcher found " << report.gapTicks << " tick(s) inside the string "
           "on which the defender was actionable and was hit anyway. That is a "
           "BLOCKABLE STRING, not a true combo, and the ground-truth test measures "
           "this loop as unescapable."
        << DescribeReport(report, rig.build.moves[0])
        << Table(log, rig.build.moves[0], hits.front(), 32);
    EXPECT_TRUE(report.TrueCombo());
    EXPECT_LE(report.whiffedStarts, 1)
        << report.whiffedStarts << " moves started and never connected inside a "
           "chain the analysis describes as unbroken";

    // The probe: the count grew ONE HIT AT A TIME rather than arriving at the
    // end, and it never went backwards. A watcher that recomputed from scratch
    // each tick could pass every assertion above and still flicker in front of a
    // playtester.
    std::int32_t previous = 0;
    std::size_t  increments = 0;
    for (const WatcherProbe::Frame& f : probe.frames) {
        ASSERT_GE(f.hits, previous)
            << "the hit count went backwards at tick " << f.tick;
        ASSERT_LE(f.hits - previous, 1)
            << "the hit count jumped by " << (f.hits - previous) << " at tick "
            << f.tick << "; the kernel resolves at most one hit each way per tick";
        if (f.hits > previous) ++increments;
        previous = f.hits;
    }
    EXPECT_EQ(increments, hits.size());
    ASSERT_FALSE(probe.frames.empty());
    EXPECT_FALSE(probe.frames.back().stale);

    EXPECT_FALSE(watcher.Describe().empty())
        << "the string has no description, so a log, a test failure message and "
           "the editor's copy-to-clipboard have nothing to print";

    // A WATCHER WITH NO ANALYSIS IS STILL A COMBO COUNTER, and that is a useful
    // thing on its own. Every judgement field stays at its default; the count
    // does not.
    {
        ComboWatcher bare(0, nullptr, nullptr);
        TickLog      bareLog;
        WatcherProbe bareProbe(&bare);
        watchFight(rig, trace, bare, bareLog, bareProbe, kSettleTicks);
        ASSERT_FALSE(::testing::Test::HasFatalFailure());

        const ComboReport& r = bare.Current();
        EXPECT_EQ(r.hits, report.hits)
            << "the hit count changed when the analysis was taken away, so "
               "counting is entangled with judging";
        EXPECT_EQ(r.damage, report.damage);
        EXPECT_EQ(r.gapTicks, report.gapTicks);
        EXPECT_EQ(r.cycleRun, 0) << "a cycle was matched with no loop to match it against";
        EXPECT_EQ(r.loopTurnsCompleted, 0);
        EXPECT_FALSE(r.onWitness);
        EXPECT_FALSE(r.completedProverLoop);
        EXPECT_FALSE(r.performedDeadCancel);
        EXPECT_FALSE(r.deadEdgeConnected);
        EXPECT_FALSE(bare.Describe().empty())
            << "a watcher with no MoveIndexMap cannot describe its string at all; "
               "a null map costs the HUD its labels and must cost the judgement "
               "nothing";
    }

    RecordProperty("watcher_hits", report.hits);
    RecordProperty("watcher_damage", report.damage);
    RecordProperty("watcher_gap_ticks", report.gapTicks);
}

// CLAIM 7, FIRST HALF: the loop the decision procedure PRINTED, performed in the
// running game and recognised as the analysis's own.
//
// This is ARCHITECTURE.md 5.5 item 4 happening in front of a playtester rather
// than in a test -- except that it is also, now, in a test.
TEST(GameComboWatcher, ThePrintedLoopIsFlaggedAsTheProversOwn) {
    Rig rig{};
    bringUpInfinite(rig);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    ASSERT_EQ(rig.verdict.status, ProverStatus::Infinite);

    std::vector<Input> trace;
    demoTrace(rig, trace);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    ComboWatcher watcher(0, &rig.build.moves[0], &rig.verdict);
    TickLog      log;
    WatcherProbe probe(&watcher);
    watchFight(rig, trace, watcher, log, probe, kSettleTicks);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    const ComboReport& report = watcher.Current();
    ASSERT_GT(report.hits, static_cast<std::int32_t>(rig.verdict.loop.size()))
        << "not one full turn of the printed loop connected"
        << DescribeReport(report, rig.build.moves[0]);

    // THE CYCLE MATCHER runs over CONNECTING moves only. Every connecting move
    // here is the loop's single move, so the run is the hit count.
    EXPECT_EQ(report.cycleRun, report.hits)
        << "the cycle matcher lost the loop after " << report.cycleRun
        << " of " << report.hits << " connecting moves, although every one of "
           "them is the move the witness names."
        << DescribeReport(report, rig.build.moves[0]);
    EXPECT_EQ(report.loopTurnsCompleted,
              report.cycleRun / static_cast<std::int32_t>(rig.verdict.loop.size()))
        << "loopTurnsCompleted is not cycleRun / loop.size()";
    EXPECT_GE(report.loopTurnsCompleted, static_cast<std::int32_t>(kDemoTurns));

    // THE LOUD MARKER. loopTurnsCompleted >= 1 while the analysis's status is
    // Infinite: the player performed, in the running game, the loop the decision
    // procedure printed out of the character file.
    EXPECT_TRUE(report.completedProverLoop)
        << "the demonstration went round the printed loop "
        << report.loopTurnsCompleted << " times and the watcher did not say so. "
           "This flag is the point of the file."
        << DescribeReport(report, rig.build.moves[0]);

    // The witness cursor walks prefix ++ loop without wrapping. Its value once
    // the string runs off the end of the witness is deliberately not specified,
    // so this asserts only what the header states: that it is true while the
    // player is on the witness, and that it advances.
    const std::size_t witnessLength = rig.verdict.prefix.size() + rig.verdict.loop.size();
    ASSERT_GT(witnessLength, 0u);
    std::size_t connectingSoFar = 0;
    std::size_t lastIndex       = 0;
    bool        sawWitness      = false;
    std::int32_t previousHits   = 0;
    for (const WatcherProbe::Frame& f : probe.frames) {
        if (f.hits > previousHits) {
            ++connectingSoFar;
            if (connectingSoFar <= witnessLength) {
                ASSERT_TRUE(f.onWitness)
                    << "connecting move " << connectingSoFar << " (tick " << f.tick
                    << ") is the one the witness names and the watcher had already "
                       "dropped off the witness. It never comes back true for the "
                       "same string, so this is not recoverable.";
                ASSERT_GE(f.witnessIndex, lastIndex)
                    << "the witness cursor went backwards at tick " << f.tick;
                lastIndex  = f.witnessIndex;
                sawWitness = true;
            }
        }
        previousHits = f.hits;
    }
    EXPECT_TRUE(sawWitness) << "no connecting move was ever on the witness";
    EXPECT_GT(lastIndex, 0u)
        << "the witness cursor never advanced past zero, so `witnessIndex` is not "
           "a position in prefix ++ loop";

    // `witnessIncomplete` is the adapter's caveat wearing its own name rather
    // than a mismatch wearing a verdict's clothes.
    EXPECT_EQ(report.witnessIncomplete, !rig.verdict.loopEntryKnown)
        << "witnessIncomplete is supposed to be exactly "
           "!ProverResult::loopEntryKnown";

    // No dead cancel was taken: this loop is the edge the file's own
    // `engine.the_bug` names, and the prover reports it as LIVE.
    EXPECT_FALSE(report.performedDeadCancel)
        << "the printed loop is running through an edge the analysis called DEAD, "
           "which would mean the witness and the dead-cancel list disagree"
        << DescribeReport(report, rig.build.moves[0]);
    EXPECT_FALSE(report.deadEdgeConnected);

    std::cout << "\n[ LIVE JUDGE ] the printed loop, judged while it ran on `"
              << rig.character.id << "`\n"
              << "  witness            " << rig.witness.ToString() << "\n"
              << "  hits               " << report.hits << " for " << report.damage
              << " damage, gapTicks " << report.gapTicks << "\n"
              << "  cycleRun           " << report.cycleRun << " ("
              << report.loopTurnsCompleted << " full turns of the printed loop)\n"
              << "  completedProverLoop "
              << (report.completedProverLoop ? "YES" : "no") << "\n\n";

    RecordProperty("prover_loop_turns", report.loopTurnsCompleted);
}

// CLAIM 7, SECOND HALF: on the SAFE character, a cycle the certificate retires
// is still flagged when the kernel performs it anyway.
//
// `fighter_a` is TERMINATING and its certificate is that juggle runs down. Its
// one live self-cancel cannot be taken more than a few times IN THE MODEL,
// because the move spends a point of a finite budget. THE KERNEL HAS NO
// RESOURCES -- `move.effect` is a KernelOmits row and
// BuildReport::playsAsAnalysed is false -- so it performs the cycle forever, and
// tests/test_ground_truth.cpp section 5 and tests/test_gap_extent.cpp measure
// exactly that.
//
// The loop handed to the watcher here is DERIVED from the analysis, not invented:
// it is the self-cancel the prover did NOT put in deadCancels. What is asserted
// is that the watcher recognises the player performing it -- while REFUSING to
// call it a completed prover loop, because the verdict is Terminating and the
// analysis printed no loop at all.
TEST(GameComboWatcher, ACycleTheCertificateRetiresIsStillFlaggedWhenTheKernelRunsIt) {
    Rig         rig{};
    std::string moveId;
    bringUpSafeSelfCycle(rig, moveId);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    ASSERT_FALSE(moveId.empty());

    const std::uint16_t slot = rig.build.moves[0].Find(moveId);
    ASSERT_NE(slot, 0u);
    const MoveIndex characterMove = MoveIndexMap::CharacterMoveOf(slot);
    ASSERT_NE(characterMove, kInvalidMove);
    ASSERT_EQ(MoveIndexMap::KernelMoveIdOf(characterMove), slot)
        << "the two directions of the move-index mapping disagree";

    // The edge crossed into the kernel with a window it can actually match. An
    // empty window would make it inert and there would be nothing to perform --
    // which is exactly what happens to this character's DEAD self-cancels.
    const cse::kernel::CancelEdge* edge = nullptr;
    for (std::int32_t i = 0; i < rig.build.data.p[0].cancelCount; ++i) {
        const cse::kernel::CancelEdge& e = rig.build.data.p[0].cancels[i];
        if (e.from == slot && e.to == slot) { edge = &e; break; }
    }
    ASSERT_NE(edge, nullptr) << "the self-cancel did not cross into the kernel";
    ASSERT_LE(edge->earliestFrame, edge->latestFrame)
        << "the kernel window is empty, so this edge is inert and there is no "
           "cycle for the kernel to run";

    // The analysis, with the derived cycle written into it. Everything else --
    // status, deadCancels, the certificate -- is the real verdict, so the
    // judgement fields below are judged against the real analysis.
    ProverResult analysis = rig.verdict;
    ASSERT_TRUE(analysis.loop.empty())
        << "a TERMINATING verdict came with a loop witness, which is a "
           "contradiction in the result rather than a fact about the character";
    analysis.loop.push_back(characterMove);
    analysis.prefix.clear();
    // A one-move loop is entered on its own first move by construction, so the
    // adapter's omit-the-opening-move caveat cannot apply to it.
    analysis.loopEntryKnown = true;

    constexpr std::uint32_t kSafeTurns = 6;

    GameState from{};
    cse::kernel::ResetMatch(from, kSeed);
    from.p[0].posX = kP0X;
    from.p[1].posX = kP1X;

    Demonstration demo{};
    demonstrate(rig, from, kSafeTurns, 0, demo);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    ComboWatcher watcher(0, &rig.build.moves[0], &analysis);
    TickLog      log;
    WatcherProbe probe(&watcher);
    watchFight(rig, demo.inputs, watcher, log, probe, kSettleTicks);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    const ComboReport&               report = watcher.Current();
    const std::vector<std::uint32_t> hits   = log.HitTicks(1);

    ASSERT_GT(log.Final().p[1].health, 0)
        << "the defender was knocked out, so the counts here are against the "
           "health clamp. Lower kSafeTurns.";
    ASSERT_GE(hits.size(), static_cast<std::size_t>(kSafeTurns))
        << "the certified-away cycle did not run at all, so there is nothing to "
           "flag. tests/test_ground_truth.cpp section 5 executes this same edge."
        << Table(log, rig.build.moves[0], 0, 32);

    EXPECT_EQ(report.hits, static_cast<std::int32_t>(hits.size()));
    for (std::uint16_t m : report.sequence)
        ASSERT_EQ(m, slot) << "a connecting move other than `" << moveId
                           << "` appeared in a one-move cycle";

    // FLAGGED. The player is going round a cycle the analysis knows about, and
    // the watcher says how many times.
    EXPECT_EQ(report.cycleRun, report.hits)
        << "the cycle matcher did not follow a one-move loop through "
        << report.hits << " repetitions of that very move."
        << DescribeReport(report, rig.build.moves[0]);
    EXPECT_GE(report.loopTurnsCompleted, static_cast<std::int32_t>(kSafeTurns))
        << "the kernel performed `" << moveId << "` -> itself " << report.hits
        << " times and the watcher counted " << report.loopTurnsCompleted
        << " turns.";

    // AND NOT CLAIMED AS A PROVER LOOP. `completedProverLoop` is
    // loopTurnsCompleted >= 1 AND the analysis's status is Infinite. It is
    // Terminating here, so the flag must stay down however many turns were
    // performed -- the distinction between "you are looping" and "you have
    // performed the loop the tool printed" is the whole difference between a HUD
    // that informs and one that lies.
    EXPECT_FALSE(report.completedProverLoop)
        << "the watcher claims the player completed a PROVER LOOP on a character "
           "the decision procedure calls TERMINATING, which printed no loop at "
           "all. `completedProverLoop` is gated on ProverStatus::Infinite."
        << DescribeReport(report, rig.build.moves[0]);
    EXPECT_EQ(analysis.status, ProverStatus::Terminating);

    // The edge the player took is one the prover KEPT, so nothing here is a dead
    // cancel. If this fires, the derivation above picked a dead edge and the test
    // is measuring the wrong one.
    EXPECT_FALSE(report.performedDeadCancel)
        << "the cycle this test derived as the prover's LIVE self-cancel is "
           "reported as dead"
        << DescribeReport(report, rig.build.moves[0]);
    EXPECT_FALSE(report.deadEdgeConnected);

    // The defender never gets a tick back, on the DIRECT reading. This is the
    // detector that turned 37 into 33 on this character, and it is why the count
    // in test_gap_extent.cpp is 33.
    EXPECT_EQ(report.gapTicks, 0)
        << "the defender was actionable on " << report.gapTicks
        << " tick(s) inside this string, so it is a blockable string rather than "
           "the unescapable loop test_gap_extent.cpp counts among its 33."
        << DescribeReport(report, rig.build.moves[0])
        << Table(log, rig.build.moves[0], hits.front(), 32);
    EXPECT_TRUE(report.TrueCombo());

    std::cout << "\n[ LIVE JUDGE ] a certified-away cycle, judged while it ran on `"
              << rig.character.id << "`\n"
              << "  the decision procedure says TERMINATING and its certificate is "
                 "that juggle runs down.\n"
              << "  `" << moveId << "` cancels into itself; the kernel has no "
                 "resources, so it ran " << report.loopTurnsCompleted
              << " turns for " << report.damage << " damage\n"
              << "  with the defender actionable on " << report.gapTicks
              << " tick(s). completedProverLoop is "
              << (report.completedProverLoop ? "true" : "false")
              << ", because the verdict is TERMINATING.\n\n";

    RecordProperty("safe_cycle_move", moveId);
    RecordProperty("safe_cycle_turns", report.loopTurnsCompleted);
}

// A CANCEL AND A LINK ARE DIFFERENT THINGS AND THE ANALYSIS ONLY KNOWS ONE.
//
// Observed at end of tick t-1 as (A, f), with D = MoveDuration(A):
//   f + 1 >= D    A ran out and the held button started the follow-up. A LINK,
//                 which the character's cancel table says nothing about and the
//                 prover's graph does not contain.
//   f + 1 <  D    A was interrupted mid-move. A CANCEL, and f + 1 is the frame
//                 the inclusive window was matched against.
//
// 32 of fighter_a's 41 cycles run by the LINK route, so a watcher that reported
// those as cancels would be reporting that the player took edges the kernel
// demonstrably cannot take. This test recomputes the classification from the tick
// log, by the header's own rule, and requires the watcher to agree on every edge.
TEST(GameComboWatcher, CancelsAndLinksAreToldApartByTheHeadersOwnRule) {
    Rig rig{};
    bringUpInfinite(rig);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    std::vector<Input> trace;
    demoTrace(rig, trace);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    ComboWatcher watcher(0, &rig.build.moves[0], &rig.verdict);
    TickLog      log;
    WatcherProbe probe(&watcher);
    watchFight(rig, trace, watcher, log, probe, kSettleTicks);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    const ComboReport& report = watcher.Current();
    ASSERT_FALSE(report.edges.empty())
        << "no move start was recorded at all inside a string of " << report.hits
        << " hits";

    // Every move start the kernel performed, read off the state. The watcher's
    // edge list must name the same ticks -- not a subset, and not extra ones.
    const std::vector<std::uint32_t> starts = log.MoveStartTicks(0);
    ASSERT_FALSE(starts.empty());

    std::size_t cancels = 0, links = 0, fromIdle = 0;
    for (const PerformedEdge& e : report.edges) {
        // The tick is a tick on which the attacker really did start a move.
        bool isAStart = false;
        for (std::uint32_t t : starts)
            if (t == e.tick) { isAStart = true; break; }
        ASSERT_TRUE(isAStart)
            << "the watcher recorded an edge on tick " << e.tick
            << ", on which the attacker's moveFrame was not 0."
            << Table(log, rig.build.moves[0], e.tick > 2 ? e.tick - 2 : 0, 6);

        ASSERT_LT(e.tick, log.Size());
        ASSERT_EQ(e.to, log.samples[e.tick].state.p[0].moveId)
            << "the edge on tick " << e.tick << " says it went to `"
            << moveName(rig.build.moves[0], e.to) << "` and the state says `"
            << moveName(rig.build.moves[0], log.samples[e.tick].state.p[0].moveId)
            << "`";

        if (e.tick == 0) { ++fromIdle; continue; }

        // WHAT WAS OBSERVED AT THE END OF THE PREVIOUS TICK. This is the whole
        // input to the classification rule.
        const cse::kernel::Fighter& previous = log.samples[e.tick - 1].state.p[0];
        ASSERT_EQ(e.from, previous.moveId)
            << "the edge on tick " << e.tick << " says it came from `"
            << moveName(rig.build.moves[0], e.from)
            << "` and the attacker was in `"
            << moveName(rig.build.moves[0], previous.moveId)
            << "` at the end of the tick before";

        if (e.from == 0) { ++fromIdle; EXPECT_FALSE(e.cancel); continue; }

        const cse::kernel::MoveDef* source =
            cse::kernel::MoveAt(rig.build.data.p[0], e.from);
        ASSERT_NE(source, nullptr);
        const std::int32_t sourceFrame =
            static_cast<std::int32_t>(previous.moveFrame) + 1;
        const std::int32_t duration = cse::kernel::MoveDuration(*source);
        const bool expectCancel = sourceFrame < duration;

        ASSERT_EQ(e.cancel, expectCancel)
            << "the edge on tick " << e.tick << " out of `"
            << moveName(rig.build.moves[0], e.from) << "` is reported as a "
            << (e.cancel ? "CANCEL" : "LINK") << " and the rule says a "
            << (expectCancel ? "CANCEL" : "LINK") << ": the source was observed at "
               "frame " << previous.moveFrame << " at the end of tick "
            << (e.tick - 1) << ", so f + 1 = " << sourceFrame
            << " against a duration of " << duration << "."
            << Table(log, rig.build.moves[0], e.tick > 3 ? e.tick - 3 : 0, 8);

        if (e.cancel) {
            ++cancels;
            EXPECT_EQ(e.sourceFrame, sourceFrame)
                << "the edge on tick " << e.tick << " records source frame "
                << e.sourceFrame << " and the frame the inclusive window was "
                   "matched against is " << sourceFrame;
        } else {
            ++links;
        }
    }

    // The infinite character's loop is a CANCEL -- that is its whole deliberate
    // bug, `stand_lp -> stand_lp, delay 2, on hit`. A run of pure links here
    // would mean the loop is running by the held-button route and the file's own
    // claim about itself has moved.
    EXPECT_GT(cancels, 0u)
        << "not one transition in the printed loop was a cancel, though the file's "
           "`engine.the_bug` says the only cycle in its graph is a cancel edge.";

    // NO EDGE COMES FROM IDLE HERE, AND THAT IS THE RULE RATHER THAN A GAP.
    // `edges` records the move starts INSIDE the string, and a string opens on
    // the tick its first hit LANDS -- so the move that landed it began `startup`
    // ticks earlier, when there was no string yet, and it appears in `sequence`
    // instead. Every move in this character file has startup >= 3, so that is
    // every opener it can produce. The only other route into this counter is a
    // move started from idle WHILE the string was open, and BuildDemonstration
    // holds a button on every tick of the trace, so the attacker is never idle
    // between the first hit and the last.
    //
    // THE ALTERNATIVE -- OPENING THE EDGE LIST WITH THE MOVE THAT LANDED THE
    // FIRST HIT -- IS REFUSED BY THIS TEST'S OWN ASSERTIONS. Such an edge would
    // have to name a source, and the watcher keeps exactly one tick of history
    // (ComboWatcher.h section 6); the opener's source is older than that, so the
    // edge could only claim `from = 0`, which is a lie whenever the opener was
    // itself cancelled out of a whiffed move. The `ASSERT_EQ(e.from, ...)` above
    // reads the state at `e.tick - 1` and would catch that lie -- and an edge
    // synthesised at the OPENING tick instead would fail `isAStart`, because no
    // move began on the tick a startup-4 move connected.
    EXPECT_EQ(fromIdle, 0u)
        << "an edge was recorded with no source, although the attacker holds a "
           "button on every tick of the demonstration and is therefore never "
           "idle inside the string";

    // ...AND THE OPENER REALLY IS ABSENT FROM THE LIST, which is what keeps the
    // line above a statement about this watcher rather than an accident of the
    // fight. The loop above proved `edges` is a SUBSET of the starts; the top of
    // it claims "not a subset, and not extra ones", and this is the other half:
    // `edges` is EXACTLY the starts from the string's first hit onwards.
    std::size_t startsInside = 0;
    for (std::uint32_t t : starts)
        if (t >= report.startTick) ++startsInside;

    EXPECT_EQ(report.edges.size(), startsInside)
        << "the watcher recorded " << report.edges.size() << " edge(s) for the "
        << startsInside << " move start(s) that happened at or after tick "
        << report.startTick << ", where the string opened";
    EXPECT_LT(startsInside, starts.size())
        << "every move start in this fight happened inside the string, so the "
           "opener that began before the first hit landed is not among them and "
           "the assertion above is about nothing";

    RecordProperty("edges_cancel", static_cast<int>(cancels));
    RecordProperty("edges_link", static_cast<int>(links));
}

// A ROLLBACK INVALIDATES THE JUDGEMENT, so the watcher marks itself STALE and
// stops reporting until Reset(). The alternative -- a per-tick ring of its own
// history -- is real state needing its own rollback-correctness argument, in an
// object whose entire job is to tell a human something TRUE. A verdict shown to a
// playtester that is silently about a timeline that no longer happened is worse
// than no verdict.
TEST(GameComboWatcher, ARollbackMakesTheJudgementStaleAndResetClearsIt) {
    Rig rig{};
    bringUpInfinite(rig);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    const std::uint16_t held = rig.bindings[0].button;

    FightSession session;
    std::string error;
    ASSERT_TRUE(session.Begin(rig.setup, error)) << error;

    ComboWatcher watcher(0, &rig.build.moves[0], &rig.verdict);
    watcher.Reset();
    ASSERT_TRUE(session.AddObserver(&watcher));

    TickLog log;
    ASSERT_TRUE(session.AddObserver(&log));

    for (int i = 0; i < 30; ++i) session.Tick(pairOf(held, 0));
    ASSERT_GT(watcher.Current().hits, 0)
        << "nothing connected, so there is no judgement for a rollback to spoil";
    ASSERT_FALSE(watcher.Stale());
    const std::int32_t before = watcher.Current().hits;

    GameState atFifteen{};
    {
        FightSession probe;
        std::string probeError;
        ASSERT_TRUE(probe.Begin(rig.setup, probeError)) << probeError;
        for (int i = 0; i < 15; ++i) probe.Tick(pairOf(held, 0));
        probe.Snapshot(atFifteen);
    }

    session.Restore(atFifteen);
    session.Tick(pairOf(held, 0));

    EXPECT_TRUE(watcher.Stale())
        << "the first re-simulated tick did not make the judgement stale, so the "
           "watcher is now reporting about a timeline that no longer happened";

    // It stops REPORTING; it does not start lying. Nothing about the string may
    // grow while stale.
    const std::int32_t whileStale = watcher.Current().hits;
    for (int i = 0; i < 10; ++i) session.Tick(pairOf(held, 0));
    EXPECT_TRUE(watcher.Stale());
    EXPECT_EQ(watcher.Current().hits, whileStale)
        << "the watcher went on counting after marking itself stale";

    // IT STOPPED *AT* THE FLAG, not one tick after it. `before` was sampled
    // before the rollback and `whileStale` after the tick that raised the flag,
    // so their equality is the claim that the re-simulated tick was not judged
    // at all -- the `return` that follows `stale_ = true`. The loop above only
    // compares later ticks against `whileStale` and would pass even if the
    // staling tick had itself been counted.
    EXPECT_EQ(whileStale, before)
        << "the tick that raised the flag was judged before the flag went up, so "
           "the last thing the watcher counted belongs to a timeline that no "
           "longer happened";

    // Reset() is where the conversation starts again: it forgets the string in
    // progress and the one tick of history, and clears Stale().
    watcher.Reset();
    EXPECT_FALSE(watcher.Stale());
    EXPECT_EQ(watcher.Current().hits, 0);
    EXPECT_FALSE(watcher.Current().open);
    EXPECT_EQ(watcher.CompletedCombos(), 0);
    EXPECT_TRUE(watcher.Current().sequence.empty());
    EXPECT_TRUE(watcher.Current().edges.empty());

    // ...and it goes on judging from there, which is what makes Reset a resume
    // rather than a teardown.
    for (int i = 0; i < 30; ++i) session.Tick(pairOf(held, 0));
    EXPECT_FALSE(watcher.Stale());
    EXPECT_GT(watcher.Current().hits, 0)
        << "the watcher never recovered after Reset()";
}

// THE STRING ENDS WHEN THE DEFENDER GETS OUT, and the ended one is kept
// separately so a HUD can go on showing it while the next one accumulates. This
// is the case a training mode shows most often and the one the infinite never
// reaches.
TEST(GameComboWatcher, AStringThatEndsIsMovedToPreviousAndCounted) {
    Rig rig{};
    bringUpInfinite(rig);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    const std::uint16_t held = rig.bindings[0].button;

    FightSession session;
    std::string error;
    ASSERT_TRUE(session.Begin(rig.setup, error)) << error;

    ComboWatcher watcher(0, &rig.build.moves[0], &rig.verdict);
    watcher.Reset();
    ASSERT_TRUE(session.AddObserver(&watcher));
    TickLog log;
    ASSERT_TRUE(session.AddObserver(&log));

    // Hold the loop, then let go for long enough that the defender's hitstun
    // certainly expires, then hold it again.
    constexpr std::uint32_t kFirst = 40, kIdle = 90, kSecond = 40;
    for (std::uint32_t i = 0; i < kFirst; ++i) session.Tick(pairOf(held, 0));
    const std::int32_t firstHits = watcher.Current().hits;
    ASSERT_GT(firstHits, 0);
    ASSERT_TRUE(watcher.Current().open);

    for (std::uint32_t i = 0; i < kIdle; ++i) session.Tick(pairOf(0, 0));

    ASSERT_EQ(watcher.CompletedCombos(), 1)
        << "the defender was left alone for " << kIdle << " ticks and the string "
           "never ended, so `open` is not being cleared."
        << DescribeReport(watcher.Current(), rig.build.moves[0])
        << Table(log, rig.build.moves[0], kFirst, 24);
    EXPECT_FALSE(watcher.Current().open);

    // WHAT THE STRING ENDED WITH, AND WHY IT IS NOT `firstHits`.
    //
    // RELEASING THE BUTTON DOES NOT STOP THE MOVE ALREADY IN FLIGHT. StepAttack
    // advances `moveFrame` at the top of the tick and only consults the input
    // further down, so a move that has started plays out under neutral input --
    // which is the same rule BuildDemonstration relies on when it ends a
    // complete trace on the tick the last move STARTED. stand_lp is startup 4
    // and the self-cancel repeats it every 6 ticks, so repetitions begin on
    // ticks 0, 6, ... 36 and connect on 4, 10, ... 40: the one that began on
    // tick 36 reaches its active frame on tick 40, the FIRST tick of the idle
    // phase, and connects there. `firstHits`, sampled after tick 39, is
    // therefore a count taken one hit before the string finished, and asserting
    // the ended string against it would be asserting that the watcher MISSED
    // that last hit.
    //
    // So the count to compare against is the independent one this file uses
    // everywhere else -- the ticks the defender's health fell on, read off the
    // state the kernel produced and never off the watcher being measured.
    // Sampled here, before the second string below starts adding to it.
    const std::int32_t endedHits =
        static_cast<std::int32_t>(log.HitTicks(1).size());
    EXPECT_GE(endedHits, firstHits)
        << "the string ended with fewer hits than it had already been counted "
           "with while it was open";
    EXPECT_LE(endedHits, firstHits + 1)
        << "more than one hit landed after the button was released, and only the "
           "single repetition already in flight can land: both routes to another "
           "one -- the self-cancel and the button scan out of idle -- need the "
           "button held";

    const ComboReport& ended = watcher.Previous();
    EXPECT_EQ(ended.hits, endedHits)
        << "the string that ended did not keep its hit count, so a HUD showing "
           "'37 hits, TRUE COMBO' after the defender got out has nothing to show";
    EXPECT_GT(ended.damage, 0);
    EXPECT_GT(ended.endTick, ended.lastHitTick)
        << "the string ended on or before its own last hit";
    EXPECT_LT(ended.startTick, ended.lastHitTick);

    // AND `Current()` STILL HOLDS IT, until the next string opens. That is the
    // other half of the header's pair -- "the string in progress, OR THE LAST
    // ONE THAT FINISHED" -- and it is what a SWAP at the closing tick would
    // cost: the ended string would be handed away from the accessor documented
    // to keep it, and a HUD reading Current() after the defender got out would
    // draw zeroes with `open` false, which is the state that is supposed to mean
    // "nothing has happened yet".
    EXPECT_EQ(watcher.Current().hits, endedHits)
        << "the string that just ended was taken out of Current() when it closed";

    // The next string is a NEW one, counted separately -- the point of keeping
    // Current() and Previous() apart.
    for (std::uint32_t i = 0; i < kSecond; ++i) session.Tick(pairOf(held, 0));
    EXPECT_TRUE(watcher.Current().open);
    EXPECT_GT(watcher.Current().hits, 0);
    EXPECT_EQ(watcher.Previous().hits, endedHits)
        << "the previous string was overwritten by the new one before it ended";
    EXPECT_EQ(watcher.CompletedCombos(), 1);

    // Zero with Current().open false means nothing has happened yet, which a HUD
    // needs to tell apart from a combo that ended with no hits.
    watcher.Reset();
    EXPECT_EQ(watcher.CompletedCombos(), 0);
    EXPECT_FALSE(watcher.Current().open);
    EXPECT_EQ(watcher.Current().hits, 0);
}

// A witness that cancels a move into ITSELF -- fighter_a_infinite's whole
// deliberate bug is `stand_lp -> stand_lp` -- asks for the same button twice in
// a row. Emitting that bit continuously is a HOLD, and a kernel that starts
// moves on a PRESS would never see the second one: the loop stops being
// performable, not because the analysis was wrong but because the trace was
// written for a kernel that could not tell a hold from a press.
//
// So a derived trace releases between repeats. This is pinned here, in the
// SHIPPED builder, rather than in the tests that happen to drive loops today,
// because the same function turns every showcase verdict into a replay
// (ROADMAP M1.6) and every one of those is a derived trace.
//
// The release costs nothing: it is spent one tick after the move started, deep
// inside startup, where the attacker cannot act whatever is held.
TEST(GameDemonstration, ASelfCancellingWitnessReleasesBetweenRepeats) {
    Rig rig{};
    bringUpInfinite(rig);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    FightSession session;
    std::string error;
    ASSERT_TRUE(session.Begin(rig.setup, error)) << error;

    Demonstration demo{};
    demonstrate(rig, session.State(), kDemoTurns, session.CurrentTick(), demo);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    ASSERT_TRUE(demo.complete) << demo.error;
    ASSERT_FALSE(demo.inputs.empty());

    // The trace must not be one unbroken hold. Counting zero-bit ticks rather
    // than inspecting positions keeps this about the PROPERTY -- there is a gap
    // between repeats -- and not about where the builder chose to put it.
    std::size_t releases = 0;
    for (const cse::kernel::Input& in : demo.inputs)
        if (in.bits == 0u) ++releases;

    EXPECT_GT(releases, 0u)
        << "the derived trace holds its button for all " << demo.inputs.size()
        << " ticks without ever releasing. The witness cancels a move into "
           "itself, so every repeat asks for the same bit -- and a held bit is "
           "one press however long it lasts.";

    // And it is still a trace that performs the witness, which is the half a
    // release count alone cannot say.
    EXPECT_GE(demo.turnsDone, static_cast<std::uint32_t>(kDemoTurns))
        << "the release frames broke the demonstration: " << demo.turnsDone
        << " of " << kDemoTurns << " turns. A gap in the wrong place delays the "
           "next move rather than enabling it.";
}

// A REHEARSAL STILL PERFORMS THE WITNESS FOR A CHARACTER THAT BUFFERS INPUT --
// and, more usefully, WHY the builder's release tick is safe to skip.
//
// BuildDemonstration spends a release tick and `continue`s, on the reasoning
// that "nothing starts from an input of zero, so there is nothing to test for".
// Buffering looks like it should break that: a press made two ticks ago is
// CONSUMED the tick the fighter becomes actionable, so a move can begin on a
// tick the trace is silent on -- and a `continue` there would leave the cursor
// pointing at a move already running, exactly the blindness that cost most of a
// session in the three test drivers (ROADMAP M1.1d).
//
// It does not break, and the reason is the PLACEMENT of the release rather than
// the absence of a buffer. The builder releases only on the tick immediately
// after an advance: the fighter is one frame into a move it just started, and
// the press that started it has already been consumed, so there is nothing
// buffered to fire. The second assertion below turns that sentence into a check,
// because it is the invariant that makes the `continue` correct and it is not
// obvious from the code -- move the release anywhere else and this test is what
// says so.
//
// I wrote this expecting it to fail and change the builder. It passed against
// the unchanged builder, which is the answer.
//
// The window is set on the built MatchData rather than authored, because no
// character file can author one yet -- that is ROADMAP M1.1e.
TEST(GameDemonstration, ABufferedPressIsSeenEvenWhenItLandsOnAReleaseTick) {
    Rig rig{};
    bringUpInfinite(rig);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    // Wide enough that a press is pending across the release the builder emits
    // after every advance, which is what puts a move start ON a release tick.
    rig.build.data.p[0].inputBufferFrames = 8;
    rig.build.data.p[1].inputBufferFrames = 8;

    FightSession session;
    std::string error;
    ASSERT_TRUE(session.Begin(rig.setup, error)) << error;

    Demonstration demo{};
    demonstrate(rig, session.State(), kDemoTurns, session.CurrentTick(), demo);
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    EXPECT_TRUE(demo.complete)
        << "the rehearsal did not finish for a character that buffers input, "
           "though the same witness completes for one that does not. Buffering "
           "makes a link EASIER, so a rehearsal it breaks is a rehearsal that "
           "stopped watching.\n  reachedIndex "
        << demo.reachedIndex << " of " << rig.kernelWitness.size()
        << "\n  turnsDone    " << demo.turnsDone << " of " << kDemoTurns
        << "\n  stalledAt    " << demo.stalledAt
        << "\n  error        " << demo.error;

    EXPECT_GE(demo.turnsDone, static_cast<std::uint32_t>(kDemoTurns))
        << "the buffered rehearsal managed " << demo.turnsDone << " of "
        << kDemoTurns << " turns.";

    // --- and no move ever begins on a tick the trace is silent on ------------
    //
    // Replayed rather than inferred: BuildDemonstration reports the inputs, not
    // the states, so the only honest way to ask "did a move start on a release
    // tick" is to run them.
    GameState replay = session.State();
    std::size_t startsOnSilentTicks = 0, silentTicks = 0, startsSeen = 0;
    for (const cse::kernel::Input& in : demo.inputs) {
        cse::kernel::InputPair pair{};
        pair.p[0] = in;
        pair.p[1] = cse::kernel::Input{};   // the silent dummy, as demonstrate() asks for
        cse::kernel::Simulate(replay, pair, rig.build.data);
        const bool started =
            replay.p[0].moveId != 0u && replay.p[0].moveFrame == 0u;
        if (started) ++startsSeen;
        if (in.bits != 0u) continue;
        ++silentTicks;
        if (started) ++startsOnSilentTicks;
    }

    ASSERT_GT(silentTicks, 0u)
        << "the trace never releases, so this check saw nothing. The witness "
           "cancels a move into itself; there must be release ticks.";
    // NOT VACUOUS: the replay does observe move starts, so a zero below is the
    // absence of one on a SILENT tick and not the loop failing to see any.
    ASSERT_GT(startsSeen, 0u)
        << "replaying the demonstration's own inputs started no move at all, so "
           "this replay is not the run BuildDemonstration rehearsed and the "
           "check beneath it means nothing.";
    EXPECT_EQ(startsOnSilentTicks, 0u)
        << startsOnSilentTicks << " of the trace's " << silentTicks
        << " release tick(s) started a move. BuildDemonstration skips its "
           "cursor check on a release tick, which is only safe while no move can "
           "begin there -- and it is safe today because the release lands one "
           "tick after a start, when the buffer has just been consumed. If this "
           "fails, the release moved, and the `continue` has to look at the "
           "state before spending it.";
}
