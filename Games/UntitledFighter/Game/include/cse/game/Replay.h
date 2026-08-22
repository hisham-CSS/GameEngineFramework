// The recording, and the two halves that make one.
//
// ---------------------------------------------------------------------------
// WHAT A REPLAY IS HERE, AND WHY IT IS SO SMALL
// ---------------------------------------------------------------------------
// A replay in this engine is NOT a recording of what happened. It is a recording
// of WHAT WAS PRESSED, plus the initial conditions, and playback is
// re-simulation. Nothing about positions, health, hit events, move ids or
// damage is stored, because none of it is independent information: Simulate is a
// pure function of (state, inputs, data), so the input log determines every one
// of them exactly.
//
// That is only possible because of a property this project spent its first three
// phases earning. A replay of this shape in a float-based engine is a lie, and
// in an engine that reads a clock inside a tick it is a different lie every time
// it is played. Here it is exact, on both toolchains, byte for byte -- which is
// the same fact tests/test_determinism_crossplat.cpp asserts with a golden hash.
//
// The size difference is not incidental, it is the feature. Sixty seconds of a
// fight at 60 Hz:
//
//   full state per tick   3600 x 80 bytes  = 288 000 bytes
//   input log, flat       3600 x  4 bytes  =  14 400 bytes
//   input log, THIS FILE  a held button for a whole round = ONE 6-byte run
//
// A playtester who validates a combo and wants to post it is handling a file
// measured in hundreds of bytes, and the tool-assisted player's demonstration --
// which is one button held for 160 ticks -- is 118 bytes ALL IN: a 104-byte
// header, one 6-byte run, and one 8-byte end-of-replay checkpoint.
//
// ---------------------------------------------------------------------------
// A REPLAY FILE IS UNTRUSTED INPUT. IT IS THE ONLY ARTIFACT HERE A STRANGER
// HANDS YOU.
// ---------------------------------------------------------------------------
// docs/MAINTENANCE.md's rule ("authored paths are untrusted") admits no
// exceptions, and this is a harder case than a scene file: a character file
// comes from the project, a replay comes from whoever posted it. Every rule
// below is a refusal the reader owes the caller.
//
//   * The path goes through MyCoreEngine::PathIsContained BEFORE anything is
//     opened, on both the read and the WRITE side -- a playtester names the file
//     they are saving, and a name is a path.
//   * EVERY COUNT IN THE HEADER IS CHECKED AGAINST THE BYTES ACTUALLY PRESENT
//     BEFORE IT IS USED TO SIZE OR INDEX ANYTHING. The reader never trusts
//     `runCount` to describe the file; it computes what the file's own length
//     implies and refuses when they disagree. This is the rule that keeps a
//     four-byte field from becoming a multi-gigabyte allocation.
//   * A truncated, padded or hostile file is a CLEAN REFUSAL with a message
//     naming the field and the offset. Never a throw, never a read past the end,
//     never an abort. Failure is DATA (ReplayReport), exactly as it is for
//     LoadReport, BuildReport and ProverReport.
//   * SIZE MUST MATCH EXACTLY. Trailing bytes after the last section are a
//     refusal, not something to ignore. A lenient reader is how a payload rides
//     along inside a file that otherwise validates.
//
// ---------------------------------------------------------------------------
// THE FORMAT, BYTE BY BYTE
// ---------------------------------------------------------------------------
// EVERY MULTI-BYTE FIELD IS LITTLE-ENDIAN, ON EVERY PLATFORM, ALWAYS. Not host
// order. The host's order is not written down anywhere in the file and cannot
// be, so "native" would mean a replay recorded on one machine is silently
// garbage on another -- and this project's whole claim is that Windows and Linux
// agree byte for byte. Signed fields are two's complement.
//
// THE IMPLEMENTATION MUST ASSEMBLE FIELDS BYTE BY BYTE WITH SHIFTS AND MASKS. It
// must NOT memcpy a scalar and it must NOT reinterpret_cast a byte pointer to a
// header struct. That is two bugs in one line: an endianness bug on a big-endian
// target, and an alignment bug that is undefined behaviour on targets that care.
// This is the single most likely place for three implementers to disagree, so it
// is stated as a rule rather than left to taste.
//
//   HEADER -- 104 bytes, offsets absolute from the start of file
//
//   off  size  field               notes
//   ---  ----  ------------------  -------------------------------------------
//     0     4  magic               the bytes 'C','S','R','P' in that order.
//                                  Written as BYTES, not as a uint32 constant,
//                                  so the check is endian-independent and the
//                                  file is greppable.
//     4     2  version             uint16. Must equal kReplayVersion exactly.
//     6     2  stateBytes          uint16. sizeof(cse::kernel::GameState). See
//                                  the refusal table: this separates "the state
//                                  LAYOUT changed" from "the state VALUES
//                                  changed", and those need different answers.
//     8     4  matchDataHash       uint32. HashMatchData over the MatchData
//                                  this was recorded against.
//    12     4  seed                uint32. MatchStart::seed, as given.
//    16     4  startPosX0          int32.  MatchStart::startPosX[0], sub-units.
//    20     4  startPosX1          int32.  MatchStart::startPosX[1].
//    24     4  tickCount           uint32. >= 1, <= ReplayReadOptions::maxTicks.
//    28     4  runCount            uint32. >= 1.
//    32     4  checkpointCount     uint32. >= 1.
//    36     4  checkpointInterval  uint32. >= 1. Stored so a reader can explain
//                                  how precisely a divergence can be located.
//    40    32  characterId0        UTF-8 bytes, NUL-padded. No terminator is
//                                  required when the id fills all 32.
//    72    32  characterId1        same.
//   104        end of header (kReplayHeaderBytes)
//
//   RUN SECTION -- at offset 104, `runCount` entries of 6 bytes
//
//   off  size  field    notes
//   ---  ----  -------  ------------------------------------------------------
//     0     2  p0 bits  uint16, cse::kernel::Input::bits for slot 0
//     2     2  p1 bits  uint16, slot 1
//     4     2  length   uint16, how many consecutive ticks carry these bits.
//                       1..65535. ZERO IS A REFUSAL: a zero-length run is how a
//                       hostile file makes a decoder loop without consuming
//                       input, and "harmless" is not a property to leave to the
//                       shape of somebody's for-loop.
//
//   The lengths must sum to EXACTLY tickCount. Less is truncation, more is a
//   lie; both are refusals.
//
//   CHECKPOINT SECTION -- at offset 104 + 6*runCount, `checkpointCount` entries
//                         of 8 bytes
//
//   off  size  field     notes
//   ---  ----  --------  -----------------------------------------------------
//     0     4  tick      uint32. The index of the tick AFTER WHICH the checksum
//                        was taken. Strictly increasing, every value < tickCount.
//     4     4  checksum  uint32. cse::kernel::Checksum of the state at that
//                        moment -- when state.tick == tick + 1. THE OFF-BY-ONE
//                        IS STATED HERE ON PURPOSE; it is TickView's, and an
//                        implementer who keys on state.tick writes every
//                        checkpoint one tick late.
//
//   The tick is stored rather than implied by the entry's index. It costs four
//   bytes per second and buys two things: the section validates on its own (a
//   hostile file's checkpoints can be rejected structurally, before any of them
//   is compared), and the final checkpoint can sit off the interval.
//
//   TOTAL FILE SIZE = 104 + 6*runCount + 8*checkpointCount, exactly.
//
// ---------------------------------------------------------------------------
// WHY THE INPUT STREAM IS RUN-LENGTH ENCODED
// ---------------------------------------------------------------------------
// Because of the actual shape of fighting-game input, which is not the shape of
// general time-series data: A HELD BUTTON IS THE COMMON CASE AND MOST TICKS
// REPEAT THE PREVIOUS ONE. A player walks forward for forty ticks, holds down
// for twenty, holds a punch through its whole 14-tick animation. The kernel
// makes this even more pronounced than a real game would -- most of a trace is
// a direction held or nothing at all -- and even a self-cancel loop, which
// since ROADMAP M1.1d must RELEASE between repeats to give the kernel a second
// press, is a long run of the held button broken by single zero ticks. That is
// still runs of tens of ticks rather than a byte per tick.
//
// (Before M1.1d the kernel took the button HELD, and that same demonstration
// was literally one button held for its whole 160 ticks -- a single 6-byte run.
// The encoding was chosen against that shape and is merely less spectacular
// against this one.)
//
// The worst case is honest and small: input that changes every single tick costs
// 6 bytes per tick against a flat log's 4, a 1.5x loss on a file that would be
// 14 KB a minute either way. Paying 1.5x in the case that never happens to save
// 500x in the case that always does is not a close call.
//
// RUNS ARE OVER THE PAIR, NOT PER PLAYER. Two independent streams would break
// their runs at different ticks and would compress a two-human match slightly
// better; they would also double the header bookkeeping, double the validation
// surface, and require a decoder to keep two cursors in step. In the case this
// module exists for -- one attacker and a silent training dummy -- a pair-run is
// exactly as good as two streams, because the dummy's bits never change. One
// stream, and the second player costs two bytes per run.
//
// THE FILE IS RLE; THE DECODED REPLAY IS FLAT. ReplayData::inputs is expanded to
// one entry per tick at load. That is what keeps ReplayInputSource::At an O(1)
// pure function of a tick index -- seeking through runs would make a scrubber
// quadratic and a rollback re-simulation slow in exactly the place it must not
// be -- and it puts every bounds check in one place, at load, where a refusal is
// still possible.
//
// ---------------------------------------------------------------------------
// WHAT IS DELIBERATELY NOT IN THE FILE
// ---------------------------------------------------------------------------
// THE ANALYSIS. No ProverResult, no verdict, no witness. A replay records THE
// MATCH; the judgement is re-derived at playback by running the prover over the
// character file again. That is not laziness -- it is what makes the character
// hash meaningful. If the verdict travelled with the replay, a replay of an
// edited character would play back next to the OLD verdict and look correct.
//
// THE CHARACTER DATA ITSELF. Only its hash and its id. Shipping the data would
// make a replay self-contained and would make it a distribution channel for
// character files, which is a licence question (tests/fixtures/characters
// carries a README about exactly that) and a much larger untrusted-input
// surface. The hash is the check; the id is for the human reading the error.
#pragma once

