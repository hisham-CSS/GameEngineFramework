#pragma once
#include "../core/Core.h"

#include <string>
#include <vector>

namespace MyCoreEngine {

    class JobSystem;

    // Asset validation (the first AssetCooker operation): walk the asset
    // tree and report everything that would load wrong or wastefully at
    // runtime. Model decodes run in PARALLEL on the given JobSystem's
    // workers; the caller's thread pumps until done.
    //
    // Checks today:
    // - models that fail to import (ERR)
    // - models referencing texture files that don't decode (WARN)
    // - models where no LOD level simplified (WARN — usually the OBJ
    //   disconnected-vertex issue: the mesh pays full cost at every
    //   distance)
    // - textures exceeding their ImportSettings maxDimension (WARN)
    //
    // IMPORTANT: pass a DEDICATED JobSystem (the cooker's or a test's) —
    // this function pumps completions itself, which must not race an
    // application main loop pumping the same pool. The editor never calls
    // this in-process; it spawns `AssetCooker validate` instead (crash
    // isolation: a hostile asset takes down the cooker, not the editor).
    struct AssetValidationIssue {
        enum class Level { Warn, Err };
        Level level = Level::Warn;
        std::string path;    // engine-style asset path
        std::string message;
    };

    struct AssetValidationReport {
        int modelsChecked = 0;
        int texturesChecked = 0;
        std::vector<AssetValidationIssue> issues;
        int errorCount() const {
            int n = 0;
            for (const auto& i : issues)
                if (i.level == AssetValidationIssue::Level::Err) ++n;
            return n;
        }
    };

    ENGINE_API AssetValidationReport ValidateAssetTree(const std::string& root,
                                                       JobSystem& jobs);

} // namespace MyCoreEngine
