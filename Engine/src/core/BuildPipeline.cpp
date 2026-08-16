// Engine/src/core/BuildPipeline.cpp
//
// The pipeline BuildPipeline.h describes. That header carries the design and the
// contract; this file carries the decisions the contract deliberately left to
// whoever implemented it, and each one is written down where it is made rather
// than summarised here. Four of them are worth finding from the top:
//
//   * HOW THE MIRROR IS MADE TRUE -- see `PreCleanOwnedDirectory`. The contract
//     asks for a mirror ("a file in the output directory that this build did not
//     write is REMOVED"), and the obvious implementation of that -- assemble,
//     then remove whatever the assemble did not touch -- CANNOT BE WRITTEN
//     against `cmake --install`. So the mirror happens at the FRONT, by emptying
//     a directory the build owns, and everything present afterwards was written
//     by this build by construction.
//
//   * WHAT THE CAMERA CHECK READS -- see `BootSceneHasEnabledCamera`. The header
//     left the mechanism open; this picks the JSON-level check, reads exactly
//     three keys, and takes the default for the third from `CameraComponent`
//     itself so it cannot drift away from the loader.
//
//   * HOW THE MISSING PlayerDebug INSTALL RULE IS DETECTED -- see
//     `ProfileHasInstallRule`. Not hard-coded to "Shipping only": the generated
//     install script is asked, so the refusal disappears by itself on the day the
//     one-line rule lands in Player/CMakeLists.txt.
//
//   * WHICH `Exported/` THE SCENES ARE CHECKED AGAINST -- see the note above the
//     scene-list block in `RunPreflight`. Preflight can only honestly inspect the
//     directory the EDITOR is running out of; the build assembles from the
//     PROFILE's. On a single-config tree they are the same directory and the
//     question does not arise, which is the default case on this project.
#include "BuildPipeline.h"

#include "AssetManager.h"
#include "Components.h"   // CameraComponent -- for its `enabled` default only
#include "PathSandbox.h"
#include "ProjectSettings.h"
#include "SceneSerializer.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <system_error>
#include <thread>
#include <utility>

namespace MyCoreEngine {

    namespace {

        namespace fs = std::filesystem;

        // -----------------------------------------------------------------
        // The marker, and what it means
        // -----------------------------------------------------------------
        // THE ONE FACT THAT MAKES DELETING SAFE. Everything destructive in this
        // file -- the pre-clean, the prune, the rollback -- is licensed by this
        // file being present, so it is written before anything else is created
        // and removed last on the way out.
        //
        // A DOTFILE AT THE BUNDLE ROOT, and it ships. Four lines of JSON; the
        // alternative is a record kept somewhere else, and a record that can be
        // separated from the directory it describes is a record that eventually
        // authorises deleting a different one.
        const char* kMarkerFile = ".csebuild";
        const char* kMarkerMagic = "CatSplatEngine.PlayerBundle";
        constexpr int kMarkerVersion = 1;

        // Bound on a file this pipeline will PARSE while walking a tree. Scenes
        // are tens of kilobytes; this is far past any of them and short of the
        // point where scanning a bundle could be turned into an out-of-memory by
        // one large file somebody dropped into Exported/.
        //
        // ERRING THIS WAY IS THE SAFE DIRECTION, and it is worth saying which way
        // that is: a file too large to parse is judged NOT A SCENE, so the prune
        // KEEPS it. The bundle ends up a superset of what was asked for, never a
        // subset. A silent omission is the failure this file exists to prevent; a
        // silent inclusion is a larger download.
        constexpr std::uintmax_t kMaxParsedFileBytes = 64ull * 1024ull * 1024ull;

        // -----------------------------------------------------------------
        // Path helpers
        // -----------------------------------------------------------------

        // weakly_canonical without the throwing overload, and with a lexical
        // fallback: it reports an error on some platforms for a path whose parent
        // does not exist, and a build must still be able to say where it was
        // going to write when the directory is not there yet.
        bool Canonical(const std::string& in, fs::path& out) {
            if (in.empty()) return false;
            std::error_code ec;
            fs::path c = fs::weakly_canonical(fs::path(in), ec);
            if (ec || c.empty()) c = fs::path(in).lexically_normal();
            if (c.empty()) return false;
            out = c;
            return true;
        }

        bool Canonical(const fs::path& in, fs::path& out) {
            return Canonical(in.string(), out);
        }

        // Case folding for the path comparisons below, ON WINDOWS ONLY.
        //
        // PathSandbox::PathIsContained compares components case-SENSITIVELY, and
        // for the check it performs that is merely conservative: a case mismatch
        // there refuses a path it could have allowed. Here the same comparison
        // decides whether an output directory sits inside the build tree, and a
        // case mismatch would FAIL TO REFUSE -- `c:/proj/out` judged not to be
        // under `C:/proj`. The dangerous direction is the opposite one, so this
        // does not inherit that behaviour.
        std::string FoldCase(const std::string& s) {
#if defined(_WIN32)
            std::string out = s;
            for (char& c : out) {
                if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            }
            return out;
#else
            return s;
#endif
        }

        // Is `p` AT or UNDER `root`? Component-wise on absolute paths, which is
        // what stops "/proj/output-old" from being judged to be inside
        // "/proj/output" the way a plain string prefix would.
        bool IsAtOrUnder(const fs::path& root, const fs::path& p) {
            auto r = root.begin();
            auto c = p.begin();
            for (; r != root.end(); ++r, ++c) {
                if (c == p.end()) return false;
                if (FoldCase(r->string()) != FoldCase(c->string())) return false;
            }
            return true;
        }

        bool SamePath(const fs::path& a, const fs::path& b) {
            return IsAtOrUnder(a, b) && IsAtOrUnder(b, a);
        }

        bool IsDir(const fs::path& p) {
            std::error_code ec;
            return fs::is_directory(p, ec) && !ec;
        }

        bool IsFile(const fs::path& p) {
            std::error_code ec;
            return fs::is_regular_file(p, ec) && !ec;
        }

        bool DirIsEmpty(const fs::path& p) {
            std::error_code ec;
            const bool e = fs::is_empty(p, ec);
            return !ec && e;
        }

        // Extension test that does not care about case: a hand-copied ".JSON"
        // scene is still a scene, and an ".IMPORT" sidecar is still a sidecar
        // that must not reach a bundle.
        bool ExtIs(const fs::path& p, const char* dotted) {
            return FoldCase(p.extension().string()) == FoldCase(dotted);
        }

        // The spelling BuildSettings::scenes uses, for a file inside a tree:
        // relative to the tree root, forward slashes, normalized.
        //
        // THIS IS THE FUNCTION THAT SILENTLY EMPTIES A BUNDLE IF IT IS WRONG --
        // BuildPipeline.h says so in as many words. Get the spelling wrong and
        // the build list matches nothing, every scene is judged "not in the
        // build", and the prune removes all of them without one error. Hence
        // NormalizeScenePath, and never a comparison of this file's own.
        //
        // lexically_relative rather than fs::relative: purely textual, no
        // filesystem access, and both operands are already canonical here.
        std::string RelativeSpelling(const fs::path& root, const fs::path& file) {
            const fs::path rel = file.lexically_relative(root);
            if (rel.empty()) return std::string();
            return BuildSettings::NormalizeScenePath(rel.generic_string());
        }

        // How deep a path is, for removing directories child-first.
        std::ptrdiff_t Depth(const fs::path& p) {
            return std::distance(p.begin(), p.end());
        }

        // -----------------------------------------------------------------
        // Scanning a child's output for a handful of strings
        // -----------------------------------------------------------------
        // A compile log is unbounded and this only ever needs a few yes/no
        // answers about it, so it keeps a small overlap between chunks rather
        // than the whole thing. The overlap is what stops a needle that straddles
        // a pipe read from being missed -- which would be an INTERMITTENT failure
        // to explain a locked Engine.dll, i.e. the worst possible shape for that
        // particular bug.
        class ChunkScanner {
        public:
            explicit ChunkScanner(std::vector<std::string> needles)
                : needles_(std::move(needles)), hits_(needles_.size(), false) {}

            void Feed(const std::string& chunk) {
                const std::string window = tail_ + chunk;
                for (std::size_t i = 0; i < needles_.size(); ++i) {
                    if (!hits_[i] && window.find(needles_[i]) != std::string::npos) {
                        hits_[i] = true;
                    }
                }
                tail_ = window.size() > kOverlap
                            ? window.substr(window.size() - kOverlap)
                            : window;
            }

            bool Hit(std::size_t i) const { return i < hits_.size() && hits_[i]; }

        private:
            // Comfortably longer than any needle used here; one longer than this
            // could straddle two overlaps and be missed.
            static constexpr std::size_t kOverlap = 512;
            std::vector<std::string> needles_;
            std::vector<bool> hits_;
            std::string tail_;
        };

        // -----------------------------------------------------------------
        // "Is this a scene?" -- discovery, as opposed to verification
        // -----------------------------------------------------------------
        // SceneSerializer::Validate is the engine's definition and this file has
        // no opinion of its own. But Validate PRINTS -- "is not a scene file", on
        // stderr, once per file -- and discovery runs over every JSON in a tree
        // that also holds project.json, build.json and a fighting game's
        // character data. Unfiltered, "which scenes are in this bundle" becomes a
        // screenful of errors about files nobody claimed were scenes.
        //
        // So discovery asks a cheaper question first, and it is deliberately the
        // SAME question Validate's own first gate asks
        // (SceneSerializer.cpp:366-369): is the top level an object with an
        // `entities` ARRAY. Anything this rejects, Validate would reject at that
        // line -- so the filter can only remove work, never change an answer.
        //
        // A file this refuses is judged NOT A SCENE, so the prune keeps it. See
        // kMaxParsedFileBytes for why that is the safe direction.
        bool LooksLikeScene(const fs::path& file) {
            std::error_code ec;
            const std::uintmax_t size = fs::file_size(file, ec);
            if (ec || size > kMaxParsedFileBytes) return false;

            std::ifstream in(file);
            if (!in.is_open()) return false;
            nlohmann::json root;
            try {
                in >> root;
            }
            catch (const std::exception&) {
                return false; // not JSON at all
            }
            return root.is_object() && root.contains("entities") &&
                   root["entities"].is_array();
        }

        // Verification, as opposed to discovery: the engine's own probe, on a
        // file the build list NAMES. No prefilter here -- a listed scene that
        // does not load must produce Validate's message, not this file's guess.
        //
        // The AssetManager is a stack local and is never touched: the probe runs
        // with dryRun_ set, and the one call into it (SceneSerializer.cpp:562)
        // sits in the `else` of that branch. Read rather than assumed, because a
        // probe that DID import models would mean decoding every mesh in the game
        // on a thread with no GL context.
        bool SceneLoads(const fs::path& file, std::string* whyNot) {
            AssetManager scratchAssets;
            return SceneSerializer::Validate(file.string(), scratchAssets, whyNot);
        }

