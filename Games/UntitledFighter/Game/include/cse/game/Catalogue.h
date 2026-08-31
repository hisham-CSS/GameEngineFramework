// THE COOKER RECORDING (ROADMAP M1.6, ADR-011 section 4): every catalogue
// entry becomes ARTIFACTS -- both verdicts, a verified replay, a graph.dot --
// instead of assertions that are re-derived on every test run and recorded
// nowhere. "A replay per verdict" is this file.
//
// PER ENTRY, THE COOK IS THE WHOLE ARGUMENT IN MINIATURE:
//   load (base or variant patch) -> the PROVER's verdict on the file ->
//   build with the manifest's bindings -> the SEARCH's verdict on the game
//   (the corner bench every shipped verdict is computed for: seed 0x1D7,
//   origins 34 px apart at the right wall) -> the demonstrated sequence (the
//   INFINITE's witness with two turns of its loop, or the longest measured
//   string once) rehearsed by BuildDemonstration -> recorded through a live
//   FightSession by ReplayRecorder -> VERIFIED bit-identical by re-simulating
//   the encoded bytes under ReplayVerifier -- ADR-011 section 4's own rule:
//   "every replay is verified bit-identical by ReplayVerifier before it is
//   written" -- and only then written, beside its cancel graph and a summary
//   line carrying both verdicts and the entry's authored description.
//
// WHY THIS IS IN CseGAME AND NOT IN Cooker/: CLAUDE.md's never-list -- the
// engine's AssetCooker may not depend on a title, and the root CMakeLists
// enforces that at configure time. The catalogue is title content cooked by
// title code; the thin CLI over this function is a title-owned executable.
//
// An entry that cannot be cooked -- a witness the engine cannot perform, a
// replay that does not verify -- is recorded as that entry's error and FAILS
// the cook, because a catalogue with a silently missing row is a shop window
// with a painted-over gap. The one sanctioned absence: an entry whose search
// found no string at all records "nothing to demonstrate" with no replay,
// which is a true sentence about that exhibit.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cse::game {

struct CookedEntry {
    std::string name;
    std::string description;     // the variant's authored line; synthesized for base

    std::string proverStatus;    // "TERMINATING" / "INFINITE" / ...
    std::string searchVerdict;   // "Terminating" / "Infinite" / "Unresolved"
    bool        verdictsAgree = false;
    std::int32_t maxHits      = 0;

    std::string replayRel;       // empty when nothing was demonstrable
    std::string dotRel;

    std::string error;           // empty on success
};

struct CatalogueReport {
    std::vector<CookedEntry> entries;
    std::string              error;   // manifest-level failure
    bool ok() const {
        if (!error.empty()) return false;
        for (const CookedEntry& e : entries)
            if (!e.error.empty()) return false;
        return !entries.empty();
    }
};

// Cook every manifest entry into `outDir` (created if absent): one
// `<name>.csrp` + one `<name>.dot` per entry plus a `catalogue.txt` summary.
// Returns report.ok().
bool CookCatalogue(const std::string& charactersDir,
                   const std::string& manifestRel,
                   const std::string& outDir,
                   CatalogueReport& report);

} // namespace cse::game
