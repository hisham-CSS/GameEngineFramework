#pragma once
#include "Core.h"

#include <filesystem>
#include <string>

namespace MyCoreEngine {

    // Rejects an untrusted relative path that would escape `baseDir`.
    //
    // Scene and script files are authored content that may be hostile: the
    // asset/script paths they carry must never be able to point a loader
    // (Assimp, stbi, the Lua reader) at a file outside the project. This
    // matters most where the loader has parser CVE history -- Assimp's mesh
    // importers have shipped heap-overflow bugs on malformed input -- so
    // containment is a memory-safety boundary, not merely info disclosure.
    //
    // std::filesystem::path's operator/ REPLACES the base entirely when the
    // right-hand side is absolute or carries a drive/UNC root, so "C:/Windows/x",
    // "//host/share/x" and "../../secret" would otherwise resolve well outside
    // the asset tree even though they were "joined" onto it.
    //
    // Returns false (leaving outFull untouched) for:
    //   - absolute paths, drive/UNC root names, or a root directory
    //   - any ".." component -- a purely lexical check performed BEFORE any
    //     filesystem access, so a symlink cannot be used to slip past
    //     canonicalization
    //   - a canonical result that does not stay under baseDir
    //
    // On success returns true and sets outFull to the weakly-canonical path.
    // `baseDir` defaults to the current working directory when empty (matching
    // how the loaders resolve project-relative paths). The target need not
    // exist yet -- weakly_canonical resolves any surviving "." or symlink
    // components without requiring the file to be present.
    ENGINE_API bool PathIsContained(const std::string& baseDir,
                                    const std::string& rel,
                                    std::filesystem::path& outFull);

} // namespace MyCoreEngine
