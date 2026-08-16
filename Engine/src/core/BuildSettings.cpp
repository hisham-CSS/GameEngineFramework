#include "BuildSettings.h"

#include "ProjectSettings.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <system_error>

namespace MyCoreEngine {

    namespace {
        // Returned by StartupScene() when the list is empty, so the accessor can
        // hand back a reference without every caller checking for null.
        const std::string kEmpty;

        // A refusal, phrased for a panel's status line and logged on the way out.
        // Every early return in Load goes through here so that the "*this is
        // untouched" half of the contract is impossible to forget: this function
        // touches nothing but the result.
        BuildSettingsLoadResult malformed(const std::string& path,
                                          const std::string& why) {
            BuildSettingsLoadResult r;
            r.status = BuildSettingsStatus::Malformed;
            r.message = why;
            std::cerr << "BuildSettings: '" << path << "' refused: " << why
                      << " - the existing settings were NOT changed and must not "
                         "be overwritten." << std::endl;
            return r;
        }

        // The containment rule, applied to one authored path, with a message that
        // names the path rather than the rule. Shared by the scene list and the
        // output directory because they are refused for the same three reasons
        // (absolute, rooted, or escaping) even though only one of them is later
        // opened and only the other is later written into.
        bool pathAllowed(const std::string& baseDir, const std::string& rel,
                         std::string* whyNot) {
            std::filesystem::path full;
            if (PathIsContained(baseDir, rel, full)) return true;
            if (whyNot) {
                *whyNot = "'" + rel +
                    "' is absolute, names a drive/UNC root, or escapes the project "
                    "with '..'; paths here must stay inside it";
            }
            return false;
        }
    }

    // -----------------------------------------------------------------------
    // The profile, and the two existing axes it selects
    // -----------------------------------------------------------------------
    // Every switch below is DEFAULT-LESS on purpose. Engine/CMakeLists.txt:340-355
    // turns an unhandled enumerator into a compile ERROR (/we4062, -Werror=switch)
    // precisely so that adding a profile cannot produce one that parses, saves and
    // then silently builds the wrong target. A `default:` would hand that error
    // back. The return after each switch is unreachable and exists only because
    // the compiler cannot know that.

    const char* BuildProfileToken(BuildProfile p) {
        switch (p) {
        case BuildProfile::Debug:       return "debug";
        case BuildProfile::Development: return "development";
        case BuildProfile::Shipping:    return "shipping";
        }
        return "development";
    }

    const char* BuildProfileDisplayName(BuildProfile p) {
        switch (p) {
        case BuildProfile::Debug:       return "Debug";
        case BuildProfile::Development: return "Development";
        case BuildProfile::Shipping:    return "Shipping";
        }
        return "Development";
    }

    bool ParseBuildProfile(const std::string& token, BuildProfile& out) {
        if (token == "debug") { out = BuildProfile::Debug; return true; }
        if (token == "development") { out = BuildProfile::Development; return true; }
        if (token == "shipping") { out = BuildProfile::Shipping; return true; }
        return false; // strict: see the header
    }

    const char* BuildProfileCMakeConfig(BuildProfile p) {
        switch (p) {
        case BuildProfile::Debug:       return "Debug";
        case BuildProfile::Development: return "RelWithDebInfo";
        case BuildProfile::Shipping:    return "Release";
        }
        return "RelWithDebInfo";
    }

    const char* BuildProfileCMakeTarget(BuildProfile p) {
        switch (p) {
        case BuildProfile::Debug:       return "PlayerDebug";
        case BuildProfile::Development: return "PlayerDebug";
        case BuildProfile::Shipping:    return "PlayerShipping";
        }
        return "PlayerDebug";
    }

    std::string BuildPlayerExecutableName(BuildProfile p) {
        // OUTPUT_NAME "Player" on PlayerShipping only (Player/CMakeLists.txt:31-32),
        // which is why this is not the target name with an extension stuck on.
        const char* stem = nullptr;
        switch (p) {
        case BuildProfile::Debug:       stem = "PlayerDebug"; break;
        case BuildProfile::Development: stem = "PlayerDebug"; break;
        case BuildProfile::Shipping:    stem = "Player";      break;
        }
        if (!stem) stem = "PlayerDebug";
#if defined(_WIN32)
        return std::string(stem) + ".exe";
#else
        // Linux/macOS produce an extension-less binary. CI compiles both
        // platforms, so this is not a hypothetical branch.
        return std::string(stem);
#endif
    }

