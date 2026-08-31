#include "cse/game/Catalogue.h"

#include "cse/data/CancelGraphDot.h"
#include "cse/data/CatalogueManifest.h"
#include "cse/data/CharacterData.h"
#include "cse/data/MatchBuilder.h"
#include "cse/data/ProverAdapter.h"
#include "cse/game/ComboSearch.h"
#include "cse/game/FightSession.h"
#include "cse/game/Replay.h"

// Same arrangement as Replay.cpp: the header is on the include path, the one
// definition of PathIsContained is compiled into CseData.
#include "PathSandbox.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace cse::game {
namespace {

const std::vector<std::string> kResources = { "meter", "juggle" };

const char* proverName(cse::data::ProverStatus s) {
    switch (s) {
        case cse::data::ProverStatus::Terminating: return "TERMINATING";
        case cse::data::ProverStatus::Infinite:    return "INFINITE";
        case cse::data::ProverStatus::Unknown:     return "UNKNOWN";
    }
    return "?";
}

const char* searchName(ComboVerdict v) {
    switch (v) {
        case ComboVerdict::Terminating: return "TERMINATING";
        case ComboVerdict::Infinite:    return "INFINITE";
        case ComboVerdict::Unresolved:  return "UNRESOLVED";
    }
    return "?";
}

// The arcade normals binding, the same (button x stance-prefix) rule the mode
// ships and every exhibit test used -- restated here because the tests' copy
// is a test fixture and a cooker cannot link a test.
cse::data::BuildOptions normalBindings(const cse::data::CharacterData& c) {
    const char* kButtons[] = { "lp", "mp", "hp", "lk", "mk", "hk" };
    const std::uint16_t kBits[] = {
        cse::kernel::kInputLP, cse::kernel::kInputMP, cse::kernel::kInputHP,
        cse::kernel::kInputLK, cse::kernel::kInputMK, cse::kernel::kInputHK };
    cse::data::BuildOptions options{};
    for (int b = 0; b < 6; ++b)
        for (const char* prefix : { "stand_", "crouch_", "air_" }) {
            const std::string id = std::string(prefix) + kButtons[b];
            if (c.FindMove(id) != cse::data::kInvalidMove)
                options.bindings.push_back({ id, kBits[b] });
        }
    return options;
}

// A small text file under the sandboxed output directory, written whole or
// not at all -- the same containment rule every authored read obeys, applied
// to the one place this module writes.
bool writeText(const std::string& outDir, const std::string& relPath,
               const std::string& text, std::string& error) {
    std::filesystem::path full;
    if (!MyCoreEngine::PathIsContained(outDir, relPath, full)) {
        error = relPath + ": refused (escapes the output directory)";
        return false;
    }
    std::ofstream out(full, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = relPath + ": cannot open for writing";
        return false;
    }
    out << text;
    out.close();
    if (!out) {
        error = relPath + ": write failed";
        std::error_code ec;
        std::filesystem::remove(full, ec);
        return false;
    }
    return true;
}

// One entry, cooked end to end. Returns with entry.error set on any refusal.
void cookEntry(const std::string& charactersDir,
               const cse::data::CatalogueManifest& manifest,
               const cse::data::CatalogueEntry& row,
               const std::string& outDir,
               CookedEntry& entry) {
    entry.name = row.name;

    // --- load ---------------------------------------------------------------
    cse::data::CharacterData character{};
    cse::data::LoadReport    loadReport{};
    cse::data::LoadOptions   loadOptions;
    loadOptions.expectedResources = kResources;
    if (row.variantRel.empty()) {
        if (!cse::data::LoadCharacterFile(charactersDir, manifest.baseFile,
                                          loadOptions, character, loadReport)) {
            entry.error = loadReport.error;
            return;
        }
        entry.description =
            "The shipped file, unpatched -- the row every patch is a diff "
            "against, and the pair every exhibit's deltas are measured from.";
    } else {
        std::string description;
        if (!cse::data::LoadCharacterVariant(charactersDir, manifest.baseFile,
                                             row.variantRel, loadOptions,
                                             character, loadReport,
                                             &description)) {
            entry.error = loadReport.error;
            return;
        }
        entry.description = description;
    }

    // --- the model's verdict ------------------------------------------------
    cse::data::ProverOptions proverOptions;
    proverOptions.expectedResources = kResources;
    cse::data::ProverResult verdict{};
    cse::data::ProverReport proverReport{};
    if (!cse::data::AnalyseCharacter(character, proverOptions, verdict,
                                     proverReport)) {
        entry.error = "the prover refused the file";
        return;
    }
    entry.proverStatus = proverName(verdict.status);

    // --- the bindings the manifest records ----------------------------------
    cse::data::BuildOptions options;
    if (!row.soloBindingMove.empty()) {
        options.bindings = { { row.soloBindingMove, cse::kernel::kInputLP } };
    } else {
        options = normalBindings(character);
        for (const cse::data::CatalogueBinding& b : row.extraBindings)
            options.bindings.push_back({ b.moveId, b.buttons });
    }

    cse::data::MatchBuild build{};
    if (!cse::data::BuildMatchData(character, options, character, options,
                                   build)) {
        entry.error = build.report[0].error;
        return;
    }

    // --- the game's verdict, from the corner bench --------------------------
    //
    // The bench every shipped verdict is computed for: the defender's BODY
    // against the right wall (derived from its own pushbox, never a recorded
    // pixel count), origins 34 px apart, seed 0x1D7 -- the same opening the
    // exhibit tests measured everything on.
    cse::kernel::Fighter probe{};
    probe.facing = 1;
    const std::int32_t defX =
        cse::kernel::WallLimitFor(build.data.p[1], probe);
    const std::int32_t atkX = defX - 34 * cse::kernel::kSubUnitsPerPixel;

    cse::game::ComboSearchRequest request{};
    request.data         = &build.data;
    request.attackerSlot = 0;
    cse::kernel::ResetMatch(request.from, 0x1D7u);
    request.from.p[0].posX = atkX;
    request.from.p[1].posX = defX;
    const ComboSearchResult searched = RunComboSearch(request);
    entry.searchVerdict = searchName(searched.verdict);
    entry.maxHits       = searched.maxHits;
    entry.verdictsAgree =
        (verdict.status == cse::data::ProverStatus::Terminating &&
         searched.verdict == ComboVerdict::Terminating) ||
        (verdict.status == cse::data::ProverStatus::Infinite &&
         searched.verdict == ComboVerdict::Infinite);

    if (searched.verdict == ComboVerdict::Unresolved) {
        entry.error = "the search did not resolve this entry: " + searched.note;
        return;
    }

    // --- the graph, drawn ---------------------------------------------------
    entry.dotRel = row.name + ".dot";
    std::string writeError;
    if (!writeText(outDir, entry.dotRel,
                   cse::data::WriteCancelGraphDot(character, verdict),
                   writeError)) {
        entry.error = writeError;
        return;
    }

    // --- the replay: rehearsed, recorded, VERIFIED, then written ------------
    std::vector<std::uint16_t> sequence;
    std::size_t   loopStart = 0;
    std::uint32_t turns     = 1;
    if (searched.verdict == ComboVerdict::Infinite) {
        // THE LOOP ALONE. The witness's prefix is the search's own path to
        // the repeating state -- thousands of exploratory macro-actions for a
        // walked infinite -- and replaying it would be a recording of the
        // SEARCH, not of the exhibit. The loop is the exhibit, it enters
        // from the bench on its own (the first full cook measured a 9285-
        // entry prefix stalling a 20k-tick budget; the loop performs in
        // hundreds), and a loop that genuinely needed the ridden-in state
        // would stall here and become this entry's honest error rather than
        // a silently absent replay. Four turns: enough to watch it cycle.
        sequence.assign(searched.witness.begin() +
                            static_cast<std::ptrdiff_t>(searched.loopStart),
                        searched.witness.end());
        loopStart = 0;
        turns     = 4;

        // A WATCHABLE EXCERPT when the provable loop is enormous. The
        // microwalk's induction closes over a drift super-cycle thousands of
        // entries long -- walk-regain crumbs against the pushbox clamp give
        // the exact state a period far above the behaviour's -- and a replay
        // of the whole of it demonstrates nothing a 24-entry excerpt does
        // not. The INDUCTION is the search's and stands as recorded; the
        // replay is the watchable slice of the loop, run through once.
        constexpr std::size_t kMaxDemoLoop = 8;
        constexpr std::size_t kExcerpt     = 24;
        if (sequence.size() > kMaxDemoLoop) {
            if (sequence.size() > kExcerpt) sequence.resize(kExcerpt);
            turns = 1;
        }
    } else {
        sequence = searched.longestString;
    }
    if (sequence.empty()) {
        // A true sentence about this exhibit: nothing connects from the
        // bench, so there is no string to demonstrate and no replay to fake.
        entry.replayRel.clear();
        return;
    }

    // A loop's own first entry may be enterable only mid-string -- meter_loop
    // opens on a super whose chord is shadowed from idle and reached via
    // FindCancel -- so the loop is tried at every ROTATION until one performs
    // from the bench. Rotations of a cycle are the same cycle; the first that
    // works is the watchable spelling of it. Rotation 0 first, so a loop that
    // performs as printed is recorded as printed.
    std::vector<std::size_t> rotations;
    if (searched.verdict == ComboVerdict::Infinite && sequence.size() > 1)
        for (std::size_t rot = 0; rot < sequence.size(); ++rot)
            rotations.push_back(rot);
    else
        rotations.push_back(0);

    DemonstrationRequest demoRequest{};
    demoRequest.from         = &request.from;
    demoRequest.data         = &build.data;
    demoRequest.attackerSlot = 0;
    demoRequest.moveIds      = sequence;
    demoRequest.loopStart    = loopStart;
    demoRequest.turns        = turns;
    // An INFINITE's witness is the whole ride to the provable state-repeat --
    // the hitstun_plus_7 restart is 256 moves of prefix before its loop,
    // because the repeat waits for the combo counter to saturate -- so the
    // budget is sized for saturation-length demonstrations, not sparring
    // clips. The replay stays small regardless: the format is RLE runs.
    demoRequest.maxTicks     = 20000;
    Demonstration demo{};
    bool          performed = false;
    std::string   lastDemoError;
    for (const std::size_t rot : rotations) {
        std::vector<std::uint16_t> rotated;
        rotated.reserve(sequence.size());
        for (std::size_t i = 0; i < sequence.size(); ++i)
            rotated.push_back(sequence[(rot + i) % sequence.size()]);
        demoRequest.moveIds = rotated;
        demo = Demonstration{};
        if (BuildDemonstration(demoRequest, demo)) {
            performed = true;
            break;
        }
        lastDemoError = demo.error;
    }
    if (!performed) {
        entry.error = "the verdict's own sequence could not be performed at "
                      "any rotation: " + lastDemoError;
        return;
    }

    FightSetup setup{};
    setup.start.seed         = 0x1D7u;
    setup.start.startPosX[0] = atkX;
    setup.start.startPosX[1] = defX;
    setup.data               = &build.data;

    FightSession session{};
    std::string  sessionError;
    if (!session.Begin(setup, sessionError)) {
        entry.error = sessionError;
        return;
    }

    ReplayRecorder recorder;
    std::string    recorderError;
    if (!recorder.Begin(setup.start, HashMatchData(build.data), character.id,
                        character.id, recorderError)) {
        entry.error = recorderError;
        return;
    }
    session.AddObserver(&recorder);

    ScriptedInputSource script(demo.inputs, 0, "CATALOGUE");
    session.SetInputSource(0, &script);
    for (std::size_t t = 0; t < demo.inputs.size(); ++t) session.Tick();
    if (!recorder.Error().empty()) {
        entry.error = recorder.Error();
        return;
    }

    // ADR-011 section 4's own rule: verified bit-identical BEFORE it is
    // written. The encoded bytes are decoded and re-simulated under
    // ReplayVerifier; a single diverging checkpoint or input refuses the file.
    std::vector<std::uint8_t> bytes;
    if (!recorder.Encode(bytes, recorderError)) {
        entry.error = recorderError;
        return;
    }
    cse::game::ReplayData    replay{};
    cse::game::ReplayReport  replayReport{};
    cse::game::ReplayReadOptions readOptions{};
    readOptions.expectedMatchDataHash = HashMatchData(build.data);
    if (!DecodeReplay(row.name, bytes.data(), bytes.size(), readOptions,
                      replay, replayReport)) {
        entry.error = "the recorded replay does not decode: " +
                      replayReport.error;
        return;
    }

    FightSession verifySession{};
    if (!verifySession.Begin(setup, sessionError)) {
        entry.error = sessionError;
        return;
    }
    ReplayInputSource p0(replay, 0);
    ReplayInputSource p1(replay, 1);
    verifySession.SetInputSource(0, &p0);
    verifySession.SetInputSource(1, &p1);
    ReplayVerifier verifier(replay);
    verifySession.AddObserver(&verifier);
    for (std::uint32_t t = 0; t < replay.TickCount(); ++t) verifySession.Tick();
    if (verifier.Result().diverged || verifier.Result().inputMismatch) {
        entry.error = "the replay does not verify bit-identical against its "
                      "own recording";
        return;
    }
    if (verifier.CheckpointsCompared() == 0) {
        entry.error = "the verifier compared nothing, which must not read as "
                      "agreement";
        return;
    }

    entry.replayRel = row.name + ".csrp";
    if (!recorder.Write(outDir, entry.replayRel, recorderError)) {
        entry.error = recorderError;
        return;
    }
}

} // namespace

