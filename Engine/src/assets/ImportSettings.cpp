#include "ImportSettings.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

using json = nlohmann::json;

namespace MyCoreEngine {

    std::string ImportSettingsPathFor(const std::string& assetPath)
    {
        return assetPath + ".import";
    }

    ImportSettings LoadImportSettings(const std::string& assetPath)
    {
        ImportSettings s;
        std::ifstream in(ImportSettingsPathFor(assetPath));
        if (!in) return s; // no sidecar: defaults

        try {
            json j;
            in >> j;
            if (j.is_object()) {
                s.maxDimension = std::max(0, j.value("maxDimension", 0));
            }
        }
        catch (const json::exception&) {
            // malformed sidecar: defaults, never fatal (hand-edited files)
        }
        return s;
    }

    bool SaveImportSettings(const std::string& assetPath, const ImportSettings& s)
    {
        json j;
        j["maxDimension"] = s.maxDimension;
        std::ofstream out(ImportSettingsPathFor(assetPath));
        if (!out) return false;
        out << j.dump(2) << "\n";
        // close BEFORE checking: the write is buffered, and a flush failure
        // (disk full, OneDrive lock) only surfaces at close
        out.close();
        return !out.fail();
    }

} // namespace MyCoreEngine
