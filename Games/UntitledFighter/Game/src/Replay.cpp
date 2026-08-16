// The replay format: the writer, the reader, and the two observers that sit on
// either end of it.
//
// ---------------------------------------------------------------------------
// THE READER IS THE JOB
// ---------------------------------------------------------------------------
// The recorder writes bytes this process just produced. The reader parses bytes
// A STRANGER ON THE INTERNET PRODUCED -- a playtester posts a combo, somebody
// downloads the file, and it lands in this function. It is the most hostile
// input surface in this repository: a character file at least comes from the
// project, and a scene file at least came from the editor. Everything below the
// magic is a number chosen by whoever made the file, including the numbers that
// say how big the rest of it is.
//
// So the reader obeys three rules absolutely, and every one of them is about the
// same failure -- a four-byte field turning into an allocation, an index, or a
// read past the end:
//
//   1. A COUNT IS NOT A COUNT UNTIL THE FILE'S OWN LENGTH AGREES WITH IT. The
//      reader never believes `runCount`. It computes 104 + 6*runCount +
//      8*checkpointCount and refuses unless that is EXACTLY the number of bytes
//      present. After that single comparison, every offset formed below is
//      provably inside the buffer -- which is why it happens before any of them
//      is formed, and why trailing bytes are a refusal rather than something to
//      skip. A lenient reader is how a payload rides along inside a file that
//      otherwise validates.
//   2. NOTHING IS SIZED FROM A HEADER FIELD BEFORE IT IS BOUNDED. `tickCount` is
//      checked against the caller's cap AND against kMaxMatchTicks before a
//      single element is reserved, and the run lengths are summed in 64-bit and
//      abandoned the moment they exceed it. The largest allocation this file can
//      be talked into by its input is 216000 InputPair -- 864 KB -- and that is
//      true before the first byte of payload is touched.
//   3. FAILURE IS DATA. No throw, no abort, no read past the end, on a truncated
//      file, an all-zero file, a one-byte file, or a file whose tickCount is
//      0xFFFFFFFF. ReplayReport carries the refusal exactly as LoadReport,
//      BuildReport and ProverReport carry theirs.
//
// ---------------------------------------------------------------------------
// THE ORDER OF THE CHECKS IS THE DIAGNOSTIC
// ---------------------------------------------------------------------------
// A replay that will not load is a support conversation, and there are five
// different conversations here. Which one a file gets depends entirely on what
// is asked first, so the order is chosen so that the FIRST question a file fails
// is the one whose answer the person holding it can act on:
//
//   1. magic          "this is not a replay"          -> pick a different file
//   2. version        "your build cannot read it"     -> change build, re-record
//   3. stateBytes     "the state layout changed"      -> engine bug, file it
//   4. structure      "the file is damaged"           -> re-download it
//   5. matchDataHash  "the character was edited"      -> get the old character
//
// Asking the content hash before the structure would tell somebody holding a
// half-downloaded file that their character was edited -- a conversation that
// ends nowhere, about a fact nobody can verify from a truncated file. Asking the
// version after the structure is the same mistake pointing the other way: on a
// file from a future version the layout is not ours to interpret, so every
// offset past byte 6 is a guess and "malformed" would be a lie about somebody
// else's correct file.
//
// And CharacterChanged is not a corruption report. The file is perfect; the
// GAME moved. Its message says "edited since" and names the character, because
// during development that is an ordinary Tuesday and the remedy is to check out
// the old character file, not to re-download anything.
//
// ---------------------------------------------------------------------------
// EVERY MULTI-BYTE FIELD IS ASSEMBLED WITH SHIFTS AND MASKS
// ---------------------------------------------------------------------------
// No memcpy of a scalar, no reinterpret_cast of a byte pointer to a header
// struct, in either direction. Replay.h states this as a rule rather than
// leaving it to taste, and the rule is worth restating here because the tempting
// version is one line and contains two bugs: it writes host byte order into a
// file whose whole purpose is that Windows and Linux agree on it, and it reads a
// uint32 through a pointer with no alignment guarantee, which is undefined
// behaviour on the targets that care and a silent halving of throughput on the
// ones that do not.
//
// The offsets are named once, in kOff* below. The READER is built out of those
// names; the WRITER appends its fields in that order and then checks that the
// header came out to exactly kReplayHeaderBytes. That check is not decoration --
// an order/offset disagreement is the one bug that makes every file this build
// writes unreadable while every existing test still passes, because the tests
// would be round-tripping the same mistake through both halves.
//
// NOTHING HERE READS A CLOCK, and nothing here is a float. The recorder is on
// the 60 Hz path (it is an ITickObserver) and the reader produces the input log
// a tick path consumes; a replay whose contents depended on when it was recorded
// or played back would not be a replay.
#include "cse/game/Replay.h"

// PathSandbox.h lives in Engine/src/core and pulls in nothing but Core.h,
// <filesystem> and <string>. This library's own CMakeLists.txt puts that
// directory on this target's PRIVATE include path — repo-rooted, because the
// engine sits at the repo root and this title does not — and deliberately does
// NOT compile
// PathSandbox.cpp -- CseData already does, and a second archive defining
// PathIsContained is an LNK2005 waiting for the first unlucky link order.
#include "PathSandbox.h"

// Included explicitly rather than leaned on transitively: gcc is stricter than
// MSVC about what a header drags in, CI compiles both, and "it built on Windows"
// is not evidence about anything this project claims.
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace cse::game {

namespace {

// --- The offsets, named once ------------------------------------------------
//
// Written out as constants rather than as literals at each call site so that the
// reader and the writer are demonstrably describing the same file. A format
// whose two halves each carry their own copy of "the seed is at 12" is a format
// that will one day have two different answers.
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

static_assert(kOffCharacterId1 + kReplayCharacterIdBytes == kReplayHeaderBytes,
              "The header offsets no longer tile the 104-byte header exactly. "
              "Either a field moved or kReplayHeaderBytes changed; both are "
              "format changes and need a kReplayVersion bump.");
static_assert(kOffCharacterId0 + kReplayCharacterIdBytes == kOffCharacterId1,
              "The two character id fields overlap or leave a hole between them.");

// The longest run one entry can express. 16 bits, so a held button costs one run
// per 65535 ticks -- eighteen minutes -- and a whole legal replay (kMaxMatchTicks
// = one hour) is four runs at worst.
constexpr std::uint32_t kMaxRunLength = 0xFFFFu;

// --- Little-endian primitives -----------------------------------------------
//
// Shifts and masks in both directions. These six are the ONLY functions in this
// file that touch the byte layout, which is what makes the "no memcpy of a
// scalar, no reinterpret_cast to a header struct" rule enforceable by reading
// forty lines rather than the whole file.

std::uint16_t readU16(const std::uint8_t* p) {
    const unsigned lo = p[0];
    const unsigned hi = p[1];
    return static_cast<std::uint16_t>(lo | (hi << 8));
}

std::uint32_t readU32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

// Two's complement, decoded by ARITHMETIC rather than by a cast.
//
// `static_cast<std::int32_t>(u)` for u > INT32_MAX is implementation-defined
// before C++20 and this target is C++17, so the obvious spelling is a conversion
// whose result the standard does not pin down. The subtraction below is defined
// on every implementation and every intermediate stays inside int32's range:
// u - 0x80000000 lands in [0, 0x7FFFFFFF], and subtracting 2147483648 from that
// in two steps never forms a value the type cannot hold.
std::int32_t readI32(const std::uint8_t* p) {
    const std::uint32_t u = readU32(p);
    if (u <= 0x7FFFFFFFu) {
        return static_cast<std::int32_t>(u);
    }
    return static_cast<std::int32_t>(u - 0x80000000u) - 2147483647 - 1;
}

void writeU16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
}

void writeU32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
}

// The inverse of readI32, and built the same way and for the same reason: the
// conversion of a negative int32 to uint32 is well defined, but spelling it as a
// cast invites the reverse cast at the other end, which is not. Round-tripping
// through these two functions is exact for every int32 including INT32_MIN --
// which is the value a "negate it and set the sign bit" implementation gets
// wrong, because INT32_MIN has no positive counterpart.
void writeI32(std::vector<std::uint8_t>& out, std::int32_t v) {
    std::uint32_t u = 0;
    if (v >= 0) {
        u = static_cast<std::uint32_t>(v);
    } else {
        u = 0xFFFFFFFFu - static_cast<std::uint32_t>(-(v + 1));
    }
    writeU32(out, u);
}

// --- Message helpers --------------------------------------------------------

// A TEMPLATE and not two overloads. The obvious pair -- num(std::int64_t) and
// num(std::size_t) -- is ambiguous for every uint32_t argument in this file,
// which is most of them, because a uint32 converts equally well to both. That is
// a compile error rather than a bug, but only after somebody has written forty
// call sites against it.
template <typename T>
std::string num(T v) {
    return std::to_string(v);
}

std::string hex32(std::uint32_t v) {
    static const char digits[] = "0123456789ABCDEF";
    std::string s = "0x";
    for (int shift = 28; shift >= 0; shift -= 4) {
        s += digits[(v >> shift) & 0xFu];
    }
    return s;
}

