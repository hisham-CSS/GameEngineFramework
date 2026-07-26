#include "UIDataSource.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace MyCoreEngine::ui {

// ------------------------------------------------------------- UIDataSource

int UIDataSource::IndexOf(const std::string& name) const {
    for (size_t i = 0; i < props_.size(); ++i) {
        if (props_[i].name == name) return int(i);
    }
    return -1;
}

int UIDataSource::ActionIndexOf(const std::string& name) const {
    for (size_t i = 0; i < actions_.size(); ++i) {
        if (actions_[i].name == name) return int(i);
    }
    return -1;
}

UIDataSource::Property& UIDataSource::findOrAdd_(const std::string& name) {
    const int i = IndexOf(name);
    if (i >= 0) return props_[size_t(i)];
    props_.push_back(Property{ name, {}, {}, {}, Notify::OnWrite, UIValue::Kind::None, 1 });
    // A new property is itself a change: a binding that reported "no property
    // 'ammo'" must resolve once the app adds it, and that needs the version to
    // move even though no existing value did.
    ++version_;
    return props_.back();
}

void UIDataSource::bump_(Property& p) {
    ++p.version;
    ++version_;
    if (onChanged_) onChanged_(p.name);
}

// Every setter funnels through here so the equality gate exists in exactly one
// place. Writing an unchanged value must be free: gameplay pushing the same
// health every frame is the normal case, not the exception.
void UIDataSource::SetValue(const std::string& name, UIValue v) {
    Property& p = findOrAdd_(name);
    if (p.get) {
        // An observed property's value lives in the app's object. Writing here
        // would put the source and the object out of step, so route to the
        // setter (which is also where the read-only check belongs).
        if (p.set) { p.set(v); bump_(p); }
        return;
    }
    if (p.value == v) return;
    p.value = std::move(v);
    bump_(p);
}

void UIDataSource::SetBool(const std::string& n, bool v)              { SetValue(n, UIValue::Bool(v)); }
void UIDataSource::SetInt(const std::string& n, long long v)          { SetValue(n, UIValue::Int(v)); }
void UIDataSource::SetNumber(const std::string& n, float v)           { SetValue(n, UIValue::Number(v)); }
void UIDataSource::SetLength(const std::string& n, const StyleLength& v) { SetValue(n, UIValue::Len(v)); }
void UIDataSource::SetColor(const std::string& n, const glm::vec4& v) { SetValue(n, UIValue::Color4(v)); }
void UIDataSource::SetString(const std::string& n, std::string v)     { SetValue(n, UIValue::Str(std::move(v))); }

void UIDataSource::Observe(std::string name, Getter get, Setter set, Notify notify) {
    Property& p = findOrAdd_(name);
    // Replacing a polled property with an unpolled one (or the reverse) has to
    // keep the counter honest, or hasPolled() lies and the binder either polls
    // forever or stops polling something it must.
    if (p.get && p.notify == Notify::Poll) --polled_;
    p.get = std::move(get);
    p.set = std::move(set);
    p.notify = notify;
    p.declaredKind = UIValue::Kind::None;
    if (p.get) {
        if (notify == Notify::Poll) ++polled_;
        // Run the getter once so the declared kind is known at load, which is
        // what makes "'score' is declared int, but width needs a length" a
        // load-time diagnostic instead of a runtime surprise.
        p.value = p.get();
        p.declaredKind = p.value.kind;
    }
    bump_(p);
}

void UIDataSource::MarkChanged(const std::string& name) {
    const int i = IndexOf(name);
    if (i < 0) return;
    bump_(props_[size_t(i)]);
}

void UIDataSource::AddAction(std::string name, ActionFn fn) {
    const int i = ActionIndexOf(name);
    if (i >= 0) { actions_[size_t(i)].fn = std::move(fn); return; }
    actions_.push_back(Action{ std::move(name), std::move(fn) });
}

void UIDataSource::RemoveAction(const std::string& name) {
    const int i = ActionIndexOf(name);
    if (i >= 0) actions_.erase(actions_.begin() + i);
}

bool UIDataSource::ReadAt(int i, UIValue& out) const {
    if (i < 0 || size_t(i) >= props_.size()) return false;
    const Property& p = props_[size_t(i)];
    if (p.get) { out = p.get(); return true; }
    out = p.value;
    return true;
}

