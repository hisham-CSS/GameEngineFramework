#pragma once
// Batched 2D renderer: textured, tinted, transformable quads.
//
// This is a GENERAL-PURPOSE engine feature, not a UI internal. The in-game UI
// is its first consumer, but it is deliberately shaped so a 2D GAME can be
// built on the same layer — hence the two projection modes below, sprite UVs
// for atlases, and sort layers. Nothing here knows what a UI element is.
//
// COORDINATE CONVENTIONS (each mode uses its own domain's convention, on
// purpose — mixing them is what makes 2D APIs confusing):
//
//   BeginScreen(w, h)  origin TOP-LEFT, +x right, +y DOWN, units = pixels.
//                      Matches HTML/CSS and Unity UI Toolkit, which is what UI
//                      code and designers expect.
//
//   BeginWorld(cam)    origin at the camera, +x right, +y UP, units = world
//                      units. Matches every 2D game engine and the 3D engine's
//                      own +y-up world.
//
// BATCHING: draws accumulate into one CPU vertex buffer and flush when state
// must change (different texture, clip rect, or the buffer fills). Sort layer
// is applied as a stable sort before flushing, so a caller can emit in any
// order and still get correct back-to-front painting.
#include "../core/Core.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace MyCoreEngine {

    class Shader;
    class Font; // U1b

    // A 2D camera for world-space rendering (2D games). Screen-space UI does
    // not use one.
    struct Camera2D {
        glm::vec2 position{ 0.0f };  // world point at the centre of the view
        float     zoom = 1.0f;       // >1 magnifies; must stay > 0
        float     rotationDeg = 0.0f;
    };

    // Sub-rectangle of a texture, in normalised UVs. Default = the whole thing.
    struct TexRegion {
        glm::vec2 uvMin{ 0.0f, 0.0f };
        glm::vec2 uvMax{ 1.0f, 1.0f };
    };

    class ENGINE_API Renderer2D {
    public:
        Renderer2D();
        ~Renderer2D();
        Renderer2D(const Renderer2D&) = delete;
        Renderer2D& operator=(const Renderer2D&) = delete;

        // Creates GL objects (VAO/VBO/IBO, shader, 1x1 white texture). Safe to
        // call twice; the second call is a no-op. Needs a current GL context.
        bool Init();
        void Shutdown();
        bool IsReady() const { return ready_; }

        // ---- frame ----
        // Screen space: pixels, origin top-left, +y down.
        void BeginScreen(int widthPx, int heightPx);
        // World space: world units, origin at the camera, +y up. The viewport
        // size sets the aspect so a square stays square.
        void BeginWorld(const Camera2D& cam, int viewportW, int viewportH);
        // Flushes everything and restores the GL state that was in effect at
        // Begin. The 3D pipeline runs passes in a bare loop with no inter-pass
        // reset, so leaving blend/depth/cull changed would corrupt the next
        // pass and the next frame's opaque draw.
        void End();

        // ---- draws ----
        // Axis-aligned, untextured. `size` is (w,h) from `pos` along the mode's
        // axes; in screen mode that means pos is the TOP-LEFT corner.
        void DrawQuad(const glm::vec2& pos, const glm::vec2& size,
                      const glm::vec4& color, int layer = 0);
        // Textured. texture 0 draws untextured (the built-in white pixel).
        void DrawSprite(const glm::vec2& pos, const glm::vec2& size,
                        unsigned texture, const TexRegion& region = {},
                        const glm::vec4& tint = glm::vec4(1.0f), int layer = 0);
        // Rotated about `origin`, expressed as a fraction of the quad
        // (0,0 = the pos corner, 0.5,0.5 = centre).
        void DrawSpriteRotated(const glm::vec2& pos, const glm::vec2& size,
                               float rotationDeg, const glm::vec2& origin,
                               unsigned texture, const TexRegion& region = {},
                               const glm::vec4& tint = glm::vec4(1.0f), int layer = 0);

        // ---- clipping ----
        // Rectangles nest: a pushed rect is INTERSECTED with the one below, so a
        // child can never draw outside its parent (which is what a scroll view
        // or a clipped panel needs). Screen-space pixels in both modes, because
        // that is what the scissor test operates on.
        void PushClipRect(const glm::vec2& posPx, const glm::vec2& sizePx);
        void PopClipRect();

        // ---- stats (per frame, reset by Begin*) ----
        struct Stats {
            int drawCalls = 0;
            int quads = 0;
            int flushes = 0; // state-change flushes; drawCalls == flushes
        };
        const Stats& stats() const { return stats_; }

        // The built-in 1x1 opaque white texture. Untextured draws use it so
        // every quad takes one shader path.
        unsigned whiteTexture() const { return whiteTex_; }

    private:
        struct Vertex {
            glm::vec2 pos;
            glm::vec2 uv;
            glm::vec4 color;
        };
        struct Cmd {          // one quad, before sorting
            Vertex   v[4];
            unsigned texture;
            int      layer;
            int      seq;     // submission order: keeps the sort stable
            int      clip;    // index into clipStack_ history, -1 = none
        };
        struct ClipRect { glm::vec2 pos, size; };

        void beginCommon_(const glm::mat4& viewProj, int vpW, int vpH);
        void flush_();
        void pushQuad_(const Vertex v[4], unsigned texture, int layer);

        bool ready_ = false;
        unsigned vao_ = 0, vbo_ = 0, ibo_ = 0, whiteTex_ = 0;
        std::unique_ptr<Shader> shader_;

        std::vector<Cmd>    cmds_;
        std::vector<Vertex> verts_;   // scratch, reused across frames
        std::vector<ClipRect> clipStack_;
        std::vector<ClipRect> clipHistory_; // resolved (intersected) rects
        int   curClip_ = -1;
        int   seq_ = 0;
        bool  inFrame_ = false;
        int   vpW_ = 0, vpH_ = 0;
        glm::mat4 viewProj_{ 1.0f };
        Stats stats_{};

        // GL state captured at Begin and restored by End.
        struct SavedGL {
            unsigned char blend, depthTest, cullFace, scissor, depthMask;
            int blendSrcRGB, blendDstRGB, blendSrcA, blendDstA;
            int viewport[4];
            int scissorBox[4];
        } saved_{};
    };

} // namespace MyCoreEngine
