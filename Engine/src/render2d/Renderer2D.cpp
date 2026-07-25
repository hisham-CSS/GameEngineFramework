#include <glad/glad.h>

#include "Renderer2D.h"
#include "../core/Shader.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace MyCoreEngine {

namespace {
    // One flush's worth of quads. 4 verts each; the index buffer is built once
    // for this maximum and reused, so a flush never touches index memory.
    constexpr int kMaxQuadsPerFlush = 4096;

    constexpr const char* kVertPath = "Exported/Shaders/sprite2d_vert.glsl";
    constexpr const char* kFragPath = "Exported/Shaders/sprite2d_frag.glsl";
}

Renderer2D::Renderer2D() = default;
Renderer2D::~Renderer2D() { Shutdown(); }

bool Renderer2D::Init() {
    if (ready_) return true;

    shader_ = std::make_unique<Shader>(kVertPath, kFragPath);
    if (!shader_->isValid()) {
        shader_.reset();
        return false;
    }

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ibo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 sizeof(Vertex) * 4 * kMaxQuadsPerFlush, nullptr, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void*)offsetof(Vertex, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void*)offsetof(Vertex, uv));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void*)offsetof(Vertex, color));

    // Static index buffer: 0,1,2, 2,3,0 per quad, for every quad up front.
    {
        std::vector<unsigned> idx;
        idx.reserve(kMaxQuadsPerFlush * 6);
        for (int q = 0; q < kMaxQuadsPerFlush; ++q) {
            const unsigned b = unsigned(q) * 4;
            idx.push_back(b + 0); idx.push_back(b + 1); idx.push_back(b + 2);
            idx.push_back(b + 2); idx.push_back(b + 3); idx.push_back(b + 0);
        }
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     GLsizeiptr(idx.size() * sizeof(unsigned)), idx.data(),
                     GL_STATIC_DRAW);
    }
    glBindVertexArray(0);

    // 1x1 opaque white: untextured draws sample this, so there is exactly one
    // shader path and colour-only quads still batch with textured ones.
    const unsigned char white[4] = { 255, 255, 255, 255 };
    glGenTextures(1, &whiteTex_);
    glBindTexture(GL_TEXTURE_2D, whiteTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    cmds_.reserve(1024);
    verts_.reserve(4 * kMaxQuadsPerFlush);
    ready_ = true;
    return true;
}

void Renderer2D::Shutdown() {
    if (vbo_)      { glDeleteBuffers(1, &vbo_);            vbo_ = 0; }
    if (ibo_)      { glDeleteBuffers(1, &ibo_);            ibo_ = 0; }
    if (vao_)      { glDeleteVertexArrays(1, &vao_);       vao_ = 0; }
    if (whiteTex_) { glDeleteTextures(1, &whiteTex_);      whiteTex_ = 0; }
    shader_.reset();
    ready_ = false;
}

void Renderer2D::beginCommon_(const glm::mat4& viewProj, int vpW, int vpH) {
    viewProj_ = viewProj;
    vpW_ = vpW; vpH_ = vpH;
    cmds_.clear();
    clipStack_.clear();
    clipHistory_.clear();
    curClip_ = -1;
    seq_ = 0;
    stats_ = Stats{};
    inFrame_ = true;

    // Snapshot every bit of state we are about to change. The 3D pipeline has
    // no inter-pass reset, so anything left modified corrupts the next pass.
    saved_.blend     = glIsEnabled(GL_BLEND);
    saved_.depthTest = glIsEnabled(GL_DEPTH_TEST);
    saved_.cullFace  = glIsEnabled(GL_CULL_FACE);
    saved_.scissor   = glIsEnabled(GL_SCISSOR_TEST);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &saved_.depthMask);
    glGetIntegerv(GL_BLEND_SRC_RGB,   &saved_.blendSrcRGB);
    glGetIntegerv(GL_BLEND_DST_RGB,   &saved_.blendDstRGB);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &saved_.blendSrcA);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &saved_.blendDstA);
    glGetIntegerv(GL_VIEWPORT,        saved_.viewport);
    glGetIntegerv(GL_SCISSOR_BOX,     saved_.scissorBox);

    // 2D is painted back-to-front with alpha; depth would reject overlapping
    // quads drawn later, and culling would drop quads whose winding flips when
    // a negative scale or a rotation mirrors them.
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                        GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
}

