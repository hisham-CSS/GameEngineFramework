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
    // A missing file must bind -- the content that did not stage, the file
    // about to appear. PathIsContained absolutizes its own base, so a
    // nonexistent target under a relative base is contained, not refused;
    // the measured MSVC weakly_canonical account is at PathSandbox.cpp.
    std::filesystem::path full;
    if (!MyCoreEngine::PathIsContained(baseDir, relPath, full)) {
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