    // -----------------------------------------------------------------------
    // Path spelling
    // -----------------------------------------------------------------------
    std::string BuildSettings::NormalizeScenePath(const std::string& path) {
        if (path.empty()) return path;
        // lexically_normal collapses "a/./b" and "a/b/../c" TEXTUALLY -- it opens
        // nothing and follows no symlink, which is what makes it safe to run on
        // an untrusted string. It is emphatically not the containment check;
        // PathIsContained is, and it refuses ".." lexically before any filesystem
        // access for exactly the symlink reason PathSandbox.h:25-27 records.
        std::filesystem::path p =
            std::filesystem::path(path).lexically_normal();

        std::string s = p.generic_string(); // forward slashes on every platform
        // Drop a leading "./": lexically_normal keeps it, and it is the one
        // difference that would split "./Exported/x.json" and "Exported/x.json"
        // into two list entries that name one file.
        while (s.rfind("./", 0) == 0) s.erase(0, 2);
        // A trailing slash on a scene path is meaningless and would defeat the
        // duplicate check the same way.
        while (s.size() > 1 && s.back() == '/') s.pop_back();
        return s;
    }

    // NORMALIZE, THEN CHECK, THEN STORE THE NORMALIZED FORM -- and the order is
    // load-bearing rather than tidy.
    //
    // lexically_normal COLLAPSES a ".." it can cancel, so "Exported/../x.json"
    // becomes "x.json" and then passes the containment check that the original
    // spelling would have been refused by. That collapse is purely textual and
    // ignores symlinks: if `Exported` were a symlink pointing elsewhere, the two
    // spellings name DIFFERENT files, and approving the collapsed one while
    // opening the original would be exactly the escape PathSandbox.h:25-27
    // performs its lexical ".." refusal to prevent.
    //
    // It is safe here because the two are never different: every path in this
    // file is normalized BEFORE it is checked and the NORMALIZED form is what is
    // stored, so the path that was approved is the path that will later be
    // opened. Anyone adding a path to `scenes` by another route must keep that
    // invariant -- storing a raw string that was validated in its collapsed form
    // is the way to break it.