        // -----------------------------------------------------------------
        // The boot-scene camera check
        // -----------------------------------------------------------------
        // BuildPipeline.h left the MECHANISM open and named the two candidates.
        // This is the JSON one, and the reason is that the other one is not
        // available: a real load needs an AssetManager and would import every
        // model in the scene, and `SceneSerializer::dryRun_` -- the probe that
        // skips exactly that -- is private, reachable only through Validate and
        // CollectModelPaths, neither of which reports which components a scene
        // contains.
        //
        // SO IT READS THREE KEYS, whose writer is SceneSerializer.cpp:162-169.
        // The interesting one is the third: `enabled` is OPTIONAL in the file and
        // its absent value is CameraComponent's own default, so that default is
        // taken from the struct rather than written as a literal `true` here. A
        // second copy of it is exactly the drift that would make this check pass
        // on a scene the player then refuses to render from.
        //
        // WHY "ENABLED" IS THE WHOLE CONDITION: the director selects the highest
        // priority among ENABLED cameras (Components.h:36-39), so FindActiveCamera
        // returns null if and only if there is no enabled camera. Priority
        // decides WHICH camera, never WHETHER there is one.
        bool BootSceneHasEnabledCamera(const fs::path& file, std::string& whyNot) {
            std::error_code ec;
            const std::uintmax_t size = fs::file_size(file, ec);
            if (ec || size > kMaxParsedFileBytes) {
                whyNot = "could not be read to check for a camera";
                return false;
            }
            std::ifstream in(file);
            if (!in.is_open()) {
                whyNot = "could not be opened to check for a camera";
                return false;
            }
            nlohmann::json root;
            try {
                in >> root;
            }
            catch (const std::exception& e) {
                whyNot = std::string("is not readable JSON (") + e.what() + ")";
                return false;
            }
            if (!root.is_object() || !root.contains("entities") ||
                !root["entities"].is_array()) {
                whyNot = "is not a scene file";
                return false;
            }
            const bool enabledByDefault = CameraComponent{}.enabled;
            for (const auto& je : root["entities"]) {
                if (!je.is_object()) continue;
                if (!je.contains("camera") || !je["camera"].is_object()) continue;
                const nlohmann::json& jc = je["camera"];
                bool enabled = enabledByDefault;
                if (jc.contains("enabled") && jc["enabled"].is_boolean()) {
                    enabled = jc["enabled"].get<bool>();
                }
                if (enabled) return true;
            }
            whyNot = "contains no enabled CameraComponent, so the built game has "
                     "nothing to render from";
            return false;
        }

        // -----------------------------------------------------------------
        // Walking a tree without throwing
        // -----------------------------------------------------------------
        // Directory symlinks are NOT followed (the iterator's default), and that
        // matters for a routine whose results get deleted: a symlink into
        // somebody's home directory inside a bundle would otherwise be walked and
        // emptied. The link itself is removed; its target is not touched.
        struct TreeWalk {
            std::vector<fs::path> files;
            std::vector<fs::path> dirs;
            bool ok = false;
            std::string error;
        };

        TreeWalk WalkTree(const fs::path& root) {
            TreeWalk out;
            std::error_code ec;
            fs::recursive_directory_iterator it(
                root, fs::directory_options::skip_permission_denied, ec);
            if (ec) {
                out.error = ec.message();
                return out;
            }
            const fs::recursive_directory_iterator end;
            for (; it != end; it.increment(ec)) {
                if (ec) {
                    out.error = ec.message();
                    return out;
                }
                std::error_code sec;
                const fs::path p = it->path();
                if (it->is_directory(sec) && !sec) out.dirs.push_back(p);
                else out.files.push_back(p);
            }
            out.ok = true;
            return out;
        }

        // -----------------------------------------------------------------
        // Which preset to configure, read from the project rather than guessed
        // -----------------------------------------------------------------
        // Preflight must refuse a profile whose configuration a single-config
        // tree cannot produce, and BuildPipeline.h asks that the refusal NAME THE
        // PRESET. Compiling "x64-release" into Engine.dll would be a general
        // engine knowing one project's preset names, so the project's own
        // CMakePresets.json is asked instead. Absent or unhelpful, the message
        // falls back to naming the cache variable, which is still actionable.
        std::string SuggestPresetFor(const fs::path& sourceDir,
                                     const std::string& config) {
            const fs::path presets = sourceDir / "CMakePresets.json";
            std::error_code ec;
            const std::uintmax_t size = fs::file_size(presets, ec);
            if (ec || size > kMaxParsedFileBytes) return std::string();
            std::ifstream in(presets);
            if (!in.is_open()) return std::string();
            nlohmann::json root;
            try {
                in >> root;
            }
            catch (const std::exception&) {
                return std::string();
            }
            if (!root.is_object() || !root.contains("configurePresets")) {
                return std::string();
            }
            const nlohmann::json& arr = root["configurePresets"];
            if (!arr.is_array()) return std::string();
            for (const auto& p : arr) {
                if (!p.is_object()) continue;
                if (p.value("hidden", false)) continue;
                if (!p.contains("cacheVariables")) continue;
                const nlohmann::json& cv = p["cacheVariables"];
                if (!cv.is_object() || !cv.contains("CMAKE_BUILD_TYPE")) continue;
                const nlohmann::json& bt = cv["CMAKE_BUILD_TYPE"];
                std::string value;
                if (bt.is_string()) {
                    value = bt.get<std::string>();
                }
                else if (bt.is_object() && bt.contains("value") &&
                         bt["value"].is_string()) {
                    // The long form: { "type": "STRING", "value": "Release" }.
                    value = bt["value"].get<std::string>();
                }
                if (value == config && p.contains("name") && p["name"].is_string()) {
                    return p["name"].get<std::string>();
                }
            }
            return std::string();
        }

        // -----------------------------------------------------------------
        // Does an install rule exist for this profile's executable?
        // -----------------------------------------------------------------
        // THE FACT: `install(TARGETS PlayerShipping RUNTIME DESTINATION .)` is the
        // only player install rule in the tree (Player/CMakeLists.txt:49), so a
        // Development or Debug profile assembled by `cmake --install` produces a
        // bundle with no player executable in it. BuildPipeline.h says refuse it,
        // and refusing is right -- but refusing on a HARD-CODED "Shipping only"
        // would go on refusing after somebody added the missing line, and the
        // person who added it would have no idea why.
        //
        // So the GENERATED install script is asked. CMake writes one
        // cmake_install.cmake per directory that installs anything, each naming
        // its files by full path, so "will this build install Player.exe" has a
        // real answer sitting in the build tree. The refusal therefore disappears
        // by itself on the day the rule lands.
        //
        // THE MATCH IS ANCHORED, and on Linux it has to be: the executables are
        // extension-less there, so a bare search for "Player" also matches
        // "PlayerDebug". Both a leading separator and a trailing quote are
        // required, which is how the generated FILES argument spells it.
        //
        // UNREADABLE MEANS UNKNOWN, NOT ABSENT. When no install script can be read
        // at all -- a tree generated by something else, a layout that moved --
        // this answers Unknown and the caller falls back to the documented static
        // fact rather than refusing a Shipping build that would have worked.
        enum class InstallRuleAnswer { Present, Absent, Unknown };

        InstallRuleAnswer ProfileHasInstallRule(const fs::path& binaryDir,
                                                const std::string& exeName) {
            const std::string anchoredSlash = "/" + exeName + "\"";
            const std::string anchoredBack = "\\" + exeName + "\"";

            std::vector<fs::path> scripts;
            const fs::path top = binaryDir / "cmake_install.cmake";
            if (IsFile(top)) scripts.push_back(top);
            // One level down covers CMake's per-subdirectory scripts (Player/,
            // Engine/, ...) without walking a whole build tree, which on this
            // project means several thousand object-file directories.
            std::error_code ec;
            fs::directory_iterator it(binaryDir, ec);
            if (!ec) {
                const fs::directory_iterator end;
                for (; it != end; it.increment(ec)) {
                    if (ec) break;
                    std::error_code dec;
                    if (!it->is_directory(dec) || dec) continue;
                    const fs::path s = it->path() / "cmake_install.cmake";
                    if (IsFile(s)) scripts.push_back(s);
                }
            }
            if (scripts.empty()) return InstallRuleAnswer::Unknown;

            for (const fs::path& s : scripts) {
                std::error_code sec;
                const std::uintmax_t size = fs::file_size(s, sec);
                if (sec || size > kMaxParsedFileBytes) continue;
                std::ifstream in(s);
                if (!in.is_open()) continue;
                std::string line;
                while (std::getline(in, line)) {
                    if (line.find(anchoredSlash) != std::string::npos ||
                        line.find(anchoredBack) != std::string::npos) {
                        return InstallRuleAnswer::Present;
                    }
                }
            }
            return InstallRuleAnswer::Absent;
        }

        // -----------------------------------------------------------------
        // Everything preflight worked out, so the job does not work it out twice
        // -----------------------------------------------------------------
        struct ResolvedEnv {
            fs::path sourceDir;
            fs::path binaryDir;
            fs::path exeDir;         // the EDITOR's: the config it is running as
            fs::path profileExeDir;  // the PROFILE's: what `cmake --install` reads
            fs::path outputRoot;
            fs::path outputDir;      // absolute, contained
            std::string config;      // BuildProfileCMakeConfig
            std::string target;      // BuildProfileCMakeTarget
            std::string exeName;     // BuildPlayerExecutableName
            // The output directory does not exist yet, so the build creates it and
            // owns it outright -- which is what decides whether a rollback removes
            // the directory or merely empties it.
            bool willCreateOutputDir = false;
        };

        // The known CMake configuration directory names. Used only to decide
        // whether the leaf of `exeDir` is a configuration that can be swapped for
        // another; anything else and the pipeline refuses to guess.
        bool IsConfigDirName(const std::string& s) {
            return s == "Debug" || s == "Release" || s == "RelWithDebInfo" ||
                   s == "MinSizeRel";
        }

        void Push(std::vector<std::string>& v, std::string s) {
            v.push_back(std::move(s));
        }

        // A timestamp for the marker: enough to tell two builds apart in a
        // sentence, and no locale in it.
        std::string NowStamp() {
            const std::time_t t = std::time(nullptr);
            std::tm tmv{};
#if defined(_WIN32)
            localtime_s(&tmv, &t);
#else
            localtime_r(&t, &tmv);
#endif
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                          tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                          tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
            return std::string(buf);
        }