#include "cse/game/FightSession.h"
#include "cse/game/InputSource.h"

#include "cse/kernel/Combat.h"
#include "cse/kernel/GameState.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cse::game {

// --- Format constants -------------------------------------------------------

// 'C','S','R','P' -- CatSplat RePlay. Compared byte by byte.
inline constexpr unsigned char kReplayMagic[4] = { 'C', 'S', 'R', 'P' };

// Bumped for ANY change to the byte layout, to the meaning of a field, or to
// MatchStart. There is no forward compatibility and no backward compatibility
// shim, and there will not be one: a shim is a second definition of the format,
// and a format with two definitions is a format that plays some old files back
// wrong and says nothing. A version mismatch is a refusal that names both
// numbers.
inline constexpr std::uint16_t kReplayVersion = 1;

inline constexpr std::size_t kReplayHeaderBytes      = 104;
inline constexpr std::size_t kReplayRunBytes         = 6;
inline constexpr std::size_t kReplayCheckpointBytes  = 8;
inline constexpr std::size_t kReplayCharacterIdBytes = 32;

// One checkpoint a second. The trade: interval 1 names the exact tick a
// divergence happened at and costs 8 bytes a tick (doubling a dense file);
// interval 60 costs 8 bytes a second and localises a divergence to a one-second
// window. Sixty is the default because a divergence is a REGRESSION HUNT, not a
// live desync -- the investigator has the file and can re-read it at interval 1
// by re-recording. The final tick always gets a checkpoint whatever the interval
// is, so every replay has an end-to-end check.
inline constexpr std::uint32_t kDefaultCheckpointInterval = 60;

