#include "ProjectSettings.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <iostream>

namespace MyCoreEngine {

    bool ProjectSettings::Load(const std::string& path) {
        std::ifstream in(path);
        if (!in.is_open()) return true; // no file yet: defaults stand

        // Convert into LOCALS and commit only once every field has converted.
        // Assigning straight into *this left a HALF-POPULATED object behind on
        // failure, and the failure is easy to reach: nlohmann's
        // value(key, default) returns the default only when the key is ABSENT.
        // A key that is present but wrong-typed goes through get<T>() and
        // throws. So a well-formed file whose masterVolume was hand-edited to
        // "loud" applied the new startupScene, threw on the next line, and
        // returned false -- the caller saw "defaults stand" while the object
        // held one field from the file and one from the defaults.
        std::string newStartupScene = startupScene;
        float       newMasterVolume = masterVolume;
        bool        newFromBuild = startupSceneFromBuild;
        try {
            nlohmann::json root = nlohmann::json::parse(in);
            newStartupScene = root.value("startupScene", startupScene);
            newMasterVolume = std::clamp(root.value("masterVolume", masterVolume), 0.0f, 1.0f);
            // ABSENT MEANS FALSE, which is what makes this backward compatible
            // with every project.json already sitting in somebody's build tree:
            // a file written before this field existed is a preference, and a
            // preference is exactly what those files are.
            newFromBuild = root.value("startupSceneFromBuild", startupSceneFromBuild);
        }
        catch (const std::exception& e) {
            // Not necessarily a PARSE failure -- a type error is just as
            // likely, and saying "parse" sends the reader hunting for a
            // missing brace that is not there.
            std::cerr << "ProjectSettings: could not read '" << path
                      << "': " << e.what() << " — using defaults" << std::endl;
            return false;
        }
        startupScene = std::move(newStartupScene);
        masterVolume = newMasterVolume;
        startupSceneFromBuild = newFromBuild;
        return true;
    }

    bool ProjectSettings::Save(const std::string& path) const {
        nlohmann::json root;
        root["startupScene"] = startupScene;
        root["masterVolume"] = masterVolume;
        // ALWAYS WRITTEN, including when false. This is what makes the flag
        // survive the running game's load-modify-save of its own settings
        // (MenuUIContent.cpp:200-207): omitting it when false would be harmless,
        // but omitting it conditionally is one edit away from omitting it when
        // true, and that edit turns a bundle's boot scene back into the title's
        // front end the first time a player touches the volume.
        root["startupSceneFromBuild"] = startupSceneFromBuild;

        std::ofstream out(path);
        if (!out.is_open()) {
            std::cerr << "ProjectSettings: cannot write '" << path << "'" << std::endl;
            return false;
        }
        out << root.dump(2) << "\n";
        return true;
    }

} // namespace MyCoreEngine