        // -----------------------------------------------------------------
        // Preflight
        // -----------------------------------------------------------------
        // CREATES NOTHING, DELETES NOTHING, SPAWNS NOTHING -- including no
        // `cmake --version` to find out whether cmake is on PATH, which is why a
        // missing toolchain is reported by the COMPILE phase, with the launcher's
        // own message, rather than guessed at here.
        //
        // EVERY reason, not the first: each block appends and carries on.
        BuildPreflight RunPreflight(const BuildSettings& settings,
                                    const BuildEnvironment& env,
                                    ResolvedEnv* resolvedOut) {
            BuildPreflight pf;
            ResolvedEnv R;

            R.config = BuildProfileCMakeConfig(settings.profile);
            R.target = BuildProfileCMakeTarget(settings.profile);
            R.exeName = BuildPlayerExecutableName(settings.profile);
            pf.cmakeConfig = R.config;
            pf.cmakeTarget = R.target;
            pf.playerExecutable = R.exeName;

            // ---- the host filled the struct in ---------------------------
            if (!env.launchProcess) {
                Push(pf.errors,
                     "this build has no way to start a process: "
                     "BuildEnvironment::launchProcess was not set, and the engine "
                     "deliberately does not spawn processes itself (BuildPipeline.h "
                     "says why: Engine.dll is linked by the shipped player too).");
            }
            if (env.sourceDir.empty()) {
                Push(pf.errors, "BuildEnvironment::sourceDir is empty.");
            }
            if (env.binaryDir.empty()) {
                Push(pf.errors, "BuildEnvironment::binaryDir is empty.");
            }
            if (env.exeDir.empty()) {
                Push(pf.errors, "BuildEnvironment::exeDir is empty.");
            }
            if (env.cmakeCommand.empty()) {
                Push(pf.errors,
                     "BuildEnvironment::cmakeCommand is empty; it must name a "
                     "CMake to run (\"cmake\" finds one on PATH).");
            }

            const bool haveSource = Canonical(env.sourceDir, R.sourceDir);
            const bool haveBinary = Canonical(env.binaryDir, R.binaryDir);
            const bool haveExe = Canonical(env.exeDir, R.exeDir);

            // ---- IS THERE A BUILD TREE TO BUILD FROM AT ALL ---------------
            // The case this exists for is an editor with no source tree: copied to
            // another machine, or installed from a package. It has an exeDir and
            // nothing else, and `cmake --build` on a directory that is not a build
            // tree fails with CMake's own message about a missing CMakeCache.txt
            // -- accurate, and useless to somebody who did not know their editor
            // was supposed to have one. Refusing here, before any process starts,
            // is the honest answer the brief asked for over a silent half-build.
            if (haveSource && !IsDir(R.sourceDir)) {
                Push(pf.errors,
                     "the project source directory '" + R.sourceDir.generic_string() +
                         "' does not exist, so this editor has no project to build.");
            }
            if (haveBinary) {
                if (!IsDir(R.binaryDir)) {
                    Push(pf.errors,
                         "the CMake build directory '" + R.binaryDir.generic_string() +
                             "' does not exist, so there is nothing to build the "
                             "player from.");
                }
                else if (!IsFile(R.binaryDir / "CMakeCache.txt")) {
                    Push(pf.errors,
                         "'" + R.binaryDir.generic_string() +
                             "' is not a configured CMake build tree (no "
                             "CMakeCache.txt). Building the player needs the tree "
                             "this editor was built from: configure the project "
                             "first, or point BuildEnvironment::binaryDir at a "
                             "build directory.");
                }
            }
            if (haveExe && !IsDir(R.exeDir)) {
                Push(pf.errors, "the application directory '" +
                                    R.exeDir.generic_string() + "' does not exist.");
            }

            // ---- CAN THIS TREE PRODUCE THIS PROFILE'S CONFIGURATION -------
            // The single-config case is the DEFAULT case here (CMakePresets.json
            // uses Ninja, and so does CI), so this is not the exotic branch. A
            // build that ignored it would produce a Debug player under the name
            // Player.exe and call it a Shipping build.
            if (!env.multiConfigGenerator) {
                if (env.configuredBuildType.empty()) {
                    Push(pf.errors,
                         "this is a single-configuration build tree but "
                         "BuildEnvironment::configuredBuildType is empty, so there "
                         "is no way to tell whether it can produce a " + R.config +
                             " player.");
                }
                else if (env.configuredBuildType != R.config) {
                    std::string msg =
                        "the " +
                        std::string(BuildProfileDisplayName(settings.profile)) +
                        " profile needs a " + R.config + " build, but this tree was "
                        "configured as " + env.configuredBuildType + " and a "
                        "single-configuration generator (Ninja, Makefiles) can only "
                        "produce the configuration it was configured with.";
                    const std::string preset =
                        haveSource ? SuggestPresetFor(R.sourceDir, R.config)
                                   : std::string();
                    if (!preset.empty()) {
                        msg += " Configure the '" + preset + "' preset and run the "
                               "editor from there, or choose a profile that builds " +
                               env.configuredBuildType + ".";
                    }
                    else {
                        msg += " Configure a build tree with CMAKE_BUILD_TYPE=" +
                               R.config + ", or choose a profile that builds " +
                               env.configuredBuildType + ".";
                    }
                    Push(pf.errors, std::move(msg));
                }
            }

            // ---- WHERE THE PROFILE'S BINARIES AND CONTENT LIVE ------------
            // Derived by replacing the trailing configuration directory, which is
            // the layout every target in this tree pins (Engine/CMakeLists.txt:
            // 336-349, Player/CMakeLists.txt:18-22). Reported in the preflight so
            // a wrong answer is VISIBLE before anything is copied, rather than
            // discovered as a bundle full of the wrong configuration.
            R.profileExeDir = R.exeDir;
            if (haveExe) {
                const std::string leaf = R.exeDir.filename().string();
                if (leaf == R.config) {
                    // Already the right one: the common case, and the only case on
                    // a single-config tree.
                }
                else if (IsConfigDirName(leaf)) {
                    R.profileExeDir = R.exeDir.parent_path() / R.config;
                }
                else if (env.multiConfigGenerator) {
                    // A multi-config tree whose output directory does not end in a
                    // configuration name: there is no safe way to derive the other
                    // one, and inventing an answer would assemble content out of a
                    // directory nobody writes to.
                    Push(pf.errors,
                         "cannot work out where a " + R.config + " build lands: the "
                         "application directory '" + R.exeDir.generic_string() +
                             "' does not end in a configuration name.");
                }
                // Single-config with an unusual leaf: the configuration check above
                // already established that this tree builds R.config, so exeDir IS
                // the profile's directory whatever it happens to be called.
            }
            pf.resolvedExeDir = R.profileExeDir.generic_string();

            // A CONTENT WARNING RATHER THAN A REFUSAL, and it is a real hazard:
            // editor-authored scenes are saved into the RUNNING editor's Exported/,
            // and the staging step fills each configuration's Exported/ from the
            // SOURCE roots -- never from another configuration's runtime directory.
            // So a Release bundle built from a Debug editor gets the source-tree
            // content, not what was saved five minutes ago. It does not refuse,
            // because VALIDATE inspects the assembled bundle and will name the
            // missing scene exactly; saying it here first turns that from a
            // surprise into an expectation.
            if (haveExe && !SamePath(R.exeDir, R.profileExeDir)) {
                Push(pf.warnings,
                     "this build assembles its content from '" +
                         R.profileExeDir.generic_string() +
                         "', not from the directory this editor is running in ('" +
                         R.exeDir.generic_string() +
                         "'). Scenes saved by this editor live only in its own "
                         "configuration's Exported/, so anything not also in the "
                         "project's asset sources will be missing from the bundle.");
            }

            // ---- IS THERE AN INSTALL RULE BEHIND THIS PROFILE -------------
            if (haveBinary && IsDir(R.binaryDir)) {
                const InstallRuleAnswer answer =
                    ProfileHasInstallRule(R.binaryDir, R.exeName);
                const bool refuse =
                    answer == InstallRuleAnswer::Absent ||
                    (answer == InstallRuleAnswer::Unknown &&
                     settings.profile != BuildProfile::Shipping);
                if (refuse) {
                    Push(pf.errors,
                         "the " +
                             std::string(BuildProfileDisplayName(settings.profile)) +
                             " profile has no install rule behind it, so this build "
                             "would produce a bundle with no " + R.exeName +
                             " in it. Only PlayerShipping is installed today "
                             "(Player/CMakeLists.txt:49); adding an install rule for " +
                             R.target + " there is a one-line fix, after which this "
                             "profile stops being refused.");
                }
            }

            // ---- THE SCENE LIST -------------------------------------------
            // WHICH `Exported/` THESE ARE CHECKED AGAINST is a real choice: the
            // EDITOR's (env.exeDir), not the profile's. The editor's is the tree
            // the author is looking at and the only one certainly populated right
            // now -- the profile's may not have been built yet, and on a
            // multi-config tree its Exported/ is staged by the very compile this
            // preflight runs before. On a single-config tree the two are the same
            // directory and the distinction does not exist, which is the default
            // case here. The warning above covers the case where they differ, and
            // VALIDATE checks the bundle itself.
            if (settings.scenes.empty()) {
                Push(pf.errors,
                     "the build list is empty, so there would be nothing for the "
                     "game to boot. Add at least one scene.");
            }
            else if (haveExe && IsDir(R.exeDir)) {
                for (std::size_t i = 0; i < settings.scenes.size(); ++i) {
                    const std::string& scene = settings.scenes[i];
                    fs::path full;
                    if (!PathIsContained(R.exeDir.string(), scene, full)) {
                        Push(pf.errors,
                             "scene " + std::to_string(i) + " ('" + scene +
                                 "') is absolute, names a drive/UNC root, or escapes "
                                 "the project with '..'.");
                        continue;
                    }
                    if (!IsFile(full)) {
                        // AN ERROR, NOT A WARNING, and BuildPipeline.h is explicit
                        // about why: it is a level that would not be in the game.
                        Push(pf.errors,
                             "scene " + std::to_string(i) + " ('" + scene +
                                 "') is in the build list but is not in the asset "
                                 "root ('" + R.exeDir.generic_string() + "').");
                        continue;
                    }
                    std::string why;
                    if (!SceneLoads(full, &why)) {
                        Push(pf.errors, "scene " + std::to_string(i) + " ('" + scene +
                                            "') would not load: " + why);
                    }
                }
            }

            // scenes[0] MUST BE THE TITLE'S FRONT END, when the host knows what
            // that is. This is the second of the two roads to ADR-008 decision 5's
            // invariant, and today it is the LIVE one: PlayerMain.cpp:196-200 still
            // resolves command line, then TitleFrontEndScene(), then project.json,
            // so a bundle whose scenes[0] is a level is overruled at boot whatever
            // the manifest says. Verified by reading PlayerMain, not assumed.
            //
            // NOTHING IS SAID WHEN titleFrontEndScene IS EMPTY, and that is a
            // decision rather than an omission: the editor cannot fill this field
            // in (it is not linked against the title), so a warning here would fire
            // on every build the editor ever runs and would say nothing the panel's
            // help text does not say better.
            if (!env.titleFrontEndScene.empty() && !settings.scenes.empty()) {
                const std::string want =
                    BuildSettings::NormalizeScenePath(env.titleFrontEndScene);
                if (settings.scenes.front() != want) {
                    Push(pf.errors,
                         "this build links a title whose front end is '" + want +
                             "', and the player boots that ahead of the build "
                             "manifest, so a bundle starting at '" +
                             settings.scenes.front() +
                             "' would not actually do so. Move '" + want +
                             "' to the top of the build list.");
                }
            }

            // ---- THE OUTPUT DIRECTORY -------------------------------------
            const std::string rootSpec =
                env.outputRoot.empty() ? env.sourceDir : env.outputRoot;
            const bool haveRoot = Canonical(rootSpec, R.outputRoot);
            if (!haveRoot) {
                Push(pf.errors,
                     "there is no output root to resolve the output directory "
                     "against (BuildEnvironment::outputRoot and sourceDir are both "
                     "empty).");
            }
            else if (settings.outputDirectory.empty()) {
                Push(pf.errors, "the output directory is empty.");
            }
            else {
                fs::path contained;
                if (!PathIsContained(R.outputRoot.string(), settings.outputDirectory,
                                     contained)) {
                    Push(pf.errors,
                         "the output directory '" + settings.outputDirectory +
                             "' is absolute, names a drive/UNC root, or escapes '" +
                             R.outputRoot.generic_string() +
                             "' with '..'. The build writes into this directory and "
                             "removes what it finds there, so it must stay inside "
                             "the project.");
                }
                else if (!Canonical(contained, R.outputDir)) {
                    Push(pf.errors, "the output directory could not be resolved.");
                }
                else {
                    pf.resolvedOutputDir = R.outputDir.generic_string();

                    // THE FOUR REFUSALS OF THE ASSEMBLE CONTRACT, and one of them
                    // is not spelled the way the contract spells it. "Refuse the
                    // source tree" cannot mean "refuse anywhere under
                    // CMAKE_SOURCE_DIR": the DEFAULT output directory is `Builds`
                    // under exactly that, and a Builds/ folder in the project is
                    // the Unity-shaped answer this whole feature exists to produce.
                    // What the contract protects is (a) the inputs of the next
                    // build and (b) the project itself. So:
                    //
                    //   * being AT or ABOVE the source/binary/application
                    //     directories is refused here, because emptying such a
                    //     directory deletes the project;
                    //   * being INSIDE the build tree is refused here, because
                    //     stage_runtime_assets.cmake mirrors there and would delete
                    //     the bundle, loudly, on the next build;
                    //   * being inside an ASSET ROOT (Editor/src/Exported,
                    //     Games/<title>/Assets) is refused by the MARKER rule below
                    //     and needs no separate check: those directories are full
                    //     of files and carry no marker. That is the right mechanism
                    //     for it -- the alternative is a general-purpose engine
                    //     with one project's directory layout compiled into it.
                    if (SamePath(R.outputDir, R.outputRoot)) {
                        Push(pf.errors,
                             "the output directory resolves to the project root "
                             "itself ('" + R.outputRoot.generic_string() +
                                 "'). The build empties its output directory before "
                                 "assembling, so it must be a subdirectory.");
                    }
                    if (haveSource && IsAtOrUnder(R.outputDir, R.sourceDir)) {
                        Push(pf.errors,
                             "the output directory '" +
                                 R.outputDir.generic_string() +
                                 "' contains the project source tree, and the build "
                                 "empties it before assembling.");
                    }
                    if (haveBinary && IsAtOrUnder(R.outputDir, R.binaryDir)) {
                        Push(pf.errors,
                             "the output directory '" +
                                 R.outputDir.generic_string() +
                                 "' contains the CMake build tree, and the build "
                                 "empties it before assembling.");
                    }
                    if (haveBinary && IsAtOrUnder(R.binaryDir, R.outputDir)) {
                        Push(pf.errors,
                             "the output directory '" +
                                 R.outputDir.generic_string() +
                                 "' is inside the CMake build tree, which the build "
                                 "system owns: the asset staging step mirrors that "
                                 "tree and would delete the bundle on the next "
                                 "build.");
                    }
                    if (haveExe && (IsAtOrUnder(R.exeDir, R.outputDir) ||
                                    IsAtOrUnder(R.profileExeDir, R.outputDir) ||
                                    IsAtOrUnder(R.outputDir, R.exeDir))) {
                        Push(pf.errors,
                             "the output directory '" +
                                 R.outputDir.generic_string() +
                                 "' overlaps the directory the applications and the "
                                 "staged assets live in. That is the build system's "
                                 "own directory: the bundle must not be written "
                                 "into it or around it.");
                    }

                    // ---- THE MARKER: is this directory ours to empty ------
                    std::error_code ec;
                    const bool exists = fs::exists(R.outputDir, ec) && !ec;
                    if (!exists) {
                        R.willCreateOutputDir = true;
                    }
                    else if (!IsDir(R.outputDir)) {
                        Push(pf.errors, "'" + R.outputDir.generic_string() +
                                            "' exists and is not a directory.");
                    }
                    else if (DirIsEmpty(R.outputDir)) {
                        // Empty is safe: nothing to lose, and nothing that could be
                        // mistaken for somebody else's files.
                    }
                    else if (!IsFile(R.outputDir / kMarkerFile)) {
                        Push(pf.errors,
                             "'" + R.outputDir.generic_string() +
                                 "' already contains files and no " + kMarkerFile +
                                 " from a previous build. The build REPLACES "
                                 "everything in its output directory, and it will "
                                 "only do that to a directory it created. Choose an "
                                 "empty directory, or delete this one yourself "
                                 "first.");
                    }
                    else {
                        // Ours. Say what is about to happen to it. This is the "an
                        // output directory that already existed" warning, and it is
                        // worth more than a note because the replacement is TOTAL:
                        // the previous bundle is removed BEFORE the new one is
                        // installed, so a build that fails at the install step
                        // leaves the directory empty rather than holding the old
                        // game. See PreCleanOwnedDirectory for why it is done that
                        // way round.
                        std::string when;
                        std::ifstream in(R.outputDir / kMarkerFile);
                        if (in.is_open()) {
                            try {
                                nlohmann::json m;
                                in >> m;
                                if (m.is_object() && m.contains("builtAt") &&
                                    m["builtAt"].is_string()) {
                                    when = " (" + m["builtAt"].get<std::string>() + ")";
                                }
                            }
                            catch (const std::exception&) {
                                // The marker's text is a nicety; its PRESENCE is
                                // the thing that matters and that has been checked.
                            }
                        }
                        Push(pf.warnings,
                             "'" + R.outputDir.generic_string() +
                                 "' already holds a build" + when +
                                 ". Everything in it is removed before this one is "
                                 "assembled.");
                    }

                    // ---- what this build LEAVES OUT ----------------------
                    // Requires walking the staged tree, which is why
                    // BuildPipeline.h says preflight is not free. Available BEFORE
                    // the build so the panel can show it beside the list, rather
                    // than in the report afterwards.
                    if (haveExe && IsDir(R.exeDir / "Exported")) {
                        const TreeWalk walk = WalkTree(R.exeDir / "Exported");
                        for (const fs::path& f : walk.files) {
                            if (!ExtIs(f, ".json")) continue;
                            const std::string rel = RelativeSpelling(R.exeDir, f);
                            if (rel.empty()) continue;
                            if (settings.Contains(rel)) continue;
                            if (!LooksLikeScene(f)) continue;
                            pf.scenesExcluded.push_back(rel);
                        }
                        std::sort(pf.scenesExcluded.begin(), pf.scenesExcluded.end());
                    }
                }
            }

            // ---- warnings worth having ------------------------------------
            if (settings.scenes.size() == 1) {
                Push(pf.warnings,
                     "this build contains one scene. That is what a project looks "
                     "like immediately after it moves off project.json's single "
                     "startup scene -- add the rest of the game's scenes, or the "
                     "built game can only ever be in one of them.");
            }
            // NOT REPORTED, and named here so its absence is a decision rather than
            // an oversight: BuildPipeline.h asks for a warning on "a Shipping
            // profile in a tree with no title linked". THE ENGINE CANNOT ANSWER
            // THAT. `TitleFrontEndScene()` is declared behind
            // CSE_HOST_TITLE_FRONT_END and DEFINED BY A TITLE, in whatever archive
            // the executable links (TitleFrontEnd.h), so Engine.dll cannot call it;
            // and BuildEnvironment::titleFrontEndScene being empty means "the host
            // did not say", not "there is no title". A warning derived from that
            // would fire on every build the editor ever runs and would be wrong
            // about the reason.

            pf.ok = pf.errors.empty();
            if (resolvedOut) *resolvedOut = R;
            return pf;
        }

    } // namespace

