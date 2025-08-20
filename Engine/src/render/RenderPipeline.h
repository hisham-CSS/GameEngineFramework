// Engine/src/render/RenderPipeline.h
#pragma once
#include "IRenderPass.h"
#include <vector>
#include <memory>
#include <utility>          // <-- needed for std::forward

class RenderPipeline {
public:
    template<class T, class...Args>
    T& add(Args&&...args) {
        static_assert(std::is_base_of<IRenderPass, T>::value, "T must derive IRenderPass");
        auto p = std::make_unique<T>(std::forward<Args>(args)...);
        auto& ref = *p;
        passes_.push_back(std::move(p)); // unique_ptr<T> -> unique_ptr<IRenderPass>
        return ref;
    }

    void setup(PassContext& ctx) { for (auto& p : passes_) p->setup(ctx); }
    void resize(PassContext& ctx, int w, int h) { for (auto& p : passes_) p->resize(ctx, w, h); }
    void executeAll(PassContext& ctx, MyCoreEngine::Scene& scene, Camera& cam, const FrameParams& fp) {
        for (auto& p : passes_) p->execute(ctx, scene, cam, fp);
    }
private:
    std::vector<std::unique_ptr<IRenderPass>> passes_;
};
