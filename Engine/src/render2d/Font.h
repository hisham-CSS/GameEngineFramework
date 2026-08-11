#pragma once
// Bitmap glyph atlas baked from a TrueType file with stb_truetype.
//
// No new dependency: stb_truetype.h and stb_rect_pack.h already ship with the
// engine's existing `stb` package (implementations live in
// Engine/src/thirdparty/stb_impl.cpp).
//
// A Font is baked at ONE pixel height and is a GL resource, so it belongs to
// the thread with the context. Drawing at a different size scales the baked
// glyphs, which is fine for modest ratios and goes soft when magnified a lot —
// bake a second Font at the larger size if that matters. (A future SDF/MSDF
// path would remove the limit; msdfgen is available in vcpkg if it ever earns
// its complexity.)
#include "../core/Core.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace MyCoreEngine {

    // The size both hosts bake the UI font at, and therefore the unit that
    // `font-scale` in a .cstyle multiplies: 0.375 is 18px, 1.0 is 48px.
    //
    // Deliberately large. A menu title wants ~40px, and there is no font-size,
    // font-family or font-weight property in the stylesheet language -- the
    // atlas IS the size, so magnifying a small one is the only alternative and
    // it looks it. Downsampling from 48 costs nothing and makes body text
    // sharper too. The atlas auto-grows through 512..4096 (Font.cpp), so a
    // larger bake just picks a bigger one.
    //
    // Here rather than in each host because it is half of a contract whose
    // other half is authored in a text file: change this and every font-scale
    // in every .cstyle silently means something else. kUIFontBaseScale is the
    // value a stylesheet uses to mean "ordinary text", and the two are checked
    // against each other in tests/test_ui_stylesheet.cpp.
    constexpr float kUIFontAtlasPixels = 48.0f;
    constexpr float kUIFontBodyPixels  = 18.0f;
    constexpr float kUIFontBaseScale   = kUIFontBodyPixels / kUIFontAtlasPixels;

    // One packed glyph, in pixels at the baked height.
    struct Glyph {
        glm::vec2 uvMin{ 0.0f };   // atlas UVs
        glm::vec2 uvMax{ 0.0f };
        glm::vec2 size{ 0.0f };    // quad size to draw
        glm::vec2 offset{ 0.0f };  // from the pen (baseline) to the quad's TOP-LEFT,
                                   // +y DOWN to match screen space
        float     advance = 0.0f;  // pen movement to the next glyph
    };

    class ENGINE_API Font {
    public:
        Font();
        ~Font();
        Font(const Font&) = delete;
        Font& operator=(const Font&) = delete;

        // Bakes printable ASCII (32..126) at `pixelHeight`. Returns false if the
        // file is missing/unreadable, is not a font, or the atlas could not be
        // packed — never throws, and leaves the object invalid but safe to use
        // (Measure returns 0, drawing emits nothing).
        //
        // The path is NOT sandboxed here: like every other asset loader, callers
        // that take a path from scene content must run PathIsContained first.
        bool LoadFromFile(const std::string& ttfPath, float pixelHeight);
        void Release();
        bool IsValid() const { return atlasTex_ != 0; }

        unsigned atlasTexture() const { return atlasTex_; }
        int  atlasWidth() const { return atlasW_; }
        int  atlasHeight() const { return atlasH_; }

        // Vertical metrics in pixels at the baked height. ascent is positive
        // (above the baseline), descent negative (below), matching stb.
        float ascent() const { return ascent_; }
        float descent() const { return descent_; }
        float lineHeight() const { return lineHeight_; }
        float bakedPixelHeight() const { return pixelHeight_; }

        // Null when the codepoint was not baked.
        const Glyph* FindGlyph(std::uint32_t codepoint) const;

        // Width/height of `utf8` laid out on one or more lines ('\n' breaks).
        // Height is lineHeight * lineCount, so an empty string measures
        // (0, lineHeight) rather than nothing — layout wants a stable row height.
        glm::vec2 Measure(const std::string& utf8, float scale = 1.0f) const;

        // ---- word wrap ------------------------------------------------------
        //
        // One line of wrapped text: a byte range into the ORIGINAL string, so
        // wrapping allocates one small vector rather than a string per line
        // every frame. `width` is that line's measured width, which the caller
        // needs anyway for alignment and for the widest-line measure.
        struct Line {
            std::size_t begin = 0, end = 0;   // [begin, end) bytes
            float       width = 0.0f;
            // The break was taken AT a soft hyphen, so this line renders with a
            // trailing '-' that is not in the source string. `width` already
            // includes it.
            bool        hyphen = false;
        };

        // HOW the breaks are chosen once the opportunities are known.
        enum class WrapFit : std::uint8_t {
            // Take the furthest break that fits, line by line. What every
            // browser did until recently, and what a caret or a scroller wants:
            // a line's contents never change because of text further down.
            Greedy,
            // Minimise total RAGGEDNESS over the whole paragraph -- the sum of
            // squared leftover space on every line but the last.
            //
            // This is Knuth-Plass with the stretch and shrink removed, and the
            // removal is forced rather than a simplification: DrawText walks
            // fixed glyph advances, so there is no variable inter-word space to
            // stretch and therefore no justification to optimise. Minimum
            // raggedness IS the paragraph-fitting problem for ragged-right
            // text, and it is what stops a heading leaving one word alone on
            // its second line.
            Balanced,
        };

        struct WrapOptions {
            float   maxWidthPx = 0.0f;   // <= 0 => no limit
            float   scale = 1.0f;
            // U+00AD SOFT HYPHEN becomes a break opportunity, and rendering a
            // break taken there appends a real '-'. Off by default so a string
            // carrying one is inert until the author asks.
            bool    softHyphens = false;
            WrapFit fit = WrapFit::Greedy;
        };

        // Word wrap, appending to `out`.
        //
        // BREAKS AT SPACES, and the trailing spaces stay OUT of the line: a
        // line whose measured width included the space that caused the break
        // would right-align and centre wrong, by exactly one space.
        //
        // A WORD LONGER THAN THE LINE is broken mid-word rather than allowed to
        // overflow. Refusing to break it is the other defensible choice and it
        // is the wrong one here: a URL or a 40-character asset path in a
        // 200px panel would silently paint across everything to its right,
        // because DrawText walks the pen with no clipping of its own.
        //
        // `maxWidthPx <= 0` means "no limit", so a caller with no constraint to
        // offer gets the same answer Measure gives.
        //
        // Explicit '\n' always breaks, and always starts a new line even when
        // the text would have fitted -- an authored break is an instruction,
        // not a hint.
        void WrapLines(const std::string& utf8, const WrapOptions& opt,
                       std::vector<Line>& out) const;
        // The plain form, kept because most callers have nothing to say beyond
        // a width and a scale.
        void WrapLines(const std::string& utf8, float maxWidthPx, float scale,
                       std::vector<Line>& out) const;

        // Measure(), but wrapped: the widest LINE by height * lineCount.
        // Same stable-row-height rule for the empty string.
        glm::vec2 MeasureWrapped(const std::string& utf8, float maxWidthPx,
                                 float scale = 1.0f) const;
        glm::vec2 MeasureWrapped(const std::string& utf8, const WrapOptions& opt) const;

        // Decodes UTF-8 into codepoints. Invalid bytes yield U+FFFD rather than
        // throwing or truncating, so hostile/mojibake text degrades visibly
        // instead of silently dropping the rest of the string.
        static std::vector<std::uint32_t> DecodeUTF8(const std::string& s);
        // The inverse. Lives here beside the decoder so the two cannot drift.
        // Needed by every host that receives typed input as CODEPOINTS (GLFW's
        // char callback, ImGui's input queue) but must hand the UI UTF-8.
        // A codepoint outside Unicode, or a surrogate, appends U+FFFD.
        static void AppendUTF8(std::string& out, std::uint32_t codepoint);

    private:
        std::unordered_map<std::uint32_t, Glyph> glyphs_;
        unsigned atlasTex_ = 0;
        int   atlasW_ = 0, atlasH_ = 0;
        float ascent_ = 0.0f, descent_ = 0.0f, lineHeight_ = 0.0f;
        float pixelHeight_ = 0.0f;
    };

} // namespace MyCoreEngine