    // ---------------------------------------------------------------------
    // Names
    // ---------------------------------------------------------------------
    // DEFAULT-LESS, like every switch in BuildSettings.cpp and for the same
    // reason: Engine/CMakeLists.txt:364-368 turns an unhandled enumerator into an
    // ERROR (/we4062, -Werror=switch), so adding a phase or a result cannot
    // produce one that renders as an empty string in a panel. The return after
    // each switch is unreachable and exists only because the compiler cannot know
    // that.
    const char* BuildPhaseName(BuildPhase p) {
        switch (p) {
        case BuildPhase::Idle:       return "Idle";
        case BuildPhase::Preflight:  return "Preflight";
        case BuildPhase::Compiling:  return "Compiling";
        case BuildPhase::Assembling: return "Assembling";
        case BuildPhase::Validating: return "Validating";
        case BuildPhase::Finished:   return "Finished";
        }
        return "Idle";
    }

    const char* BuildResultName(BuildResult r) {
        switch (r) {
        case BuildResult::NotFinished:      return "NotFinished";
        case BuildResult::Succeeded:        return "Succeeded";
        case BuildResult::FailedPreflight:  return "FailedPreflight";
        case BuildResult::FailedCompile:    return "FailedCompile";
        case BuildResult::FailedAssemble:   return "FailedAssemble";
        case BuildResult::FailedValidation: return "FailedValidation";
        case BuildResult::Cancelled:        return "Cancelled";
        }
        return "NotFinished";
    }

