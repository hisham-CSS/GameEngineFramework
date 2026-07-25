#pragma once
// A UIDocument backed by markup + stylesheet ASSETS, reloaded when either file
// changes on disk.
//
// This is the payoff of authoring UI as data: edit hud.uss, alt-tab, and the
// running game has the new look — no rebuild, no restart, no losing the state
// you were testing.
//
// Reloading REBUILDS THE TREE, so every element pointer and every event handler
// the app registered is invalidated. That is why Load takes a bind callback:
// it runs after each successful (re)load and is the one place an app re-caches
// pointers and re-attaches behaviour. Forgetting to re-bind is the obvious
// failure mode of any hot-reload system, so the API makes it the only way in.
#include "../core/Core.h"
#include "UIElement.h"
#include "UIStyleSheet.h"

#include <functional>
#include <string>
#include <vector>

namespace MyCoreEngine::ui {

    class ENGINE_API UIAssetDocument {
    public:
        // Runs after every successful (re)load, with a freshly built tree.
        using BindFn = std::function<void(UIDocument&)>;

        // `stylePath` may be empty for markup with no stylesheet. Returns false
        // if the markup could not be loaded (a bad STYLESHEET is not fatal —
        // the structure still loads and the errors are reported, which is more
        // useful than a blank screen).
        bool Load(const std::string& markupPath, const std::string& stylePath,
                  BindFn bind = {});

        // Call once per frame. Cheap: it only stats the files every
        // pollInterval seconds, and reloads only when a timestamp or size
        // actually changed. Returns true if a reload happened this call.
        bool Update(float dt);

        void SetHotReloadEnabled(bool on) { hotReload_ = on; }
        bool hotReloadEnabled() const { return hotReload_; }
        void SetPollInterval(float seconds) { pollInterval_ = seconds; }

        // Forces a reload regardless of timestamps.
        bool Reload();

        UIDocument& document() { return doc_; }
        const UIDocument& document() const { return doc_; }
        const UIStyleSheet& styleSheet() const { return sheet_; }

        // Diagnostics from the last load attempt (markup and stylesheet).
        const std::vector<std::string>& errors() const { return errors_; }
        bool ok() const { return errors_.empty(); }

    private:
        struct Stamp {
            long long writeTime = 0;
            unsigned long long size = 0;
            bool operator!=(const Stamp& o) const {
                return writeTime != o.writeTime || size != o.size;
            }
        };
        static Stamp stampOf(const std::string& path);

        UIDocument   doc_;
        UIStyleSheet sheet_;
        std::string  markupPath_, stylePath_;
        BindFn       bind_;
        Stamp        markupStamp_{}, styleStamp_{};
        std::vector<std::string> errors_;
        float pollAccum_ = 0.0f;
        float pollInterval_ = 0.25f;
        bool  hotReload_ = true;
        bool  loaded_ = false;
    };

} // namespace MyCoreEngine::ui
