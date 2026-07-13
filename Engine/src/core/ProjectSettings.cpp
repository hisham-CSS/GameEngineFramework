#include "ProjectSettings.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>

namespace MyCoreEngine {

    bool ProjectSettings::Load(const std::string& path) {
        std::ifstream in(path);
        if (!in.is_open()) return true; // no file yet: defaults stand

        try {
            nlohmann::json root = nlohmann::json::parse(in);
            startupScene = root.value("startupScene", startupScene);
            return true;
        }
        catch (const std::exception& e) {
            std::cerr << "ProjectSettings: failed to parse '" << path
                      << "': " << e.what() << " — using defaults" << std::endl;
            return false;
        }
    }

    bool ProjectSettings::Save(const std::string& path) const {
        nlohmann::json root;
        root["startupScene"] = startupScene;

        std::ofstream out(path);
        if (!out.is_open()) {
            std::cerr << "ProjectSettings: cannot write '" << path << "'" << std::endl;
            return false;
        }
        out << root.dump(2) << "\n";
        return true;
    }

} // namespace MyCoreEngine