// --- The content hash both peers, and both playbacks, must agree on ---------

// FNV-1a over the object representation of MatchData.
//
// The same construction as cse::kernel::Checksum and for the same reason: it is
// sound to hash these bytes ONLY because the type has no padding holes with
// indeterminate values. Combat.h's MoveDef::pad_ and CancelEdge::pad_ exist for
// this, MatchBuilder.cpp writes them explicitly, and the static asserts below
// are what keep the property from rotting -- they are written as a sum of the
// members rather than as a magic byte count, so they check for PADDING rather
// than for a number somebody remembered to update.
//
// THIS IS THE FUNCTION ARCHITECTURE.md 4.8's CONNECT HANDSHAKE SHOULD USE. The
// handshake hashes "the loaded POD arrays, not canonicalized text" (text
// canonicalization is where float-repr and key-order bugs live), and a replay
// asks the identical question of a file instead of a peer. Two hashes for one
// question would eventually disagree.
std::uint32_t HashMatchData(const cse::kernel::MatchData& data);

// Three Boxes now: the standing body, the crouching one, and the pushbox.
// The eleven int32s are maxHealth, juggleMax, inputBufferFrames,
// hitstunDecayStep, hitstunDecayFloor, resourceCount, walkSpeedSub, gravitySub,
// jumpImpulseSub, moveCount and cancelCount; the ResourceDef array is counted
// separately. Written as a sum of the members rather than as a
// byte count so this keeps checking for PADDING after the P2 expansion rather
// than becoming a number to update.
static_assert(sizeof(cse::kernel::FighterData) ==
                  3 * sizeof(cse::kernel::Box) + 11 * sizeof(std::int32_t) +
                      cse::kernel::kMaxResources * sizeof(cse::kernel::ResourceDef) +
                      cse::kernel::kMaxMovesPerFighter * sizeof(cse::kernel::MoveDef) +
                      cse::kernel::kMaxCancelsPerFighter * sizeof(cse::kernel::CancelEdge),
              "FighterData has acquired padding. HashMatchData reads its object "
              "representation, so a padding hole would make two machines with "
              "identical characters compute different hashes and refuse each "
              "other's replays.");