bool UIDataSource::WriteAt(int i, const UIValue& v) {
    if (i < 0 || size_t(i) >= props_.size()) return false;
    Property& p = props_[size_t(i)];
    if (p.get && !p.set) return false;   // observed and read-only
    if (p.get) { p.set(v); bump_(p); return true; }
    if (p.value == v) return true;       // no-op write is still a success
    p.value = v;
    bump_(p);
    return true;
}

bool UIDataSource::InvokeAction(int i) const {
    if (i < 0 || size_t(i) >= actions_.size()) return false;
    if (!actions_[size_t(i)].fn) return false;
    actions_[size_t(i)].fn();
    return true;
}

std::uint32_t UIDataSource::VersionAt(int i) const {
    if (i < 0 || size_t(i) >= props_.size()) return 0;
    return props_[size_t(i)].version;
}

bool UIDataSource::IsPolledAt(int i) const {
    if (i < 0 || size_t(i) >= props_.size()) return false;
    const Property& p = props_[size_t(i)];
    return p.get && p.notify == Notify::Poll;
}

bool UIDataSource::IsWritableAt(int i) const {
    if (i < 0 || size_t(i) >= props_.size()) return false;
    const Property& p = props_[size_t(i)];
    return !p.get || bool(p.set);
}

UIValue::Kind UIDataSource::DeclaredKindAt(int i) const {
    if (i < 0 || size_t(i) >= props_.size()) return UIValue::Kind::None;
    return props_[size_t(i)].declaredKind;
}

UIValue UIDataSource::Get(const std::string& name) const {
    UIValue v;
    ReadAt(IndexOf(name), v);
    return v;
}

bool UIDataSource::GetBool(const std::string& name, bool d) const {
    UIValue v; bool out = d;
    if (ReadAt(IndexOf(name), v) && v.AsBool(out)) return out;
    return d;
}
long long UIDataSource::GetInt(const std::string& name, long long d) const {
    UIValue v; long long out = d;
    if (ReadAt(IndexOf(name), v) && v.AsInt(out)) return out;
    return d;
}
float UIDataSource::GetNumber(const std::string& name, float d) const {
    UIValue v; float out = d;
    if (ReadAt(IndexOf(name), v) && v.AsNumber(out)) return out;
    return d;
}
std::string UIDataSource::GetString(const std::string& name) const {
    UIValue v;
    if (!ReadAt(IndexOf(name), v)) return {};
    return v.ToDisplayString();
}

std::vector<std::string> UIDataSource::propertyNames() const {
    std::vector<std::string> out;
    out.reserve(props_.size());
    for (const auto& p : props_) out.push_back(p.name);
    return out;
}

std::vector<std::string> UIDataSource::actionNames() const {
    std::vector<std::string> out;
    out.reserve(actions_.size());
    for (const auto& a : actions_) out.push_back(a.name);
    return out;
}

// --------------------------------------------------------- UIConverterTable

void UIConverterTable::Register(std::string name, UIConvertFn fn) {
    for (auto& e : fns_) {
        if (e.first == name) { e.second = std::move(fn); return; }
    }
    fns_.emplace_back(std::move(name), std::move(fn));
}

void UIConverterTable::Remove(const std::string& name) {
    fns_.erase(std::remove_if(fns_.begin(), fns_.end(),
                              [&](const auto& e) { return e.first == name; }),
               fns_.end());
}

void UIConverterTable::Clear() { fns_.clear(); }

const UIConvertFn* UIConverterTable::Find(const std::string& name) const {
    for (const auto& e : fns_) {
        if (e.first == name) return &e.second;
    }
    return nullptr;
}

std::vector<std::string> UIConverterTable::names() const {
    std::vector<std::string> out;
    out.reserve(fns_.size());
    for (const auto& e : fns_) out.push_back(e.first);
    return out;
}

namespace {

    // Shared shape for the numeric builtins: pull a number out, transform it,
    // hand a number back, and name both kinds when the input will not coerce.
    UIConvertFn numeric(const char* name, float (*fn)(float)) {
        std::string n = name;
        return [n, fn](const UIValue& in, UIValue& out, std::string& err) {
            float v = 0.0f;
            if (!in.AsNumber(v)) {
                err = "converter '" + n + "' needs a number, got " + in.KindName();
                return false;
            }
            out = UIValue::Number(fn(v));
            return true;
        };
    }

    std::string mapCase(std::string s, bool upper) {
        std::transform(s.begin(), s.end(), s.begin(), [upper](unsigned char c) {
            return char(upper ? std::toupper(c) : std::tolower(c));
        });
        return s;
    }

