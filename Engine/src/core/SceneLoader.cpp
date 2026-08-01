#include "SceneLoader.h"

#include "AssetManager.h"
#include "Scene.h"

#include <algorithm>
#include <iostream>

namespace MyCoreEngine {

namespace {
    const std::string kNoPath;

    std::string joinPaths(const std::vector<std::string>& v, std::size_t max = 4) {
        std::string out;
        for (std::size_t i = 0; i < v.size() && i < max; ++i) {
            if (i) out += ", ";
            out += v[i];
        }
        if (v.size() > max) out += ", ... (" + std::to_string(v.size() - max) + " more)";
        return out;
    }
}

class SceneLoader::FnObserver : public ISceneSwapObserver {
public:
    FnObserver(std::function<void(Scene&)> u, std::function<void(Scene&)> d)
        : unload_(std::move(u)), load_(std::move(d)) {}
    void OnSceneWillUnload(Scene& s, const SceneSwapContext&) override {
        if (unload_) unload_(s);
    }
    void OnSceneDidLoad(Scene& s, const SceneSwapContext&) override {
        if (load_) load_(s);
    }
private:
    std::function<void(Scene&)> unload_, load_;
};

SceneLoader::ObserverHandle SceneLoader::AddObserver(ISceneSwapObserver* obs) {
    if (!obs) return 0;
    const ObserverHandle h = nextHandle_++;
    observers_.push_back(Entry{ h, obs, nullptr });
    return h;
}

SceneLoader::ObserverHandle SceneLoader::AddObserver(std::function<void(Scene&)> willUnload,
                                                     std::function<void(Scene&)> didLoad) {
    auto owned = std::make_shared<FnObserver>(std::move(willUnload), std::move(didLoad));
    const ObserverHandle h = nextHandle_++;
    observers_.push_back(Entry{ h, owned.get(), owned });
    return h;
}

void SceneLoader::RemoveObserver(ObserverHandle h) {
    observers_.erase(std::remove_if(observers_.begin(), observers_.end(),
                                    [h](const Entry& e) { return e.handle == h; }),
                     observers_.end());
}

const std::string& SceneLoader::pendingPath() const {
    return pending_ ? pending_->path : kNoPath;
}

void SceneLoader::CancelPendingSwap() { pending_.reset(); }

bool SceneLoader::RequestSwap(std::string path, SceneSwapOrigin origin) {
    // Validated HERE, not at the drain. The swap tears the outgoing scene's
    // subsystems down before it loads, so a file that turns out to be bad would
    // leave the caller with a live scene whose physics and scripts had already
    // been cleared. Catching it at request time keeps the ordinary bad-path
    // case — a typo, a deleted file — completely inert.
    std::string whyNot;
    if (!SceneSerializer::Validate(path, assets_, &whyNot)) {
        SceneSwapResult r;
        r.status = SceneSwapStatus::Invalid;
        r.path = path;
        r.message = whyNot;
        std::cerr << "[SceneLoader] swap to '" << path << "' refused: " << whyNot << "\n";
        finish_(std::move(r));
        return false;
    }

    if (pending_) {
        std::cerr << "[SceneLoader] swap to '" << pending_->path
                  << "' superseded by '" << path << "' before it ran.\n";
        SceneSwapResult r;
        r.status = SceneSwapStatus::Superseded;
        r.path = pending_->path;
        r.message = "superseded by '" + path + "'";
        finish_(std::move(r));
    }
    pending_ = SceneSwapContext{ std::move(path), origin };
    return true;
}

bool SceneLoader::DrainPendingSwap() {
    if (!pending_ || swapping_) return false;

    swapping_ = true;
    struct Guard {
        bool* f;
        ~Guard() { *f = false; }
    } guard{ &swapping_ };

    const SceneSwapContext ctx = *pending_;
    pending_.reset();

    // 1. Veto, before anything is touched.
    for (const Entry& e : observers_) {
        std::string reason;
        if (e.obs->AllowSceneSwap(ctx, reason)) continue;
        SceneSwapResult r;
        r.status = SceneSwapStatus::Refused;
        r.path = ctx.path;
        r.message = reason;
        std::cerr << "[SceneLoader] swap to '" << ctx.path << "' refused: " << reason << "\n";
        finish_(std::move(r));
        return false;
    }

    // 2. Teardown, while the outgoing entities are STILL ALIVE.
    for (const Entry& e : observers_) e.obs->OnSceneWillUnload(scene_, ctx);

    // 3. The load. The only destructive act, and atomic by the serializer's own
    //    validation probe — which RequestSwap already ran, so reaching the
    //    failure branch here means the file changed on disk in between, or an
    //    asset-stage throw the probe cannot rehearse.
    SceneLoadReport rep;
    SceneSerializer sz(scene_, assets_);
    const bool loaded = sz.Load(ctx.path, &rep);

    // 4. World matrices BEFORE anything reads them. Engine knowledge, kept
    //    here rather than repeated in every host: physics builds its bodies
    //    from world poses, and a scaled ground plane built before this lands at
    //    the origin at unit size.
    scene_.UpdateTransforms();

    // 5. Rebuild. Runs even on a failed load, deliberately: teardown has
    //    already happened, so leaving the subsystems dead would be strictly
    //    worse than rebuilding them against whatever survived.
    for (const Entry& e : observers_) e.obs->OnSceneDidLoad(scene_, ctx);

    SceneSwapResult r;
    r.path = ctx.path;
    r.report = rep;
    if (!loaded) {
        r.status = SceneSwapStatus::LoadFailed;
        r.message = "'" + ctx.path + "' validated but failed to load";
        std::cerr << "[SceneLoader] swap to '" << ctx.path
                  << "' failed AFTER teardown - see the error above. Subsystems "
                     "have been rebuilt against whatever survived.\n";
        finish_(std::move(r));
        return true;
    }

    // 6. A scene can load perfectly and still be unusable. Say so, because the
    //    bool return cannot: entities whose model never imported are invisible
    //    while their colliders are entirely real.
    if (!rep.complete()) {
        std::cerr << "[SceneLoader] scene '" << ctx.path << "' loaded with "
                  << (rep.failedModels.size() + rep.rejectedModels.size())
                  << " model(s) that did not import: "
                  << joinPaths(rep.failedModels)
                  << (rep.rejectedModels.empty() ? "" : " (rejected: " +
                          joinPaths(rep.rejectedModels) + ")")
                  << " - those entities are invisible, but their colliders are real.\n";
    }
    r.status = SceneSwapStatus::Ok;
    finish_(std::move(r));
    return true;
}

void SceneLoader::finish_(SceneSwapResult r) {
    last_ = std::move(r);
    if (onComplete_) onComplete_(last_);
}

} // namespace MyCoreEngine