static_assert(sizeof(cse::kernel::MatchData) ==
                  cse::kernel::kMaxFighters * sizeof(cse::kernel::FighterData),
              "MatchData has acquired padding between its fighters. Same hazard "
              "as above.");
static_assert(sizeof(cse::kernel::GameState) <= 0xFFFFu,
              "GameState no longer fits the replay header's uint16 stateBytes "
              "field. That is a format change: bump kReplayVersion.");

// --- The decoded replay -----------------------------------------------------

struct ReplayCheckpoint {
    std::uint32_t tick     = 0;   // the tick AFTER WHICH the checksum was taken
    std::uint32_t checksum = 0;
};

// A replay, decoded and validated. Producing one of these is the reader's whole
// job; every refusal below happens before a caller ever sees one, so a
// ReplayData in hand is a replay whose structure has already been proven.
struct ReplayData {
    std::uint16_t version       = 0;
    std::uint32_t matchDataHash = 0;

    // Human-facing only. The hash is what decides whether this replay is about
    // the same game; the id is what the error message says out loud. A reader
    // must NEVER accept on a matching id, and must never refuse on a mismatched
    // one -- two characters can legitimately be renamed, and no character can
    // legitimately have its frame data changed under a replay.
    std::string characterId[2];

    // Exactly the fields FightSession's MatchStart carries. Feed it straight to
    // FightSetup and the opening position is reproduced.
    MatchStart start{};

    // DECODED AND FLAT: one entry per tick, size() == the header's tickCount.
    // See the header note on why the file is RLE and this is not.
    std::vector<cse::kernel::InputPair> inputs;

    // Sorted, strictly increasing, every tick < inputs.size(). The last entry's
    // tick is always inputs.size() - 1.
    std::vector<ReplayCheckpoint> checkpoints;
    std::uint32_t                 checkpointInterval = kDefaultCheckpointInterval;

    std::uint32_t TickCount() const {
        return static_cast<std::uint32_t>(inputs.size());
    }
};

// --- Refusals ---------------------------------------------------------------

