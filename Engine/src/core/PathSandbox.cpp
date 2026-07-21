#include "PathSandbox.h"

#include <system_error>

namespace MyCoreEngine {

    bool PathIsContained(const std::string& baseDir, const std::string& rel,
                         std::filesystem::path& outFull) {
        namespace fs = std::filesystem;
        const fs::path relp(rel);
        // No absolute paths, no drive/UNC roots.
        if (relp.is_absolute() || relp.has_root_name() || relp.has_root_directory())
            return false;
        // No ".." components (a plain lexical check, before any filesystem
        // access, so a symlink cannot be used to slip past canonicalization).
        for (const auto& part : relp)
            if (part == "..") return false;

        std::error_code ec;
        const fs::path base = baseDir.empty() ? fs::current_path(ec)
                                              : fs::path(baseDir);
        const fs::path joined = base / relp;
        // weakly_canonical resolves any surviving . or symlink and does not
        // require the file to exist yet.
        const fs::path canon = fs::weakly_canonical(joined, ec);
        if (ec) return false;
        const fs::path canonBase = fs::weakly_canonical(base, ec);
        if (ec) return false;

        // Prefix check on the normalized component sequences.
        auto b = canonBase.begin();
        auto c = canon.begin();
        for (; b != canonBase.end(); ++b, ++c) {
            if (c == canon.end() || *c != *b) return false;
        }
        outFull = canon;
        return true;
    }

} // namespace MyCoreEngine
