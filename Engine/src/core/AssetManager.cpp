#include "AssetManager.h"
#include "JobSystem.h"
#include "Model.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <utility>

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

    AssetManager::ModelRequestHandle AssetManager::RequestModel(JobSystem& jobs,
                                                                const std::string& path,
                                                                bool gamma)
    {
        const std::string key = NormalizePath(path);

        // cache hit: hand back an already-Live handle, no job at all
        {
            std::lock_guard<std::mutex> lock(mtx_);
            if (auto it = models_.find(key); it != models_.end()) {
                if (auto sp = it->second.lock()) {
                    auto req = std::make_shared<ModelRequest>();
                    req->path = path;
                    req->model = sp;
                    req->state = sp->Meshes().empty() ? LoadState::Failed
                                                      : LoadState::Live;
                    return req;
                }
            }
        }

        // in flight (or queued): share the same status block
        if (auto it = pending_.find(key); it != pending_.end()) {
            return it->second;
        }

        auto req = std::make_shared<ModelRequest>();
        req->path = path;
        req->state = LoadState::Queued;
        pending_.emplace(key, req);
        queued_.push_back(QueuedLoad{ req, path, gamma });
        launchQueued_(jobs);
        return req;
    }

    void AssetManager::launchQueued_(JobSystem& jobs)
    {
        while (inFlight_ < kMaxConcurrentDecodes && !queued_.empty()) {
            QueuedLoad load = std::move(queued_.front());
            queued_.pop_front();
            ++inFlight_;
            load.req->state = LoadState::Decoding;

            // snapshot the texture cache ON THE MAIN THREAD so the worker
            // skips decoding pixels that are already uploaded (entries never
            // evict, so the snapshot can't go stale)
            auto skipKeys = std::make_shared<std::unordered_set<std::string>>(
                Model::CachedTextureKeys());
            struct JobState { ModelCPUData cpu; };
            auto js = std::make_shared<JobState>();

            const std::string rawPath = load.rawPath;
            const bool gamma = load.gamma;
            auto req = load.req;

            // `this` is safe here: completions only ever execute inside
            // RunLoop's pump or its exit drain (which loops until chained
            // submissions are quiescent), while the app and this manager
            // are alive — see the JobSystem lifetime contract.
            jobs.submit(
                [js, rawPath, gamma, skipKeys] {
                    js->cpu = Model::Decode(rawPath, gamma, skipKeys.get());
                },
                [this, &jobs, js, req] {
                    // finalize may throw (bad_alloc on a huge model is the
                    // realistic one) and the pump swallows completion
                    // exceptions — the bookkeeping below must run REGARDLESS
                    // or the decode slot leaks and the path wedges forever
                    std::shared_ptr<Model> model;
                    try {
                        model = std::make_shared<Model>(std::move(js->cpu));
                    }
                    catch (const std::exception& e) {
                        std::fprintf(stderr, "[AssetManager] finalize failed for '%s': %s\n",
                                     req->path.c_str(), e.what());
                    }
                    catch (...) {
                        std::fprintf(stderr, "[AssetManager] finalize failed for '%s'\n",
                                     req->path.c_str());
                    }
                    if (!model) {
                        // Failed-with-empty-model parity (invalid cpu data
                        // constructs without touching GL)
                        try { model = std::make_shared<Model>(ModelCPUData{}); }
                        catch (...) {} // truly out of memory: req->model stays null
                    }

                    // pending_ is the ownership token: ReloadModel/Clear may
                    // have superseded this load — a stale result must not
                    // clobber the newer cache entry (the handle still gets
                    // its result; it just isn't cached)
                    const std::string key = NormalizePath(req->path);
                    auto pit = pending_.find(key);
                    const bool owns = (pit != pending_.end() && pit->second == req);
                    if (owns && model) {
                        std::lock_guard<std::mutex> lock(mtx_);
                        models_[key] = model;
                    }
                    req->model = std::move(model);
                    req->state = (req->model && !req->model->Meshes().empty())
                               ? LoadState::Live : LoadState::Failed;
                    if (owns) pending_.erase(key);
                    --inFlight_;
                    launchQueued_(jobs); // free slot: start the next queued load
                });
        }
    }

    std::shared_ptr<Model> AssetManager::ReloadModel(const std::string& path, bool gamma) {
        const std::string key = NormalizePath(path);
        // supersede any in-flight async load: dropping its pending_ entry
        // strips the completion's ownership token, so a stale result can't
        // clobber this fresh reload (the old handle still resolves — its
        // model just isn't cached)
        pending_.erase(key);
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
        // in-flight async loads lose their ownership tokens too: their
        // completions finish the handles but repopulate nothing
        pending_.clear();
        std::lock_guard<std::mutex> lock(mtx_);
        models_.clear();
    }

} // namespace MyCoreEngine