void Renderer2D::BeginScreen(int widthPx, int heightPx) {
    const float w = float(std::max(1, widthPx));
    const float h = float(std::max(1, heightPx));
    // Top-left origin, +y down: ortho with top=0 and bottom=h flips the axis.
    beginCommon_(glm::ortho(0.0f, w, h, 0.0f, -1.0f, 1.0f), widthPx, heightPx);
}

void Renderer2D::BeginWorld(const Camera2D& cam, int viewportW, int viewportH) {
    const float w = float(std::max(1, viewportW));
    const float h = float(std::max(1, viewportH));
    const float zoom = (cam.zoom > 1e-6f) ? cam.zoom : 1e-6f;
    // Half-extents in world units. Deriving both from the viewport keeps a
    // square square at any aspect ratio.
    const float halfW = (w * 0.5f) / zoom;
    const float halfH = (h * 0.5f) / zoom;
    // +y UP here (bottom < top), the 2D-game convention.
    glm::mat4 proj = glm::ortho(-halfW, halfW, -halfH, halfH, -1.0f, 1.0f);
    glm::mat4 view(1.0f);
    if (cam.rotationDeg != 0.0f) {
        view = glm::rotate(view, glm::radians(-cam.rotationDeg), glm::vec3(0, 0, 1));
    }
    view = glm::translate(view, glm::vec3(-cam.position, 0.0f));
    beginCommon_(proj * view, viewportW, viewportH);
}

