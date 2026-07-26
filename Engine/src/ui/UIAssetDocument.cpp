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

    // Drop the binding index BEFORE anything can free an element, not after.
    // The invariant is that the binder never holds a pointer into a tree that
    // is being rebuilt, not even for the length of one function.
    binder_.Clear();

    // Markup first. On failure the document is left exactly as it was (see
    // UIMarkup::LoadInto), so a broken edit never blanks a working UI — it
    // just reports and keeps running the last good version.
    if (!UIMarkup::LoadFileInto(doc_, markupPath_, errors_)) {
        for (const auto& e : errors_) std::cerr << "[UI] " << e << "\n";
        // The surviving tree still carries its own authored bindings, so
        // re-collecting restores exactly what was running. A half-typed file
        // must not silently UNBIND a working UI any more than it may blank one.
        binder_.Rebuild(doc_, ctx_, markupPath_);
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

    // AFTER the cascade, never before: a binding has to outrank the stylesheet.
    // Rebuild ends by applying every binding unconditionally, so the tree
    // carries current values before anything lays out or paints — which is why
    // a bound label never flashes empty for a frame as you save the file, and
    // why the app no longer has to re-push what it cached.
    binder_.Rebuild(doc_, ctx_, markupPath_);
    for (const auto& e : binder_.errors()) {
        errors_.push_back(e);
        std::cerr << "[UI] " << e << "\n";
    }
    for (const auto& n : binder_.notes()) std::cerr << "[UI] note: " << n << "\n";

    // Re-attach behaviour LAST, on the finished tree. Every pointer the app
    // held is dead after a rebuild, so this is the only safe place to re-cache.
    // Values are no longer its job — those live in the data source.
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
