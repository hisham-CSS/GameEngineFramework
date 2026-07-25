#include "UIAssetDocument.h"

#include "UIMarkup.h"

#include <filesystem>
#include <iostream>
#include <system_error>

namespace MyCoreEngine::ui {

UIAssetDocument::Stamp UIAssetDocument::stampOf(const std::string& path) {
    Stamp s;
    if (path.empty()) return s;
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(path, ec);
    if (!ec) s.writeTime = (long long)t.time_since_epoch().count();
    const auto sz = std::filesystem::file_size(path, ec);
    // Size as well as timestamp: some filesystems have coarse mtime
    // granularity, so two edits within the same tick would otherwise look
    // identical — precisely what happens when you save twice while iterating.
    if (!ec) s.size = (unsigned long long)sz;
    return s;
}

bool UIAssetDocument::Load(const std::string& markupPath, const std::string& stylePath,
                           BindFn bind) {
    markupPath_ = markupPath;
    stylePath_ = stylePath;
    bind_ = std::move(bind);
    loaded_ = false;
    return Reload();
}

bool UIAssetDocument::Reload() {
    errors_.clear();

    // Markup first. On failure the document is left exactly as it was (see
    // UIMarkup::LoadInto), so a broken edit never blanks a working UI — it
    // just reports and keeps running the last good version.
    if (!UIMarkup::LoadFileInto(doc_, markupPath_, errors_)) {
        for (const auto& e : errors_) std::cerr << "[UI] " << e << "\n";
        // Still refresh the stamps: without this a file that fails to parse is
        // re-read every poll, spamming the log until it is fixed.
        markupStamp_ = stampOf(markupPath_);
        styleStamp_ = stampOf(stylePath_);
        return false;
    }

    // A broken STYLESHEET is deliberately non-fatal: structure without styling
    // is far more useful to look at than nothing, and UIStyleSheet keeps its
    // previous rules on a failed parse, so the last good styling stays applied.
    if (!stylePath_.empty()) {
        if (!sheet_.LoadFromFile(stylePath_)) {
            for (const auto& e : sheet_.errors()) {
                errors_.push_back(e);
                std::cerr << "[UI] " << e << "\n";
            }
        }
    }
    sheet_.ApplyTo(doc_.root());

    markupStamp_ = stampOf(markupPath_);
    styleStamp_ = stampOf(stylePath_);
    loaded_ = true;

    // Re-attach behaviour LAST, on the finished tree. Every pointer the app
    // held is dead after a rebuild, so this is the only safe place to re-cache.
    if (bind_) bind_(doc_);
    return true;
}

bool UIAssetDocument::Update(float dt) {
    if (!hotReload_ || markupPath_.empty()) return false;

    pollAccum_ += dt;
    if (pollAccum_ < pollInterval_) return false;
    pollAccum_ = 0.0f;

    const Stamp m = stampOf(markupPath_);
    const Stamp s = stampOf(stylePath_);
    if (m != markupStamp_ || s != styleStamp_) {
        std::cout << "[UI] assets changed - reloading\n";
        return Reload();
    }
    return false;
}

} // namespace MyCoreEngine::ui
