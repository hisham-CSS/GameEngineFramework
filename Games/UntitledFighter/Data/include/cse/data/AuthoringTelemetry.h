// One appended line per prover run, recorded while authoring (ROADMAP M1.7,
// ADR-017). The Combo Prover panel measures everything a paper harvest needs
// -- run latency, the resource-check's separate cost, the content fingerprint,
// the verdict -- and until this pair existed it recorded none of it: the
// figures lived in panel members and died at editor exit, and the archived
// NORTHSTAR's warning ("worthless retroactively") came true for every session
// before 2026-08-31.
//
// This is a WRITER AND READER PAIR on purpose. Tests must parse the file the
// panel actually writes (the Done-when is "the log grows by one line per
// append and a test parses it back"), and a future harvest script deserves a
// documented reader rather than a format it has to reverse-engineer from the
// writer -- the same both-halves argument ReplayRecorder/DecodeReplay makes
// one module over.
//
// THE RECORD CARRIES NUMBERS IT DOES NOT MEASURE. Wall time and milliseconds
// are caller-supplied: this library is legal in determinism-scanned company
// precisely because it never touches a clock, and the only caller with a
// clock is Editor-side. The resource-check cost (`gapMs`) is a SEPARATE field
// from the run cost (`runMs`) -- the panel keeps them apart so the analyse
// latency distribution stays comparable (ComboProverPanel note 7), and a
// record that folded them together would corrupt the harvest at the source.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cse::data {

// A declared resource as the verdict saw it: the ranking certificate names
// resources, so a line that recorded only counts could not say what the
// certificate was resting on when the verdict was logged.
struct ProverRunResource {
    std::string  name;
    std::int32_t initial = 0;
    std::int32_t floor   = 0;
};

struct ProverRunRecord {
    std::int64_t  unixTimeSeconds = 0;   // caller-supplied wall time; 0 = unknown
    // The authored path the character was loaded from, EMPTY when the caller
    // handed an in-memory character. Named because the staged-copy trap
    // (fighting-core.md, hot reload) taught that a record which does not say
    // WHICH file it read faithfully describes the wrong copy.
    std::string   file;
    std::string   character;             // CharacterData::name (or id)
    std::uint64_t contentHash      = 0;  // the panel's NONCE-FREE fingerprint
    bool          changedSinceLast = false;  // content moved, not Re-run pressed
    std::int32_t  moveCount   = 0;
    std::int32_t  cancelCount = 0;
    std::vector<ProverRunResource> resources;
    std::int32_t  explored = 0;          // ProverResult::explored
    bool          capped   = false;      // the search hit its limit
    double        runMs = 0.0;           // the analysis alone
    double        gapMs = 0.0;           // the resource check, SEPARATE (above)
    std::string   verdict;               // ProverStatusName, or "analysis-failed"
};

// Appends `record` as one JSON line to baseDir/relPath. The path goes through
// the same containment gate as every authored read (absolute paths, drive/UNC
// roots and `..` refused; the base absolutized first for the MSVC
// weakly_canonical reason CharacterFileWatch.cpp records); missing parent
// directories are created; the whole line is serialized before the file is
// opened, so a failure costs nothing and a success is one write.
bool AppendProverRun(const std::string& baseDir, const std::string& relPath,
                     const ProverRunRecord& record, std::string& error);

// Parses the log back. A malformed line is SKIPPED AND COUNTED rather than
// failing the file: a crash mid-append must cost one line, not the log --
// while a reader that skipped silently would report a half-eaten file as a
// short, healthy one, which is why the count is an out-parameter the caller
// can put in front of a human. A file that cannot be opened is an error;
// "no runs recorded yet" and "the log is unreadable" are different answers.
bool ReadProverRuns(const std::string& baseDir, const std::string& relPath,
                    std::vector<ProverRunRecord>& out,
                    std::int32_t& skippedLines, std::string& error);

} // namespace cse::data
