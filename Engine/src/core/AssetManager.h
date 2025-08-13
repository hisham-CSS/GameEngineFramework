#pragma once
#include "Core.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <mutex>

namespace MyCoreEngine {

    class Model;

    // Minimal model asset manager: dedupe by normalized path.
    // Thread-safe enough for editor use (coarse mutex).
    class ENGINE_API AssetManager {
    public:
        // Get a shared handle to a GPU-ready Model for this path.
        // If already loaded and still alive, returns the existing instance.
        std::shared_ptr<Model> GetModel(const std::string& path, bool gamma = false);

        // Force reload of a model from disk and replace the cache entry.
        // Existing holders keep their old shared_ptr (you can retarget them manually).
        std::shared_ptr<Model> ReloadModel(const std::string& path, bool gamma = false);

        // Remove expired entries from the cache.
        void GarbageCollect();

        // Clear cache map (dangerous if callers still hold shared_ptrs, but safe: they keep their instances).
        void Clear();

    private:
        static std::string NormalizePath(const std::string& path);

        // path -> weak ref (dedupe by path)
        std::unordered_map<std::string, std::weak_ptr<Model>> models_;
        mutable std::mutex mtx_;
    };

} // namespace MyCoreEngine
