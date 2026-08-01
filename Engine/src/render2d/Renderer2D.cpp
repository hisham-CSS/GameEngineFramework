#include <glad/glad.h>

#include "Renderer2D.h"
#include "Font.h"
#include "../core/Shader.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>

namespace MyCoreEngine {

namespace {
    // One flush's worth of quads. 4 verts each; the index buffer is built once
    // for this maximum and reused, so a flush never touches index memory.
    constexpr int kMaxQuadsPerFlush = 4096;

    // Box quads are tens per frame, not thousands, so this buffer is sized for
    // a realistic worst case rather than for the plain stream's ceiling: 4096
    // box quads would be 1.1 MB of VBO to serve a few dozen panels.
    constexpr int kMaxBoxQuadsPerFlush = 512;

    constexpr const char* kVertPath = "Exported/Shaders/sprite2d_vert.glsl";
    constexpr const char* kFragPath = "Exported/Shaders/sprite2d_frag.glsl";
    constexpr const char* kBoxVertPath = "Exported/Shaders/sprite2d_box_vert.glsl";
    constexpr const char* kBoxFragPath = "Exported/Shaders/sprite2d_box_frag.glsl";
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

    // The box stream. OPTIONAL, deliberately: a box shader that fails to
    // compile must cost rounded corners, not the whole UI. Returning false here
    // would take UIPass down with it and blank every document in the scene, and
    // a missing corner is not worth that.
    boxShader_ = std::make_unique<Shader>(kBoxVertPath, kBoxFragPath);
    if (!boxShader_->isValid()) {
        boxShader_.reset();
        std::cerr << "WARN::RENDERER2D box shader unavailable "
                     "(sprite2d_box_*.glsl); border-radius, border-width and "
                     "background-image will paint as plain squares\n";
    } else {
        glGenVertexArrays(1, &boxVao_);
        glGenBuffers(1, &boxVbo_);
        glBindVertexArray(boxVao_);
        glBindBuffer(GL_ARRAY_BUFFER, boxVbo_);
        glBufferData(GL_ARRAY_BUFFER,
                     sizeof(BoxVertex) * 4 * kMaxBoxQuadsPerFlush, nullptr,
                     GL_DYNAMIC_DRAW);
        // Seven attributes. GL 3.3 guarantees GL_MAX_VERTEX_ATTRIBS >= 16.
        struct Attr { int loc; int n; std::size_t off; };
        const Attr kAttrs[] = {
            { 0, 2, offsetof(BoxVertex, pos)    },
            { 1, 2, offsetof(BoxVertex, uv)     },
            { 2, 4, offsetof(BoxVertex, color)  },
            { 3, 2, offsetof(BoxVertex, local)  },
            { 4, 2, offsetof(BoxVertex, half)   },
            { 5, 4, offsetof(BoxVertex, border) },
            { 6, 2, offsetof(BoxVertex, shape)  },
        };
        for (const Attr& a : kAttrs) {
            glEnableVertexAttribArray(GLuint(a.loc));
            glVertexAttribPointer(GLuint(a.loc), a.n, GL_FLOAT, GL_FALSE,
                                  sizeof(BoxVertex), (void*)a.off);
        }
        // The same 0,1,2, 2,3,0 pattern, so the index buffer is shared.
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
        glBindVertexArray(0);
        boxVerts_.reserve(4 * 64);
        boxScratch_.reserve(4 * kMaxBoxQuadsPerFlush);
        roundedReady_ = true;
    }

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
    if (boxVbo_)   { glDeleteBuffers(1, &boxVbo_);         boxVbo_ = 0; }
    if (boxVao_)   { glDeleteVertexArrays(1, &boxVao_);    boxVao_ = 0; }
    shader_.reset();
    boxShader_.reset();
    roundedReady_ = false;
    ready_ = false;
}

void Renderer2D::beginCommon_(const glm::mat4& viewProj, int vpW, int vpH) {
    viewProj_ = viewProj;
    vpW_ = vpW; vpH_ = vpH;
    cmds_.clear();
    boxVerts_.clear();
    clipStack_.clear();
    clipHistory_.clear();
    clipStackIdx_.clear();
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
    // Rejected AT THE SINK, because nothing downstream can. `size <= 0` is false
    // for NaN, so a non-finite rect survives every guard in the UI, and
    // lround(NaN) then hands glScissor a negative width: GL_INVALID_VALUE, the
    // previous scissor box left active, and the test still enabled — i.e. the
    // wrong region clipped for the rest of the frame. A zero rect draws nothing,
    // which is what an empty intersection already does.
    const bool finite = std::isfinite(posPx.x) && std::isfinite(posPx.y) &&
                        std::isfinite(sizePx.x) && std::isfinite(sizePx.y);
    ClipRect r{ finite ? posPx : glm::vec2(0.0f),
                finite ? glm::max(sizePx, glm::vec2(0.0f)) : glm::vec2(0.0f) };
    if (!finite) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            std::cerr << "WARN::RENDERER2D non-finite clip rect; clipping everything "
                         "out rather than corrupting the scissor box" << std::endl;
        }
    }
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
    // Remembered rather than searched for on the way out. The old Pop scanned
    // clipHistory_ BACKWARDS comparing floats: O(history) per pop, which goes
    // quadratic once every row of a list clips; wrong when a child's intersected
    // rect happens to equal its parent's (it would find the child's entry and
    // split one batch into two); and silently a no-op if no entry matched,
    // leaving the rest of a sibling subtree clipped by the deeper rect.
    clipStackIdx_.push_back(curClip_);
}

