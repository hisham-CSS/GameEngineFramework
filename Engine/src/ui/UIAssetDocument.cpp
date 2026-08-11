#include "UIAssetDocument.h"

#include "UIMarkup.h"

#include <algorithm>
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
    styler_.Clear();

    // Markup first. On failure the document is left exactly as it was (see
    // UIMarkup::LoadInto), so a broken edit never blanks a working UI — it
    // just reports and keeps running the last good version.
    std::vector<UIRepeatSpec> specs;
    std::vector<UITabSpec> tabSpecs;
    if (!UIMarkup::LoadFileInto(doc_, markupPath_, errors_, &specs, &tabSpecs)) {
        for (const auto& e : errors_) std::cerr << "[UI] " << e << "\n";
        // The surviving tree still carries its own authored bindings, so
        // re-collecting restores exactly what was running. A half-typed file
        // must not silently UNBIND a working UI any more than it may blank one.
        //
        // The live pools are deliberately NOT torn down here. The old clones
        // are still in that tree, still bound to the old slot names, and the
        // re-collect below is about to resolve them again.
        binder_.Rebuild(doc_, ctx_, markupPath_, &sheet_);
        styler_.Rebuild(doc_, sheet_, &binder_);
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

    // Pools are rebuilt from the specs this load produced, and the OLD
    // registrations go first: Teardown unregisters by the names it actually
    // registered, which is the only list that survives an author deleting the
    // repeat outright.
    for (auto& p : pools_) p->Teardown(ctx_);
    pools_.clear();
    for (const auto& spec : specs) {
        pools_.push_back(std::make_unique<UIRepeatPool>());
        pools_.back()->Build(spec, ctx_, errors_, markupPath_);
    }
    // Registered every load rather than once at construction: a re-register
    // under the same name is a replace, and this way a document that never had
    // a TabView still carries the source, so markup that grows one on a hot
    // reload resolves without waiting for the context revision to move.
    ctx_.RegisterSource(kTabSourceName, &tabSrc_);
    tabs_.clear();
    for (const auto& ts : tabSpecs) {
        tabs_.push_back(std::make_unique<UITabView>());
        // The binder is what re-cascades a header when its selected class
        // flips: AddClass alone only records the class.
        tabs_.back()->Build(ts, doc_, tabSrc_, ctx_, errors_, markupPath_, &binder_);
    }

    // Once, before the binder resolves, so Rebuild's force-apply sees real rows
    // and the right panel — otherwise every panel is up for frame one.
    UpdateRepeats();
    UpdateTabs();

    // AFTER the cascade, never before: a binding has to outrank the stylesheet.
    // Rebuild ends by applying every binding unconditionally, so the tree
    // carries current values before anything lays out or paints — which is why
    // a bound label never flashes empty for a frame as you save the file, and
    // why the app no longer has to re-push what it cached.
    binder_.Rebuild(doc_, ctx_, markupPath_, &sheet_);
    // Deduplicated, exactly like DrainBinderDiagnostics. A repeat multiplies
    // its template by the pool size, so one typo in one authored line becomes
    // one identical error per clone; printing 64 copies would bury every other
    // diagnostic in the file.
    for (const auto& e : binder_.errors()) {
        if (std::find(errors_.begin(), errors_.end(), e) != errors_.end()) continue;
        errors_.push_back(e);
        std::cerr << "[UI] " << e << "\n";
    }
    for (const auto& n : binder_.notes()) std::cerr << "[UI] note: " << n << "\n";
    // Consumed here, not left set: the load path has just reported these
    // itself, and the frame after a reload would otherwise drain and print the
    // very same list a second time.
    binder_.consumeRecollected();

    // After the binder, because the styler re-applies bindings when it
    // re-cascades and needs a resolved one to do it with.
    styler_.Rebuild(doc_, sheet_, &binder_);

    // Re-attach behaviour LAST, on the finished tree. Every pointer the app
    // held is dead after a rebuild, so this is the only safe place to re-cache.
    // Values are no longer its job — those live in the data source.
    if (bind_) bind_(doc_);
    return true;
}

void UIAssetDocument::UpdateRepeats() {
    for (auto& p : pools_) p->Refresh(ctx_);
}

void UIAssetDocument::PageTabs(int by) {
    if (by == 0) return;
    for (auto& t : tabs_) {
        if (!t) continue;
        const int n = int(t->count());
        if (n <= 0) continue;
        // Wrapping modulo that works for negatives, so a left press on the
        // first page lands on the last rather than on -1.
        const int next = ((t->selected() + by) % n + n) % n;
        t->Select(next);
    }
}

void UIAssetDocument::UpdateTabs() {
    for (auto& t : tabs_) t->Refresh();
}

void UIAssetDocument::DrainBinderDiagnostics() {
    if (!binder_.consumeRecollected()) return;
    // Deduplicated against what is already here. A re-collect recomputes the
    // WHOLE diagnostic list, so a binding that is still broken reports itself
    // again on every structure change; appending blindly would grow errors_
    // without bound and bury the new line under copies of the old one.
    for (const auto& e : binder_.errors()) {
        if (std::find(errors_.begin(), errors_.end(), e) != errors_.end()) continue;
        errors_.push_back(e);
        std::cerr << "[UI] " << e << "\n";
    }
    for (const auto& n : binder_.notes()) std::cerr << "[UI] note: " << n << "\n";
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