// WHY THESE ARE SEPARATE VALUES AND NOT ALL "the file is bad".
//
// Each one means something different to the person holding the file, and three
// of them are not the file's fault at all. A playtester whose replay stops
// working needs to be told which of these happened, in these words, because the
// remedy is different every time: re-download, update the game, get the old
// character back, or file a bug against the engine.
enum class ReplayRefusal : std::uint8_t {
    None,
    // The path escaped the sandbox, or was absolute, or carried "..". Refused
    // lexically, before any filesystem access (PathSandbox.h).
    PathRefused,
    // Could not open or read, or the file exceeded maxFileBytes. The size check
    // happens BEFORE the read, so a hostile 4 GB "replay" costs one stat call.
    Unreadable,
    // The magic is wrong. This is not a replay file at all -- say so, rather
    // than reporting a corrupt replay, because the usual cause is the wrong
    // file being picked.
    NotAReplay,
    // The version differs. Name BOTH numbers. The remedy is a different build,
    // and no amount of retrying will help.
    Version,
    // sizeof(GameState) differs from the recording build's. THE STATE LAYOUT
    // CHANGED, so every checksum in the file is a hash of a differently shaped
    // object and comparing them would produce a divergence report that means
    // nothing. This is a distinct refusal from the value-level divergence below
    // and must not be collapsed into it.
    StateLayout,
    // The MatchData hash differs from the one the caller is holding. THE
    // CHARACTER FILE WAS EDITED SINCE THIS WAS RECORDED. This replay is about a
    // different game: the same inputs against different frame data are a
    // different fight, and playing it back would show a combo that never
    // happened or drop one that did. REFUSE LOUDLY AND NAME THE CHARACTER --
    // ReplayData::characterId is carried precisely so the message can say which
    // one, and the message should say "edited since", because that is the fact,
    // not "corrupt".
    CharacterChanged,
    // Structurally malformed: truncated, padded, a zero-length run, run lengths
    // that do not sum to tickCount, checkpoints out of order or out of range,
    // a count of zero where one is required, a character id with data after its
    // terminator. The message names the field and the byte offset.
    Malformed,
    // A count inside the declared limits of the format but outside this
    // caller's: more ticks than ReplayReadOptions::maxTicks. Separate from
    // Malformed because the file is well-formed and the limit is a policy the
    // caller chose.
    TooLarge,
};

const char* ReplayRefusalName(ReplayRefusal refusal);

// Failures and non-fatal notes as DATA, matching LoadReport / BuildReport /
// ProverReport. Reading a replay must not throw through a menu's paint path.
struct ReplayReport {
    std::string              error;    // empty if and only if the read succeeded
    ReplayRefusal            refusal = ReplayRefusal::None;
    std::vector<std::string> warnings;
};

struct ReplayReadOptions {
    // Refuse a file larger than this before reading it. The bound the format's
    // own caps imply is 104 + 6*runCount + 8*checkpointCount with runCount
    // bounded by tickCount; 8 MiB is comfortably above any legitimate hour-long
    // replay and far below a denial of service.
    std::size_t maxFileBytes = 8u * 1024u * 1024u;

    // Refuse a tickCount above this BEFORE reserving anything. kMaxMatchTicks is
    // one hour at 60 Hz, so the largest allocation this reader can be talked
    // into is 216000 InputPair -- 864 KB, bounded, and bounded before the first
    // byte of payload is touched.
    std::uint32_t maxTicks = kMaxMatchTicks;

    // The hash the caller's own MatchData produces. Nonzero arms the
    // CharacterChanged refusal.
    //
    // ZERO SKIPS THE CHECK AND RECORDS A WARNING, never a silent pass. Same
    // shape as LoadOptions::expectedResources and for the same reason: a caller
    // who never sets this has no character-change check at all, and that should
    // be visible in the report rather than inferred from a green return. A tool
    // that dumps replays legitimately has no MatchData; a play path never does.
    std::uint32_t expectedMatchDataHash = 0;
};

// --- Reading ----------------------------------------------------------------

// From a file. `relPath` is UNTRUSTED and is resolved through
// MyCoreEngine::PathIsContained against `baseDir` before the file is opened --
// docs/MAINTENANCE.md, and that rule has no exceptions. Absolute paths,
// drive/UNC roots and any ".." component are refused lexically, before any
// filesystem access.
bool ReadReplayFile(const std::string&       baseDir,
                    const std::string&       relPath,
                    const ReplayReadOptions& options,
                    ReplayData&              out,
                    ReplayReport&            report);

// From bytes already in memory. `sourceName` is used only to name the source in
// error messages. This exists for the reason LoadCharacterJson exists: a
// refusal nobody has watched fire is not a refusal, and every one of the cases
// above needs a test that builds the hostile bytes by hand rather than writing
// a malformed file to disk.
bool DecodeReplay(std::string_view         sourceName,
                  const std::uint8_t*      bytes,
                  std::size_t              byteCount,
                  const ReplayReadOptions& options,
                  ReplayData&              out,
                  ReplayReport&            report);

// The character-changed check, on its own, so the play path always states it the
// same way and the message is written once. Returns false and fills `error` with
// a sentence naming both character ids and both hashes when they disagree.
//
// Call it even when ReplayReadOptions::expectedMatchDataHash was set -- it is
// then a cheap re-assertion at the point of use, which is where a mistaken
// MatchData would actually bite.
bool ReplayMatchesData(const ReplayData&             replay,
                       const cse::kernel::MatchData& data,
                       std::string&                  error);

