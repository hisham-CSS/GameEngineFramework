#pragma once
#include "../core/Core.h"

#include <string>

namespace MyCoreEngine {

    // Per-asset import settings, stored in a sidecar next to the asset
    // ("foo.png" -> "foo.png.import", JSON). The seam for the eventual
    // import pipeline (the editor's Inspector edits these; AssetCooker
    // enforces/applies them). Deliberately minimal today:
    //
    // - maxDimension: for textures — the largest width/height this asset
    //   should ship at. 0 = unlimited. Enforced by `AssetCooker validate`
    //   (reported as a warning when the source exceeds it); the future
    //   P4-2 texture cook will downscale/compress to it.
    //
    // Planned, NOT implemented: per-asset sRGB override, compression
    //   format, mesh import options, and GUID identity — sidecars are
    //   keyed by PATH until the GUID/reference-fixup work lands, so
    //   moving an asset means moving its .import file with it.
    //
    // Sidecars are metadata: AssetIndex hides them from the browser tree.
    struct ImportSettings {
        int maxDimension = 0; // 0 = unlimited

        bool operator==(const ImportSettings& o) const {
            return maxDimension == o.maxDimension;
        }
    };

    // Sidecar path for an asset ("Exported/tex.png" -> "Exported/tex.png.import").
    ENGINE_API std::string ImportSettingsPathFor(const std::string& assetPath);

    // Load the sidecar; missing or malformed files yield defaults (never
    // an error — an asset without settings is the normal case).
    ENGINE_API ImportSettings LoadImportSettings(const std::string& assetPath);

    // Write the sidecar (default settings still write — an explicit reset
    // is user intent). Returns false when the file can't be written.
    ENGINE_API bool SaveImportSettings(const std::string& assetPath,
                                       const ImportSettings& s);

} // namespace MyCoreEngine