    // -----------------------------------------------------------------------
    // Load: all-or-nothing, bounded, and refusing rather than clamping
    // -----------------------------------------------------------------------
    BuildSettingsLoadResult BuildSettings::Load(const std::string& path) {
        namespace fs = std::filesystem;

        std::error_code ec;
        // exists() through an error_code rather than the throwing overload: a
        // path on a disconnected network drive throws from the plain form, and a
        // panel asking "do I have build settings yet" must not take an exception
        // for an answer.
        if (!fs::exists(path, ec) || ec) {
            BuildSettingsLoadResult r;
            r.status = BuildSettingsStatus::Missing;
            return r; // no file yet: defaults stand, and seeding is correct
        }

        // BOUND THE BYTES BEFORE THE PARSE. Every other bound in this function is
        // consulted after nlohmann has already materialized the whole document,
        // so without this one a 2 GB build.json is an out-of-memory before any
        // rule gets a chance to refuse it.
        const std::uintmax_t size = fs::file_size(path, ec);
        if (ec) {
            return malformed(path, "cannot determine the file size (" +
                                   ec.message() + ")");
        }
        if (size > kMaxFileBytes) {
            return malformed(path, "the file is " + std::to_string(size) +
                                   " bytes; the limit is " +
                                   std::to_string(kMaxFileBytes));
        }

        std::ifstream in(path);
        if (!in.is_open()) {
            // Distinct from Missing: the file is THERE and unreadable, which is a
            // permissions or lock problem, and answering "no build settings yet"
            // would invite the caller to write a fresh one over it.
            return malformed(path, "the file exists but could not be opened");
        }

        // LOCALS, COMMITTED ONLY AT THE END. See the header, and
        // ProjectSettings.cpp:15-23 for the bug that made this the house rule.
        std::vector<std::string> newScenes;
        std::string              newOutput = outputDirectory;
        BuildProfile             newProfile = profile;

        nlohmann::json root;
        try {
            root = nlohmann::json::parse(in);
        }
        catch (const std::exception& e) {
            return malformed(path, std::string("not valid JSON: ") + e.what());
        }
        if (!root.is_object()) {
            return malformed(path, "the top level is not a JSON object");
        }

        // ---- version ------------------------------------------------------
        if (root.contains("version")) {
            if (!root["version"].is_number_integer()) {
                return malformed(path, "'version' is not an integer");
            }
            const int v = root["version"].get<int>();
            if (v > kVersion) {
                // Refuse forward, do not guess. A newer editor may have moved a
                // meaning rather than only added a key, and reading it with these
                // rules would produce a plausible build list that is not the one
                // somebody authored.
                return malformed(path, "version " + std::to_string(v) +
                                       " was written by a newer editor (this one "
                                       "understands " + std::to_string(kVersion) + ")");
            }
        }

        // ---- scenes -------------------------------------------------------
        if (root.contains("scenes")) {
            const nlohmann::json& arr = root["scenes"];
            if (!arr.is_array()) {
                return malformed(path, "'scenes' is not an array");
            }
            if (arr.size() > kMaxScenes) {
                // REFUSED, NOT TRUNCATED. A clamp here ships a game missing its
                // last levels and says nothing about it.
                return malformed(path, "'scenes' has " + std::to_string(arr.size()) +
                                       " entries; the limit is " +
                                       std::to_string(kMaxScenes));
            }
            newScenes.reserve(arr.size());
            for (std::size_t i = 0; i < arr.size(); ++i) {
                const std::string at = "scenes[" + std::to_string(i) + "]";
                if (!arr[i].is_string()) {
                    return malformed(path, at + " is not a string");
                }
                const std::string raw = arr[i].get<std::string>();
                if (raw.empty()) {
                    return malformed(path, at + " is empty");
                }
                if (raw.size() > kMaxPathLength) {
                    return malformed(path, at + " is " + std::to_string(raw.size()) +
                                           " characters; the limit is " +
                                           std::to_string(kMaxPathLength));
                }
                const std::string rel = NormalizeScenePath(raw);
                std::string why;
                if (!pathAllowed(std::string(), rel, &why)) {
                    return malformed(path, at + ": " + why);
                }
                // DUPLICATES ARE REFUSED rather than deduped, because the index
                // is the meaning here: with the same scene at 0 and at 4 there is
                // no answer to "what is this scene's build index", which is the
                // question the panel and any future load-by-index both ask.
                for (std::size_t j = 0; j < newScenes.size(); ++j) {
                    if (newScenes[j] == rel) {
                        return malformed(path, at + " ('" + rel +
                                               "') is already at index " +
                                               std::to_string(j));
                    }
                }
                newScenes.push_back(rel);
            }
        }

        // ---- outputDirectory ----------------------------------------------
        if (root.contains("outputDirectory")) {
            if (!root["outputDirectory"].is_string()) {
                return malformed(path, "'outputDirectory' is not a string");
            }
            const std::string raw = root["outputDirectory"].get<std::string>();
            if (raw.empty()) {
                return malformed(path, "'outputDirectory' is empty");
            }
            if (raw.size() > kMaxPathLength) {
                return malformed(path, "'outputDirectory' is " +
                                       std::to_string(raw.size()) +
                                       " characters; the limit is " +
                                       std::to_string(kMaxPathLength));
            }
            const std::string rel = NormalizeScenePath(raw);
            std::string why;
            if (!pathAllowed(std::string(), rel, &why)) {
                // The sharpest of the refusals: the build WRITES here and mirrors
                // what it wrote, so an escaping output directory is a delete
                // primitive with a text field in front of it.
                return malformed(path, "'outputDirectory' " + why);
            }
            newOutput = rel;
        }

        // ---- profile ------------------------------------------------------
        if (root.contains("profile")) {
            if (!root["profile"].is_string()) {
                return malformed(path, "'profile' is not a string");
            }
            const std::string token = root["profile"].get<std::string>();
            if (!ParseBuildProfile(token, newProfile)) {
                return malformed(path, "'profile' is '" + token +
                                       "'; expected one of debug, development, "
                                       "shipping");
            }
        }

        // Everything converted. Commit.
        scenes = std::move(newScenes);
        outputDirectory = std::move(newOutput);
        profile = newProfile;

        BuildSettingsLoadResult r;
        r.status = BuildSettingsStatus::Ok;
        return r;
    }

    bool BuildSettings::Save(const std::string& path) const {
        nlohmann::json root;
        root["version"] = kVersion;
        root["scenes"] = scenes;
        root["outputDirectory"] = outputDirectory;
        root["profile"] = BuildProfileToken(profile);

        std::ofstream out(path);
        if (!out.is_open()) {
            std::cerr << "BuildSettings: cannot write '" << path << "'" << std::endl;
            return false;
        }
        out << root.dump(2) << "\n";
        // CHECKED, unlike ProjectSettings::Save. A stream that goes bad partway
        // through leaves a truncated file, and a truncated file here comes back
        // as a Malformed load on the next launch -- at which point the panel
        // correctly refuses to save over it and the author has lost their build
        // list to a write that reported success.
        out.flush();
        if (!out.good()) {
            std::cerr << "BuildSettings: write to '" << path
                      << "' failed partway through; the file is incomplete"
                      << std::endl;
            return false;
        }
        return true;
    }