// --- Recording --------------------------------------------------------------

struct ReplayRecorderOptions {
    // Ticks between checkpoints. Must be >= 1. See kDefaultCheckpointInterval
    // for the trade; 1 records every tick and names the exact divergence tick.
    std::uint32_t checkpointInterval = kDefaultCheckpointInterval;

    // Stop recording (and report) past this many ticks rather than growing
    // without bound. A training session left running overnight must not consume
    // memory in proportion to how long nobody was looking at it.
    std::uint32_t maxTicks = kMaxMatchTicks;
};

// Captures a session, one tick at a time, by observing it.
//
// USAGE, and the order matters:
//     ReplayRecorder recorder;
//     recorder.Begin(setup.start, HashMatchData(*setup.data), id0, id1);
//     session.AddObserver(&recorder);
//     ... session.Tick() ...
//     recorder.Write(dir, "combo.csrp", error);
//
// Begin() BEFORE the first Tick, always. A recorder that receives a TickView
// without having been told the initial conditions cannot produce a valid file
// and refuses every later tick rather than writing a header of zeroes -- an
// unplayable file that validates is worse than no file.
//
// RE-SIMULATION OVERWRITES, IT DOES NOT APPEND. On a TickView with
// `resimulated` set, the entry at `view.tick` is REPLACED. This is the rule that
// makes the recorder correct under rollback, and it is stated here because the
// obvious implementation -- push_back -- produces a plausible-looking file whose
// input log is the union of every predicted and corrected timeline, which then
// replays as something nobody did.
//
// A GAP IS A HARD ERROR. `view.tick` may be equal to the current length (append)
// or below it (overwrite). Above it means a tick was never delivered, and the
// recorder REFUSES the rest of the recording rather than zero-filling: neutral
// input is a legal thing to press, so a zero-filled gap is indistinguishable
// from a player who let go, and the resulting file would be a confident
// recording of a fight that did not happen.
class ReplayRecorder final : public ITickObserver {
public:
    explicit ReplayRecorder(const ReplayRecorderOptions& options = {});

    // Character ids longer than kReplayCharacterIdBytes are REFUSED here rather
    // than truncated. Truncation would produce a file whose error message names
    // the wrong character, and the fixed-width field exists so that no count
    // read from an untrusted file ever sizes an allocation -- paying for that
    // with a silent rename would give the property back.
    bool Begin(const MatchStart& start,
               std::uint32_t     matchDataHash,
               std::string_view  characterId0,
               std::string_view  characterId1,
               std::string&      error);

    void OnTick(const TickView& view) override;

    // Empty until something has gone wrong; once set, the recorder is inert and
    // Encode/Write refuse. Checked by the host after a match rather than after
    // every tick, because OnTick is the 60 Hz path and cannot report.
    const std::string& Error() const;
    bool               Recording() const;
    std::uint32_t      TickCount() const;

    // The bytes, exactly as they would land on disk. Public because a test must
    // be able to assert the format without touching the filesystem, and because
    // a future "upload this replay" path wants bytes rather than a file.
    bool Encode(std::vector<std::uint8_t>& out, std::string& error) const;

    // `relPath` is UNTRUSTED -- a playtester types the name -- and goes through
    // PathIsContained against `baseDir` exactly as the read path does. Writes
    // nothing at all on refusal; a partially written replay that fails
    // validation on load is a worse outcome than no file.
    bool Write(const std::string& baseDir,
               const std::string& relPath,
               std::string&       error) const;

private:
    ReplayRecorderOptions               options_{};
    MatchStart                          start_{};
    std::uint32_t                       matchDataHash_ = 0;
    std::string                         characterId_[2];
    std::vector<cse::kernel::InputPair> inputs_;
    std::vector<ReplayCheckpoint>       checkpoints_;
    bool                                recording_ = false;
    std::string                         error_;
};

// --- Playing back -----------------------------------------------------------

// A source that reads one player's bits out of a decoded replay.
//
// `replay` is BORROWED and must outlive this object. One source per player, both
// reading the same ReplayData -- which is why the decoded stream is a flat
// vector of InputPair rather than two vectors: the pairing is what was recorded,
// and splitting it here would let a host play back one player's input against
// the other's from a different tick.
class ReplayInputSource final : public IInputSource {
public:
    ReplayInputSource(const ReplayData& replay, int player);