    BuildPreflight PreflightBuild(const BuildSettings& settings,
                                  const BuildEnvironment& env) {
        return RunPreflight(settings, env, nullptr);
    }

    // =====================================================================
    // The job
    // =====================================================================
    struct BuildJob::Impl {
        BuildSettings settings;
        BuildEnvironment env;
        ResolvedEnv R;

        // ONE mutex over everything the two threads share -- progress, report and
        // the pending log. Every critical section is a few string operations long
        // and the job thread NEVER holds it across a blocking call, which is the
        // property that lets RequestCancel take it while a ReadChunk is parked in
        // a pipe.
        mutable std::mutex mtx;
        BuildProgress progress;
        BuildReport report;
        std::string pendingLog;

        std::atomic<bool> cancel{ false };
        std::thread thread;
        std::chrono::steady_clock::time_point started;

        // THE RUNNING CHILD, and the reason it is a raw pointer under the mutex
        // rather than owned here. BuildPipeline.h's calling discipline: Kill may
        // run on ANY thread while ReadChunk is blocked, and must be a no-op after
        // Wait. The job thread owns the process object as a local and publishes a
        // pointer to it here for the duration of the drain, then CLEARS THE
        // POINTER BEFORE CALLING Wait. So a Kill arriving after the drain finds
        // nothing to signal, and there is no window in which Kill and Wait are
        // both looking at the same child -- which is the use-after-reap
        // Subprocess.cpp:188-198 records this repository having already met.
        IBuildProcess* child = nullptr;

        // Set once the assemble phase has started changing the output directory,
        // so a rollback knows whether there is anything to undo.
        bool assembleTouchedDisk = false;

        // ---- talking to the panel ---------------------------------------
        void Log(const std::string& line) {
            std::lock_guard<std::mutex> lk(mtx);
            pendingLog += line;
            pendingLog += '\n';
        }

        // EVERY report mutation goes through here. The report is shared state --
        // `BuildJob::report()` is a main-thread call that can happen at any moment
        // -- and a vector<string> being appended to on one thread while another
        // reads it is not made safe by the reader taking a lock the writer did
        // not.
        template <class Fn>
        void EditReport(Fn&& fn) {
            std::lock_guard<std::mutex> lk(mtx);
            fn(report);
        }

        void SetPhase(BuildPhase phase, std::string status, float fraction) {
            std::lock_guard<std::mutex> lk(mtx);
            progress.phase = phase;
            progress.status = std::move(status);
            progress.fraction = fraction;
        }

        void SetStatus(std::string status, float fraction) {
            std::lock_guard<std::mutex> lk(mtx);
            progress.status = std::move(status);
            progress.fraction = fraction;
        }

        bool Cancelled() const { return cancel.load(std::memory_order_acquire); }

        // The single exit. `seconds` is stamped HERE rather than after the phases
        // return, so that the moment `finished` becomes true the whole report is
        // final -- a panel that polls, sees finished, and reads report() must not
        // catch a field still being written.
        void Finish(BuildResult result, std::string message, BuildPhase failedIn) {
            std::lock_guard<std::mutex> lk(mtx);
            report.result = result;
            report.message = message;
            report.failedIn = failedIn;
            report.seconds = std::chrono::duration<double>(
                                 std::chrono::steady_clock::now() - started)
                                 .count();
            progress.phase = BuildPhase::Finished;
            progress.finished = true;
            progress.fraction = (result == BuildResult::Succeeded) ? 1.0f : -1.0f;
            progress.status = std::move(message);
        }

        // ---- running a child --------------------------------------------
        // The ONLY place a process is started, so the calling discipline lives in
        // one function. Returns false when the LAUNCH failed (in which case
        // exitCode is untouched and `launchError` carries the reason).
        bool RunChild(const std::vector<std::string>& argv, ChunkScanner* scan,
                      int& exitCode, std::string& launchError) {
            // argv[0] IS A PROGRAM, NOT A TARGET NAME. BuildPipeline.h's first
            // Subprocess note: today's editor Spawn prefixes ".\\" and appends
            // ".exe", which finds AssetCooker beside the editor and can never find
            // a cmake on PATH. That is the host adapter's to fix; if it has not
            // been, the failure arrives HERE as a launch error -- which is why the
            // callers below quote argv[0] rather than relaying the launcher's text
            // on its own.
            std::unique_ptr<IBuildProcess> proc = env.launchProcess(argv, launchError);
            if (!proc) return false;

            {
                std::lock_guard<std::mutex> lk(mtx);
                child = proc.get();
            }
            // A cancel that landed between the launch and the publish above would
            // have found no child to kill. Catch it here, before parking in a read
            // that can last minutes.
            if (Cancelled()) proc->Kill();

            std::string chunk;
            for (;;) {
                chunk.clear();
                if (!proc->ReadChunk(chunk)) break; // false == end of stream
                if (scan) scan->Feed(chunk);
                std::lock_guard<std::mutex> lk(mtx);
                pendingLog += chunk;
            }
            {
                // BEFORE Wait, never after: see the member's comment.
                std::lock_guard<std::mutex> lk(mtx);
                child = nullptr;
            }
            exitCode = proc->Wait();
            return true;
        }

        // ---- the output directory ---------------------------------------

        // THE MIRROR, AND WHY IT IS A PRE-CLEAN RATHER THAN A COMPARISON.
        //
        // BuildPipeline.h asks that "a file in the output directory that this
        // build did not write is REMOVED". The natural implementation of that --
        // record what was there, assemble, remove whatever the assemble did not
        // touch -- CANNOT BE WRITTEN against `cmake --install`, and both reasons
        // were read out of the tree rather than assumed:
        //
        //   * install(FILES/DIRECTORY) SKIPS FILES THAT ARE ALREADY IDENTICAL
        //     ("-- Up-to-date:"), so modification times cannot distinguish
        //     "install deliberately left this alone" from "this is last build's
        //     leftover". A timestamp mirror would delete the first kind.
        //   * THE install(CODE) BLOCK ANNOUNCES NOTHING PER FILE.
        //     Player/CMakeLists.txt:63-81 layers the editor-authored content with
        //     file(COPY), which prints not one line -- and that block is most of
        //     Exported/. A mirror driven by parsing install's output would judge
        //     every scene in the bundle a leftover and remove it. The bundle would
        //     ship with no content and no error, which is precisely the failure
        //     this file exists to prevent.
        //
        // So the directory is EMPTIED FIRST, and everything present afterwards was
        // written by this build BY CONSTRUCTION. It is licensed by the marker:
        // preflight has already refused any directory that is non-empty and does
        // not carry one, and the check is repeated below because minutes of
        // compiling sit in between.
        //
        // THE COST IS REAL AND IS STATED RATHER THAN HIDDEN: an assemble that
        // fails leaves the directory EMPTY instead of holding the previous bundle.
        // That is why preflight warns when the directory already has a build in
        // it, why this runs AFTER the compile (the step that actually fails), and
        // why it is acceptable at all -- a bundle is a derived artifact, and
        // neither the source tree nor the staged assets are touched by any of it.
        //
        // Every removal is announced, which is the discipline
        // cmake/stage_runtime_assets.cmake:176-191 adopted after silently
        // retaining files that could not be distributed.
        bool PreCleanOwnedDirectory(std::vector<std::string>& problems) {
            std::error_code ec;
            if (R.willCreateOutputDir || !IsDir(R.outputDir)) {
                fs::create_directories(R.outputDir, ec);
                if (ec) {
                    problems.push_back("could not create '" +
                                       R.outputDir.generic_string() +
                                       "': " + ec.message());
                    return false;
                }
                assembleTouchedDisk = true;
                R.willCreateOutputDir = true; // a rollback removes it entirely
                Log("[build] created " + R.outputDir.generic_string());
                return true;
            }

            // RE-CHECKED, not trusted from preflight. This is the check standing
            // between a mistyped path and somebody's Documents folder, and the
            // compile it sits behind can take minutes.
            if (!DirIsEmpty(R.outputDir) && !IsFile(R.outputDir / kMarkerFile)) {
                problems.push_back("'" + R.outputDir.generic_string() +
                                   "' gained files while the build was compiling and "
                                   "carries no " + kMarkerFile +
                                   ", so it is no longer safe to replace.");
                return false;
            }

            assembleTouchedDisk = true;
            const TreeWalk walk = WalkTree(R.outputDir);
            if (!walk.ok) {
                problems.push_back("could not read '" + R.outputDir.generic_string() +
                                   "': " + walk.error);
                return false;
            }
            std::size_t removed = 0;
            for (const fs::path& f : walk.files) {
                // Containment PER WRITTEN PATH, as the contract asks, on top of
                // the resolution preflight already did: this loop deletes, and a
                // walk that escaped through something unexpected would be deleting
                // somewhere else.
                if (!IsAtOrUnder(R.outputDir, f)) {
                    problems.push_back("refusing to remove '" + f.generic_string() +
                                       "': it is outside the output directory.");
                    return false;
                }
                const std::string rel = RelativeSpelling(R.outputDir, f);
                std::error_code rec;
                if (!fs::remove(f, rec) || rec) {
                    problems.push_back("could not remove '" + f.generic_string() +
                                       "': " + rec.message());
                    return false;
                }
                Log("[build] REMOVING (from the previous build) " + rel);
                ++removed;
            }
            // Deepest first, so a directory is empty by the time it is reached.
            std::vector<fs::path> dirs = walk.dirs;
            std::sort(dirs.begin(), dirs.end(),
                      [](const fs::path& a, const fs::path& b) {
                          return Depth(a) > Depth(b);
                      });
            for (const fs::path& d : dirs) {
                if (!IsAtOrUnder(R.outputDir, d)) continue;
                std::error_code rec;
                fs::remove(d, rec); // a directory that is somehow not empty stays
            }
            if (removed > 0) {
                Log("[build] emptied " + R.outputDir.generic_string() + " (" +
                    std::to_string(removed) + " file(s) from the previous build)");
            }
            return true;
        }

        bool WriteMarker() {
            nlohmann::json m;
            m["tool"] = kMarkerMagic;
            m["version"] = kMarkerVersion;
            m["profile"] = BuildProfileToken(settings.profile);
            m["configuration"] = R.config;
            m["executable"] = R.exeName;
            m["builtAt"] = NowStamp();
            m["scenes"] = settings.scenes;

            std::ofstream out(R.outputDir / kMarkerFile);
            if (!out.is_open()) return false;
            out << m.dump(2) << "\n";
            out.flush();
            return out.good();
        }

