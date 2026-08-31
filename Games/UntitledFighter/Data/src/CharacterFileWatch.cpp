#include "cse/data/CharacterFileWatch.h"

// The same one-file compile CharacterData.cpp explains: PathSandbox.h pulls in
// nothing but Core.h, <filesystem> and <string>, so the containment gate is
// shared with the loader without linking the Engine.
#include "PathSandbox.h"

#include <filesystem>
#include <system_error>

namespace cse::data {

bool CharacterFileWatch::Bind(const std::string& baseDir,
                              const std::string& relPath, std::string& error) {
    // The base is ABSOLUTIZED before the containment check, and that is a
    // measured workaround rather than tidiness: MSVC's weakly_canonical hands
    // a RELATIVE path back for a target that does not exist yet, so
    // PathIsContained's prefix check refuses every missing file when the base
    // is relative -- and a missing file is precisely the state this class must
    // bind in (the content that did not stage, the file about to appear).
    // libstdc++ absolutizes either way, so without this the behaviour would
    // differ by toolchain.
    std::error_code ec;
    const std::filesystem::path absBase =
        std::filesystem::absolute(baseDir.empty() ? "." : baseDir, ec);
    std::filesystem::path full;
    if (ec || !MyCoreEngine::PathIsContained(absBase.string(), relPath, full)) {
        error = relPath + ": path: refused, because it is absolute, carries a "
                          "drive/UNC root, or contains a `..` component that "
                          "would escape the character directory";
        bound_ = false;
        full_.clear();
        return false;
    }
    full_   = full.string();
    bound_  = true;
    accum_  = 0.0f;
    stamp_  = stampNow_();
    error.clear();
    return true;
}

CharacterFileWatch::Stamp CharacterFileWatch::stampNow_() const {
    // Errors (usually: the file does not exist yet) leave the default zero
    // stamp, so a missing file has ONE stamp value and its appearance is a
    // change -- the recovery the header promises. Never an exception: this
    // runs inside the host's fixed tick, where a throw for a file mid-save
    // would take the whole match down to report a transient.
    Stamp s;
    if (full_.empty()) return s;
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(full_, ec);
    if (!ec) s.writeTime = static_cast<long long>(t.time_since_epoch().count());
    const auto sz = std::filesystem::file_size(full_, ec);
    if (!ec) s.size = static_cast<unsigned long long>(sz);
    return s;
}

bool CharacterFileWatch::Update(float dt) {
    if (!bound_) return false;
    accum_ += dt;
    if (accum_ < pollInterval_) return false;
    accum_ = 0.0f;

    const Stamp now = stampNow_();
    if (now == stamp_) return false;
    stamp_ = now;
    return true;
}

} // namespace cse::data
