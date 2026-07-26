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
#include "UIBinding.h"
#include "UIDataSource.h"
#include "UIElement.h"
#include "UIInteractionStyler.h"
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

        // Where an app registers its data sources, actions and converters.
        // These live OUTSIDE the element tree, which is what makes a hot reload
        // lossless: rebuilding the tree cannot touch a registration or a value,
        // so unlike every cached element pointer, a binding needs nothing
        // re-attached in the bind callback.
        //
        // Register sources BEFORE Load when you can. Registering later still
        // works — the affected bindings report once and resolve on the first
        // frame after the registration — but the report is noise you do not
        // need.
        UIBindingContext& bindingContext() { return ctx_; }
        const UIBindingContext& bindingContext() const { return ctx_; }

        // Drive this from your draw callback, BEFORE Layout.
        UIBinder& binder() { return binder_; }
        const UIBinder& binder() const { return binder_; }

        // Call once per frame AFTER UpdatePointer: re-applies the cascade to
        // any element whose :hover / :active state just changed, and re-runs
        // that element's bindings. Returns true if anything moved, in which
        // case lay out again — a state rule may change padding or size, not
        // only colour.
        //
        // Cheap by construction: only elements some pseudo-class rule could
        // reach are watched at all, and of those only the ones whose state
        // actually flipped are touched.
        bool RestyleInteractive() { return styler_.Update(); }

        // Element -> source, for `push-*` and a field's `bind-value`. Call
        // AFTER UpdatePointer and UpdateKeyboard, which is where the state and
        // the values it publishes are decided.
        void PublishToSources() { binder_.UpdateToSource(); }
        UIInteractionStyler& styler() { return styler_; }

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
        // AFTER doc_ and sheet_ on purpose: binder_ holds raw pointers into the
        // tree, and members are destroyed in reverse declaration order, so the
        // index dies before the thing it indexes.
        UIBindingContext    ctx_;
        UIBinder            binder_;
        UIInteractionStyler styler_;
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