// Four bytes as hex plus their printable rendering, for the magic mismatch. The
// hex is what a hex editor shows and the characters are what a human recognises
// ("oh, that's a PNG"), and having both in the message is the difference between
// "not a replay" and "you handed me a screenshot".
std::string describeBytes(const std::uint8_t* p, std::size_t n) {
    static const char digits[] = "0123456789ABCDEF";
    std::string hex;
    std::string chars;
    for (std::size_t i = 0; i < n; ++i) {
        // `p[i]` promotes to int here, so the bounds below are written as ints
        // too: comparing a promoted byte against an unsigned literal is a
        // signed/unsigned warning on MSVC for a comparison that can never go
        // wrong, and a warning nobody can act on is a warning everyone learns to
        // ignore.
        const int byte = p[i];
        if (i != 0) hex += ' ';
        hex += digits[(byte >> 4) & 0xF];
        hex += digits[byte & 0xF];
        chars += (byte >= 0x20 && byte < 0x7F) ? static_cast<char>(byte) : '.';
    }
    return hex + " ('" + chars + "')";
}

// A character id from an untrusted file, made safe to put in a log line or a
// toast. The id is at most 32 bytes so there is no length hazard, but there is
// nothing stopping it carrying a newline or an escape sequence, and an error
// message is exactly the place those do damage. The RAW bytes still reach
// ReplayData::characterId -- Replay.h says that field is human-facing and the
// hash is what decides anything -- so this is a presentation rule, not a parse
// rule, and it lives at the point of display where it belongs.
std::string quoteId(const std::string& id) {
    if (id.empty()) {
        return "<empty>";
    }
    std::string s = "'";
    for (const char c : id) {
        const int u = static_cast<unsigned char>(c);
        s += (u >= 0x20 && u != 0x7F) ? c : '?';
    }
    s += "'";
    return s;
}

// A PATH from an untrusted source, made safe the same way and for the same
// reason. The filename is the OTHER string a stranger chose -- a replay is a
// file somebody was sent, and its name arrives from a download, a lobby listing
// or a shared folder exactly as its character id does -- so a refusal that
// prints it raw hands whoever made the file a write straight into the reader's
// terminal. PathIsContained is not that check: it rejects `..`, absolute paths
// and drive/UNC roots, none of which is what an ANSI escape looks like.
//
// TWO DIFFERENCES FROM quoteId, both because a path is not a fixed-width field.
// quoteId argues it has "no length hazard" on the grounds that an id is at most
// 32 bytes; nothing bounds a path, so this one elides and says how long the real
// thing was, because a refusal that pushes its own reason off the screen is a
// refusal nobody reads. And the elision stops on a lead byte, so a name in
// somebody's own language does not end in a mangled glyph.
std::string quotePath(const std::string& path) {
    if (path.empty()) {
        return "<empty>";
    }

    // Long enough for every legitimate name -- the sandbox is a replay directory
    // and the format's own examples are "combo.csrp" -- and short enough that a
    // hostile one cannot bury the sentence that follows it.
    const std::size_t kMaxShown = 120;
    std::size_t       shown     = path.size() < kMaxShown ? path.size() : kMaxShown;

    // UTF-8 continuation bytes are 10xxxxxx. Backing off while the byte AT the
    // cut is one lands the cut on a lead byte, and three steps is the most a
    // sequence can ask for.
    if (shown < path.size()) {
        for (int i = 0; i < 3 && shown > 0 &&
                        (static_cast<unsigned char>(path[shown]) & 0xC0) == 0x80;
             ++i) {
            --shown;
        }
    }

    std::string s = "'";
    for (std::size_t i = 0; i < shown; ++i) {
        // quoteId's rule, byte for byte: control characters and DEL become '?',
        // and everything at or above 0x80 survives untouched, because a UTF-8
        // filename is a name and not an attack.
        const int u = static_cast<unsigned char>(path[i]);
        s += (u >= 0x20 && u != 0x7F) ? path[i] : '?';
    }
    s += "'";
    if (shown < path.size()) {
        s += " (elided, " + num(path.size()) + " bytes in all)";
    }
    return s;
}

// --- The reader's error channel ---------------------------------------------
//
// Every refusal goes through here so that the source name is prefixed exactly
// once and the refusal code cannot be forgotten -- CharacterData.cpp's Ctx does
// the same job for the same reason. `refuse` returns false so that every call
// site is `return ctx.refuse(...)`, which makes the "never hand back a half-built
// object" rule one line rather than two.
struct Reader {
    std::string   source;
    ReplayReport* report = nullptr;

    bool refuse(ReplayRefusal refusal, const std::string& message) const {
        report->refusal = refusal;
        report->error   = source + ": " + message;
        return false;
    }

    void warn(const std::string& message) const {
        report->warnings.push_back(source + ": " + message);
    }
};

// Decode one 32-byte NUL-padded character id.
//
// Returns false when there is data AFTER the terminator. That is not
// pedantry: the padding is the one place in this format where bytes exist that
// nothing reads, so it is the one place a payload can hide inside a file that
// otherwise validates byte for byte. It is also, much more mundanely, the
// signature of a writer that reused a buffer without clearing it, which is worth
// knowing about.
bool readCharacterId(const std::uint8_t* p, std::string& out) {
    std::size_t len = 0;
    while (len < kReplayCharacterIdBytes && p[len] != 0) {
        ++len;
    }
    for (std::size_t i = len; i < kReplayCharacterIdBytes; ++i) {
        if (p[i] != 0) {
            return false;
        }
    }
    out.assign(reinterpret_cast<const char*>(p), len);
    return true;
}

bool inputPairsEqual(const cse::kernel::InputPair& a, const cse::kernel::InputPair& b) {
    return a.p[0].bits == b.p[0].bits && a.p[1].bits == b.p[1].bits;
}

} // namespace

// --- The content hash -------------------------------------------------------

std::uint32_t HashMatchData(const cse::kernel::MatchData& data) {
    // Byte-for-byte the construction of cse::kernel::Checksum -- FNV-1a, the
    // same offset basis, the same prime -- because Replay.h argues that the
    // replay's question and the connect handshake's question are the SAME
    // question and two hashes for one question would eventually disagree.
    //
    // Reading the object representation through unsigned char is the one
    // aliasing route the standard blesses, and it is sound here only because
    // MatchData has no padding holes with indeterminate values: MoveDef::pad_
    // and CancelEdge::pad_ exist for exactly this, MatchBuilder.cpp writes them
    // explicitly, and the static_asserts in Replay.h are what keep the property
    // from rotting.
    const auto*   bytes = reinterpret_cast<const unsigned char*>(&data);
    std::uint32_t h     = 2166136261u;
    for (std::size_t i = 0; i < sizeof(cse::kernel::MatchData); ++i) {
        h ^= bytes[i];
        h *= 16777619u;
    }
    return h;
}

const char* ReplayRefusalName(ReplayRefusal refusal) {
    switch (refusal) {
        case ReplayRefusal::None:             return "None";
        case ReplayRefusal::PathRefused:      return "PathRefused";
        case ReplayRefusal::Unreadable:       return "Unreadable";
        case ReplayRefusal::NotAReplay:       return "NotAReplay";
        case ReplayRefusal::Version:          return "Version";
        case ReplayRefusal::StateLayout:      return "StateLayout";
        case ReplayRefusal::CharacterChanged: return "CharacterChanged";
        case ReplayRefusal::Malformed:        return "Malformed";
        case ReplayRefusal::TooLarge:         return "TooLarge";
    }
    // No `default` label, deliberately: -Wswitch (and MSVC's C4062) then names
    // this function the moment a refusal is added, which is the reminder that a
    // new refusal needs a name here and a message of its own at the site that
    // raises it. The fallback below is for a value cast in from outside the
    // enumeration, which is the only way execution reaches it.
    return "Unknown";
}

// --- Reading ----------------------------------------------------------------

