#include "AssetManager.h"
#include "Model.h"

#include <algorithm>
#include <cctype>

namespace MyCoreEngine {

    std::string AssetManager::NormalizePath(const std::string& path) {
        std::string p = path;
        // normalize slashes to '/', lowercase (Windows-friendly)
        std::replace(p.begin(), p.end(), '\\', '/');
        std::transform(p.begin(), p.end(), p.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return p;
    }

    std::shared_ptr<Model> AssetManager::GetModel(const std::string& path, bool gamma) {
        const std::string key = NormalizePath(path);
        std::lock_guard<std::mutex> lock(mtx_);

        if (auto it = models_.find(key); it != models_.end()) {
            if (auto sp = it->second.lock()) {
                return sp; // reuse existing
            }
            // expired -> fall through and recreate
        }

        // IMPORTANT: ensure Renderer::InitGL() has been called earlier in the app,
        // so Model construction is safe to create GL resources.
        auto sp = std::make_shared<Model>(path, gamma);
        models_[key] = sp;
        return sp;
    }

    std::shared_ptr<Model> AssetManager::ReloadModel(const std::string& path, bool gamma) {
        const std::string key = NormalizePath(path);
        std::lock_guard<std::mutex> lock(mtx_);
        auto sp = std::make_shared<Model>(path, gamma);
        models_[key] = sp; // replace cache entry
        return sp;
    }

    void AssetManager::GarbageCollect() {
        std::lock_guard<std::mutex> lock(mtx_);
        for (auto it = models_.begin(); it != models_.end(); ) {
            if (it->second.expired()) it = models_.erase(it);
            else ++it;
        }
    }

    void AssetManager::Clear() {
        std::lock_guard<std::mutex> lock(mtx_);
        models_.clear();
    }

} // namespace MyCoreEngine
