#include "ScriptBackendRegistry.h"
#include "backends/NullScriptBackend.h"

#ifdef CSE_WITH_LUA
#include "backends/LuaScriptBackend.h"
#endif

#include <unordered_map>

namespace MyCoreEngine {

    const char* ScriptCallbackName(ScriptCallback cb) {
        switch (cb) {
            case ScriptCallback::Start:       return "OnStart";
            case ScriptCallback::Update:      return "OnUpdate";
            case ScriptCallback::FixedUpdate: return "OnFixedUpdate";
            case ScriptCallback::Destroy:     return "OnDestroy";
            case ScriptCallback::Collision:   return "OnCollision";
        }
        return "";
    }

    namespace {
        // Function-local statics: constructed on first use, so there is no
        // static-initialization-order dependency between translation units.
        struct RegistryState {
            std::unordered_map<std::string, ScriptBackendRegistry::Factory> factories;
            std::vector<std::string> order; // registration order for the UI
        };
        RegistryState& state() {
            static RegistryState s;
            return s;
        }
    } // namespace

    void ScriptBackendRegistry::Register(std::string name, Factory factory) {
        if (name.empty() || !factory) return;
        auto& s = state();
        if (s.factories.find(name) == s.factories.end()) {
            s.order.push_back(name);
        }
        s.factories[std::move(name)] = std::move(factory);
    }

    std::unique_ptr<IScriptBackend> ScriptBackendRegistry::Create(const std::string& name) {
        auto& s = state();
        const auto it = s.factories.find(name);
        return (it != s.factories.end()) ? it->second() : nullptr;
    }

    bool ScriptBackendRegistry::IsRegistered(const std::string& name) {
        auto& s = state();
        return s.factories.find(name) != s.factories.end();
    }

    std::vector<std::string> ScriptBackendRegistry::Available() {
        return state().order;
    }

    void ScriptBackendRegistry::Clear() {
        auto& s = state();
        s.factories.clear();
        s.order.clear();
    }

    const char* DefaultScriptBackendName() {
        // Prefer a real language when one is compiled in; "Null" is the
        // always-available fallback so a build without any runtime still runs
        // (scenes load, scripted entities simply do nothing).
#ifdef CSE_WITH_LUA
        return "Lua";
#else
        return "Null";
#endif
    }

    void RegisterBuiltinScriptBackends() {
        ScriptBackendRegistry::Register("Null", [] {
            return std::unique_ptr<IScriptBackend>(new NullScriptBackend());
        });
#ifdef CSE_WITH_LUA
        ScriptBackendRegistry::Register("Lua", [] {
            return std::unique_ptr<IScriptBackend>(new LuaScriptBackend());
        });
#endif
    }

} // namespace MyCoreEngine
