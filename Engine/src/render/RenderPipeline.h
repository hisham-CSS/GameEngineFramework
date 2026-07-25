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

    // Sets up only the passes added since the last call. Renderer builds the
    // pipeline LAZILY -- `if (!xPass_) { add<X>(); setup(ctx); }` for each of
    // ~8 passes -- so a plain loop re-ran every earlier pass's setup: on the
    // first frame SkyboxPass::setup ran 8 times, orphaning 7 VAO/VBO pairs
    // (its glGen* are unguarded and the dtor frees only the last), and ~30
    // redundant shader compile+links happened, doubled because the editor owns
    // two Renderers. Passes are only ever appended, so a high-water mark is
    // enough, and setup() stays safe to call after every add.
    void setup(PassContext& ctx) {
        for (size_t i = setupCount_; i < passes_.size(); ++i) passes_[i]->setup(ctx);
        setupCount_ = passes_.size();
    }
    void resize(PassContext& ctx, int w, int h) { for (auto& p : passes_) p->resize(ctx, w, h); }
    void executeAll(PassContext& ctx, MyCoreEngine::Scene& scene, Camera& cam, const FrameParams& fp) {
        for (auto& p : passes_) p->execute(ctx, scene, cam, fp);
    }
private:
    std::vector<std::unique_ptr<IRenderPass>> passes_;
    size_t setupCount_ = 0; // high-water mark: passes [0, setupCount_) are set up
};
