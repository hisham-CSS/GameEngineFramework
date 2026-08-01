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
#include <string>
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

        // ---- text ----
        // `pos` is the TOP-LEFT of the text box, not the baseline — that is the
        // CSS box convention, so it drops straight into a layout rect without
        // the caller doing baseline maths. '\n' starts a new line.
        // Glyphs batch with everything else: the atlas is swizzled to read as
        // (1,1,1,coverage), so a glyph is just a white sprite with an alpha mask
        // and needs no separate shader path.
        void DrawText(const Font& font, const std::string& utf8,
                      const glm::vec2& pos, const glm::vec4& color,
                      int layer = 0, float scale = 1.0f);

        // Rounded corners and a border, as ONE signed-distance evaluation in
        // the fragment shader: the border is the band of the field within
        // `borderPx` of the edge, so it needs no second quad and no second
        // layer. A child element's layer can never slip between an element's
        // fill and its own border.
        //
        // The radius is CLAMPED in the shader to min(halfW, halfH), so a
        // radius of 9999 is a stadium rather than an erased element.
        struct BoxStyle {
            float     radiusPx = 0.0f;
            float     borderPx = 0.0f;
            glm::vec4 borderColor{ 0.0f };   // straight alpha, composited OVER the fill
        };

        // Rounded / bordered / textured box.
        //
        // DEGRADES to DrawSprite when the box needs neither a radius nor a
        // border, so a plain background costs exactly what it costs today and
        // never touches the second vertex stream. Also degrades when the box
        // shader is unavailable — see supportsRoundedBoxes.
        void DrawBox(const glm::vec2& pos, const glm::vec2& size,
                     const glm::vec4& fill, const BoxStyle& box,
                     unsigned texture = 0, const TexRegion& region = {},
                     int layer = 0);

        // False when the box shader pair failed to compile, in which case
        // DrawBox paints plain squares. Exposed so a host can say so ONCE
        // rather than shipping a UI that quietly lost its corners.
        //
        // The box shader is deliberately optional: Init returning false for a
        // cosmetic feature would take UIPass down and blank every document.
        bool supportsRoundedBoxes() const { return roundedReady_; }

        // ---- clipping ----
        // Rectangles nest: a pushed rect is INTERSECTED with the one below, so a
        // child can never draw outside its parent (which is what a scroll view
        // or a clipped panel needs). Screen-space pixels in both modes, because
        // that is what the scissor test operates on.
        //
        // PRECONDITION: Begin* was given the FULL framebuffer size and the GL
        // viewport is at the origin. The scissor test works in window pixels
        // regardless of the projection, so a shifted viewport leaves the
        // geometry right and only the CLIPPING wrong — a failure that looks like
        // a layout bug. UIPass is the only production caller and honours it.
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
        // Which vertex stream (and therefore which shader) a quad belongs to.
        //
        // A discriminant on Cmd rather than a wider Vertex, because flush_
        // stable_sorts Cmd BY VALUE: widening Vertex to carry the box fields
        // would take sizeof(Cmd) from 144 to 304 bytes and more than double the
        // sort cost of every frame — a bill paid overwhelmingly by GLYPHS,
        // which are the great majority of quads and need none of it.
        //
        // And it would buy a property this renderer does not have. The run
        // already breaks on a texture change, and an element's background uses
        // the white texture while its label uses the font atlas, so a panel
        // with a label has always been two draw calls.
        enum class QuadKind : int { Plain = 0, Box = 1 };
        // One quad of the box stream. The last four members are per-QUAD
        // constants replicated to all four corners: GL 3.3 core has no SSBO and
        // a UBO would cap a batch at the block size.
        struct BoxVertex {
            glm::vec2 pos;
            glm::vec2 uv;
            glm::vec4 color;
            // Unrotated quad-local px from the CENTRE. Stays unrotated on
            // purpose: a rotated box bakes its rotation into `pos` and leaves
            // this alone, which is exactly the frame the SDF is evaluated in.
            glm::vec2 local;
            glm::vec2 half;
            glm::vec4 border;
            glm::vec2 shape;   // x = radius px, y = border width px
        };
        struct Cmd {          // one quad, before sorting
            Vertex   v[4];
            unsigned texture;
            int      layer;
            int      seq;     // submission order: keeps the sort stable
            int      clip;    // index into clipStack_ history, -1 = none
            QuadKind kind = QuadKind::Plain;
            int      box = -1;   // index into boxVerts_ (4 entries), -1 = Plain
        };
        struct ClipRect { glm::vec2 pos, size; };

        void beginCommon_(const glm::mat4& viewProj, int vpW, int vpH);
        void flush_();
        void pushQuad_(const Vertex v[4], unsigned texture, int layer);
        void pushBoxQuad_(const BoxVertex v[4], unsigned texture, int layer);

        bool ready_ = false;
        bool roundedReady_ = false;
        unsigned vao_ = 0, vbo_ = 0, ibo_ = 0, whiteTex_ = 0;
        unsigned boxVao_ = 0, boxVbo_ = 0;
        std::unique_ptr<Shader> shader_;
        std::unique_ptr<Shader> boxShader_;

        std::vector<Cmd>    cmds_;
        std::vector<Vertex> verts_;   // scratch, reused across frames
        // Submission-ordered, 4 per box quad; Cmd::box indexes it. Kept beside
        // the commands rather than inside them so a glyph Cmd stays 148 bytes.
        std::vector<BoxVertex> boxVerts_;
        std::vector<BoxVertex> boxScratch_;
        std::vector<ClipRect> clipStack_;
        std::vector<ClipRect> clipHistory_; // resolved (intersected) rects
        // History index per stack level, so Pop restores its parent in O(1)
        // instead of searching clipHistory_ backwards by float equality.
        std::vector<int>      clipStackIdx_;
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