    InputSample   At(std::uint32_t tick) const override;
    std::uint32_t AuthoredEndTick() const override;
    const char*   Name() const override;

private:
    const ReplayData* replay_ = nullptr;
    int               player_ = 0;
};

// --- Divergence -------------------------------------------------------------

// WHAT A MID-PLAYBACK CHECKSUM MISMATCH MEANS, AND WHY IT IS NOT A CORRUPT FILE.
//
// The file loaded. Its structure validated. Its character hash matched, so the
// frame data is byte-identical to what was recorded against. Its state layout
// matched, so the checksums are hashes of the same shaped object. The same
// inputs were fed in, in the same order, from the same opening position -- and
// the state at tick T is not the state that was recorded at tick T.
//
// That is not corruption. THE SIMULATION CHANGED. Somebody edited the kernel and
// the arithmetic of a tick is no longer what it was, and this replay just
// measured it -- at the tick it first bit, from a recording made before the
// change. It is the cheapest determinism regression test this project can own
// and it is a FINDING WORTH SURFACING IN THOSE WORDS, not an error to swallow.
// It is also, in the crossplay case, the Windows-vs-Linux disagreement that
// tests/test_determinism_crossplat.cpp exists to prevent, arriving in a file
// instead of a golden hash.
struct ReplayDivergence {
    bool diverged = false;

    // The checkpoint tick at which the live checksum first disagreed, and the
    // last checkpoint that agreed. The divergence happened somewhere in
    // (previousAgreeingTick, tick] -- with the default interval that is a
    // one-second window, and the reader can say so honestly rather than claiming
    // a precision the file does not carry.
    std::uint32_t tick                 = 0;
    std::uint32_t previousAgreeingTick = 0;
    bool          hadPreviousAgreement = false;

    std::uint32_t recordedChecksum = 0;
    std::uint32_t liveChecksum     = 0;

    // A SEPARATE AND MUCH MORE COMMON CAUSE, kept separate on purpose. The bits
    // fed into tick `tick` were not the bits the replay recorded for it. That is
    // a HOST WIRING BUG -- the wrong source bound, an off-by-one in the tick
    // index, a second source overriding the first -- and reporting it as "the
    // engine changed" would send an investigator to the one place that is
    // innocent. Checked BEFORE the checksum, because a checksum mismatch caused
    // by different inputs is not evidence about the simulation at all.
    bool          inputMismatch     = false;
    std::uint32_t inputMismatchTick = 0;
};

// Watches a session replaying a ReplayData and reports the first disagreement.
//
// `replay` is BORROWED. Register it as an observer alongside whatever else is
// watching; it is one comparison per tick and one more at each checkpoint.
//
// PLAYBACK CONTINUES BY DEFAULT AFTER A DIVERGENCE, and that is a choice worth
// stating against ADR-002 CHOICE C, which says a NETWORK desync stops the match
// and names the frame. This is not that. A network desync is two live peers with
// no authority to resync from, where continuing shows a player a fight their
// opponent is not in. A replay divergence is a regression against the past: the
// investigator wants the whole drift profile -- did it recover, did it explode,
// how many checkpoints disagreed -- and stopping at the first one throws that
// away. Set `stopOnDivergence` for the other behaviour; either way the session
// is not touched by this observer, which only ever reads.
class ReplayVerifier final : public ITickObserver {
public:
    explicit ReplayVerifier(const ReplayData& replay, bool stopOnDivergence = false);

    void OnTick(const TickView& view) override;

    const ReplayDivergence& Result() const;
    // How many checkpoints were compared and how many agreed. A verifier that
    // compared nothing must not read as a verifier that agreed with everything,
    // and this is the pair that tells them apart.
    std::uint32_t CheckpointsCompared() const;
    std::uint32_t CheckpointsAgreed() const;

    // True once a divergence was reported AND stopOnDivergence was set. The
    // HOST is what stops -- this object has no way to stop a session and is not
    // going to grow one, because an observer that can halt the thing it observes
    // is no longer an observer.
    bool ShouldStop() const;

    void Reset();

private:
    const ReplayData* replay_           = nullptr;
    bool              stopOnDivergence_ = false;
    ReplayDivergence  result_{};
    std::size_t       nextCheckpoint_   = 0;
    std::uint32_t     compared_         = 0;
    std::uint32_t     agreed_           = 0;
};

} // namespace cse::game
