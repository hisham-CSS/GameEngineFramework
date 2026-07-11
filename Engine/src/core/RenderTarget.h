#pragma once
#include "Core.h"

namespace MyCoreEngine {

    // Offscreen color target the renderer can draw the final (tonemapped)
    // frame into — the editor shows it inside the Viewport panel via
    // ImGui::Image. Color-only: the scene's depth lives in the HDR FBO and
    // the tonemap resolve needs no depth.
    class ENGINE_API RenderTarget {
    public:
        RenderTarget() = default;
        ~RenderTarget();

        RenderTarget(const RenderTarget&) = delete;
        RenderTarget& operator=(const RenderTarget&) = delete;

        // Requires a current GL context. Resize is a no-op for same size.
        void Create(int width, int height);
        void Resize(int width, int height);
        void Destroy();

        unsigned fbo() const { return fbo_; }
        unsigned colorTexture() const { return color_; }
        int width() const { return w_; }
        int height() const { return h_; }

    private:
        unsigned fbo_ = 0;
        unsigned color_ = 0;
        int w_ = 0, h_ = 0;
    };

} // namespace MyCoreEngine
