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
        try {
            nlohmann::json root = nlohmann::json::parse(in);
            newStartupScene = root.value("startupScene", startupScene);
            newMasterVolume = std::clamp(root.value("masterVolume", masterVolume), 0.0f, 1.0f);
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
        return true;
    }

    bool ProjectSettings::Save(const std::string& path) const {
        nlohmann::json root;
        root["startupScene"] = startupScene;
        root["masterVolume"] = masterVolume;

        std::ofstream out(path);
        if (!out.is_open()) {
            std::cerr << "ProjectSettings: cannot write '" << path << "'" << std::endl;
            return false;
        }
        out << root.dump(2) << "\n";
        return true;
    }

} // namespace MyCoreEngine