        // ON FAILURE OR CANCEL the created paths are removed. After a pre-clean,
        // "the created paths" is everything in the directory -- which is the
        // ownership rule doing the work, exactly as BuildPipeline.h says: "that
        // ownership rule is what makes cancellation safe, rather than the
        // cancellation being careful".
        //
        // THE MARKER GOES LAST. If a removal fails partway, what is left behind is
        // a directory this pipeline still recognises as its own and can clean up
        // next time; dropping the marker first would strand it, and the author
        // would have to delete it by hand.
        void Rollback() {
            if (!assembleTouchedDisk) return;
            if (!IsDir(R.outputDir)) return;

            const TreeWalk walk = WalkTree(R.outputDir);
            if (!walk.ok) {
                EditReport([&](BuildReport& r) {
                    r.warnings.push_back("could not fully clean up '" +
                                         R.outputDir.generic_string() +
                                         "' after the build stopped: " + walk.error);
                });
                return;
            }
            const fs::path marker = R.outputDir / kMarkerFile;
            for (const fs::path& f : walk.files) {
                if (SamePath(f, marker)) continue;
                if (!IsAtOrUnder(R.outputDir, f)) continue;
                std::error_code ec;
                if (!fs::remove(f, ec) || ec) {
                    EditReport([&](BuildReport& r) {
                        r.warnings.push_back("could not remove '" +
                                             f.generic_string() + "': " + ec.message());
                    });
                }
            }
            std::vector<fs::path> dirs = walk.dirs;
            std::sort(dirs.begin(), dirs.end(),
                      [](const fs::path& a, const fs::path& b) {
                          return Depth(a) > Depth(b);
                      });
            for (const fs::path& d : dirs) {
                if (!IsAtOrUnder(R.outputDir, d)) continue;
                std::error_code ec;
                fs::remove(d, ec);
            }
            std::error_code ec;
            fs::remove(marker, ec);
            if (R.willCreateOutputDir) {
                fs::remove(R.outputDir, ec);
                Log("[build] removed " + R.outputDir.generic_string() +
                    " (this build created it)");
            }
            else {
                Log("[build] emptied " + R.outputDir.generic_string() +
                    " (nothing from this build was left behind)");
            }
        }

        // WHERE THE COOKER IS, NAMED IN FULL RATHER THAN LEFT AS A BARE NAME.
        // An integration decision rather than a style one, because the host's
        // launcher receives only an argv and must resolve argv[0] somehow. The
        // editor's Subprocess offers two rules -- `Spawn` pins a SIBLING of the
        // editor, `SpawnProgram` hands argv[0] to the OS -- and cmake needs the
        // second. If the cooker were passed as "AssetCooker" it would need the
        // first, so the adapter would have to switch on the program name, and a
        // one-rule adapter would be quietly wrong.
        //
        // It does not have to. This pipeline already knows where the cooker is:
        // all four applications share build/bin/<CONFIG>/ (root
        // CMakeLists.txt:377-389), which is exactly BuildEnvironment::exeDir. A
        // full path makes ONE rule correct for both spawns.
        //
        // IT MATTERS ON LINUX SPECIFICALLY. There a bare name goes through
        // posix_spawnp, which searches PATH -- and the editor's own directory is
        // not on PATH, so "AssetCooker" would simply not be found. On Windows
        // CreateProcess happens to try the calling process's directory first, so
        // the bare name would have worked there and the gap would have been a
        // Linux-only silent loss of one validation check.
        fs::path AssetCookerPath() const {
            fs::path p = R.exeDir / "AssetCooker";
#if defined(_WIN32)
            p += ".exe";
#endif
            return p;
        }

        bool Compile();
        bool Assemble();
        bool Validate();
        void Run();
    };

    // -------------------------------------------------------------------
    // COMPILE
    // -------------------------------------------------------------------
    bool BuildJob::Impl::Compile() {
        // INDETERMINATE, and honestly so: cmake reports no progress, the number
        // of translation units it will rebuild is not knowable in advance, and a
        // fake percentage stuck at 40% for two minutes is worse than a spinner.
        SetPhase(BuildPhase::Compiling,
                 "Compiling " + R.target + " (" + R.config + ")...", -1.0f);

        std::vector<std::string> argv;
        argv.push_back(env.cmakeCommand);
        argv.push_back("--build");
        argv.push_back(R.binaryDir.string());
        argv.push_back("--target");
        argv.push_back(R.target);
        // --config ONLY ON A MULTI-CONFIG TREE. On a single-config generator it
        // means nothing -- the configuration was fixed at configure time -- and
        // passing it anyway invites a reader to believe otherwise. Preflight has
        // already established that a single-config tree builds this profile's
        // configuration, so its absence here is not a gap.
        if (env.multiConfigGenerator) {
            argv.push_back("--config");
            argv.push_back(R.config);
        }
        // Logged as a readable line, but PASSED AS A VECTOR: this is the argument
        // list, not a command line, and BuildPipeline.h's second Subprocess note
        // is about a host that joins it with spaces. A checkout under a directory
        // with a space in it is where that difference becomes visible, and this
        // log line is where it will be seen.
        Log("[build] " + env.cmakeCommand + " --build " +
            R.binaryDir.generic_string() + " --target " + R.target +
            (env.multiConfigGenerator ? " --config " + R.config : std::string()));

        // THE KNOWN FAILURE THAT WILL SURPRISE PEOPLE (BuildPipeline.h's last
        // section): the editor has Engine.dll loaded, a loaded DLL cannot be
        // overwritten on Windows, and a dirty engine makes this build try to
        // relink it. The linker error names a file the author did not ask to build
        // and explains none of that, so it is matched and translated. One string
        // match, no architecture -- which is what that section asked for.
        ChunkScanner scan({ "LNK1104", "LNK1168", "Engine.dll" });

        int exitCode = 0;
        std::string launchError;
        if (!RunChild(argv, &scan, exitCode, launchError)) {
            Finish(BuildResult::FailedCompile,
                   "Could not start '" + env.cmakeCommand + "': " + launchError +
                       ". CMake must be installed and reachable by that name "
                       "(BuildEnvironment::cmakeCommand also takes an absolute "
                       "path). Nothing was created.",
                   BuildPhase::Compiling);
            return false;
        }

        if (Cancelled()) {
            // Nothing to undo: the compile writes into the BUILD tree, which this
            // pipeline does not own and never touches.
            Finish(BuildResult::Cancelled,
                   "Build cancelled while compiling. Nothing was created.",
                   BuildPhase::Compiling);
            return false;
        }

        if (exitCode != 0) {
            EditReport([&](BuildReport& r) { r.exitCode = exitCode; });
            std::string message = "Compiling " + R.target + " failed (cmake exited " +
                                  std::to_string(exitCode) +
                                  "). The compiler output is in the log.";
            const bool lockError = scan.Hit(0) || scan.Hit(1);
            if (lockError && scan.Hit(2)) {
                message = "Compiling " + R.target +
                          " failed because Engine.dll could not be written: this "
                          "editor has it loaded, and the engine's sources changed, "
                          "so the build had to relink it. Build from your IDE with "
                          "the editor closed, or point BuildEnvironment::binaryDir "
                          "at a second build tree used only for player builds.";
            }
            else if (lockError) {
                message = "Compiling " + R.target +
                          " failed because the linker could not open one of its "
                          "output files -- something has it open. The log names the "
                          "file.";
            }
            Finish(BuildResult::FailedCompile, message, BuildPhase::Compiling);
            return false;
        }
        return true;
    }

