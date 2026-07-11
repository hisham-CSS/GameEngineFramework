#include <glad/glad.h>
#include "RenderTarget.h"

#include <algorithm>
#include <iostream>

namespace MyCoreEngine {

    RenderTarget::~RenderTarget() {
        Destroy();
    }

    void RenderTarget::Create(int width, int height) {
        Destroy();
        w_ = std::max(1, width);
        h_ = std::max(1, height);

        glGenTextures(1, &color_);
        glBindTexture(GL_TEXTURE_2D, color_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w_, h_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glGenFramebuffers(1, &fbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_, 0);
        if (const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER); status != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "ERROR::RenderTarget FBO incomplete, status 0x"
                      << std::hex << status << std::dec << std::endl;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void RenderTarget::Resize(int width, int height) {
        width = std::max(1, width);
        height = std::max(1, height);
        if (fbo_ && width == w_ && height == h_) return;
        Create(width, height);
    }

    void RenderTarget::Destroy() {
        if (color_) { glDeleteTextures(1, &color_); color_ = 0; }
        if (fbo_) { glDeleteFramebuffers(1, &fbo_); fbo_ = 0; }
        w_ = h_ = 0;
    }

} // namespace MyCoreEngine