    UIConverterTable makeBuiltins() {
        UIConverterTable t;

        // The one every health bar needs: a 0..1 model value becomes a CSS
        // percentage. It returns a LENGTH, not a number, so authored markup can
        // write `width: {health | percent}` with no unit to get wrong — and can
        // still write `{health | ratio}%` if it prefers the unit visible.
        t.Register("percent", [](const UIValue& in, UIValue& out, std::string& err) {
            float v = 0.0f;
            if (!in.AsNumber(v)) {
                err = std::string("converter 'percent' needs a number, got ") + in.KindName();
                return false;
            }
            out = UIValue::Len(StyleLength::Pct(v * 100.0f));
            return true;
        });
        // 0..1 -> 0..100 as a plain number, for a label ("87") or an author who
        // wants to write the '%' themselves.
        t.Register("ratio", numeric("ratio", [](float v) { return v * 100.0f; }));
        // A bare number becomes an explicit pixel length.
        t.Register("px", [](const UIValue& in, UIValue& out, std::string& err) {
            float v = 0.0f;
            if (!in.AsNumber(v)) {
                err = std::string("converter 'px' needs a number, got ") + in.KindName();
                return false;
            }
            out = UIValue::Len(StyleLength::Px(v));
            return true;
        });
        t.Register("not", [](const UIValue& in, UIValue& out, std::string& err) {
            bool b = false;
            if (!in.AsBool(b)) {
                err = std::string("converter 'not' needs a bool, got ") + in.KindName();
                return false;
            }
            out = UIValue::Bool(!b);
            return true;
        });
        t.Register("round", numeric("round", [](float v) { return std::round(v); }));
        t.Register("floor", numeric("floor", [](float v) { return std::floor(v); }));
        t.Register("ceil",  numeric("ceil",  [](float v) { return std::ceil(v); }));
        t.Register("abs",   numeric("abs",   [](float v) { return std::fabs(v); }));
        // Truncating to an INT rather than a rounded float, so a label shows
        // "87" and not "87" that is really 87.0000038 waiting to print wrong.
        t.Register("int", [](const UIValue& in, UIValue& out, std::string& err) {
            long long v = 0;
            if (!in.AsInt(v)) {
                err = std::string("converter 'int' needs a number, got ") + in.KindName();
                return false;
            }
            out = UIValue::Int(v);
            return true;
        });
        t.Register("upper", [](const UIValue& in, UIValue& out, std::string&) {
            out = UIValue::Str(mapCase(in.ToDisplayString(), true));
            return true;
        });
        t.Register("lower", [](const UIValue& in, UIValue& out, std::string&) {
            out = UIValue::Str(mapCase(in.ToDisplayString(), false));
            return true;
        });
        return t;
    }

} // namespace

const UIConverterTable& BuiltinUIConverters() {
    // Function-local static: built on first use, immutable afterwards, and
    // thread-safe to initialise under C++11 magic statics. Every entry is
    // capture-free, so there is no lifetime question at shutdown.
    static const UIConverterTable table = makeBuiltins();
    return table;
}

// -------------------------------------------------------- UIBindingContext

void UIBindingContext::RegisterSource(std::string name, UIDataSource* src) {
    for (auto& e : sources_) {
        if (e.first == name) { e.second = src; ++rev_; return; }
    }
    sources_.emplace_back(std::move(name), src);
    ++rev_;
}

void UIBindingContext::RemoveSource(const std::string& name) {
    const size_t before = sources_.size();
    sources_.erase(std::remove_if(sources_.begin(), sources_.end(),
                                  [&](const auto& e) { return e.first == name; }),
                   sources_.end());
    // Only on an actual removal: a no-op remove that bumped the revision would
    // force the binder to re-resolve every entry for nothing.
    if (sources_.size() != before) ++rev_;
}

UIDataSource* UIBindingContext::Find(const std::string& name) const {
    for (const auto& e : sources_) {
        if (e.first == name) return e.second;
    }
    return nullptr;
}

std::uint32_t UIBindingContext::sourceVersionSum() const {
    std::uint32_t sum = 0;
    for (const auto& e : sources_) {
        if (e.second) sum += e.second->version();
    }
    return sum;
}

std::vector<std::string> UIBindingContext::sourceNames() const {
    std::vector<std::string> out;
    out.reserve(sources_.size());
    for (const auto& e : sources_) out.push_back(e.first);
    return out;
}

} // namespace MyCoreEngine::ui