    // -------------------------------------------------------------------
    // ASSEMBLE: install, then prune, then write the manifest
    // -------------------------------------------------------------------
    bool BuildJob::Impl::Assemble() {
        SetPhase(BuildPhase::Assembling,
                 "Preparing " + settings.outputDirectory + "...", -1.0f);

        auto fail = [&](const std::string& message, std::vector<std::string> details) {
            EditReport([&](BuildReport& r) { r.errors = std::move(details); });
            Rollback();
            Finish(BuildResult::FailedAssemble, message, BuildPhase::Assembling);
        };
        auto cancelled = [&](const char* where) {
            Rollback();
            // Cancelled ALWAYS means "nothing was left on disk", which is what the
            // enum promises and what keeps the panel's story simple. The compile
            // is incremental, so the cost of that promise is seconds on the next
            // attempt rather than the whole build again.
            Finish(BuildResult::Cancelled,
                   std::string("Build cancelled ") + where +
                       ". Nothing was left in '" + R.outputDir.generic_string() +
                       "'; the compile was incremental, so building again is quick.",
                   BuildPhase::Assembling);
        };

        // ---- 0. empty the directory this build owns -------------------
        std::vector<std::string> problems;
        if (!PreCleanOwnedDirectory(problems)) {
            fail("The output directory could not be prepared.", std::move(problems));
            return false;
        }
        if (!WriteMarker()) {
            fail("Could not write " + std::string(kMarkerFile) + " into '" +
                     R.outputDir.generic_string() + "'.",
                 { "the marker is what lets a later build recognise this directory "
                   "as its own, so a build that cannot write it stops rather than "
                   "filling a directory it would not be allowed to replace." });
            return false;
        }
        if (Cancelled()) { cancelled("before installing"); return false; }

        // ---- 1. cmake --install ---------------------------------------
        // NOT A HAND-ROLLED COPIER, and BuildPipeline.h's decision 2 is the whole
        // argument: X_VCPKG_APPLOCAL_DEPS_INSTALL deploys each installed target's
        // third-party DLL closure AT INSTALL TIME, so a copier would produce a
        // bundle with a different DLL set than `cpack` produces from the same
        // tree -- two assembly paths, and the difference only visible as a loader
        // error on somebody else's machine.
        //
        // --config IS PASSED HERE EVEN ON A SINGLE-CONFIG TREE, unlike --build,
        // and the difference is not an inconsistency. It sets
        // CMAKE_INSTALL_CONFIG_NAME, which Player/CMakeLists.txt:63-81 uses to
        // find the editor-authored content in build/bin/<CONFIG>/Exported. Leaving
        // that to the install script's own fallback works today and would break
        // silently if the fallback ever changed -- and the failure mode is a
        // bundle shipping the source-tree defaults instead of the author's saved
        // scenes, which is the exact accident that block's comment was written
        // about.
        SetStatus("Installing to " + settings.outputDirectory + "...", -1.0f);
        std::vector<std::string> argv;
        argv.push_back(env.cmakeCommand);
        argv.push_back("--install");
        argv.push_back(R.binaryDir.string());
        argv.push_back("--config");
        argv.push_back(R.config);
        argv.push_back("--prefix");
        argv.push_back(R.outputDir.string());
        Log("[build] " + env.cmakeCommand + " --install " +
            R.binaryDir.generic_string() + " --config " + R.config + " --prefix " +
            R.outputDir.generic_string());

        // The install script's own warning, PROMOTED rather than left in the log.
        // Player/CMakeLists.txt:76-79 emits it when the configuration's staged
        // Exported/ is missing, and what it means is that the bundle just shipped
        // the source-tree defaults instead of what the editor saved: a packaged
        // game that ignores the author's work. Exactly the class of silent
        // omission this pipeline exists to make loud.
        ChunkScanner scan({ "No editor-authored Exported/" });

        int exitCode = 0;
        std::string launchError;
        if (!RunChild(argv, &scan, exitCode, launchError)) {
            fail("Could not start '" + env.cmakeCommand + "' to install the bundle.",
                 { launchError });
            return false;
        }
        if (Cancelled()) {
            // THE AWKWARD CANCEL, named in BuildPipeline.h: the child was part-way
            // through writing a tree. Removing what it wrote is correct precisely
            // BECAUSE the output directory is owned by the build -- there is
            // nothing in there that is not ours to delete.
            cancelled("while installing");
            return false;
        }
        if (exitCode != 0) {
            fail("Installing the bundle failed (cmake exited " +
                     std::to_string(exitCode) +
                     "). The install output is in the log.",
                 {});
            return false;
        }
        if (scan.Hit(0)) {
            EditReport([&](BuildReport& r) {
                r.warnings.push_back(
                    "CMake found no editor-authored Exported/ for " + R.config +
                    " (it looked in '" + R.profileExeDir.generic_string() +
                    "'), so the bundle carries the project's source-tree content "
                    "rather than anything the editor saved in that configuration.");
            });
        }

        // ---- 2. prune --------------------------------------------------
        // The install rules ship the WHOLE asset root -- they know nothing about a
        // scene list -- so the selection happens after, over the assembled output,
        // which is also the only tree that reflects what install actually decided.
        const TreeWalk bundle = WalkTree(R.outputDir);
        if (!bundle.ok) {
            fail("The assembled bundle could not be read back.", { bundle.error });
            return false;
        }

        // Candidates first, so the progress fraction is a real count rather than
        // an invented one. Everything else in this phase reports -1 precisely
        // because nothing else here can be counted in advance.
        std::vector<fs::path> candidates;
        for (const fs::path& f : bundle.files) {
            if (ExtIs(f, ".json") || ExtIs(f, ".import")) candidates.push_back(f);
        }

        // THE SPELLINGS THIS COMPARES AGAINST, computed once and through
        // NormalizeScenePath so they match `BuildSettings::scenes` exactly.
        const std::string kProjectSettingsRel =
            BuildSettings::NormalizeScenePath(ProjectSettings::DefaultPath());
        const std::string kBuildSettingsRel =
            BuildSettings::NormalizeScenePath(BuildSettings::DefaultPath());

        std::vector<std::string> excluded;
        std::size_t done = 0;
        for (const fs::path& f : candidates) {
            ++done;
            if (Cancelled()) { cancelled("while selecting scenes"); return false; }
            SetStatus("Removing scenes that are not in the build (" +
                          std::to_string(done) + "/" +
                          std::to_string(candidates.size()) + ")",
                      static_cast<float>(done) /
                          static_cast<float>(candidates.size()));

            if (!IsAtOrUnder(R.outputDir, f)) continue; // per written path
            const std::string rel = RelativeSpelling(R.outputDir, f);
            if (rel.empty()) continue;

            // project.json is REPLACED, not removed -- step 3.
            if (rel == kProjectSettingsRel) continue;

            std::string reason;
            if (rel == kBuildSettingsRel) {
                // build.json: editor metadata the shipped game never reads, and it
                // is installed today because the install(CODE) block copies every
                // staged file that is not *.import. The cleaner fix is one more
                // exclusion in Player/CMakeLists.txt:63-81; until that lands it is
                // removed here, and said out loud.
                reason = "editor build settings";
            }
            else if (ExtIs(f, ".import")) {
                // BOTH INSTALL RULES ALREADY EXCLUDE THESE (Player/CMakeLists.txt
                // :51, :69), so this normally removes nothing at all. It is kept as
                // the backstop for a third route into the bundle -- and unlike most
                // belt-and-braces, this one ANNOUNCES when it fires, so it can
                // never quietly become the reason a bundle differs from `cpack`'s.
                reason = "editor asset sidecar";
            }
            else if (settings.Contains(rel)) {
                continue; // in the build list: this is the point of the exercise
            }
            else if (LooksLikeScene(f)) {
                reason = "not in the build list";
                excluded.push_back(rel);
            }
            else {
                continue; // a .json that is not a scene: data the game reads
            }

            std::error_code ec;
            if (!fs::remove(f, ec) || ec) {
                fail("Could not remove '" + rel + "' from the bundle.",
                     { ec.message() });
                return false;
            }
            // ANNOUNCED, every one, into the log AND (for scenes) into the report.
            // A scene quietly missing from a bundle is the same failure the staging
            // script's `[stage] REMOVING` message exists to prevent, one layer up.
            Log("[build] REMOVING " + rel + " (" + reason + ")");
        }
        std::sort(excluded.begin(), excluded.end());
        EditReport([&](BuildReport& r) { r.scenesExcluded = excluded; });

        // ---- 3. the manifest ------------------------------------------
        // LOAD-MODIFY-SAVE rather than write-fresh, so masterVolume survives: the
        // editor's Settings > Audio wrote it and the shipped game boots at it
        // (PlayerMain.cpp:288). Read from the EDITOR's staged Exported/, because
        // that is the file the author has been editing.
        //
        // ApplyTo does BOTH halves -- startupScene <- scenes[0] AND
        // startupSceneFromBuild = true -- in one call, which is why the pipeline
        // does not set either field itself.
        SetStatus("Writing the build manifest...", -1.0f);
        ProjectSettings ps;
        const fs::path editorSettings = R.exeDir / ProjectSettings::DefaultPath();
        if (IsFile(editorSettings)) {
            if (!ps.Load(editorSettings.string())) {
                EditReport([&](BuildReport& r) {
                    r.warnings.push_back(
                        "'" + editorSettings.generic_string() +
                        "' could not be read, so the bundle's project.json carries "
                        "default settings -- the master volume among them.");
                });
            }
        }
        if (!settings.ApplyTo(ps)) {
            // Unreachable: preflight refuses an empty list. Handled anyway, because
            // the alternative to this branch is writing a project.json whose
            // startupScene is the struct's own default -- a shipped game that boots
            // the engine's backpack demo.
            fail("The build list is empty, so there is no startup scene to write.",
                 {});
            return false;
        }
        const fs::path bundleSettings = R.outputDir / ProjectSettings::DefaultPath();
        std::error_code ec;
        fs::create_directories(bundleSettings.parent_path(), ec);
        if (ec || !IsAtOrUnder(R.outputDir, bundleSettings)) {
            fail("Could not create '" + bundleSettings.generic_string() + "'.",
                 { ec ? ec.message() : std::string("it is outside the bundle") });
            return false;
        }
        if (!ps.Save(bundleSettings.string())) {
            fail("Could not write the bundle's " +
                     std::string(ProjectSettings::DefaultPath()) + ".",
                 {});
            return false;
        }
        Log("[build] wrote " + std::string(ProjectSettings::DefaultPath()) +
            " (startupScene = " + ps.startupScene + ", startupSceneFromBuild = true)");

        // ---- what is actually in the bundle ---------------------------
        const TreeWalk finalWalk = WalkTree(R.outputDir);
        std::size_t fileCount = 0;
        std::uintmax_t bytes = 0;
        if (finalWalk.ok) {
            fileCount = finalWalk.files.size();
            for (const fs::path& f : finalWalk.files) {
                std::error_code sec;
                const std::uintmax_t s = fs::file_size(f, sec);
                if (!sec) bytes += s;
            }
        }
        EditReport([&](BuildReport& r) {
            r.filesCopied = fileCount;
            r.bytesCopied = bytes;
            r.scenesIncluded = settings.scenes;
        });
        Log("[build] the bundle holds " + std::to_string(fileCount) + " file(s), " +
            std::to_string(bytes) + " bytes");
        // AND WHAT IS NOT TRIMMED, stated once per build rather than left to be
        // discovered from the size. BuildPipeline.h section 4: dropping a scene
        // removes ONE JSON FILE, not the models, textures, scripts, UI documents
        // or audio it was the only referent of. Doing better needs an asset
        // dependency graph, which is ADR-007's resolver and not this pipeline's.
        if (!excluded.empty()) {
            Log("[build] note: " + std::to_string(excluded.size()) +
                " scene(s) were left out, but their assets still ship -- dropping a "
                "scene removes the scene, not the art it used.");
        }
        return true;
    }

