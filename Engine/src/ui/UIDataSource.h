#pragma once
// The MODEL half of data binding: named values a game owns, that authored UI
// reads by name.
//
// The single most important property of this file is that a UIDataSource is NOT
// part of the element tree. A hot reload destroys every UIElement and every
// event handler; it does not touch a source, a registration, or one value. That
// is what makes "edit the markup, save, keep playing" work without the app
// re-pushing anything — and re-pushing everything you cached is the step every
// hot-reloading system forgets exactly once.
//
// There is no reflection in C++, so this is an explicit registry rather than
// magic over your structs. Two ways in:
//   - BAG      Set*(name, value): the value lives here. Simplest, and what a
//              HUD wants — gameplay writes, the UI reads.
//   - OBSERVED Observe(name, getter, setter): the value lives in YOUR object
//              and this reads through. Use when the value already has a home.
#include "../core/Core.h"
#include "UIValue.h"

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace MyCoreEngine::ui {

    // One ROW of a collection: a small bag of named values.
    //
    // Copyable and movable, unlike UIDataSource, and that is the whole point. A
    // row is a VALUE the game builds and hands over; nothing caches a pointer
    // into one, so it is free to live in a vector that grows.
    class ENGINE_API UIRecord {
    public:
        void SetBool  (const std::string& col, bool v);
        void SetInt   (const std::string& col, long long v);
        void SetNumber(const std::string& col, float v);
        void SetLength(const std::string& col, const StyleLength& v);
        void SetColor (const std::string& col, const glm::vec4& v);
        void SetString(const std::string& col, std::string v);
        void SetValue (const std::string& col, UIValue v);

        bool    Has(const std::string& col) const { return IndexOf(col) >= 0; }
        UIValue Get(const std::string& col) const;                    // by value
        bool    ValueAt(const std::string& col, UIValue& out) const;  // false => absent
        int     IndexOf(const std::string& col) const;                // -1 when absent

        std::size_t size() const { return cols_.size(); }
        std::vector<std::string> columnNames() const;
        void Clear() { cols_.clear(); }

        // Order-insensitive: two rows carrying the same columns are equal
        // however they were built. This is what lets UIDataSource::SetList
        // equality-gate a list the game rebuilds from scratch every frame.
        bool operator==(const UIRecord& o) const;
        bool operator!=(const UIRecord& o) const { return !(*this == o); }

    private:
        std::vector<std::pair<std::string, UIValue>> cols_;
    };

    // A collection authored markup repeats over with `repeat=`.
    //
    // Deliberately NOT a UIValue kind and NOT a property: a list is never read
    // by a {hole}, only by a repeat, and keeping it out of props_ is what stops
    // a whole inventory being copied by value every time someone reads `score`.
    class ENGINE_API UIList {
    public:
        UIRecord& Add();
        void Add(UIRecord r);
        void Clear() { rows_.clear(); }
        std::size_t size() const { return rows_.size(); }
        bool empty() const { return rows_.empty(); }
        // Ordinary vector semantics on an object the GAME owns. No reference
        // into a UIDataSource's own list ever escapes — that one is reachable
        // only through the index API below.
        const UIRecord& at(std::size_t i) const { return rows_[i]; }
        UIRecord& at(std::size_t i) { return rows_[i]; }

        bool operator==(const UIList& o) const { return rows_ == o.rows_; }
        bool operator!=(const UIList& o) const { return !(*this == o); }

    private:
        std::vector<UIRecord> rows_;
    };

    class ENGINE_API UIDataSource {
    public:
        using Getter   = std::function<UIValue()>;
        using Setter   = std::function<void(const UIValue&)>;
        using ActionFn = std::function<void()>;

        // How the binder learns a value changed.
        //   OnWrite  the source knows (a bag Set*, or your MarkChanged call).
        //            Free: an unchanged frame costs one integer compare.
        //   Poll     the binder runs the getter every frame and compares. Use
        //            for a value you cannot hook, and prefer OnWrite for
        //            anything hot — Poll is the only per-frame cost here that
        //            does not vanish when nothing changes.
        enum class Notify { OnWrite, Poll };

        UIDataSource() = default;
        // Non-copyable AND non-movable, deliberately. UIBinder caches a
        // UIDataSource* for as long as it is registered, so the object needs a
        // stable address. Deleting the move makes relocating one a COMPILE
        // ERROR at the holder, instead of a HUD that draws perfectly and
        // silently stops updating.
        UIDataSource(const UIDataSource&) = delete;
        UIDataSource& operator=(const UIDataSource&) = delete;
        UIDataSource(UIDataSource&&) = delete;
        UIDataSource& operator=(UIDataSource&&) = delete;

        // ---- bag ----
        // Every setter is equality-gated: writing the value a property already
        // holds bumps no version and wakes nothing downstream, so gameplay
        // calling SetNumber("health", 1.0f) every frame costs one compare.
        void SetBool  (const std::string& name, bool v);
        void SetInt   (const std::string& name, long long v);
        void SetNumber(const std::string& name, float v);
        void SetLength(const std::string& name, const StyleLength& v);
        void SetColor (const std::string& name, const glm::vec4& v);
        void SetString(const std::string& name, std::string v);
        void SetValue (const std::string& name, UIValue v);

        // ---- lists ----
        // A list is NOT a property. It does not live in props_, no {hole} can
        // read it, and `repeat=` is its only reader.
        //
        // Equality-gated like every other setter, so a game that rebuilds and
        // re-Sets its inventory every frame pays one compare and wakes nothing.
        // Bumps the LIST's version only, never version_: no binding reads a
        // list, so moving the whole-source stamp would re-apply every unrelated
        // health and score binding on this source for a change none of them can
        // possibly see.
        void SetList(const std::string& name, UIList v);
        bool HasList(const std::string& name) const { return ListIndexOf(name) >= 0; }
        void RemoveList(const std::string& name);

        // Index-based, mirroring ReadAt/VersionAt and for the same reason: no
        // reference into lists_ escapes, so growing it cannot invalidate
        // anything a caller is holding across a second SetList.
        int           ListIndexOf(const std::string& name) const;   // -1 when absent
        std::size_t   ListRowCount(int li) const;
        std::uint32_t ListVersionAt(int li) const;
        bool          ListValueAt(int li, std::size_t row,
                                  const std::string& col, UIValue& out) const;
        // The UNION of every row's columns, sorted and unique. Rows are allowed
        // to differ, so the union is the only honest answer to "can this list
        // supply that column at all" — which is what turns a bare hole inside a
        // repeat that no row provides from a silently empty label into a
        // load-time diagnostic.
        std::vector<std::string> ListColumnNames(int li) const;
        // Used to say "(has: health, score; lists: inventory)". A list that is
        // invisible in the suggestion is worse than no suggestion at all: the
        // author is looking at the name in their own C++ while the engine
        // insists it does not exist.
        std::vector<std::string> listNames() const;

        // ---- observed ----
        // Re-observing an existing name REPLACES it, matching the backend
        // registries, so calling this from a bind callback cannot accumulate
        // duplicates across hot reloads. An empty setter makes the property
        // read-only; writing to it is reported rather than silently dropped.
        void Observe(std::string name, Getter get, Setter set = {},
                     Notify notify = Notify::Poll);
        // The explicit half of Notify::OnWrite for an observed property.
        void MarkChanged(const std::string& name);

        // ---- actions ----
        // A named C++ function that authored markup can invoke by name. This is
        // the alternative to an expression language: a real function you can
        // breakpoint, and a typo answered with the list of names that exist.
        // Registering an existing name REPLACES it — stated because a stale
        // std::function may capture a dead `this`.
        void AddAction(std::string name, ActionFn fn);
        void RemoveAction(const std::string& name);

        // ---- reads ----
        // BY VALUE, always. No reference into a growable vector ever escapes
        // this class: a caller holding one across a Set* that reallocates would
        // be reading freed memory.
        UIValue     Get(const std::string& name) const;
        bool        GetBool  (const std::string& name, bool d = false) const;
        long long   GetInt   (const std::string& name, long long d = 0) const;
        float       GetNumber(const std::string& name, float d = 0.0f) const;
        std::string GetString(const std::string& name) const;
        bool        Has(const std::string& name) const { return IndexOf(name) >= 0; }

        // ---- resolution surface (UIBinder; used once per load, not per frame)
        int  IndexOf(const std::string& name) const;        // -1 when absent
        int  ActionIndexOf(const std::string& name) const;  // -1 when absent
        bool ReadAt(int i, UIValue& out) const;             // runs the getter if observed
        bool WriteAt(int i, const UIValue& v);              // false when read-only
        bool InvokeAction(int i) const;
        std::uint32_t VersionAt(int i) const;
        bool IsPolledAt(int i) const;
        bool IsWritableAt(int i) const;
        // Kind::None for a bag property, whose kind may legitimately change
        // (SetString then SetNumber on one name is allowed). Set only by
        // Observe, where the C++ getter fixes it — which is what makes a
        // load-time type check possible at all.
        UIValue::Kind DeclaredKindAt(int i) const;

        // Whole-source view stamp: one compare skips every binding on this
        // source. Bumped by any property change and by adding a property.
        std::uint32_t version() const { return version_; }
        bool hasPolled() const { return polled_ > 0; }

        // Used to build "has no property 'scoer' (has: health, score)". Without
        // reflection, a typo'd path is otherwise indistinguishable from a value
        // that simply never changes — this is the highest-value diagnostic in
        // the whole system. Built on the error path only.
        std::vector<std::string> propertyNames() const;
        std::vector<std::string> actionNames() const;

        // Fired after any property change, with the property name.
        void SetOnChanged(std::function<void(const std::string&)> fn) {
            onChanged_ = std::move(fn);
        }

    private:
        struct Property {
            std::string   name;
            UIValue       value;      // bag storage, and the cache for a polled getter
            Getter        get;        // empty => bag property
            Setter        set;        // empty => read-only
            Notify        notify = Notify::OnWrite;
            UIValue::Kind declaredKind = UIValue::Kind::None;
            // Starts at 1 because 0 means "never applied" on the binder side.
            std::uint32_t version = 1;
        };
        struct Action { std::string name; ActionFn fn; };
        // Its own version, separate from version_, so a repeat can notice its
        // list moved without every other binding on the source noticing too.
        struct NamedList { std::string name; UIList list; std::uint32_t version = 1; };

        Property& findOrAdd_(const std::string& name);
        void      bump_(Property& p);

        // Linear scan. Searched once per load to build an index, never per
        // frame, so a HUD's handful of properties needs nothing cleverer.
        std::vector<Property>  props_;
        std::vector<Action>    actions_;
        std::vector<NamedList> lists_;
        std::function<void(const std::string&)> onChanged_;
        std::uint32_t version_ = 1;
        int polled_ = 0;
    };

    // A named C++ function that transforms a bound value on its way to the UI.
    //
    // This is where derived values live, and it is deliberately NOT an
    // expression language. Arithmetic and comparison in markup would need '<'
    // and '&&' inside XML attribute values, which XML forbids — every
    // comparison would have to be written backwards forever. A converter is a
    // real function: breakpointable, unit-testable, and a typo in its name is
    // answered with the names that exist.
    using UIConvertFn = std::function<bool(const UIValue& in, UIValue& out, std::string& err)>;

    // An INSTANCE, not a pile of statics. A converter may capture app state, and
    // a process-wide mutable registry with no way to take one back out would
    // keep that capture callable after the UI that made it died — and would leak
    // between GoogleTests sharing one binary.
    class ENGINE_API UIConverterTable {
    public:
        void Register(std::string name, UIConvertFn fn);   // later wins
        void Remove(const std::string& name);
        void Clear();
        const UIConvertFn* Find(const std::string& name) const;  // null when absent
        std::vector<std::string> names() const;
        bool empty() const { return fns_.empty(); }
    private:
        std::vector<std::pair<std::string, UIConvertFn>> fns_;
    };

    // percent, ratio, px, not, round, floor, ceil, abs, int, upper, lower.
    // Immutable, shared and capture-free, so there is no lifetime question and
    // no cross-test contamination.
    ENGINE_API const UIConverterTable& BuiltinUIConverters();

    // What a document binds against: the named sources in scope plus the
    // converters available to them.
    class ENGINE_API UIBindingContext {
    public:
        // NON-OWNING. The app owns its sources and they outlive the tree — that
        // is precisely what makes a hot reload lossless.
        void RegisterSource(std::string name, UIDataSource* src);  // replaces on re-register
        void RemoveSource(const std::string& name);
        UIDataSource* Find(const std::string& name) const;
        // The names an AUTHOR can write. Used only to build diagnostics, which
        // is why a repeat's generated slot sources are excluded: answering a
        // typo with 64 names nobody has ever seen buries the two that matter.
        std::vector<std::string> sourceNames() const;

        // A source the author did not write and cannot usefully name — one row
        // of a `repeat=`. Findable exactly like any other so the ordinary
        // binding path needs no special case, but absent from sourceNames().
        // `rowLabel` is what diagnostics say instead: the text the author
        // actually typed in repeat=, e.g. "hud.inventory".
        void RegisterInternalSource(std::string name, UIDataSource* src, std::string rowLabel);
        // Null when `name` is not a repeat row. Error path only.
        const std::string* rowLabelFor(const std::string& name) const;

        UIConverterTable&       converters()       { return converters_; }
        const UIConverterTable& converters() const { return converters_; }
        // Call after mutating converters() directly, so bindings that failed to
        // resolve a converter get another chance.
        void MarkConvertersChanged() { ++rev_; }

        // Bumped by every source registration/removal. The binder compares this
        // once per frame and, when it moved, re-resolves EVERY entry rather
        // than only the pending ones — partial re-resolution is exactly how a
        // removed or re-pointed source ends up still being read through.
        std::uint32_t revision() const { return rev_; }

        // Sum of every registered source's version. Cheap (a handful of
        // sources) and used for ONE thing: while a binding is still unresolved,
        // this is the signal that a property it named may now exist.
        //
        // Registering a source bumps `revision`, but ADDING A PROPERTY to an
        // already-registered one does not — and markup that binds `{ammo}`
        // before gameplay ever calls SetInt("ammo") is completely ordinary.
        // Without this the binding would report once and stay dead forever,
        // which is exactly the silent-never-updates failure this system exists
        // to avoid.
        std::uint32_t sourceVersionSum() const;

    private:
        // rowLabel is empty for an ordinary source and non-empty for a repeat
        // row, which is the only thing that distinguishes the two.
        struct Entry { std::string name; UIDataSource* src; std::string rowLabel; };
        std::vector<Entry> sources_;
        UIConverterTable converters_;
        std::uint32_t rev_ = 1;
    };

} // namespace MyCoreEngine::ui