bool CookCatalogue(const std::string& charactersDir,
                   const std::string& manifestRel,
                   const std::string& outDir,
                   CatalogueReport& report) {
    report = CatalogueReport{};

    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);

    cse::data::CatalogueManifest manifest{};
    if (!cse::data::LoadCatalogueManifest(charactersDir, manifestRel, manifest,
                                          report.error))
        return false;

    std::string summary;
    for (const cse::data::CatalogueEntry& row : manifest.entries) {
        CookedEntry entry;
        cookEntry(charactersDir, manifest, row, outDir, entry);

        summary += entry.name + "\n";
        summary += "  model  " + entry.proverStatus + "\n";
        summary += "  game   " + entry.searchVerdict +
                   " (longest " + std::to_string(entry.maxHits) + " hit(s))" +
                   (entry.verdictsAgree ? "" : "  <-- THE PAIR DISAGREES; the "
                                               "description says why") +
                   "\n";
        if (!entry.replayRel.empty())
            summary += "  replay " + entry.replayRel + " (verified)\n";
        else if (entry.error.empty())
            summary += "  replay (nothing to demonstrate from this bench)\n";
        summary += "  graph  " + entry.dotRel + "\n";
        if (!entry.error.empty())
            summary += "  ERROR  " + entry.error + "\n";
        summary += "  " + entry.description + "\n\n";

        report.entries.push_back(std::move(entry));
    }

    std::string writeError;
    if (!writeText(outDir, "catalogue.txt", summary, writeError)) {
        if (report.error.empty()) report.error = writeError;
        return false;
    }
    return report.ok();
}

} // namespace cse::game