void Renderer2D::End() {
    if (!inFrame_) return;
    flush_();

    // Restore exactly what Begin captured.
    if (saved_.blend)     glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (saved_.depthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (saved_.cullFace)  glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (saved_.scissor)   glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    glDepthMask(saved_.depthMask);
    glBlendFuncSeparate(saved_.blendSrcRGB, saved_.blendDstRGB,
                        saved_.blendSrcA, saved_.blendDstA);
    glViewport(saved_.viewport[0], saved_.viewport[1],
               saved_.viewport[2], saved_.viewport[3]);
    glScissor(saved_.scissorBox[0], saved_.scissorBox[1],
              saved_.scissorBox[2], saved_.scissorBox[3]);
    inFrame_ = false;
}

void Renderer2D::PushClipRect(const glm::vec2& posPx, const glm::vec2& sizePx) {
    ClipRect r{ posPx, glm::max(sizePx, glm::vec2(0.0f)) };
    if (!clipStack_.empty()) {
        // Intersect with the parent: a child must never escape it.
        const ClipRect& p = clipStack_.back();
        const glm::vec2 lo = glm::max(r.pos, p.pos);
        const glm::vec2 hi = glm::min(r.pos + r.size, p.pos + p.size);
        r.pos = lo;
        r.size = glm::max(hi - lo, glm::vec2(0.0f));
    }
    clipStack_.push_back(r);
    clipHistory_.push_back(r);
    curClip_ = int(clipHistory_.size()) - 1;
}

void Renderer2D::PopClipRect() {
    if (clipStack_.empty()) return;
    clipStack_.pop_back();
    if (clipStack_.empty()) {
        curClip_ = -1;
    } else {
        // Point at the history entry for the rect now on top.
        const ClipRect& top = clipStack_.back();
        for (int i = int(clipHistory_.size()) - 1; i >= 0; --i) {
            if (clipHistory_[size_t(i)].pos == top.pos &&
                clipHistory_[size_t(i)].size == top.size) { curClip_ = i; break; }
        }
    }
}

void Renderer2D::pushQuad_(const Vertex v[4], unsigned texture, int layer) {
    if (!inFrame_) return;
    Cmd c{};
    for (int i = 0; i < 4; ++i) c.v[i] = v[i];
    c.texture = texture ? texture : whiteTex_;
    c.layer = layer;
    c.seq = seq_++;
    c.clip = curClip_;
    cmds_.push_back(c);
    ++stats_.quads;
}

void Renderer2D::DrawQuad(const glm::vec2& pos, const glm::vec2& size,
                          const glm::vec4& color, int layer) {
    DrawSprite(pos, size, 0, TexRegion{}, color, layer);
}

void Renderer2D::DrawSprite(const glm::vec2& pos, const glm::vec2& size,
                            unsigned texture, const TexRegion& region,
                            const glm::vec4& tint, int layer) {
    Vertex v[4];
    v[0].pos = { pos.x,          pos.y };
    v[1].pos = { pos.x + size.x, pos.y };
    v[2].pos = { pos.x + size.x, pos.y + size.y };
    v[3].pos = { pos.x,          pos.y + size.y };
    v[0].uv = { region.uvMin.x, region.uvMin.y };
    v[1].uv = { region.uvMax.x, region.uvMin.y };
    v[2].uv = { region.uvMax.x, region.uvMax.y };
    v[3].uv = { region.uvMin.x, region.uvMax.y };
    for (int i = 0; i < 4; ++i) v[i].color = tint;
    pushQuad_(v, texture, layer);
}

void Renderer2D::DrawSpriteRotated(const glm::vec2& pos, const glm::vec2& size,
                                   float rotationDeg, const glm::vec2& origin,
                                   unsigned texture, const TexRegion& region,
                                   const glm::vec4& tint, int layer) {
    const float c = std::cos(glm::radians(rotationDeg));
    const float s = std::sin(glm::radians(rotationDeg));
    const glm::vec2 pivot = pos + origin * size;
    const glm::vec2 corner[4] = {
        { pos.x,          pos.y },
        { pos.x + size.x, pos.y },
        { pos.x + size.x, pos.y + size.y },
        { pos.x,          pos.y + size.y },
    };
    Vertex v[4];
    for (int i = 0; i < 4; ++i) {
        const glm::vec2 d = corner[i] - pivot;
        v[i].pos = { pivot.x + d.x * c - d.y * s, pivot.y + d.x * s + d.y * c };
        v[i].color = tint;
    }
    v[0].uv = { region.uvMin.x, region.uvMin.y };
    v[1].uv = { region.uvMax.x, region.uvMin.y };
    v[2].uv = { region.uvMax.x, region.uvMax.y };
    v[3].uv = { region.uvMin.x, region.uvMax.y };
    pushQuad_(v, texture, layer);
}

void Renderer2D::flush_() {
    if (cmds_.empty() || !shader_) return;

    // Stable by (layer, submission order): callers may emit in any order and
    // still get correct painter's-algorithm output, and equal layers keep the
    // order they were submitted in.
    std::stable_sort(cmds_.begin(), cmds_.end(),
                     [](const Cmd& a, const Cmd& b) {
                         if (a.layer != b.layer) return a.layer < b.layer;
                         return a.seq < b.seq;
                     });

    shader_->use();
    shader_->setMat4("uViewProj", viewProj_);
    shader_->setInt("uTex", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);

    size_t i = 0;
    while (i < cmds_.size()) {
        // A run is as many consecutive quads as share texture + clip, capped by
        // the vertex buffer. Every state change costs a draw call, which is
        // exactly what the stats report.
        const unsigned tex = cmds_[i].texture;
        const int clip = cmds_[i].clip;
        size_t n = 0;
        verts_.clear();
        while (i + n < cmds_.size() && n < size_t(kMaxQuadsPerFlush) &&
               cmds_[i + n].texture == tex && cmds_[i + n].clip == clip) {
            const Cmd& c = cmds_[i + n];
            verts_.insert(verts_.end(), c.v, c.v + 4);
            ++n;
        }

        if (clip >= 0 && clip < int(clipHistory_.size())) {
            const ClipRect& r = clipHistory_[size_t(clip)];
            glEnable(GL_SCISSOR_TEST);
            // Scissor is in GL window coords (origin BOTTOM-left); clip rects
            // are given top-left, so flip Y against the viewport height.
            const int x = int(std::lround(r.pos.x));
            const int y = int(std::lround(float(vpH_) - (r.pos.y + r.size.y)));
            glScissor(x, y, int(std::lround(r.size.x)), int(std::lround(r.size.y)));
        } else {
            glDisable(GL_SCISSOR_TEST);
        }

        glBindTexture(GL_TEXTURE_2D, tex);
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        GLsizeiptr(verts_.size() * sizeof(Vertex)), verts_.data());
        glDrawElements(GL_TRIANGLES, GLsizei(n * 6), GL_UNSIGNED_INT, nullptr);
        ++stats_.drawCalls;
        ++stats_.flushes;
        i += n;
    }

    glBindVertexArray(0);
    cmds_.clear();
}

} // namespace MyCoreEngine
