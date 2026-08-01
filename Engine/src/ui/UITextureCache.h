#pragma once
// GL textures for `background-image`, keyed by interned path id.
//
// WHY THIS EXISTS AT ALL, rather than a handle on the element: a hot reload
// destroys and rebuilds every UIElement, and UIStyleSheet::Recascade resets
// Style to defaults on every :hover edge. A GL name living in either would be
// re-uploaded on every reload and lost on every mouse move. Style carries an
// interned PATH id — reconstructible by the cascade, trivially copyable — and
// this turns that id into a texture exactly once.
//
// OWNED BY THE HOST, one per GL context, and handed to UIWorld the same way the
// font is. Not a global: the editor runs a SECOND renderer for its Game view,
// and a process-wide cache would hand one context's texture names to the other.
//
// Every entry is resolved LAZILY, on the first frame that paints it, and cached
// including the failures — a missing file must be reported once, not once per
// frame forever.
#include "../core/Core.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace MyCoreEngine::ui {

    class ENGINE_API UITextureCache {
    public:
        UITextureCache() = default;
        UITextureCache(const UITextureCache&) = delete;
        UITextureCache& operator=(const UITextureCache&) = delete;
        // Frees every texture. MUST run while the GL context is still current;
        // the host owns this object and therefore owns that ordering.
        ~UITextureCache();

        struct Entry {
            unsigned  texture = 0;    // 0 = unavailable (missing, or failed to decode)
            glm::ivec2 size{ 0, 0 };  // pixels, for `cover`
        };

        // Uploads on first ask, then returns the cached answer — including a
        // cached FAILURE, so a missing file costs one message and one map hit
        // rather than a decode attempt every frame.
        const Entry& Get(int assetPathId);

        // Drops every texture. For a host tearing down its context while the
        // cache outlives it, and for tests.
        void Clear();

        // Diagnostics collected during lazy loads, drained by the host the same
        // way UIAssetDocument drains the binder's. Cleared by the read.
        std::vector<std::string> TakeErrors();

        std::size_t size() const { return entries_.size(); }

    private:
        std::unordered_map<int, Entry> entries_;
        std::vector<std::string>       errors_;
    };

} // namespace MyCoreEngine::ui
