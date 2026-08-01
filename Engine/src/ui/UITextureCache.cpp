#include <glad/glad.h>

#include "UITextureCache.h"

#include "UIAssetPath.h"

#include "stb_image.h"

namespace MyCoreEngine::ui {

UITextureCache::~UITextureCache() { Clear(); }

void UITextureCache::Clear() {
    for (auto& [id, e] : entries_) {
        if (e.texture) glDeleteTextures(1, &e.texture);
    }
    entries_.clear();
}

std::vector<std::string> UITextureCache::TakeErrors() {
    std::vector<std::string> out;
    out.swap(errors_);
    return out;
}

const UITextureCache::Entry& UITextureCache::Get(int assetPathId) {
    // Cached INCLUDING failures. A missing file that re-attempted its decode
    // every frame would cost a file open per element per frame and print the
    // same line forever; one entry with texture == 0 answers it in a map hit.
    auto it = entries_.find(assetPathId);
    if (it != entries_.end()) return it->second;

    Entry e;
    const std::string& path = AssetPathOf(assetPathId);
    if (path.empty()) return entries_.emplace(assetPathId, e).first->second;

    // The path was already checked with PathIsContained when the stylesheet
    // parsed it, which is the only place it can be authored — so this open is
    // of a path the sandbox already approved.
    int w = 0, h = 0, comp = 0;
    // Bottom-up, matching the glyph atlas and the UI's top-left convention:
    // Renderer2D's screen mode puts +y down, so an unflipped load would paint
    // every image upside down.
    stbi_set_flip_vertically_on_load(0);
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &comp, 4);
    if (!pixels || w <= 0 || h <= 0) {
        const char* why = stbi_failure_reason();
        errors_.push_back("background-image: cannot load '" + path + "'" +
                          (why ? std::string(" (") + why + ")" : std::string()));
        if (pixels) stbi_image_free(pixels);
        return entries_.emplace(assetPathId, e).first->second;
    }

    glGenTextures(1, &e.texture);
    glBindTexture(GL_TEXTURE_2D, e.texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    // LINEAR, unlike the 1x1 white texture: a background image is stretched or
    // cropped to a box that is almost never its native size, and NEAREST there
    // is visibly blocky. CLAMP so `cover`'s sub-rect cannot wrap a stray column
    // of the opposite edge into the crop.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(pixels);

    e.size = { w, h };
    return entries_.emplace(assetPathId, e).first->second;
}

} // namespace MyCoreEngine::ui
