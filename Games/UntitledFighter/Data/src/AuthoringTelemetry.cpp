#include "cse/data/AuthoringTelemetry.h"

// The same one-file compile CharacterData.cpp explains: PathSandbox.h pulls in
// nothing but Core.h, <filesystem> and <string>.
#include "PathSandbox.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace cse::data {

namespace {

// Shared by the writer and the reader so the two cannot drift: the reader
// refuses exactly the paths the writer would have refused. A log that does
// not exist yet is contained, not refused -- PathIsContained absolutizes its
// own base; the measured MSVC weakly_canonical account is at PathSandbox.cpp.
bool resolveContained(const std::string& baseDir, const std::string& relPath,
                      std::filesystem::path& outFull, std::string& error) {
    if (!MyCoreEngine::PathIsContained(baseDir, relPath, outFull)) {
        error = relPath + ": path: refused, because it is absolute, carries a "
                          "drive/UNC root, or contains a `..` component that "
                          "would escape the telemetry directory";
        return false;
    }
    return true;
}

nlohmann::json toJson(const ProverRunRecord& r) {
    nlohmann::json line;
    line["t"]         = r.unixTimeSeconds;
    line["file"]      = r.file;
    line["character"] = r.character;
    line["hash"]      = r.contentHash;
    line["changed"]   = r.changedSinceLast;
    line["moves"]     = r.moveCount;
    line["cancels"]   = r.cancelCount;
    nlohmann::json res = nlohmann::json::array();
    for (const ProverRunResource& x : r.resources)
        res.push_back({ { "name", x.name },
                        { "initial", x.initial },
                        { "floor", x.floor } });
    line["resources"] = std::move(res);
    line["explored"]  = r.explored;
    line["capped"]    = r.capped;
    line["run_ms"]    = r.runMs;
    line["gap_ms"]    = r.gapMs;
    line["verdict"]   = r.verdict;
    return line;
}

ProverRunRecord fromJson(const nlohmann::json& line) {
    ProverRunRecord r;
    r.unixTimeSeconds  = line.value("t", std::int64_t{ 0 });
    r.file             = line.value("file", std::string{});
    r.character        = line.value("character", std::string{});
    r.contentHash      = line.value("hash", std::uint64_t{ 0 });
    r.changedSinceLast = line.value("changed", false);
    r.moveCount        = line.value("moves", std::int32_t{ 0 });
    r.cancelCount      = line.value("cancels", std::int32_t{ 0 });
    if (line.contains("resources") && line["resources"].is_array()) {
        for (const nlohmann::json& x : line["resources"]) {
            ProverRunResource res;
            res.name    = x.value("name", std::string{});
            res.initial = x.value("initial", std::int32_t{ 0 });
            res.floor   = x.value("floor", std::int32_t{ 0 });
            r.resources.push_back(std::move(res));
        }
    }
    r.explored = line.value("explored", std::int32_t{ 0 });
    r.capped   = line.value("capped", false);
    r.runMs    = line.value("run_ms", 0.0);
    r.gapMs    = line.value("gap_ms", 0.0);
    r.verdict  = line.value("verdict", std::string{});
    return r;
}

} // namespace

bool AppendProverRun(const std::string& baseDir, const std::string& relPath,
                     const ProverRunRecord& record, std::string& error) {
    std::filesystem::path full;
    if (!resolveContained(baseDir, relPath, full, error)) return false;

    // The whole line exists before the file is opened: a serialization
    // problem costs nothing on disk, and the write below is one statement.
    const std::string line = toJson(record).dump();

    std::error_code ec;
    const std::filesystem::path parent = full.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            error = relPath + ": directory: cannot be created (" + ec.message() + ")";
            return false;
        }
    }

    std::ofstream out(full, std::ios::app | std::ios::binary);
    if (!out) {
        error = relPath + ": file: cannot be opened for appending";
        return false;
    }
    out << line << '\n';
    out.flush();
    if (!out) {
        error = relPath + ": file: append failed";
        return false;
    }
    return true;
}

bool ReadProverRuns(const std::string& baseDir, const std::string& relPath,
                    std::vector<ProverRunRecord>& out,
                    std::int32_t& skippedLines, std::string& error) {
    out.clear();
    skippedLines = 0;
    error.clear();

    std::filesystem::path full;
    if (!resolveContained(baseDir, relPath, full, error)) return false;

    std::ifstream in(full, std::ios::binary);
    if (!in) {
        // An error, not an empty result: "no runs recorded yet" is a fact the
        // caller may report; a reader that returned zero records for a file
        // it never opened would report it as certainty.
        error = relPath + ": file: cannot be opened for reading";
        return false;
    }

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        nlohmann::json parsed =
            nlohmann::json::parse(line, /*cb=*/nullptr, /*allow_exceptions=*/false);
        if (parsed.is_discarded() || !parsed.is_object()) {
            ++skippedLines;   // one crash-torn line costs one line (header)
            continue;
        }
        out.push_back(fromJson(parsed));
    }
    if (in.bad()) {
        error = relPath + ": file: read failed";
        return false;
    }
    return true;
}

} // namespace cse::data
