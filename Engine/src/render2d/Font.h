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

        // Decodes UTF-8 into codepoints. Invalid bytes yield U+FFFD rather than
        // throwing or truncating, so hostile/mojibake text degrades visibly
        // instead of silently dropping the rest of the string.
        static std::vector<std::uint32_t> DecodeUTF8(const std::string& s);

    private:
        std::unordered_map<std::uint32_t, Glyph> glyphs_;
        unsigned atlasTex_ = 0;
        int   atlasW_ = 0, atlasH_ = 0;
        float ascent_ = 0.0f, descent_ = 0.0f, lineHeight_ = 0.0f;
        float pixelHeight_ = 0.0f;
    };

} // namespace MyCoreEngine