bool DecodeReplay(std::string_view         sourceName,
                  const std::uint8_t*      bytes,
                  std::size_t              byteCount,
                  const ReplayReadOptions& options,
                  ReplayData&              out,
                  ReplayReport&            report) {
    out    = ReplayData{};
    report = ReplayReport{};

    Reader ctx;
    ctx.source = std::string(sourceName);
    ctx.report = &report;

    if (bytes == nullptr && byteCount != 0) {
        // A caller bug rather than a file property, and it gets the caller's
        // vocabulary: there were no bytes to read.
        return ctx.refuse(ReplayRefusal::Unreadable,
                          "buffer: null, but " + num(byteCount) + " bytes were declared");
    }

    // The size cap applies to bytes in memory exactly as it applies to bytes on
    // disk. Two entry points with two different notions of "too big" would mean
    // the hand-built hostile bytes a test refuses are not the bytes the file path
    // refuses, and then the test proves nothing about the file path.
    if (byteCount > options.maxFileBytes) {
        return ctx.refuse(ReplayRefusal::Unreadable,
                          "file: " + num(byteCount) + " bytes exceeds the " +
                              num(options.maxFileBytes) + "-byte cap on a replay");
    }

    // --- 1. Is this a replay at all? ----------------------------------------
    //
    // Before anything else, including the length check for the header, because
    // "you picked the wrong file" is both the most common cause and the only one
    // whose answer is not "your file is broken". A file too short to even carry
    // the magic gets the same answer for the same reason: whatever it is, it is
    // not this.
    if (byteCount < sizeof(kReplayMagic)) {
        return ctx.refuse(ReplayRefusal::NotAReplay,
                          "magic at offset 0: the file is " + num(byteCount) +
                              " bytes, too short to carry the 4-byte 'CSRP' magic. This is "
                              "not a replay file.");
    }
    for (std::size_t i = 0; i < sizeof(kReplayMagic); ++i) {
        if (bytes[kOffMagic + i] != kReplayMagic[i]) {
            return ctx.refuse(
                ReplayRefusal::NotAReplay,
                "magic at offset 0: expected " +
                    describeBytes(kReplayMagic, sizeof(kReplayMagic)) + ", found " +
                    describeBytes(bytes + kOffMagic, sizeof(kReplayMagic)) +
                    ". This is not a replay file -- the usual cause is the wrong file "
                    "being picked, not a damaged one.");
        }
    }

    if (byteCount < kReplayHeaderBytes) {
        return ctx.refuse(ReplayRefusal::Malformed,
                          "header: the file is " + num(byteCount) + " bytes and the header "
                          "alone is " + num(kReplayHeaderBytes) +
                          ". It carries the replay magic, so this is a replay that was "
                          "truncated -- re-download it.");
    }

    // --- 2. Can this build read it at all? ----------------------------------
    const std::uint16_t version = readU16(bytes + kOffVersion);
    if (version != kReplayVersion) {
        return ctx.refuse(ReplayRefusal::Version,
                          "version at offset 4: the file says " + num(version) +
                              " and this build reads " + num(kReplayVersion) +
                              ". There is no compatibility shim and there will not be one "
                              "(Replay.h says why); use the build that wrote it, or "
                              "re-record.");
    }

    // --- 3. Are its checksums even about the same shape of object? ----------
    const std::uint16_t stateBytes = readU16(bytes + kOffStateBytes);
    if (stateBytes != static_cast<std::uint16_t>(sizeof(cse::kernel::GameState))) {
        return ctx.refuse(
            ReplayRefusal::StateLayout,
            "stateBytes at offset 6: recorded by a build whose GameState was " +
                num(stateBytes) + " bytes; this build's is " +
                num(sizeof(cse::kernel::GameState)) +
                ". THE STATE LAYOUT CHANGED, so every checksum in the file hashes a "
                "differently shaped object and comparing them would report a divergence "
                "that means nothing. The file is not damaged: the engine changed.");
    }

    // --- 4. Is the structure the file describes the structure it has? -------
    const std::uint32_t matchDataHash    = readU32(bytes + kOffMatchDataHash);
    const std::uint32_t seed             = readU32(bytes + kOffSeed);
    const std::int32_t  startPosX0       = readI32(bytes + kOffStartPosX0);
    const std::int32_t  startPosX1       = readI32(bytes + kOffStartPosX1);
    const std::uint32_t tickCount        = readU32(bytes + kOffTickCount);
    const std::uint32_t runCount         = readU32(bytes + kOffRunCount);
    const std::uint32_t checkpointCount  = readU32(bytes + kOffCheckpointCount);
    const std::uint32_t interval         = readU32(bytes + kOffInterval);

    // None of the four counts is trusted yet. Each is checked for the "zero
    // where one is required" case first, because zero is the value that makes a
    // decoder loop do nothing (or, for a run length, loop forever) and it costs
    // one comparison to rule out.
    if (tickCount == 0u) {
        return ctx.refuse(ReplayRefusal::Malformed,
                          "tickCount at offset 24: 0. A replay of no ticks is not a "
                          "replay; the format requires at least one.");
    }
    if (runCount == 0u) {
        return ctx.refuse(ReplayRefusal::Malformed,
                          "runCount at offset 28: 0, but tickCount says " + num(tickCount) +
                              " ticks. There is no way to encode a tick with no runs.");
    }
    if (checkpointCount == 0u) {
        return ctx.refuse(ReplayRefusal::Malformed,
                          "checkpointCount at offset 32: 0. Every replay carries at least "
                          "the end-of-replay checkpoint, which is what makes it verifiable "
                          "at all.");
    }
    if (interval == 0u) {
        return ctx.refuse(ReplayRefusal::Malformed,
                          "checkpointInterval at offset 36: 0. The field records how "
                          "precisely a divergence can be located and zero ticks is not an "
                          "interval.");
    }

    // THE CAP, APPLIED BEFORE ANYTHING IS SIZED. Two bounds, and the tighter one
    // wins: the caller's policy, and the format's own. InputSource.h calls
    // kMaxMatchTicks "a hard cap rather than a suggestion" and names "a decoded
    // replay" as one of the three containers it bounds, so a caller who raises
    // maxTicks does not get to raise that -- otherwise an 8 MiB file full of
    // 65535-length runs could name four billion ticks and ask for 17 GB.
    const std::uint32_t callerCap = options.maxTicks;
    const std::uint32_t tickCap   = (callerCap < kMaxMatchTicks) ? callerCap : kMaxMatchTicks;
    if (tickCount > tickCap) {
        const bool formatBound = (tickCap == kMaxMatchTicks) && (callerCap >= kMaxMatchTicks);
        return ctx.refuse(
            ReplayRefusal::TooLarge,
            "tickCount at offset 24: " + num(tickCount) + " ticks exceeds the " +
                num(tickCap) + "-tick limit " +
                (formatBound ? "this module places on any per-tick container "
                               "(kMaxMatchTicks, one hour at 60 Hz)"
                             : "the caller set in ReplayReadOptions::maxTicks") +
                ". The file may be perfectly well formed; nothing has been allocated.");
    }

    // THE ONE COMPARISON THAT MAKES EVERY OFFSET BELOW SAFE.
    //
    // 64-bit arithmetic, because the two counts are attacker-chosen uint32s and
    // 6 * 0xFFFFFFFF overflows a uint32 into a small, plausible-looking number --
    // which is precisely how a "the size checks out" branch gets taken by a file
    // that is nowhere near the size it claims.
    const std::uint64_t expectedBytes = static_cast<std::uint64_t>(kReplayHeaderBytes) +
                                        static_cast<std::uint64_t>(runCount) * kReplayRunBytes +
                                        static_cast<std::uint64_t>(checkpointCount) *
                                            kReplayCheckpointBytes;
    if (expectedBytes != static_cast<std::uint64_t>(byteCount)) {
        const bool  shortFile = expectedBytes > static_cast<std::uint64_t>(byteCount);
        std::string why       = shortFile
                                    ? "The file is TRUNCATED -- re-download it"
                                    : "The file has TRAILING BYTES after the last checkpoint. "
                                      "Extra bytes are refused rather than ignored: a "
                                      "tolerant reader is how a payload rides along inside a "
                                      "file that otherwise validates";
        return ctx.refuse(ReplayRefusal::Malformed,
                          "size: the header declares runCount " + num(runCount) +
                              " and checkpointCount " + num(checkpointCount) +
                              ", which requires exactly " + num(static_cast<std::int64_t>(expectedBytes)) +
                              " bytes (" + num(kReplayHeaderBytes) + " + " +
                              num(kReplayRunBytes) + "*" + num(runCount) + " + " +
                              num(kReplayCheckpointBytes) + "*" + num(checkpointCount) +
                              "); the file is " + num(byteCount) + " bytes. " + why + ".");
    }

    // From here on runCount and checkpointCount are no longer numbers the file
    // chose -- they are numbers the file's own length corroborates -- so the
    // section bases below cannot point outside the buffer. Restated as an
    // executable check anyway: it costs two comparisons, and what it would catch
    // is an out-of-bounds read.
    const std::size_t runBase = kReplayHeaderBytes;
    const std::size_t runEnd  = runBase + static_cast<std::size_t>(runCount) * kReplayRunBytes;
    const std::size_t cpBase  = runEnd;
    const std::size_t cpEnd   = cpBase + static_cast<std::size_t>(checkpointCount) *
                                            kReplayCheckpointBytes;
    if (runEnd > byteCount || cpEnd > byteCount || runEnd < runBase || cpEnd < cpBase) {
        return ctx.refuse(ReplayRefusal::Malformed,
                          "size: the section bounds do not fit the file even though the "
                          "size equation held. Refusing rather than reading.");
    }

    // BOTH SECTION COUNTS ARE BOUNDED BY tickCount, and both bounds are
    // structural rather than defensive: every run is at least 1 tick long, and
    // every checkpoint tick is distinct and below tickCount, so neither can
    // outnumber the ticks. Checked HERE, before either vector is sized, because
    // tickCount has already been capped and these two have not -- without this,
    // an 8 MiB file could name a million checkpoints and size an 8 MB vector out
    // of a number nothing had corroborated except the file's own length.
    if (runCount > tickCount) {
        return ctx.refuse(ReplayRefusal::Malformed,
                          "runCount at offset 28: " + num(runCount) + " runs for " +
                              num(tickCount) +
                              " ticks. Every run covers at least one tick, so there cannot "
                              "be more runs than ticks.");
    }
    if (checkpointCount > tickCount) {
        return ctx.refuse(ReplayRefusal::Malformed,
                          "checkpointCount at offset 32: " + num(checkpointCount) +
                              " checkpoints for " + num(tickCount) +
                              " ticks. Checkpoint ticks are strictly increasing and all "
                              "below tickCount, so there cannot be more of them than there "
                              "are ticks.");
    }

    // The start position is the ONE header field that is fed straight into the
    // simulation, so it is bounded here rather than left for FightSession to
    // reject later. kMaxWorldCoord is the bound that makes PlaceBox total for any
    // state, and it is the same bound ValidateSetup applies -- checked here so
    // that a ReplayData in hand is a replay FightSession::Begin will accept,
    // rather than one that loads cleanly and then fails at match start with a
    // message about something else. The coupling to ValidateSetup is real and is
    // recorded rather than hidden: if that bound moves, this moves with it.
    for (int slot = 0; slot < 2; ++slot) {
        const std::int32_t  v   = (slot == 0) ? startPosX0 : startPosX1;
        const std::size_t   off = (slot == 0) ? kOffStartPosX0 : kOffStartPosX1;
        const std::int64_t  mag = (v < 0) ? -static_cast<std::int64_t>(v) : static_cast<std::int64_t>(v);
        if (mag > static_cast<std::int64_t>(cse::kernel::kMaxWorldCoord)) {
            return ctx.refuse(ReplayRefusal::Malformed,
                              "startPosX" + num(slot) + " at offset " + num(off) + ": " +
                                  num(v) + " sub-units is outside +/-" +
                                  num(cse::kernel::kMaxWorldCoord) +
                                  ", the bound that keeps box placement total. No session "
                                  "would accept this opening.");
        }
    }

    std::string characterId[2];
    if (!readCharacterId(bytes + kOffCharacterId0, characterId[0])) {
        return ctx.refuse(ReplayRefusal::Malformed,
                          "characterId0 at offset 40: the 32-byte field carries data after "
                          "its NUL terminator. The padding is the one place in this format "
                          "nothing reads, so it is the one place something can hide.");
    }
    if (!readCharacterId(bytes + kOffCharacterId1, characterId[1])) {
        return ctx.refuse(ReplayRefusal::Malformed,
                          "characterId1 at offset 72: the 32-byte field carries data after "
                          "its NUL terminator.");
    }
    // The ids are NOT validated as UTF-8 and NOT required to be non-empty.
    // Neither is load-bearing -- the hash is what decides whether this replay is
    // about the same game, and Replay.h is explicit that a reader must never
    // accept on a matching id nor refuse on a mismatched one. An empty id is
    // still worth a note, because it is the field the CharacterChanged message
    // is supposed to name and an empty one makes that message useless.
    for (int slot = 0; slot < 2; ++slot) {
        if (characterId[slot].empty()) {
            ctx.warn("characterId" + num(slot) +
                     ": empty. A character-change refusal will not be able to name this "
                     "side.");
        }
    }

    // --- 5. The runs: validated in full BEFORE a single tick is allocated ----
    //
    // Two passes on purpose. The first proves the run table describes exactly
    // tickCount ticks; only then is the flat vector sized, and the second pass
    // is provably in bounds at both ends. Doing it in one pass would mean
    // allocating from a count the file chose and discovering it was a lie
    // afterwards, which is the whole class of bug this reader exists to avoid.
    std::uint64_t declaredTicks = 0;
    for (std::uint32_t i = 0; i < runCount; ++i) {
        const std::size_t   off = runBase + static_cast<std::size_t>(i) * kReplayRunBytes;
        const std::uint16_t len = readU16(bytes + off + 4);
        if (len == 0u) {
            return ctx.refuse(ReplayRefusal::Malformed,
                              "run " + num(i) + " length at offset " + num(off + 4) +
                                  ": 0. A zero-length run consumes no ticks, so a file full "
                                  "of them is a decoder that never terminates; the format "
                                  "requires 1..65535.");
        }
        declaredTicks += len;
        if (declaredTicks > static_cast<std::uint64_t>(tickCount)) {
            // Abandoned the moment it exceeds, rather than summed to the end:
            // the sum is bounded by runCount * 65535, which is 9.2e10 for an
            // 8 MiB file, and there is nothing to learn from computing it.
            return ctx.refuse(ReplayRefusal::Malformed,
                              "run " + num(i) + " length at offset " + num(off + 4) +
                                  ": the run lengths already total " +
                                  num(static_cast<std::int64_t>(declaredTicks)) +
                                  " ticks, past the " + num(tickCount) +
                                  " the header declared. The run table describes a longer "
                                  "match than the header does.");
        }
    }
    if (declaredTicks != static_cast<std::uint64_t>(tickCount)) {
        return ctx.refuse(ReplayRefusal::Malformed,
                          "runs: the " + num(runCount) + " runs total " +
                              num(static_cast<std::int64_t>(declaredTicks)) +
                              " ticks but the header declares " + num(tickCount) +
                              ". The input log stops before the match does, and the missing "
                              "ticks are not neutral -- they are unknown.");
    }

    // The one allocation this function makes from a file-supplied number, and it
    // is made only now: tickCount has been bounded by tickCap AND corroborated by
    // the run table's own arithmetic. At the default cap that is 216000
    // InputPair, 864 KB.
    //
    // There is no try/catch around it, deliberately. The threat this file exists
    // to stop is an allocation the FILE chose, and that is what the caps above
    // remove -- what is left is an ordinary 864 KB request, no more likely to fail
    // than the buffer the file was read into, and a build with -fno-exceptions
    // (which CharacterData.cpp already keeps itself usable in) cannot contain a
    // `try` at all. Catching std::bad_alloc here would trade a portability
    // guarantee for the pretence of handling out-of-memory.
    ReplayData decoded;
    decoded.inputs.resize(static_cast<std::size_t>(tickCount));

    std::size_t cursor = 0;
    for (std::uint32_t i = 0; i < runCount; ++i) {
        const std::size_t off = runBase + static_cast<std::size_t>(i) * kReplayRunBytes;
        cse::kernel::InputPair pair{};
        pair.p[0].bits          = readU16(bytes + off + 0);
        pair.p[1].bits          = readU16(bytes + off + 2);
        const std::uint16_t len = readU16(bytes + off + 4);
        for (std::uint16_t k = 0; k < len; ++k) {
            // The sum check above proves this cannot run off the end. It is
            // still written as a bounded loop rather than a memset-style fill so
            // that the bound is visible at the point of the write.
            decoded.inputs[cursor] = pair;
            ++cursor;
        }
    }

    // --- 6. The checkpoints, validated structurally on their own ------------
    //
    // Every rule here can be decided without simulating anything, which is the
    // point of storing the tick explicitly rather than implying it from the
    // entry's index: a hostile file's checkpoints are rejected before any of them
    // is ever compared against a live checksum.
    decoded.checkpoints.resize(static_cast<std::size_t>(checkpointCount));
    for (std::uint32_t j = 0; j < checkpointCount; ++j) {
        const std::size_t off = cpBase + static_cast<std::size_t>(j) * kReplayCheckpointBytes;
        ReplayCheckpoint  cp;
        cp.tick     = readU32(bytes + off + 0);
        cp.checksum = readU32(bytes + off + 4);

        if (cp.tick >= tickCount) {
            return ctx.refuse(ReplayRefusal::Malformed,
                              "checkpoint " + num(j) + " tick at offset " + num(off) + ": " +
                                  num(cp.tick) + " is not a tick this replay contains (0.." +
                                  num(tickCount - 1u) + ").");
        }
        if (j > 0 && cp.tick <= decoded.checkpoints[j - 1].tick) {
            return ctx.refuse(ReplayRefusal::Malformed,
                              "checkpoint " + num(j) + " tick at offset " + num(off) + ": " +
                                  num(cp.tick) + " does not come after checkpoint " +
                                  num(j - 1) + "'s tick " + num(decoded.checkpoints[j - 1].tick) +
                                  ". Checkpoints must be strictly increasing so a verifier can "
                                  "walk them with one cursor and never look backwards.");
        }
        decoded.checkpoints[static_cast<std::size_t>(j)] = cp;
    }
    // The final checkpoint is the end-to-end check every replay carries, whatever
    // its interval. Without it a file could validate completely and still say
    // nothing at all about its own last ten minutes.
    if (decoded.checkpoints.back().tick != tickCount - 1u) {
        return ctx.refuse(ReplayRefusal::Malformed,
                          "checkpoint " + num(checkpointCount - 1u) + " tick: " +
                              num(decoded.checkpoints.back().tick) +
                              ", but the last tick of the replay is " + num(tickCount - 1u) +
                              ". Every replay ends with a checkpoint on its final tick; "
                              "without one, playback verifies a prefix and calls it the "
                              "whole file.");
    }
    // The interval is NOT enforced against the checkpoint ticks, deliberately.
    // Replay.h stores it so a reader can explain how precisely a divergence can
    // be located, and it explicitly allows the final checkpoint to sit off the
    // interval. A reader that additionally demanded the grid would refuse a
    // legitimately re-intervalled recording and would be asserting a rule the
    // format does not have.

    // --- 7. Is this replay about the game that is loaded right now? ---------
    //
    // LAST, and after every structural check, even though it is the cheapest
    // question here. "This file is well formed and describes a different version
    // of the character" is only a true sentence once the structure has actually
    // been proven; saying it about a half-downloaded file would send somebody
    // hunting through git for a character edit that never happened.
    if (options.expectedMatchDataHash == 0u) {
        ctx.warn("matchDataHash: no expected hash was supplied, so the "
                 "character-change check did not run. This replay may have been recorded "
                 "against different frame data than is loaded now. (A tool that dumps "
                 "replays legitimately has no MatchData; a play path never does.)");
    } else if (options.expectedMatchDataHash != matchDataHash) {
        return ctx.refuse(
            ReplayRefusal::CharacterChanged,
            "matchDataHash at offset 8: THE CHARACTER FILE WAS EDITED SINCE this replay was "
            "recorded. It records " +
                quoteId(characterId[0]) + " vs " + quoteId(characterId[1]) +
                " whose frame data hashed to " + hex32(matchDataHash) +
                "; what is loaded now hashes to " + hex32(options.expectedMatchDataHash) +
                ". The file is not damaged -- the same inputs against different frame data "
                "are a different fight, and playing this back would show a combo that never "
                "happened or drop one that did. Restore the character file this was recorded "
                "against, or re-record.");
    }

    decoded.version            = version;
    decoded.matchDataHash      = matchDataHash;
    decoded.characterId[0]     = characterId[0];
    decoded.characterId[1]     = characterId[1];
    decoded.start.seed         = seed;
    decoded.start.startPosX[0] = startPosX0;
    decoded.start.startPosX[1] = startPosX1;
    decoded.checkpointInterval = interval;

    // Assigned in one move at the very end. Every `return ctx.refuse(...)` above
    // therefore leaves `out` exactly as it was zeroed on entry -- CharacterData.cpp's
    // "never hand back a half-built character", which matters more here because a
    // caller that ignores the bool gets an empty replay rather than a partly
    // decoded one it will index into.
    out = std::move(decoded);
    return true;
}

