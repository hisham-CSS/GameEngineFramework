#include "cse/data/VersusConfig.h"

// The same one-file compile CharacterData.cpp explains: PathSandbox.h pulls in
// nothing but Core.h, <filesystem> and <string>.
#include "PathSandbox.h"

#include <nlohmann/json.hpp>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

namespace cse::data {

namespace {

using json = nlohmann::json;

// Every authored read is capped (CharacterData.cpp's readAuthoredFile). A lobby
// config is three fields; anything past this is not one, and a parser handed a
// gigabyte by a hostile content root should say so rather than try.
constexpr std::uintmax_t kMaxVersusBytes = 64u * 1024u;

// The closed key list, refused by name with the legal names listed -- the
// character loader's rule, restated here because CseData and the presentation
// library do not see each other's helpers.
bool checkKeys(const json& obj, const char* const* legal, std::size_t n, std::string& error) {
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        bool ok = false;
        for (std::size_t i = 0; i < n; ++i) if (it.key() == legal[i]) { ok = true; break; }
        if (!ok) {
            error = "versus: `" + it.key() + "` is not a field. The fields are:";
            for (std::size_t i = 0; i < n; ++i) error += std::string(" ") + legal[i];
            return false;
        }
    }
    return true;
}

bool slotInRange(long long v) { return v == 0 || v == 1; }
bool portInRange(long long v) { return v >= 1 && v <= 65535; }

// A WHOLE-string decimal integer. std::atoi("1x") is 1, std::atoi("one") is 0
// and std::atoi("99999999999") is anything: strtoll with an end pointer says
// whether every character was consumed and whether the value fit.
bool parseWholeInt(const std::string& s, long long& out) {
    if (s.empty()) return false;
    errno = 0;
    char* end = nullptr;
    const long long v = std::strtoll(s.c_str(), &end, 10);
    if (end != s.c_str() + s.size() || errno == ERANGE) return false;
    out = v;
    return true;
}

} // namespace

bool ParseVersusConfig(const std::string& jsonText, VersusConfig& out, std::string& error) {
    error.clear();
    // allow_exceptions = false: a malformed file is a discarded value, not an
    // unwind through the mode's Enter -- the loader's idiom.
    const json doc = json::parse(jsonText.begin(), jsonText.end(), nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) { error = "versus: not a JSON object"; return false; }

    static const char* const kKeys[] = { "slot", "port", "peer" };
    if (!checkKeys(doc, kKeys, sizeof(kKeys) / sizeof(kKeys[0]), error)) return false;

    VersusConfig cfg{};
    if (doc.contains("slot")) {
        const json& v = doc["slot"];
        if (!v.is_number_integer()) { error = "versus: `slot` is not an integer"; return false; }
        const long long n = v.get<long long>();
        if (!slotInRange(n)) { error = "versus: `slot` must be 0 or 1"; return false; }
        cfg.slot = static_cast<int>(n);
    }
    if (doc.contains("port")) {
        const json& v = doc["port"];
        if (!v.is_number_integer()) { error = "versus: `port` is not an integer"; return false; }
        const long long n = v.get<long long>();
        if (!portInRange(n)) { error = "versus: `port` must be 1..65535"; return false; }
        cfg.port = static_cast<std::uint16_t>(n);
    }
    if (doc.contains("peer")) {
        const json& v = doc["peer"];
        if (!v.is_string()) { error = "versus: `peer` is not a string"; return false; }
        cfg.peer = v.get<std::string>();
        if (cfg.peer.empty()) { error = "versus: `peer` is empty"; return false; }
    }
    out = cfg;
    return true;
}

bool LoadVersusConfig(const std::string& contentRoot, const std::string& relPath,
                      VersusConfig& out, std::string& error) {
    error.clear();
    std::filesystem::path full;
    if (!MyCoreEngine::PathIsContained(contentRoot, relPath, full)) {
        error = relPath + ": path: refused, because it is absolute, carries a "
                          "drive/UNC root, or contains a `..` component that "
                          "would escape the content root";
        return false;
    }

    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(full, ec);
    if (ec) {
        error = relPath + ": file: cannot be opened (" + ec.message() + ")";
        return false;
    }
    if (size > kMaxVersusBytes) {
        error = relPath + ": file: " + std::to_string(size) + " bytes exceeds the " +
                std::to_string(kMaxVersusBytes) + "-byte cap on authored content";
        return false;
    }

    std::ifstream in(full, std::ios::binary);
    if (!in) {
        error = relPath + ": file: cannot be opened for reading";
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) {
        error = relPath + ": file: read failed";
        return false;
    }
    return ParseVersusConfig(text, out, error);
}

bool ApplyVersusOverrides(const std::vector<std::string>& args, VersusConfig& io,
                          std::string& error) {
    error.clear();
    // Applied to a copy and committed at the end, so a bad third pair cannot
    // leave the first two in place (the header says what that lobby does).
    VersusConfig cfg = io;
    const auto isFlag = [](const std::string& a) { return a.size() >= 2 && a[0] == '-' && a[1] == '-'; };

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& key = args[i];
        if (!isFlag(key)) continue;   // argv[0], the Player's scene, anything positional
        const bool known = key == "--slot" || key == "--port" || key == "--peer";
        if (i + 1 >= args.size()) {
            if (known) { error = "versus: `" + key + "` has no value"; return false; }
            break;
        }
        const std::string& value = args[++i];   // consumed whether or not the key is ours
        if (!known) continue;

        long long n = 0;
        if (key == "--slot") {
            if (!parseWholeInt(value, n) || !slotInRange(n)) {
                error = "versus: `--slot` must be 0 or 1, not `" + value + "`";
                return false;
            }
            cfg.slot = static_cast<int>(n);
        } else if (key == "--port") {
            if (!parseWholeInt(value, n) || !portInRange(n)) {
                error = "versus: `--port` must be 1..65535, not `" + value + "`";
                return false;
            }
            cfg.port = static_cast<std::uint16_t>(n);
        } else {
            if (value.empty()) { error = "versus: `--peer` is empty"; return false; }
            cfg.peer = value;
        }
    }
    io = cfg;
    return true;
}

} // namespace cse::data