    // -------------------------------------------------------------------
    // VALIDATE: over the assembled output, not over the staged tree
    // -------------------------------------------------------------------
    // A BUILD IS THE ONLY MOMENT THE TOOL KNOWS WHAT WILL SHIP (ADR-008 decision
    // 9). Every check below runs against the bundle rather than the staged tree,
    // because the bundle is the artifact and the two can differ -- and the case
    // where they differ is the one preflight warned about.
    bool BuildJob::Impl::Validate() {
        SetPhase(BuildPhase::Validating, "Checking the bundle...", -1.0f);
        std::vector<std::string> failures;

        auto cancelled = [&](const char* where) {
            Rollback();
            Finish(BuildResult::Cancelled,
                   std::string("Build cancelled ") + where +
                       ". Nothing was left in '" + R.outputDir.generic_string() +
                       "'; the compile was incremental, so building again is quick.",
                   BuildPhase::Validating);
        };

        // 1. THE PLAYER IS IN THERE. This is the backstop behind preflight's
        //    install-rule refusal, and it is what makes that refusal safe to
        //    relax: whatever the generated install script suggested, a bundle
        //    with no executable is not a build.
        if (!IsFile(R.outputDir / R.exeName)) {
            failures.push_back(
                "the bundle has no " + R.exeName +
                " in it. Only PlayerShipping has an install rule today "
                "(Player/CMakeLists.txt:49), so a " + R.target +
                " build assembles without its executable.");
        }

        // 2. EVERY LISTED SCENE IS PRESENT AND LOADS, IN THE BUNDLE.
        for (std::size_t i = 0; i < settings.scenes.size(); ++i) {
            if (Cancelled()) { cancelled("while checking the bundle"); return false; }
            SetStatus("Checking scenes (" + std::to_string(i + 1) + "/" +
                          std::to_string(settings.scenes.size()) + ")",
                      static_cast<float>(i + 1) /
                          static_cast<float>(settings.scenes.size()));

            const fs::path inBundle = R.outputDir / settings.scenes[i];
            if (!IsAtOrUnder(R.outputDir, inBundle)) {
                failures.push_back("scene " + std::to_string(i) + " ('" +
                                   settings.scenes[i] +
                                   "') does not resolve inside the bundle.");
                continue;
            }
            if (!IsFile(inBundle)) {
                failures.push_back(
                    "scene " + std::to_string(i) + " ('" + settings.scenes[i] +
                    "') is in the build list but did not reach the bundle.");
                continue;
            }
            std::string why;
            if (!SceneLoads(inBundle, &why)) {
                failures.push_back("scene " + std::to_string(i) + " ('" +
                                   settings.scenes[i] +
                                   "') is in the bundle but would not load: " + why);
            }
        }

        // 3. THE BOOT SCENE HAS SOMETHING TO RENDER FROM. Exactly the failure
        //    PlayerMain.cpp:624-634 reports at runtime, whose comment records that
        //    falling back to a free-fly camera "looks exactly like the game ignored
        //    my camera". Catching it here is the difference between a message in
        //    the editor and a bug report.
        if (!settings.scenes.empty()) {
            const fs::path boot = R.outputDir / settings.scenes.front();
            if (IsFile(boot)) {
                std::string why;
                if (!BootSceneHasEnabledCamera(boot, why)) {
                    failures.push_back("the boot scene '" + settings.scenes.front() +
                                       "' " + why + ".");
                }
            }
        }

        // 4. THE ASSETS COOK CLEAN. Reuses the editor's protocol rather than
        //    inventing one -- 0 clean, 1 errors found, 2 bad usage
        //    (Cooker/src/AssetCooker.cpp) -- through the same launcher as the
        //    compile.
        //
        //    A WARNING, NOT A FAILURE, WHEN THE COOKER CANNOT RUN, and the
        //    distinction is between "the artifact is wrong" and "one of four
        //    checks could not be run". The bundle exists and the other checks
        //    passed; failing a build because a sibling tool is missing would fail
        //    it for a reason that is not about the build. Every such case is
        //    STATED in the report, so a build that was only three-quarters checked
        //    never claims otherwise.
        if (!Cancelled()) {
            SetStatus("Validating the bundle's assets...", -1.0f);
            const fs::path assetRoot = R.outputDir / "Exported";
            if (!IsDir(assetRoot)) {
                failures.push_back("the bundle has no Exported/ directory, so it "
                                   "contains no content at all.");
            }
            else if (!IsFile(AssetCookerPath())) {
                EditReport([&](BuildReport& r) {
                    r.warnings.push_back(
                        "the bundle's assets were NOT checked: there is no "
                        "AssetCooker beside the editor at '" +
                        AssetCookerPath().generic_string() + "'.");
                });
            }
            else {
                std::vector<std::string> argv;
                argv.push_back(AssetCookerPath().string());
                argv.push_back("validate");
                argv.push_back(assetRoot.string());
                Log("[build] " + AssetCookerPath().generic_string() + " validate " +
                    assetRoot.generic_string());

                int exitCode = 0;
                std::string launchError;
                if (!RunChild(argv, nullptr, exitCode, launchError)) {
                    EditReport([&](BuildReport& r) {
                        r.warnings.push_back(
                            "the bundle's assets were NOT checked: AssetCooker could "
                            "not be started (" + launchError + ").");
                    });
                }
                else if (Cancelled()) {
                    cancelled("while checking the bundle's assets");
                    return false;
                }
                else if (exitCode == 1) {
                    failures.push_back("AssetCooker found errors in the bundle's "
                                       "assets; its report is in the log.");
                }
                else if (exitCode != 0) {
                    EditReport([&](BuildReport& r) {
                        r.warnings.push_back(
                            "AssetCooker exited " + std::to_string(exitCode) +
                            " (neither clean nor a findings report), so the bundle's "
                            "assets were NOT checked.");
                    });
                }
            }
        }

        // THE LAST CANCELLATION POINT, and it is not redundant. Every check above
        // tests the flag before doing its work, so a cancel arriving in the gap
        // between the final scene and the cooker -- or while the cooker block was
        // being skipped for a cancel it had already seen -- would fall straight
        // through to the success return below. A build reported as Succeeded
        // after the author pressed Cancel is the one outcome worse than either.
        if (Cancelled()) { cancelled("while checking the bundle"); return false; }

        if (!failures.empty()) {
            EditReport([&](BuildReport& r) { r.errors = failures; });
            // THE BUNDLE IS LEFT ON DISK -- no Rollback() here, unlike every other
            // failure, and deliberately: a validation failure is a statement about
            // CONTENT (a boot scene with no camera) rather than a broken artifact,
            // and the author needs to look at what was produced to understand it.
            // See BuildResult::FailedValidation.
            Finish(BuildResult::FailedValidation,
                   "The bundle was assembled but did not pass its checks (" +
                       std::to_string(failures.size()) +
                       (failures.size() == 1 ? " problem" : " problems") +
                       "). It was LEFT in '" + R.outputDir.generic_string() +
                       "' so you can look at it.",
                   BuildPhase::Validating);
            return false;
        }
        return true;
    }

    // -------------------------------------------------------------------
    // The thread body
    // -------------------------------------------------------------------
    void BuildJob::Impl::Run() {
        // Each phase Finishes the job itself on failure, so this only has to name
        // the success. That keeps the failure message next to the thing that
        // failed rather than reconstructed from a status code here.
        if (!Compile()) return;
        if (!Assemble()) return;
        if (!Validate()) return;

        std::size_t files = 0;
        std::size_t leftOut = 0;
        {
            std::lock_guard<std::mutex> lk(mtx);
            files = report.filesCopied;
            leftOut = report.scenesExcluded.size();
        }
        std::string message =
            "Built " + std::string(BuildProfileDisplayName(settings.profile)) +
            " into '" + R.outputDir.generic_string() + "': " +
            std::to_string(files) + " files, " +
            std::to_string(settings.scenes.size()) +
            (settings.scenes.size() == 1 ? " scene" : " scenes");
        if (leftOut > 0) {
            message += ", " + std::to_string(leftOut) + " left out";
        }
        message += ".";
        Finish(BuildResult::Succeeded, std::move(message), BuildPhase::Idle);
    }

    // -------------------------------------------------------------------
    // The public surface -- MAIN THREAD ONLY, every one of them
    // -------------------------------------------------------------------
    BuildJob::BuildJob() : impl_(new Impl()) {}

    BuildJob::~BuildJob() {
        if (impl_->thread.joinable()) {
            // A destructor that blocked for the rest of a two-minute compile would
            // hang the editor's shutdown. The editor's own hung-child handling
            // already establishes the shape (EditorApplication::cancelValidate_):
            // kill, then join.
            RequestCancel();
            impl_->thread.join();
        }
    }

    std::unique_ptr<BuildJob> BuildJob::Start(BuildSettings settings,
                                              BuildEnvironment env) {
        std::unique_ptr<BuildJob> job(new BuildJob());
        Impl* impl = job->impl_.get();
        impl->settings = std::move(settings);
        impl->env = std::move(env);
        impl->started = std::chrono::steady_clock::now();
        impl->progress.phase = BuildPhase::Preflight;
        impl->progress.status = "Checking the build settings...";
        impl->progress.fraction = -1.0f;

        // RE-RUN, never trusting the panel's cached result: the settings or the
        // disk may have moved since it last asked, and this is the last moment
        // before a process starts.
        const BuildPreflight pf = RunPreflight(impl->settings, impl->env, &impl->R);
        impl->report.outputDirectory = pf.resolvedOutputDir;
        impl->report.warnings = pf.warnings;
        impl->report.scenesExcluded = pf.scenesExcluded;

        for (const std::string& w : pf.warnings) impl->Log("[build] warning: " + w);

        if (!pf.ok) {
            for (const std::string& e : pf.errors) impl->Log("[build] error: " + e);
            impl->report.errors = pf.errors;
            // AN ALREADY-FINISHED JOB, not a null return: the panel gets ONE code
            // path for "how did the build go" instead of two. Nothing was created,
            // and the phases below never run.
            impl->Finish(BuildResult::FailedPreflight,
                         pf.errors.size() == 1
                             ? pf.errors.front()
                             : "The build settings are not ready (" +
                                   std::to_string(pf.errors.size()) +
                                   " problems). Nothing was created.",
                         BuildPhase::Preflight);
            return job;
        }

        // ONE DEDICATED THREAD, and it is the same thread that drains the pipe.
        // Not the JobSystem: its pool is hardware_concurrency/3 clamped to [1,4]
        // and exists for asset decodes, it has no cancellation at all, and a build
        // occupies a thread for minutes (JobSystem.h:55-66). BuildPipeline.h
        // carries the full argument.
        //
        // The lambda captures the Impl POINTER, not the reference above: capturing
        // a reference by reference would leave the thread reading a local that
        // Start is about to destroy.
        impl->thread = std::thread([impl] { impl->Run(); });
        return job;
    }

    BuildProgress BuildJob::Poll(std::string* appendLog) {
        std::lock_guard<std::mutex> lk(impl_->mtx);
        if (appendLog && !impl_->pendingLog.empty()) {
            // APPENDED, so the panel owns the accumulated buffer: this hands back
            // only what is new and never a growing string copied once a frame.
            appendLog->append(impl_->pendingLog);
            impl_->pendingLog.clear();
        }
        return impl_->progress;
    }

    void BuildJob::RequestCancel() {
        std::lock_guard<std::mutex> lk(impl_->mtx);
        impl_->cancel.store(true, std::memory_order_release);
        // Killing the child EOFs the pipe, so the job thread's blocked ReadChunk
        // returns and it notices the flag. Safe from here while that read is in
        // flight -- and a no-op once the job thread has cleared the pointer in
        // preparation for Wait, which is the whole reason the pointer is cleared
        // under this same mutex.
        if (impl_->child) impl_->child->Kill();
    }

    bool BuildJob::finished() const {
        std::lock_guard<std::mutex> lk(impl_->mtx);
        return impl_->progress.finished;
    }

    const BuildReport& BuildJob::report() const {
        // The lock is taken for the memory barrier rather than for a copy: once
        // `finished` is true the job thread has stopped writing, and this is
        // documented as a main-thread call.
        std::lock_guard<std::mutex> lk(impl_->mtx);
        return impl_->report;
    }

} // namespace MyCoreEngine