void Renderer2D::PopClipRect() {
    if (clipStack_.empty()) return;
    clipStack_.pop_back();
    clipStackIdx_.pop_back();
    curClip_ = clipStackIdx_.empty() ? -1 : clipStackIdx_.back();
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

void Renderer2D::pushBoxQuad_(const BoxVertex v[4], unsigned texture, int layer) {
    if (!inFrame_) return;
    Cmd c{};
    c.texture = texture ? texture : whiteTex_;
    c.layer = layer;
    c.seq = seq_++;
    c.clip = curClip_;
    c.kind = QuadKind::Box;
    c.box = int(boxVerts_.size());
    boxVerts_.insert(boxVerts_.end(), v, v + 4);
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

void Renderer2D::DrawBox(const glm::vec2& pos, const glm::vec2& size,
                         const glm::vec4& fill, const BoxStyle& box,
                         unsigned texture, const TexRegion& region, int layer) {
    // A box that needs neither a radius nor a border IS a sprite. Routing it
    // through the plain stream keeps every existing background quad, every
    // square background-image and every glyph in one batch class, so this
    // feature costs nothing at all until somebody uses it.
    if (!roundedReady_ || (box.radiusPx <= 0.0f && box.borderPx <= 0.0f)) {
        DrawSprite(pos, size, texture, region, fill, layer);
        return;
    }
    const glm::vec2 half = size * 0.5f;
    BoxVertex v[4];
    // The same corner order DrawSprite uses: TL, TR, BR, BL.
    v[0].pos = { pos.x,          pos.y };
    v[1].pos = { pos.x + size.x, pos.y };
    v[2].pos = { pos.x + size.x, pos.y + size.y };
    v[3].pos = { pos.x,          pos.y + size.y };
    v[0].uv = { region.uvMin.x, region.uvMin.y };
    v[1].uv = { region.uvMax.x, region.uvMin.y };
    v[2].uv = { region.uvMax.x, region.uvMax.y };
    v[3].uv = { region.uvMin.x, region.uvMax.y };
    v[0].local = { -half.x, -half.y };
    v[1].local = {  half.x, -half.y };
    v[2].local = {  half.x,  half.y };
    v[3].local = { -half.x,  half.y };
    for (int i = 0; i < 4; ++i) {
        v[i].color  = fill;
        v[i].half   = half;
        v[i].border = box.borderColor;
        v[i].shape  = { box.radiusPx, box.borderPx };
    }
    pushBoxQuad_(v, texture, layer);
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

void Renderer2D::DrawText(const Font& font, const std::string& utf8,
                          const glm::vec2& pos, const glm::vec4& color,
                          int layer, float scale) {
    if (!inFrame_ || !font.IsValid() || utf8.empty()) return;

    // pos is the box's top-left; the pen rides the BASELINE, which sits one
    // ascent below it. Getting this wrong is the classic "text sits above its
    // box" bug, and it only shows up once text shares a row with other content.
    const float lineAdvance = font.lineHeight() * scale;
    glm::vec2 pen{ pos.x, pos.y + font.ascent() * scale };

    for (const std::uint32_t cp : Font::DecodeUTF8(utf8)) {
        if (cp == '\n') {
            pen.x = pos.x;
            pen.y += lineAdvance;
            continue;
        }
        const Glyph* g = font.FindGlyph(cp);
        if (!g) continue; // not baked (or unprintable): skip, do not stall the pen
        if (g->size.x > 0.0f && g->size.y > 0.0f) { // space has no quad
            const glm::vec2 p = pen + g->offset * scale;
            const glm::vec2 s = g->size * scale;
            DrawSprite(p, s, font.atlasTexture(),
                       TexRegion{ g->uvMin, g->uvMax }, color, layer);
        }
        pen.x += g->advance * scale;
    }
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

    // glActiveTexture is global state, not per-VAO, so it stays hoisted. The
    // shader, its uniforms and both buffer bindings do NOT: they move into the
    // loop below, because a run can now switch streams.
    glActiveTexture(GL_TEXTURE0);

    size_t i = 0;
    while (i < cmds_.size()) {
        // A run is as many consecutive quads as share texture + clip, capped by
        // the vertex buffer. Every state change costs a draw call, which is
        // exactly what the stats report.
        const unsigned tex = cmds_[i].texture;
        const int clip = cmds_[i].clip;
        // ...and, now, stream KIND. The break lands on a boundary the texture
        // change already breaks in every real UI, so a panel with a label is
        // still two draw calls, exactly as it was before boxes existed.
        const QuadKind kind = cmds_[i].kind;
        const size_t runCap = (kind == QuadKind::Box) ? size_t(kMaxBoxQuadsPerFlush)
                                                      : size_t(kMaxQuadsPerFlush);
        size_t n = 0;
        verts_.clear();
        boxScratch_.clear();
        while (i + n < cmds_.size() && n < runCap &&
               cmds_[i + n].texture == tex && cmds_[i + n].clip == clip &&
               cmds_[i + n].kind == kind) {
            const Cmd& c = cmds_[i + n];
            if (kind == QuadKind::Box) {
                boxScratch_.insert(boxScratch_.end(), boxVerts_.begin() + c.box,
                                   boxVerts_.begin() + c.box + 4);
            } else {
                verts_.insert(verts_.end(), c.v, c.v + 4);
            }
            ++n;
        }

        // Re-set on EVERY run, not hoisted. A shader switch resets neither the
        // uniforms nor the bindings, and a stale uViewProj would silently draw
        // the second and every later plain run with the previous frame's
        // projection. The GL_ARRAY_BUFFER bind is needed as well as the VAO
        // bind: a VAO restores the attribute pointers' buffer bindings, not the
        // current target that glBufferSubData writes through.
        Shader* sh = (kind == QuadKind::Box) ? boxShader_.get() : shader_.get();
        if (!sh) { i += n; continue; }
        sh->use();
        sh->setMat4("uViewProj", viewProj_);
        sh->setInt("uTex", 0);
        glBindVertexArray(kind == QuadKind::Box ? boxVao_ : vao_);
        glBindBuffer(GL_ARRAY_BUFFER, kind == QuadKind::Box ? boxVbo_ : vbo_);

        if (clip >= 0 && clip < int(clipHistory_.size())) {
            const ClipRect& r = clipHistory_[size_t(clip)];
            glEnable(GL_SCISSOR_TEST);
            // Scissor is in GL window coords (origin BOTTOM-left); clip rects
            // are given top-left, so flip Y against the viewport height.
            //
            // Both EDGES are rounded, not the origin and the extent separately:
            // with pos.x = 10.5 and size.x = 10.5, lround(pos) + lround(size)
            // gives 22 where the true edge 21.0 rounds to 21. Integer layouts
            // never noticed, but a scroll offset produces fractional rects by
            // construction and the boundary would shimmer as the content moved.
            const int x0 = int(std::lround(r.pos.x));
            const int x1 = int(std::lround(r.pos.x + r.size.x));
            const int yTop = int(std::lround(float(vpH_) - (r.pos.y + r.size.y)));
            const int yBot = int(std::lround(float(vpH_) - r.pos.y));
            // The intersection above guarantees size >= 0, so neither extent can
            // come out negative here.
            glScissor(x0, yTop, x1 - x0, yBot - yTop);
        } else {
            glDisable(GL_SCISSOR_TEST);
        }

        glBindTexture(GL_TEXTURE_2D, tex);
        if (kind == QuadKind::Box) {
            glBufferSubData(GL_ARRAY_BUFFER, 0,
                            GLsizeiptr(boxScratch_.size() * sizeof(BoxVertex)),
                            boxScratch_.data());
        } else {
            glBufferSubData(GL_ARRAY_BUFFER, 0,
                            GLsizeiptr(verts_.size() * sizeof(Vertex)), verts_.data());
        }
        glDrawElements(GL_TRIANGLES, GLsizei(n * 6), GL_UNSIGNED_INT, nullptr);
        ++stats_.drawCalls;
        ++stats_.flushes;
        i += n;
    }

    glBindVertexArray(0);
    cmds_.clear();
}

TexRegion CoverRegion(const glm::vec2& boxPx, const glm::vec2& imagePx) {
    if (boxPx.x <= 0.0f || boxPx.y <= 0.0f ||
        imagePx.x <= 0.0f || imagePx.y <= 0.0f) {
        return TexRegion{};   // the whole image
    }
    const float boxA = boxPx.x / boxPx.y;
    const float imgA = imagePx.x / imagePx.y;
    glm::vec2 f{ 1.0f, 1.0f };
    if (imgA > boxA) f.x = boxA / imgA;   // image wider than the box: crop the sides
    else             f.y = imgA / boxA;   // image taller: crop top and bottom
    const glm::vec2 lo = (glm::vec2(1.0f) - f) * 0.5f;   // centred
    return TexRegion{ lo, lo + f };
}

} // namespace MyCoreEngine
