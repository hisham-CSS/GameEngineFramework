#include "UIRepeatPool.h"

#include <algorithm>
#include <cmath>

namespace MyCoreEngine::ui {

namespace {

    std::string join(const std::vector<std::string>& v) {
        std::string out;
        for (size_t i = 0; i < v.size(); ++i) {
            if (i) out += ", ";
            out += v[i];
        }
        return out.empty() ? std::string("none") : out;
    }

} // namespace

void UIRepeatPool::Build(const UIRepeatSpec& spec, UIBindingContext& ctx,
                         std::vector<std::string>& errors, const std::string& origin) {
    spec_ = spec;
    slots_.clear();
    registeredNames_.clear();
    primed_ = false;
    lastSrc_ = nullptr;
    lastListVersion_ = 0;
    lastOffset_ = 0;

    // Reported ONCE, here, rather than per frame from Refresh. A repeat naming
    // a list that does not exist is a load-time authoring mistake, and the pool
    // still builds: the list may be registered later, and until then every slot
    // is simply absent.
    const std::string where = origin + ": repeat '" + spec_.label + "': ";
    if (UIDataSource* src = ctx.Find(spec_.listSource)) {
        const int li = src->ListIndexOf(spec_.listName);
        if (li < 0) {
            std::string msg = where + "data source '" + spec_.listSource +
                              "' has no list '" + spec_.listName + "' (has lists: " +
                              join(src->listNames()) + ")";
            // Naming which it IS turns "that name does not exist" into "you
            // reached for the wrong one of the two things it could be", which
            // is a different afternoon.
            if (src->IndexOf(spec_.listName) >= 0) {
                msg += "; '" + spec_.listName +
                       "' is a property - a repeat needs a list, set with SetList";
            }
            errors.push_back(std::move(msg));
        } else {
            // Every column the template reads is SEEDED on every slot below —
            // that is what stops an unreached slot leaving its bindings
            // unresolved forever. The side effect is that the binder can never
            // report a bare hole that misses the row: it resolves, and renders
            // empty. So the check moves here, where the data is, and is asked
            // ONCE per repeat rather than once per clone.
            //
            // The UNION over all rows, not row 0: rows are allowed to differ,
            // and a column only some rows carry is legitimate.
            const std::vector<std::string> have = src->ListColumnNames(li);
            // An empty list says nothing about what its rows will carry, so
            // there is nothing honest to report yet.
            if (!have.empty()) {
                for (const std::string& col : spec_.columns) {
                    if (!col.empty() && col[0] == '$') continue;
                    if (std::binary_search(have.begin(), have.end(), col)) continue;
                    errors.push_back(where + "no row has a column '" + col +
                                     "' (row columns: " + join(have) +
                                     ") - inside repeat=, a bare {" + col +
                                     "} reads the ROW; write {source." + col +
                                     "} to reach an outer source");
                }
            }
        }
    } else {
        errors.push_back(where + "unknown data source '" + spec_.listSource +
                         "' (registered: " + join(ctx.sourceNames()) + ")");
    }

    slots_.reserve(size_t(spec_.count));
    for (int i = 0; i < spec_.count; ++i) {
        Slot s;
        s.src = std::make_unique<UIDataSource>();

        // EVERY column, on EVERY slot, before the binder ever resolves. A slot
        // the window never reaches would otherwise carry no properties at all,
        // its bindings would never resolve, and UIBinder would count them
        // unresolved for the life of the process — arming its pending-retry
        // check so that any source moving anywhere forces a full re-collect.
        //
        // Slot sources are pure bags. Nothing may ever Observe() one: an
        // observed property is not equality-gated and would bump its slot every
        // frame forever.
        s.colIndex.reserve(spec_.columns.size());
        for (const std::string& col : spec_.columns) {
            if (!col.empty() && col[0] == '$') { s.colIndex.push_back(-1); continue; }
            s.src->SetValue(col, UIValue{});
            s.colIndex.push_back(s.src->IndexOf(col));
        }
        s.src->SetInt(kRepeatSlotProp, i);
        s.src->SetInt(kRepeatIndexProp, 0);
        s.src->SetInt(kRepeatCountProp, 0);
        // Seeded FALSE, so the force-apply at the end of UIBinder::Rebuild
        // hides a surplus slot before the first Layout — never a frame of empty
        // boxes on load.
        s.src->SetBool(kRepeatPresentProp, false);
        s.slotIdx    = s.src->IndexOf(kRepeatSlotProp);
        s.indexIdx   = s.src->IndexOf(kRepeatIndexProp);
        s.countIdx   = s.src->IndexOf(kRepeatCountProp);
        s.presentIdx = s.src->IndexOf(kRepeatPresentProp);

        std::string name = spec_.slotSourceName(i);
        ctx.RegisterInternalSource(name, s.src.get(), spec_.label);
        registeredNames_.push_back(std::move(name));
        slots_.push_back(std::move(s));
    }
}

void UIRepeatPool::Teardown(UIBindingContext& ctx) {
    // Unregister FIRST, from the names we actually registered, then free. The
    // reverse order leaves a dangling UIDataSource* that sourceVersionSum()
    // dereferences on the very next frame.
    for (const std::string& n : registeredNames_) ctx.RemoveSource(n);
    registeredNames_.clear();
    slots_.clear();
    primed_ = false;
    lastSrc_ = nullptr;
}

void UIRepeatPool::Refresh(UIBindingContext& ctx) {
    if (slots_.empty()) return;

    UIDataSource* src = ctx.Find(spec_.listSource);
    const int  li  = src ? src->ListIndexOf(spec_.listName) : -1;
    const std::size_t   n   = (li >= 0) ? src->ListRowCount(li) : 0;
    const std::uint32_t ver = (li >= 0) ? src->ListVersionAt(li) : 0;

    long long off = 0;
    if (!spec_.offsetProp.empty()) {
        if (UIDataSource* os = ctx.Find(spec_.offsetSource)) {
            off = (long long)std::floor(os->GetNumber(spec_.offsetProp, 0.0f));
        }
    }
    // Negative is clamped because there is no row before the first. An offset
    // PAST the end is not: each slot bounds-checks itself and goes absent, so
    // scrolling off the end shows an empty list, which is what the numbers say.
    if (off < 0) off = 0;

    // The common frame: nothing moved, so nothing is written and no version on
    // any slot changes. This is what makes a 64-slot pool free when idle.
    if (primed_ && src == lastSrc_ && ver == lastListVersion_ && off == lastOffset_) return;

    for (std::size_t i = 0; i < slots_.size(); ++i) {
        Slot& s = slots_[i];
        const std::size_t row = std::size_t(off) + i;
        const bool present = (li >= 0) && (row < n);

        // The FULL column set every time, not just the columns this row
        // happens to carry. props_ never shrinks and there is no way to remove
        // one, so sliding from a row with `durability` onto a row without it
        // would otherwise leave the previous row's value on screen, with no
        // version bump and no diagnostic to explain it.
        for (std::size_t k = 0; k < spec_.columns.size(); ++k) {
            const int idx = s.colIndex[k];
            if (idx < 0) continue;                  // a $-column, written below
            UIValue v;                              // absent => empty, cleared
            if (present) src->ListValueAt(li, row, spec_.columns[k], v);
            s.src->WriteAt(idx, v);
        }
        // Only when the template reads them. An absolute $index written
        // unconditionally moves every slot's version on every window step, and
        // change detection is per source — the whole pool would re-apply on
        // every notch of a scroll that changed two visible rows.
        if (spec_.readsIndex) {
            s.src->WriteAt(s.indexIdx, UIValue::Int(present ? (long long)row : 0));
        }
        if (spec_.readsCount) s.src->WriteAt(s.countIdx, UIValue::Int((long long)n));
        s.src->WriteAt(s.presentIdx, UIValue::Bool(present));
    }

    lastSrc_ = src;
    lastListVersion_ = ver;
    lastOffset_ = off;
    primed_ = true;
}

} // namespace MyCoreEngine::ui