bool ReadReplayFile(const std::string&       baseDir,
                    const std::string&       relPath,
                    const ReplayReadOptions& options,
                    ReplayData&              out,
                    ReplayReport&            report) {
    out    = ReplayData{};
    report = ReplayReport{};

    // QUOTED ONCE, HERE, and every refusal below quotes this and never `relPath`
    // -- including the one handed to DecodeReplay as its source name, which
    // prefixes every parse refusal in the file. One conversion at the entry point
    // is the same shape as Reader::refuse one function up: the sanitising cannot
    // be forgotten at a site that is added later, because there is no unsanitised
    // string in scope worth reaching for.
    const std::string shown = quotePath(relPath);

    // Containment BEFORE the file is opened, every time. docs/MAINTENANCE.md:
    // "anything from scene content goes through PathIsContained before the file
    // is opened. Absolute paths and `..` are refused." A replay path is a harder
    // case than a scene path, not an easier one -- it is the name of a file
    // somebody was sent.
    std::filesystem::path full;
    if (!MyCoreEngine::PathIsContained(baseDir, relPath, full)) {
        report.refusal = ReplayRefusal::PathRefused;
        report.error   = shown +
                       ": path: refused, because it is absolute, carries a drive/UNC root, "
                       "or contains a `..` component that would escape the replay directory. "
                       "Refused lexically, before any filesystem access.";
        return false;
    }

    // The size is checked BEFORE the read, so a hostile 4 GB "replay" costs one
    // stat call and no memory at all.
    std::error_code      ec;
    const std::uintmax_t size = std::filesystem::file_size(full, ec);
    if (ec) {
        report.refusal = ReplayRefusal::Unreadable;
        report.error   = shown + ": file: cannot be opened (" + ec.message() + ")";
        return false;
    }
    if (size > static_cast<std::uintmax_t>(options.maxFileBytes)) {
        report.refusal = ReplayRefusal::Unreadable;
        report.error   = shown + ": file: " + num(static_cast<std::int64_t>(size)) +
                       " bytes exceeds the " + num(options.maxFileBytes) +
                       "-byte cap on a replay. The format's own bound is 104 + 6*runCount + "
                       "8*checkpointCount with runCount bounded by tickCount, so a "
                       "legitimate hour-long replay is far below this.";
        return false;
    }

    std::ifstream in(full, std::ios::binary);
    if (!in) {
        report.refusal = ReplayRefusal::Unreadable;
        report.error   = shown + ": file: cannot be opened for reading";
        return false;
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (size != 0) {
        in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
        if (in.bad()) {
            report.refusal = ReplayRefusal::Unreadable;
            report.error   = shown + ": file: read failed";
            return false;
        }
        // TIME OF CHECK IS NOT TIME OF USE. The file can change between the stat
        // and the read -- ordinarily because it is still being downloaded. Only
        // the bytes actually delivered are handed on, so a file that SHRANK is
        // validated as the short file it is (and refused as truncated), and a
        // file that GREW is refused here rather than being silently validated as
        // its own prefix.
        const std::streamsize got = in.gcount();
        const std::size_t     have = (got > 0) ? static_cast<std::size_t>(got) : 0u;
        if (have != bytes.size()) {
            bytes.resize(have);
        } else if (in.peek() != std::ifstream::traits_type::eof()) {
            report.refusal = ReplayRefusal::Unreadable;
            report.error   = shown +
                           ": file: grew while it was being read. Nothing was parsed; if it "
                           "is still downloading, wait for it to finish.";
            return false;
        }
    }

    // `shown` and not `relPath`: DecodeReplay's source name is documented as a
    // label for error messages, and it prefixes EVERY parse refusal -- which is
    // most of the refusals a hostile file can provoke, and therefore most of the
    // chances a filename would have had to reach a terminal unsanitised. The
    // quoting is not done inside DecodeReplay because its callers include tests
    // that pass a fixed label of their own, and a label that names bytes rather
    // than a file has nothing to escape.
    return DecodeReplay(shown, bytes.data(), bytes.size(), options, out, report);
}

bool ReplayMatchesData(const ReplayData&             replay,
                       const cse::kernel::MatchData& data,
                       std::string&                  error) {
    const std::uint32_t live = HashMatchData(data);
    if (live == replay.matchDataHash) {
        error.clear();
        return true;
    }
    // The same sentence the reader raises, written once and in one place so the
    // play path and the load path cannot end up phrasing the same fact two
    // different ways to the same playtester.
    error = "the character file was EDITED SINCE this replay was recorded. It records " +
            quoteId(replay.characterId[0]) + " vs " + quoteId(replay.characterId[1]) +
            " whose frame data hashed to " + hex32(replay.matchDataHash) +
            "; the data loaded now hashes to " + hex32(live) +
            ". The replay is not damaged -- it is about a different fight. Restore the "
            "character file it was recorded against, or re-record.";
    return false;
}

// --- Recording --------------------------------------------------------------

ReplayRecorder::ReplayRecorder(const ReplayRecorderOptions& options) : options_(options) {}

bool ReplayRecorder::Begin(const MatchStart& start,
                           std::uint32_t     matchDataHash,
                           std::string_view  characterId0,
                           std::string_view  characterId1,
                           std::string&      error) {
    error.clear();
    inputs_.clear();
    checkpoints_.clear();
    recording_ = false;
    error_.clear();

    // The options are validated HERE rather than in the constructor because the
    // constructor has no way to report and clamping silently would produce a file
    // whose checkpointInterval field lies about how the file was made -- a reader
    // would then explain a divergence window using an interval that was never
    // used.
    if (options_.checkpointInterval == 0u) {
        error  = "replay recorder: checkpointInterval is 0; it must be at least 1 tick.";
        error_ = error;
        return false;
    }
    if (options_.maxTicks == 0u || options_.maxTicks > kMaxMatchTicks) {
        error = "replay recorder: maxTicks is " + num(options_.maxTicks) +
                "; it must be between 1 and " + num(kMaxMatchTicks) +
                " (kMaxMatchTicks, one hour at 60 Hz), which is the hard cap this module "
                "places on every per-tick container.";
        error_ = error;
        return false;
    }

    // REFUSED, NOT TRUNCATED. A truncated id produces a file whose
    // CharacterChanged message names the wrong character, and the fixed-width
    // field exists so that no count read from an untrusted file ever sizes an
    // allocation -- paying for that with a silent rename gives the property back
    // at the only moment it was ever going to matter.
    const std::string_view ids[2] = { characterId0, characterId1 };
    for (int slot = 0; slot < 2; ++slot) {
        if (ids[slot].size() > kReplayCharacterIdBytes) {
            error = "replay recorder: characterId" + num(slot) + " is " +
                    num(ids[slot].size()) + " bytes and the field is " +
                    num(kReplayCharacterIdBytes) +
                    ". Refused rather than truncated: a shortened id would make every "
                    "later error message name the wrong character.";
            error_ = error;
            return false;
        }
    }

    // The recorder must not be able to write a file its own reader refuses, so
    // the one header field the reader bounds is bounded here too, at the moment
    // the caller can still do something about it.
    for (int slot = 0; slot < 2; ++slot) {
        const std::int32_t v   = start.startPosX[slot];
        const std::int64_t mag = (v < 0) ? -static_cast<std::int64_t>(v)
                                         : static_cast<std::int64_t>(v);
        if (mag > static_cast<std::int64_t>(cse::kernel::kMaxWorldCoord)) {
            error = "replay recorder: startPosX[" + num(slot) + "] is " + num(v) +
                    " sub-units, outside +/-" + num(cse::kernel::kMaxWorldCoord) +
                    ". A file recorded with this opening is one the reader would refuse.";
            error_ = error;
            return false;
        }
    }

    start_          = start;
    matchDataHash_  = matchDataHash;
    characterId_[0] = std::string(characterId0);
    characterId_[1] = std::string(characterId1);
    recording_      = true;
    return true;
}

void ReplayRecorder::OnTick(const TickView& view) {
    // OnTick is the 60 Hz path and cannot report, so everything it can refuse it
    // refuses by latching error_ and going inert. The FIRST error is the one
    // kept: a recorder that overwrote its error with each later tick would
    // report the consequence and lose the cause.
    if (!error_.empty()) {
        return;
    }
    if (!recording_) {
        error_ = "replay recorder: a tick arrived before a successful Begin(). The initial "
                 "conditions are unknown, so no valid file can be produced, and every later "
                 "tick is refused rather than writing a header of zeroes -- an unplayable "
                 "file that validates is worse than no file.";
        return;
    }
    if (view.state == nullptr) {
        error_ = "replay recorder: a TickView arrived at tick " + num(view.tick) +
                 " with a null state. There is nothing to checksum, so the recording stops "
                 "here rather than writing a checkpoint of zero.";
        recording_ = false;
        return;
    }

    // A GAP IS A HARD ERROR, and this is the check that catches it. Zero-filling
    // would be the tempting repair and it is the wrong one: neutral input is a
    // legal thing to press, so a zero-filled gap is indistinguishable from a
    // player who let go, and the file would be a confident recording of a fight
    // that did not happen.
    if (static_cast<std::size_t>(view.tick) > inputs_.size()) {
        error_ = "replay recorder: tick " + num(view.tick) + " arrived with only " +
                 num(inputs_.size()) + " ticks recorded, so ticks " + num(inputs_.size()) +
                 ".." + num(view.tick - 1u) +
                 " were never delivered. Refusing rather than zero-filling: neutral input is "
                 "a legal thing to press, so the filled ticks would be indistinguishable "
                 "from a player who let go.";
        recording_ = false;
        return;
    }

    if (static_cast<std::size_t>(view.tick) == inputs_.size()) {
        // The cap. Reaching it is reported as an ERROR rather than as a quiet
        // stop, and that is the interesting decision here: a quiet stop leaves a
        // recorder that keeps returning a writable file of the FIRST hour, so a
        // playtester who lands a combo in hour three saves a replay, posts it,
        // and it shows something else entirely. No file is a far better outcome
        // than a confident file of the wrong fight.
        if (inputs_.size() >= static_cast<std::size_t>(options_.maxTicks)) {
            error_ = "replay recorder: the recording reached its " + num(options_.maxTicks) +
                     "-tick cap. Recording stopped and this recorder is now inert -- the "
                     "ticks it holds are the FIRST " + num(options_.maxTicks) +
                     ", not the most recent ones, so writing them would produce a replay of "
                     "a different part of the session than whoever pressed save meant.";
            recording_ = false;
            return;
        }
        inputs_.push_back(view.inputs);
    } else if (!view.resimulated) {
        // A FRESH TICK BELOW THE LENGTH IS A RESTARTED SESSION, NOT A ROLLBACK,
        // and it is the third hard error rather than the third silent overwrite.
        //
        // Replay.h keys the overwrite on the FLAG -- "on a TickView with
        // `resimulated` set, the entry at view.tick is REPLACED" -- and keying it
        // on the index alone, as this branch used to, quietly accepts a case the
        // header never described. FightSession::Begin resets the tick index and
        // the high-water mark but KEEPS its observers, by design, so a host that
        // restarts a round and forgets to re-Begin its recorder delivers tick 0
        // with resimulated clear while inputs_ still holds the previous round.
        //
        // Overwriting there throws the recording away one tick at a time while
        // start_, matchDataHash_ and both character ids still describe the
        // PREVIOUS match -- so the file records its opening (startPosX above all)
        // from a round that was abandoned and its inputs from the round that
        // replaced it. That file VALIDATES: the reader has no way to know the
        // header and the input log came from different matches, and the divergence
        // it eventually produces is blamed on the engine. It is the same sin as a
        // zero-filled gap, arriving by a third route, and it is worse than both
        // because it survives being shared.
        //
        // The recorder cannot repair this itself: the initial conditions of the
        // new match are exactly what it does not have. So it says which two
        // numbers disagree and names the call that was missed.
        error_ = "replay recorder: tick " + num(view.tick) + " arrived below the " +
                 num(inputs_.size()) +
                 " ticks already recorded but was NOT flagged as a re-simulation. Replay.h "
                 "keys the overwrite on TickView::resimulated, and a fresh tick below the "
                 "recording's length is what a RESTARTED session looks like -- "
                 "FightSession::Begin resets the tick index and the high-water mark but "
                 "keeps its observers, so the next tick arrives at 0 with the flag clear. "
                 "Overwriting would discard the recording so far while the header still "
                 "holds the PREVIOUS match's start positions, MatchData hash and character "
                 "ids, producing a file that records an opening nobody played and that "
                 "validates cleanly. Call ReplayRecorder::Begin() again when the session "
                 "restarts.";
        recording_ = false;
        return;
    } else {
        // RE-SIMULATION OVERWRITES. Replay.h says the entry at view.tick is
        // REPLACED, and it is -- and everything above it is dropped, which is the
        // same operation in the case the rule is about and strictly safer in the
        // case it is not.
        //
        // A rollback host restores to tick T and re-runs forward to where it was,
        // so every entry above T is about to be rewritten anyway and dropping
        // them changes nothing. A host that restores and then runs FEWER ticks --
        // an investigation, a scrub, a match that ended during the re-run --
        // would otherwise leave a tail belonging to a timeline that was
        // abandoned, and Encode would write a tickCount counting ticks nobody
        // played. That is the same sin as a zero-filled gap, arriving from the
        // other end.
        //
        // It also preserves the invariant Encode depends on: inputs_.size() - 1
        // is always the tick the last checkpoint is about.
        inputs_[static_cast<std::size_t>(view.tick)] = view.inputs;
        inputs_.resize(static_cast<std::size_t>(view.tick) + 1u);
    }

    // --- Checkpoints, and why one is written every tick and most are dropped --
    //
    // The format requires the LAST checkpoint to be at tickCount - 1, and OnTick
    // cannot know which tick is the last one: the host stops ticking when the
    // playtester stops playing and nothing announces it in advance. The
    // alternatives were an End() call (another thing to forget, and forgetting it
    // produces an unwritable recording at the exact moment somebody wanted to
    // save one), a spare member holding the last checksum until Encode needs it
    // (ReplayRecorder's private section is frozen -- three agents are
    // implementing against this header at once -- and there is no spare scalar),
    // and checkpointing every tick unconditionally (8 bytes a tick, doubling a
    // dense file, for data that is thrown away).
    //
    // So the tick that just ran always holds a PROVISIONAL final checkpoint and
    // the next tick replaces it, unless it sits on the interval grid, in which
    // case it is kept. checkpoints_ therefore always ends at the last tick
    // delivered -- exactly Encode's invariant -- for one pop_back and one
    // push_back per tick, with no allocation once the vector has grown.
    //
    // THE GRID IS `tick % interval == 0`, so tick 0 always gets one. That costs
    // eight bytes and buys a checkpoint on the very first tick, which is where a
    // change to ResetMatch or to how MatchStart::startPosX is applied shows up --
    // the cheapest regression to introduce and, without this, the one a replay
    // would not notice until a full interval later. A 160-tick demonstration at
    // the default interval therefore carries checkpoints at 0, 60, 120 and 159:
    // four of them, 142 bytes all in. (Replay.h's header note quotes 118 bytes
    // for that case, which is the arithmetic of the MINIMUM legal file -- one run
    // and one checkpoint -- rather than of a 160-tick one; no interval rule
    // produces a single checkpoint across 160 ticks at interval 60. A host that
    // wants the minimum file for a demo sets checkpointInterval above the
    // demo's length.)
    //
    // The value is Checksum(state AFTER the tick), keyed by view.tick: Replay.h
    // states that off-by-one on purpose, and an implementer who keys on
    // state->tick writes every checkpoint one tick late. This code deliberately
    // does not cross-check state->tick == view.tick + 1 -- view.tick is the
    // contract, ReplayVerifier is the instrument that catches a host which gets
    // it wrong, and a recorder that refused a hand-built TickView would be
    // asserting a rule the header gives to the session.
    while (!checkpoints_.empty() && checkpoints_.back().tick >= view.tick) {
        checkpoints_.pop_back();
    }
    if (!checkpoints_.empty() &&
        (checkpoints_.back().tick % options_.checkpointInterval) != 0u) {
        checkpoints_.pop_back();
    }
    ReplayCheckpoint cp;
    cp.tick     = view.tick;
    cp.checksum = cse::kernel::Checksum(*view.state);
    checkpoints_.push_back(cp);
}

const std::string& ReplayRecorder::Error() const { return error_; }

bool ReplayRecorder::Recording() const { return recording_; }

std::uint32_t ReplayRecorder::TickCount() const {
    return static_cast<std::uint32_t>(inputs_.size());
}

bool ReplayRecorder::Encode(std::vector<std::uint8_t>& out, std::string& error) const {
    out.clear();
    error.clear();

    if (!error_.empty()) {
        error = error_;
        return false;
    }
    if (!recording_) {
        error = "replay recorder: Begin() has not been called, so the initial conditions "
                "this file has to record are unknown.";
        return false;
    }
    if (inputs_.empty()) {
        error = "replay recorder: nothing was recorded. A replay of no ticks is not a "
                "replay and the reader refuses one.";
        return false;
    }
    if (options_.checkpointInterval == 0u) {
        error = "replay recorder: checkpointInterval is 0.";
        return false;
    }

    // Everything below this line is the recorder checking that it is not about to
    // write a file its own reader would refuse. These are internal invariants
    // rather than untrusted input, and they are still checked, because the
    // failure they prevent is a file that reaches a stranger and does not load.
    if (checkpoints_.empty()) {
        error = "replay recorder: no checkpoints were captured for " + num(inputs_.size()) +
                " ticks.";
        return false;
    }
    if (checkpoints_.back().tick != static_cast<std::uint32_t>(inputs_.size() - 1u)) {
        error = "replay recorder: the last checkpoint is at tick " +
                num(checkpoints_.back().tick) + " but the recording ends at tick " +
                num(inputs_.size() - 1u) +
                ". Every replay must carry a checkpoint on its final tick.";
        return false;
    }
    for (std::size_t j = 0; j < checkpoints_.size(); ++j) {
        if (checkpoints_[j].tick >= static_cast<std::uint32_t>(inputs_.size())) {
            error = "replay recorder: checkpoint " + num(j) + " is at tick " +
                    num(checkpoints_[j].tick) + ", past the end of a " +
                    num(inputs_.size()) + "-tick recording.";
            return false;
        }
        if (j > 0 && checkpoints_[j].tick <= checkpoints_[j - 1].tick) {
            error = "replay recorder: checkpoints " + num(j - 1) + " and " + num(j) +
                    " are at ticks " + num(checkpoints_[j - 1].tick) + " and " +
                    num(checkpoints_[j].tick) + "; they must be strictly increasing.";
            return false;
        }
    }
    for (int slot = 0; slot < 2; ++slot) {
        if (characterId_[slot].size() > kReplayCharacterIdBytes) {
            error = "replay recorder: characterId" + num(slot) + " is " +
                    num(characterId_[slot].size()) + " bytes and the field is " +
                    num(kReplayCharacterIdBytes) + ".";
            return false;
        }
    }

    // --- Run-length encode the pair stream ----------------------------------
    //
    // Runs are over the PAIR, not per player: two streams would break their runs
    // at different ticks and compress a two-human match slightly better, at the
    // cost of doubling the header bookkeeping and the validation surface and
    // making a decoder keep two cursors in step. In the case this module exists
    // for -- one attacker and a silent training dummy -- a pair-run is exactly as
    // good, because the dummy's bits never change.
    struct Run {
        cse::kernel::InputPair pair{};
        std::uint32_t          length = 0;
    };
    std::vector<Run> runs;
    for (const cse::kernel::InputPair& pair : inputs_) {
        if (!runs.empty() && runs.back().length < kMaxRunLength &&
            inputPairsEqual(runs.back().pair, pair)) {
            ++runs.back().length;
            continue;
        }
        Run run;
        run.pair   = pair;
        run.length = 1;
        runs.push_back(run);
    }

    const std::uint32_t tickCount       = static_cast<std::uint32_t>(inputs_.size());
    const std::uint32_t runCount        = static_cast<std::uint32_t>(runs.size());
    const std::uint32_t checkpointCount = static_cast<std::uint32_t>(checkpoints_.size());

    const std::size_t total = kReplayHeaderBytes +
                              static_cast<std::size_t>(runCount) * kReplayRunBytes +
                              static_cast<std::size_t>(checkpointCount) * kReplayCheckpointBytes;
    out.reserve(total);

    // Magic as BYTES, in order, not as a uint32 constant: the check at the other
    // end is then endian-independent and the file is greppable.
    for (std::size_t i = 0; i < sizeof(kReplayMagic); ++i) {
        out.push_back(static_cast<std::uint8_t>(kReplayMagic[i]));
    }
    writeU16(out, kReplayVersion);
    writeU16(out, static_cast<std::uint16_t>(sizeof(cse::kernel::GameState)));
    writeU32(out, matchDataHash_);
    writeU32(out, start_.seed);
    writeI32(out, start_.startPosX[0]);
    writeI32(out, start_.startPosX[1]);
    writeU32(out, tickCount);
    writeU32(out, runCount);
    writeU32(out, checkpointCount);
    writeU32(out, options_.checkpointInterval);
    for (int slot = 0; slot < 2; ++slot) {
        for (std::size_t i = 0; i < kReplayCharacterIdBytes; ++i) {
            const std::uint8_t byte =
                (i < characterId_[slot].size())
                    ? static_cast<std::uint8_t>(static_cast<unsigned char>(characterId_[slot][i]))
                    : 0u;
            out.push_back(byte);
        }
    }
    // The fields were appended in order rather than poked at kOff* offsets, so
    // this is the check that the order and the offset table still agree. It is
    // the cheapest possible guard against the one bug that makes every future
    // file unreadable while every existing test still passes.
    if (out.size() != kReplayHeaderBytes) {
        error = "replay recorder: internal inconsistency -- the header assembled to " +
                num(out.size()) + " bytes instead of " + num(kReplayHeaderBytes) + ".";
        out.clear();
        return false;
    }

    for (const Run& run : runs) {
        writeU16(out, run.pair.p[0].bits);
        writeU16(out, run.pair.p[1].bits);
        writeU16(out, static_cast<std::uint16_t>(run.length));
    }
    for (const ReplayCheckpoint& cp : checkpoints_) {
        writeU32(out, cp.tick);
        writeU32(out, cp.checksum);
    }

    if (out.size() != total) {
        error = "replay recorder: internal inconsistency -- assembled " + num(out.size()) +
                " bytes where the format's own equation requires " + num(total) + ".";
        out.clear();
        return false;
    }
    return true;
}

bool ReplayRecorder::Write(const std::string& baseDir,
                           const std::string& relPath,
                           std::string&       error) const {
    error.clear();

    // The path is untrusted on the WRITE side too -- a playtester types the name
    // of the file they are saving, and a name is a path. Same rule, same
    // function, checked first because a containment failure is the one refusal
    // here that is about security rather than about content.
    //
    // And it is quoted for display by the same argument, which the read path
    // spells out: the name reaches a terminal in every refusal below, an escape
    // sequence in it does its damage there, and PathIsContained has an opinion
    // about `..` and none about ESC.
    const std::string     shown = quotePath(relPath);
    std::filesystem::path full;
    if (!MyCoreEngine::PathIsContained(baseDir, relPath, full)) {
        error = shown +
                ": path: refused, because it is absolute, carries a drive/UNC root, or "
                "contains a `..` component that would escape the replay directory.";
        return false;
    }

    // Encoded in full BEFORE the file is opened, so a recorder that cannot
    // produce a valid file does not truncate an existing one on its way to
    // finding that out. "Writes nothing at all on refusal" is a property of this
    // ordering, not of a cleanup path.
    std::vector<std::uint8_t> bytes;
    if (!Encode(bytes, error)) {
        return false;
    }

    std::ofstream file(full, std::ios::binary | std::ios::trunc);
    if (!file) {
        error = shown + ": file: cannot be opened for writing";
        return false;
    }
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    file.flush();
    if (!file) {
        // A partial replay is worse than no replay: it is a file that looks like
        // a recording, gets posted, and fails to load on somebody else's machine.
        // Remove it rather than leaving it behind.
        //
        // The fully correct version writes a temporary and renames it, which is
        // atomic even against a crash. It is not done here because it would mean
        // manufacturing a SECOND path -- and every path in this file is one the
        // sandbox has to be asked about, so inventing one is not free.
        file.close();
        std::error_code ec;
        std::filesystem::remove(full, ec);
        error = shown + ": file: write failed after " + num(bytes.size()) +
                " bytes; the partial file was removed.";
        return false;
    }
    file.close();
    if (!file) {
        std::error_code ec;
        std::filesystem::remove(full, ec);
        error = shown + ": file: close failed, so the replay may not have reached the "
                          "disk; the file was removed.";
        return false;
    }
    return true;
}

// --- Playing back -----------------------------------------------------------

ReplayInputSource::ReplayInputSource(const ReplayData& replay, int player)
    : replay_(&replay), player_(player) {}

InputSample ReplayInputSource::At(std::uint32_t tick) const {
    // Zeroed by its own default member initialisers: `authored` false AND `input`
    // 0. InputSource.h requires the zero as belt-and-braces so that a caller who
    // ignores the flag gets neutral rather than stale bits, and every early
    // return below relies on it.
    InputSample sample{};
    if (replay_ == nullptr) {
        return sample;
    }
    // An out-of-range slot AUTHORS NOTHING rather than being clamped to 0 or 1.
    // Clamping would silently play one player's recorded inputs into the other's
    // slot, which is a mistake that produces a plausible fight; authoring nothing
    // is a mistake a host notices on the first tick.
    if (player_ < 0 || player_ > 1) {
        return sample;
    }
    if (static_cast<std::size_t>(tick) >= replay_->inputs.size()) {
        return sample;
    }
    sample.input    = replay_->inputs[static_cast<std::size_t>(tick)].p[player_];
    sample.authored = true;
    return sample;
}

std::uint32_t ReplayInputSource::AuthoredEndTick() const {
    if (replay_ == nullptr || player_ < 0 || player_ > 1) {
        return 0u;
    }
    // The reader caps a decoded replay at kMaxMatchTicks so this cannot truncate
    // in practice; the clamp is here because a caller may hand-build a ReplayData
    // and a silently wrapped uint32 would make a source claim to end where it
    // begins. Clamped to UINT32_MAX - 1 rather than to kUnboundedTick, because
    // that value MEANS "never runs out" and a replay always does.
    const std::size_t cap = static_cast<std::size_t>(kUnboundedTick - 1u);
    const std::size_t n   = replay_->inputs.size();
    return static_cast<std::uint32_t>(n < cap ? n : cap);
}

const char* ReplayInputSource::Name() const { return "REPLAY"; }

// --- Divergence -------------------------------------------------------------

ReplayVerifier::ReplayVerifier(const ReplayData& replay, bool stopOnDivergence)
    : replay_(&replay), stopOnDivergence_(stopOnDivergence) {}

void ReplayVerifier::OnTick(const TickView& view) {
    if (replay_ == nullptr || view.state == nullptr) {
        return;
    }

    // INPUTS BEFORE CHECKSUM, and the order is the whole diagnostic.
    //
    // A checksum mismatch caused by different inputs is not evidence about the
    // simulation at all -- it is a host wiring bug (the wrong source bound, an
    // off-by-one in the tick index, a second source overriding the first), and
    // reporting it as "the engine changed" sends an investigator to the one place
    // that is innocent. Only the FIRST mismatch is recorded: once the inputs have
    // diverged every later tick differs too, and a hundred reports of the
    // consequence bury the one report of the cause.
    if (!result_.inputMismatch) {
        const std::size_t t = static_cast<std::size_t>(view.tick);
        if (t < replay_->inputs.size() && !inputPairsEqual(replay_->inputs[t], view.inputs)) {
            result_.inputMismatch     = true;
            result_.inputMismatchTick = view.tick;
        }
        // Past the end of the replay is NOT a mismatch. A host that keeps ticking
        // after the file runs out is doing exactly what "the demonstration ends
        // and control comes back" looks like, and the replay has no opinion about
        // those ticks.
    }

    // A re-simulated tick rewinds the cursor so the CORRECTED run is the one
    // compared. Under rollback the first run of a tick may have used predicted
    // inputs, and comparing that against the recording would manufacture a
    // divergence out of a prediction that was always going to be replaced. Note
    // the limitation, stated rather than hidden: a divergence recorded during a
    // mispredicted first run is not retracted afterwards, because ReplayDivergence
    // has no field to retract it with. In practice the input check above fires on
    // the same tick and points the investigator at the inputs, which is the true
    // cause; and the case this class actually exists for -- verifying a playback
    // -- never re-simulates at all.
    if (view.resimulated) {
        const std::size_t passed = nextCheckpoint_;
        while (nextCheckpoint_ > 0 &&
               replay_->checkpoints[nextCheckpoint_ - 1u].tick >= view.tick) {
            --nextCheckpoint_;
        }

        // AND THE TALLIES COME BACK WITH THE CURSOR. Rewinding one and not the
        // other counts every re-simulated checkpoint twice, and CheckpointsCompared()
        // then reports more comparisons than the file has checkpoints -- from the
        // one pair whose whole job is to tell "verified all of it" apart from
        // "verified none of it".
        //
        // SATURATING RATHER THAN A BARE SUBTRACTION, because not every checkpoint
        // the cursor passes was COMPARED: the skip loop below walks past
        // checkpoints for ticks this session never delivered (a playback started
        // part-way through), and those were never counted. Ticks arrive
        // consecutively, so the skipped ones are always the leading ones and the
        // compared ones are a suffix -- which makes min(unpassed, compared_) the
        // exact number to retract when delivery is consecutive, and keeps this
        // from underflowing to four billion when it is not.
        //
        // agreed_ is CLAMPED rather than subtracted, and that is exact while
        // nothing has diverged: until the first disagreement every comparison
        // agreed, so agreed_ == compared_ and the clamp removes precisely the
        // agreements being retracted. AFTERWARDS IT IS APPROXIMATE, stated rather
        // than hidden -- which of the retracted comparisons agreed is recorded
        // nowhere, since ReplayDivergence keeps the first disagreement and not a
        // list of them, so agreed_ may stay high by however many disagreements sat
        // inside the rewound range. That is the same limitation the paragraph
        // above owns for the divergence itself, and a run that has already
        // diverged is a failed verification whose exact tallies are garnish.
        const std::uint32_t unpassed = static_cast<std::uint32_t>(passed - nextCheckpoint_);
        compared_ -= (unpassed < compared_) ? unpassed : compared_;
        if (agreed_ > compared_) agreed_ = compared_;
    }

    // Skip checkpoints for ticks this session never delivered -- a host may start
    // a playback part-way through, or stop and resume. Skipping is silent because
    // a checkpoint nobody reached is not a disagreement, and CheckpointsCompared()
    // is what tells a caller how much was actually verified.
    while (nextCheckpoint_ < replay_->checkpoints.size() &&
           replay_->checkpoints[nextCheckpoint_].tick < view.tick) {
        ++nextCheckpoint_;
    }
    if (nextCheckpoint_ >= replay_->checkpoints.size()) {
        return;
    }
    const ReplayCheckpoint& cp = replay_->checkpoints[nextCheckpoint_];
    if (cp.tick != view.tick) {
        return;
    }
    ++nextCheckpoint_;

    // The state is the state AFTER the tick, which is exactly when the recorder
    // took its own checksum: state->tick == view.tick + 1 at both ends.
    const std::uint32_t live = cse::kernel::Checksum(*view.state);
    ++compared_;
    if (live == cp.checksum) {
        ++agreed_;
        // previousAgreeingTick names the last agreement BEFORE the divergence, so
        // that the window (previousAgreeingTick, tick] is honest. Agreements after
        // a divergence are part of the drift profile and are counted in agreed_,
        // but they must not widen the window backwards.
        if (!result_.diverged) {
            result_.previousAgreeingTick = view.tick;
            result_.hadPreviousAgreement = true;
        }
        return;
    }

    // Only the first disagreement is described. Playback CONTINUES by default --
    // argued against ADR-002 CHOICE C in Replay.h: a network desync stops the
    // match because two live peers have no authority to resync from, while a
    // replay divergence is a regression against the past and the investigator
    // wants the whole drift profile, which stopping at the first one throws away.
    if (!result_.diverged) {
        result_.diverged         = true;
        result_.tick             = view.tick;
        result_.recordedChecksum = cp.checksum;
        result_.liveChecksum     = live;
    }
}

const ReplayDivergence& ReplayVerifier::Result() const { return result_; }

std::uint32_t ReplayVerifier::CheckpointsCompared() const { return compared_; }

std::uint32_t ReplayVerifier::CheckpointsAgreed() const { return agreed_; }

// The HOST is what stops. This object has no way to halt a session and is not
// going to grow one: an observer that can stop the thing it observes is no longer
// an observer.
bool ReplayVerifier::ShouldStop() const { return result_.diverged && stopOnDivergence_; }

void ReplayVerifier::Reset() {
    // The replay and the policy survive; everything measured does not. Resetting
    // is what a host does before replaying the same file again, and re-binding
    // the file at that moment would be a second place for the two to get out of
    // step.
    result_         = ReplayDivergence{};
    nextCheckpoint_ = 0;
    compared_       = 0;
    agreed_         = 0;
}

} // namespace cse::game
