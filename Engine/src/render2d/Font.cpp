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

namespace {

    constexpr std::uint32_t kSoftHyphen = 0x00ADu;

    // Every place a line COULD end, found in one pass over the text.
    //
    // Separating "where may I break" from "where do I break" is what lets the
    // greedy and balanced fits share every rule about spaces, hyphens and
    // authored newlines -- the two differ only in which subset they choose, so
    // they cannot drift on what a legal break is.
    struct Opp {
        std::size_t end = 0;      // byte where the line ends (exclusive)
        std::size_t resume = 0;   // byte the NEXT line starts at
        // Cumulative advance from the paragraph start, at each of those two
        // points. A line's width is one line's cumEnd minus the previous
        // opportunity's cumResume, which is what drops the spaces between them
        // from both sides.
        float cumEnd = 0.0f, cumResume = 0.0f;
        bool  hyphen = false;     // ends at a soft hyphen: add the '-'
        bool  mandatory = false;  // an authored '\n'
    };

} // namespace

void Font::WrapLines(const std::string& utf8, float maxWidthPx, float scale,
                     std::vector<Line>& out) const {
    WrapOptions opt;
    opt.maxWidthPx = maxWidthPx;
    opt.scale = scale;
    WrapLines(utf8, opt, out);
}

void Font::WrapLines(const std::string& utf8, const WrapOptions& opt,
                     std::vector<Line>& out) const {
    if (!IsValid()) return;
    const bool bounded = opt.maxWidthPx > 0.0f;
    const Glyph* hyphenGlyph = FindGlyph('-');
    const float hyphenW = hyphenGlyph ? hyphenGlyph->advance * opt.scale : 0.0f;

    // ---- 1. every break opportunity, plus a sentinel for the start ---------
    std::vector<Opp> opps;
    opps.push_back(Opp{});                 // the paragraph start
    float cum = 0.0f;
    for (std::size_t i = 0; i < utf8.size();) {
        const std::size_t cpStart = i;
        const std::uint32_t cp = stepUTF8(utf8, i);

        if (cp == '\n') {
            Opp o;
            o.end = cpStart;
            o.cumEnd = cum;
            o.resume = i;
            o.cumResume = cum;             // the newline itself has no advance
            o.mandatory = true;
            opps.push_back(o);
            continue;
        }

        if (cp == ' ') {
            Opp o;
            o.end = cpStart;
            o.cumEnd = cum;
            // Swallow the whole run, so two spaces do not indent the next line.
            std::size_t j = i;
            cum += FindGlyph(' ') ? FindGlyph(' ')->advance * opt.scale : 0.0f;
            while (j < utf8.size()) {
                const std::size_t k = j;
                if (stepUTF8(utf8, j) != ' ') { j = k; break; }
                cum += FindGlyph(' ') ? FindGlyph(' ')->advance * opt.scale : 0.0f;
            }
            i = j;
            o.resume = j;
            o.cumResume = cum;
            opps.push_back(o);
            continue;
        }

        if (cp == kSoftHyphen) {
            // Invisible unless used: it has no glyph in the atlas, so it adds
            // nothing to `cum` and DrawText skips it without stalling the pen.
            // Breaking here costs the '-' that gets drawn in its place.
            if (opt.softHyphens) {
                Opp o;
                // The line ends BEFORE the hyphen bytes and the next begins
                // AFTER them, so a Line range is always exactly what gets
                // drawn -- the invisible codepoint belongs to neither side.
                // Leaving it inside the slice would work today only because
                // the atlas is ASCII and has no glyph for it.
                o.end = cpStart;
                o.cumEnd = cum;
                o.resume = i;
                o.cumResume = cum;
                o.hyphen = true;
                opps.push_back(o);
            }
            continue;
        }

        if (const Glyph* g = FindGlyph(cp)) cum += g->advance * opt.scale;
    }
    // The paragraph end is always a break, and never a hyphen.
    Opp last;
    last.end = utf8.size();
    last.resume = utf8.size();
    last.cumEnd = last.cumResume = cum;
    opps.push_back(last);

    const std::size_t n = opps.size();
    const auto lineWidth = [&](std::size_t from, std::size_t to) {
        return opps[to].cumEnd - opps[from].cumResume +
               (opps[to].hyphen ? hyphenW : 0.0f);
    };

    // ---- 2. choose which opportunities become breaks -----------------------
    // `prev[j]` is the opportunity a line ending at j starts from.
    std::vector<std::size_t> prev(n, 0);
    if (!bounded) {
        // Nothing to fit: only the mandatory breaks survive.
        std::size_t from = 0;
        for (std::size_t j = 1; j < n; ++j) {
            if (opps[j].mandatory || j == n - 1) { prev[j] = from; from = j; }
        }
    } else if (opt.fit == WrapFit::Balanced) {
        // Minimum raggedness by dynamic programming over opportunities.
        // EVERY line is counted, the last one included: exempting it (as
        // Knuth-Plass does, where a justified final line is set flush-left and
        // its slack is not raggedness) makes this degenerate to greedy for any
        // two-line paragraph -- the only free choice is where the first line
        // ends, and greedy already puts as much as possible there.
        constexpr float kInf = 1e30f;
        std::vector<float> best(n, kInf);
        best[0] = 0.0f;
        for (std::size_t j = 1; j < n; ++j) {
            for (std::size_t i = j; i-- > 0;) {
                if (best[i] >= kInf) continue;
                const float w = lineWidth(i, j);
                // Once a candidate start is too far back to fit, every earlier
                // one is too, so the scan can stop.
                if (w > opt.maxWidthPx && j - i > 1) break;
                // THE LAST LINE COUNTS. Knuth-Plass frees it because in
                // JUSTIFIED text a final short line is set flush-left and its
                // slack is not raggedness -- but nothing here is justified, and
                // freeing it makes the whole thing degenerate to greedy for any
                // two-line paragraph: the cheapest answer becomes "pack line
                // one as full as possible", which is greedy exactly. Counting
                // it is what makes a heading split evenly instead of stranding
                // one word underneath.
                const float slack = opt.maxWidthPx - w;
                float cost = slack * slack;
                // A hyphenated break is legal but not free, or a balanced
                // paragraph would hyphenate every line it could.
                if (opps[j].hyphen) cost += 2500.0f;
                const float total = best[i] + cost;
                if (total < best[j]) { best[j] = total; prev[j] = i; }
                // A mandatory break cannot be skipped over: a line may not
                // contain an authored newline.
                if (opps[i].mandatory && i != 0) break;
            }
            // Unreachable (one segment longer than the whole line): fall back
            // to the nearest predecessor so emit_ can hard-break it.
            if (best[j] >= kInf) { best[j] = best[j - 1]; prev[j] = j - 1; }
        }
    } else {
        // Greedy: the furthest opportunity that still fits.
        std::size_t from = 0;
        for (std::size_t j = 1; j < n; ++j) {
            const bool fits = lineWidth(from, j) <= opt.maxWidthPx;
            const bool forced = opps[j].mandatory || j == n - 1;
            if (fits && !forced) continue;
            if (!fits && j - 1 > from) {
                prev[j - 1] = from;         // the last one that DID fit
                from = j - 1;
                --j;                        // reconsider this one from the new start
                continue;
            }
            prev[j] = from;
            from = j;
        }
    }

    // ---- 3. emit, hard-breaking anything that still does not fit -----------
    std::vector<std::size_t> chosen;
    for (std::size_t j = n - 1; j > 0; j = prev[j]) chosen.push_back(j);
    std::reverse(chosen.begin(), chosen.end());

    std::size_t from = 0;
    for (const std::size_t j : chosen) {
        std::size_t begin = opps[from].resume;
        const std::size_t stop = opps[j].end;

        // A single unbreakable run wider than the line. Walked forward a glyph
        // at a time, because there is no opportunity inside it to use.
        //
        // The walk also RE-MEASURES the tail, and it has to: the segment width
        // from the opportunity table describes the whole run, so reusing it for
        // whatever survived the last hard break reported a line three times its
        // real length -- and a width that disagrees with the glyphs is a
        // right-aligned line that hangs off its box.
        float tail = 0.0f;
        if (bounded) {
            std::size_t k = begin;
            std::size_t lineStart = begin;
            while (k < stop) {
                const std::size_t cpStart = k;
                const std::uint32_t cp = stepUTF8(utf8, k);
                const Glyph* g = (cp == kSoftHyphen) ? nullptr : FindGlyph(cp);
                const float adv = g ? g->advance * opt.scale : 0.0f;
                if (tail + adv > opt.maxWidthPx && cpStart > lineStart) {
                    out.push_back(Line{ lineStart, cpStart, tail, false });
                    lineStart = cpStart;
                    tail = 0.0f;
                }
                tail += adv;
            }
            begin = lineStart;
        }
        const float finalW = bounded ? tail + (opps[j].hyphen ? hyphenW : 0.0f)
                                     : lineWidth(from, j);
        out.push_back(Line{ begin, stop, finalW, opps[j].hyphen });
        from = j;
    }
    if (out.empty()) out.push_back(Line{ 0, utf8.size(), cum, false });
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
    // A stable row height even for "" -- layout needs a line box regardless.
    if (!IsValid()) return { 0.0f, 0.0f };
    float widest = 0.0f, line = 0.0f;
    int lines = 1;
    for (std::size_t i = 0; i < utf8.size();) {
        const std::uint32_t cp = stepUTF8(utf8, i);
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

glm::vec2 Font::MeasureWrapped(const std::string& utf8, float maxWidthPx,
                               float scale) const {
    WrapOptions opt;
    opt.maxWidthPx = maxWidthPx;
    opt.scale = scale;
    return MeasureWrapped(utf8, opt);
}

glm::vec2 Font::MeasureWrapped(const std::string& utf8, const WrapOptions& opt) const {
    if (!IsValid()) return { 0.0f, 0.0f };
    std::vector<Line> lines;
    WrapLines(utf8, opt, lines);
    float widest = 0.0f;
    for (const Line& l : lines) widest = std::max(widest, l.width);
    const int n = lines.empty() ? 1 : int(lines.size());
    return { widest, lineHeight_ * opt.scale * float(n) };
}

} // namespace MyCoreEngine