    // -----------------------------------------------------------------------
    // The arrow to the runtime contract, and the migration into this one
    // -----------------------------------------------------------------------
    bool BuildSettings::ApplyTo(ProjectSettings& out) const {
        if (scenes.empty()) return false; // see the header: never write an empty
        out.startupScene = scenes.front();
        // MARKS THE FILE AS A BUILD MANIFEST, and this is the only place in the
        // engine that sets it. ProjectSettings::startupSceneFromBuild carries the
        // whole argument; the short version is that without it, a bundle whose
        // scenes[0] is a level is overruled at boot by TitleFrontEndScene() and
        // starts somewhere the panel did not say.
        //
        // Set HERE rather than by the pipeline, so that "write the startup scene"
        // and "say who wrote it" cannot become two steps with one of them
        // forgotten -- which is exactly the shape of the MenuUIHooks::modes bug
        // recorded at PlayerMain.cpp:451-464, where every half of a seam was
        // correct and nobody assigned one field.
        out.startupSceneFromBuild = true;
        return true;
    }

    bool BuildSettings::SeedFromProjectSettings(const ProjectSettings& legacy) {
        if (!scenes.empty()) return false;          // never reorder an authored list
        if (legacy.startupScene.empty()) return false;
        std::string why;
        // Through AddScene rather than a push_back, so a legacy value that would
        // not survive a reload (an absolute path somebody hand-edited into
        // project.json before this file existed) is refused HERE, where the
        // caller is a panel that can say so, rather than next launch as a
        // Malformed file with no history.
        return AddScene(legacy.startupScene, &why);
    }

    // -----------------------------------------------------------------------
    // The list, as the panel edits it
    // -----------------------------------------------------------------------
    bool BuildSettings::AddScene(const std::string& path, std::string* whyNot,
                                 const std::string& baseDir) {
        if (path.empty()) {
            if (whyNot) *whyNot = "the path is empty";
            return false;
        }
        if (path.size() > kMaxPathLength) {
            if (whyNot) *whyNot = "the path is longer than " +
                                  std::to_string(kMaxPathLength) + " characters";
            return false;
        }
        if (scenes.size() >= kMaxScenes) {
            if (whyNot) *whyNot = "the build already has the maximum of " +
                                  std::to_string(kMaxScenes) + " scenes";
            return false;
        }
        const std::string rel = NormalizeScenePath(path);
        if (!pathAllowed(baseDir, rel, whyNot)) return false;
        const int at = IndexOf(rel);
        if (at >= 0) {
            if (whyNot) *whyNot = "'" + rel + "' is already in the build at index " +
                                  std::to_string(at);
            return false;
        }
        scenes.push_back(rel);
        return true;
    }

    bool BuildSettings::RemoveSceneAt(std::size_t index) {
        if (index >= scenes.size()) return false;
        scenes.erase(scenes.begin() + static_cast<std::ptrdiff_t>(index));
        return true;
    }

    bool BuildSettings::MoveScene(std::size_t from, std::size_t to) {
        if (from >= scenes.size() || to >= scenes.size()) return false;
        if (from == to) return true;
        std::string moved = std::move(scenes[from]);
        scenes.erase(scenes.begin() + static_cast<std::ptrdiff_t>(from));
        // `to` is the index AFTER removal (see the header), so it is used
        // directly rather than adjusted -- which is what a drop target between
        // two rows actually means, and the off-by-one that the other reading
        // produces only shows up when dragging downwards.
        scenes.insert(scenes.begin() + static_cast<std::ptrdiff_t>(to),
                      std::move(moved));
        return true;
    }

    bool BuildSettings::SetStartupScene(const std::string& path,
                                        std::string* whyNot,
                                        const std::string& baseDir) {
        const std::string rel = NormalizeScenePath(path);
        int at = IndexOf(rel);
        if (at < 0) {
            if (!AddScene(rel, whyNot, baseDir)) return false;
            at = static_cast<int>(scenes.size()) - 1;
        }
        return MoveScene(static_cast<std::size_t>(at), 0);
    }

    int BuildSettings::IndexOf(const std::string& path) const {
        // Normalizes the ARGUMENT, so callers may pass whatever spelling they
        // happen to hold -- the editor's current scene path, an asset-browser
        // click -- without knowing this rule. The stored entries are already
        // normalized by every mutator and by Load.
        const std::string rel = NormalizeScenePath(path);
        for (std::size_t i = 0; i < scenes.size(); ++i) {
            if (scenes[i] == rel) return static_cast<int>(i);
        }
        return -1;
    }

    const std::string& BuildSettings::StartupScene() const {
        return scenes.empty() ? kEmpty : scenes.front();
    }

} // namespace MyCoreEngine
