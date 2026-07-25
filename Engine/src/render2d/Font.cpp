#include <glad/glad.h>

#include "Font.h"

#include "stb_truetype.h" // implementation lives in thirdparty/stb_impl.cpp

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>

namespace MyCoreEngine {

namespace {
    // Printable ASCII. Deliberately the starting point rather than the ceiling:
    // the glyph map is keyed by codepoint and Measure/Draw decode UTF-8, so
    // widening this range later needs no API change.
    constexpr int kFirstChar = 32;
    constexpr int kCharCount = 95;   // 32..126 inclusive

    // Start small and retry bigger. A 512x512 R8 atlas holds ASCII comfortably
    // up to ~48px; the retry keeps large bakes working instead of silently
    // dropping glyphs, which is how packing failures usually present.
    constexpr int kAtlasSizes[] = { 512, 1024, 2048, 4096 };
}

Font::Font() = default;
Font::~Font() { Release(); }

void Font::Release() {
    if (atlasTex_) { glDeleteTextures(1, &atlasTex_); atlasTex_ = 0; }
    glyphs_.clear();
    atlasW_ = atlasH_ = 0;
    ascent_ = descent_ = lineHeight_ = pixelHeight_ = 0.0f;
}

bool Font::LoadFromFile(const std::string& ttfPath, float pixelHeight) {
    Release();
    if (pixelHeight <= 0.0f) return false;

    std::ifstream in(ttfPath, std::ios::binary);
    if (!in) {
        std::cerr << "[Font] cannot open '" << ttfPath << "'\n";
        return false;
    }
    std::vector<unsigned char> ttf((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
    if (ttf.empty()) {
        std::cerr << "[Font] '" << ttfPath << "' is empty\n";
        return false;
    }

    // Validate before packing: stbtt_PackFontRange on a non-font can walk
    // garbage offsets, and "not a font" is a far more common mistake than a
    // packing failure (wrong path, an .otf/CFF file, a truncated download).
    stbtt_fontinfo info{};
    const int offset = stbtt_GetFontOffsetForIndex(ttf.data(), 0);
    if (offset < 0 || !stbtt_InitFont(&info, ttf.data(), offset)) {
        std::cerr << "[Font] '" << ttfPath << "' is not a usable TrueType font\n";
        return false;
    }

    std::vector<stbtt_packedchar> packed(kCharCount);
    std::vector<unsigned char> bitmap;
    int chosen = 0;
    for (const int dim : kAtlasSizes) {
        bitmap.assign(size_t(dim) * size_t(dim), 0);
        stbtt_pack_context pc{};
        if (!stbtt_PackBegin(&pc, bitmap.data(), dim, dim, /*stride*/0,
                             /*padding*/1, nullptr)) {
            continue;
        }
        // 1x oversampling: UI text is drawn at integer positions and the atlas
        // is sampled 1:1, so the extra atlas area buys nothing here.
        stbtt_PackSetOversampling(&pc, 1, 1);
        const int ok = stbtt_PackFontRange(&pc, ttf.data(), 0, pixelHeight,
                                           kFirstChar, kCharCount, packed.data());
        stbtt_PackEnd(&pc);
        if (ok) { chosen = dim; break; }
    }
    if (!chosen) {
        std::cerr << "[Font] could not pack '" << ttfPath << "' at " << pixelHeight
                  << "px into any atlas up to 4096\n";
        return false;
    }

    atlasW_ = atlasH_ = chosen;
    pixelHeight_ = pixelHeight;

    // Vertical metrics, scaled to the baked height.
    int a = 0, d = 0, gap = 0;
    stbtt_GetFontVMetrics(&info, &a, &d, &gap);
    const float scale = stbtt_ScaleForPixelHeight(&info, pixelHeight);
    ascent_ = float(a) * scale;
    descent_ = float(d) * scale;              // negative
    lineHeight_ = float(a - d + gap) * scale;

    // Convert packed chars into our own glyph records so the rest of the engine
    // never sees an stb type.
    for (int i = 0; i < kCharCount; ++i) {
        const stbtt_packedchar& pcRec = packed[size_t(i)];
        Glyph g;
        g.uvMin = { float(pcRec.x0) / float(atlasW_), float(pcRec.y0) / float(atlasH_) };
        g.uvMax = { float(pcRec.x1) / float(atlasW_), float(pcRec.y1) / float(atlasH_) };
        g.size = { float(pcRec.x1 - pcRec.x0), float(pcRec.y1 - pcRec.y0) };
        // stb's xoff/yoff are relative to the pen at the BASELINE, +y down —
        // the same orientation as screen space, so they pass straight through.
        g.offset = { pcRec.xoff, pcRec.yoff };
        g.advance = pcRec.xadvance;
        glyphs_[std::uint32_t(kFirstChar + i)] = g;
    }

    // R8 coverage, swizzled to read as (1,1,1,coverage). That makes a glyph
    // behave exactly like a white sprite with an alpha mask, so text batches
    // through the SAME shader and draw path as every other quad — no font
    // branch in the fragment shader, and no 4x memory for an RGBA atlas.
    glGenTextures(1, &atlasTex_);
    glBindTexture(GL_TEXTURE_2D, atlasTex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // R8 rows are not 4-byte aligned
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, atlasW_, atlasH_, 0,
                 GL_RED, GL_UNSIGNED_BYTE, bitmap.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4); // restore the GL default
    const GLint swizzle[4] = { GL_ONE, GL_ONE, GL_ONE, GL_RED };
    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    return true;
}

const Glyph* Font::FindGlyph(std::uint32_t cp) const {
    auto it = glyphs_.find(cp);
    return (it == glyphs_.end()) ? nullptr : &it->second;
}

std::vector<std::uint32_t> Font::DecodeUTF8(const std::string& s) {
    std::vector<std::uint32_t> out;
    out.reserve(s.size());
    const unsigned char* p = reinterpret_cast<const unsigned char*>(s.data());
    const unsigned char* end = p + s.size();
    while (p < end) {
        const unsigned char c = *p;
        std::uint32_t cp = 0;
        int extra = 0;
        if (c < 0x80)            { cp = c;          extra = 0; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1Fu; extra = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0Fu; extra = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07u; extra = 3; }
        else { out.push_back(0xFFFDu); ++p; continue; } // stray continuation
        if (p + extra >= end) { out.push_back(0xFFFDu); break; } // truncated
        bool ok = true;
        for (int i = 1; i <= extra; ++i) {
            const unsigned char cc = p[i];
            if ((cc & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        if (!ok) { out.push_back(0xFFFDu); ++p; continue; }
        out.push_back(cp);
        p += extra + 1;
    }
    return out;
}

glm::vec2 Font::Measure(const std::string& utf8, float scale) const {
    // A stable row height even for "" — layout needs a line box regardless.
    if (!IsValid()) return { 0.0f, 0.0f };
    const auto cps = DecodeUTF8(utf8);
    float widest = 0.0f, line = 0.0f;
    int lines = 1;
    for (const std::uint32_t cp : cps) {
        if (cp == '\n') {
            widest = std::max(widest, line);
            line = 0.0f;
            ++lines;
            continue;
        }
        if (const Glyph* g = FindGlyph(cp)) line += g->advance * scale;
    }
    widest = std::max(widest, line);
    return { widest, lineHeight_ * scale * float(lines) };
}

} // namespace MyCoreEngine
