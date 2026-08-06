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

namespace {

    // ONE decode step, shared by DecodeUTF8 and WrapLines.
    //
    // Factored out the moment there were two callers: wrapping needs the BYTE
    // OFFSET of each codepoint, which the vector-returning decoder throws away,
    // and a second hand-written decoder beside this one is exactly how a
    // wrapper ends up disagreeing with a renderer about where a character
    // starts. Advances `i` past the codepoint and returns it; invalid input
    // yields U+FFFD and consumes one byte, so a bad string always terminates.
    std::uint32_t stepUTF8(const std::string& s, std::size_t& i) {
        const unsigned char* p = reinterpret_cast<const unsigned char*>(s.data());
        const std::size_t n = s.size();
        const unsigned char c = p[i];
        std::uint32_t cp = 0;
        int extra = 0;
        if (c < 0x80)                { cp = c;           extra = 0; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1Fu;   extra = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0Fu;   extra = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07u;   extra = 3; }
        else { ++i; return 0xFFFDu; }                     // stray continuation
        if (i + std::size_t(extra) >= n) { i = n; return 0xFFFDu; }  // truncated
        for (int k = 1; k <= extra; ++k) {
            const unsigned char cc = p[i + std::size_t(k)];
            if ((cc & 0xC0) != 0x80) { ++i; return 0xFFFDu; }
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        i += std::size_t(extra) + 1;
        return cp;
    }

} // namespace

std::vector<std::uint32_t> Font::DecodeUTF8(const std::string& s) {
    std::vector<std::uint32_t> out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) out.push_back(stepUTF8(s, i));
    return out;
}

void Font::WrapLines(const std::string& utf8, float maxWidthPx, float scale,
                     std::vector<Line>& out) const {
    if (!IsValid()) return;
    const bool bounded = maxWidthPx > 0.0f;

    std::size_t lineBegin = 0;      // first byte of the line being built
    float lineW = 0.0f;             // its width so far
    // The last place a break could go: end of the last word, and where to
    // resume after it. Kept as a pair because they differ by the run of spaces
    // that must belong to NEITHER line.
    std::size_t breakEnd = 0, breakResume = 0;
    bool haveBreak = false;
    float breakW = 0.0f;

    std::size_t i = 0;
    while (i < utf8.size()) {
        const std::size_t cpStart = i;
        const std::uint32_t cp = stepUTF8(utf8, i);

        if (cp == '\n') {
            out.push_back(Line{ lineBegin, cpStart, lineW });
            lineBegin = i;
            lineW = 0.0f;
            haveBreak = false;
            continue;
        }

        const Glyph* g = FindGlyph(cp);
        const float adv = g ? g->advance * scale : 0.0f;

        if (cp == ' ') {
            // A break opportunity ENDS the word before it; the spaces
            // themselves are dropped from both sides.
            breakEnd = cpStart;
            breakW = lineW;
            // Swallow the whole run, so two spaces do not start the next line
            // with one.
            std::size_t j = i;
            while (j < utf8.size()) {
                const std::size_t k = j;
                if (stepUTF8(utf8, j) != ' ') { j = k; break; }
            }
            breakResume = j;
            haveBreak = true;
            lineW += adv;
            continue;
        }

        // Would this glyph overflow? Compared BEFORE adding it, so the line
        // that is emitted actually fits.
        if (bounded && lineW + adv > maxWidthPx && cpStart > lineBegin) {
            if (haveBreak && breakEnd > lineBegin) {
                out.push_back(Line{ lineBegin, breakEnd, breakW });
                lineBegin = breakResume;
                // Re-measure the tail: the partial word already consumed since
                // the break has to be carried onto the new line.
                lineW = 0.0f;
                for (std::size_t k = lineBegin; k < cpStart;) {
                    const std::uint32_t c2 = stepUTF8(utf8, k);
                    if (const Glyph* g2 = FindGlyph(c2)) lineW += g2->advance * scale;
                }
            } else {
                // A word longer than the whole line: break inside it.
                out.push_back(Line{ lineBegin, cpStart, lineW });
                lineBegin = cpStart;
                lineW = 0.0f;
            }
            haveBreak = false;
        }
        lineW += adv;
    }

    // The last line, always -- including the empty one after a trailing '\n',
    // because a paragraph that ends in a break really does have a blank line.
    out.push_back(Line{ lineBegin, utf8.size(), lineW });
}

glm::vec2 Font::MeasureWrapped(const std::string& utf8, float maxWidthPx,
                               float scale) const {
    if (!IsValid()) return { 0.0f, 0.0f };
    std::vector<Line> lines;
    WrapLines(utf8, maxWidthPx, scale, lines);
    float widest = 0.0f;
    for (const Line& l : lines) widest = std::max(widest, l.width);
    const int n = lines.empty() ? 1 : int(lines.size());
    return { widest, lineHeight_ * scale * float(n) };
}

void Font::AppendUTF8(std::string& out, std::uint32_t cp) {
    // Surrogates are not characters; a lone one reaching a font atlas or a
    // saved file is a corrupt string, so substitute rather than encode it.
    if (cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) cp = 0xFFFDu;
    if (cp < 0x80u) {
        out += char(cp);
    } else if (cp < 0x800u) {
        out += char(0xC0u | (cp >> 6));
        out += char(0x80u | (cp & 0x3Fu));
    } else if (cp < 0x10000u) {
        out += char(0xE0u | (cp >> 12));
        out += char(0x80u | ((cp >> 6) & 0x3Fu));
        out += char(0x80u | (cp & 0x3Fu));
    } else {
        out += char(0xF0u | (cp >> 18));
        out += char(0x80u | ((cp >> 12) & 0x3Fu));
        out += char(0x80u | ((cp >> 6) & 0x3Fu));
        out += char(0x80u | (cp & 0x3Fu));
    }
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
