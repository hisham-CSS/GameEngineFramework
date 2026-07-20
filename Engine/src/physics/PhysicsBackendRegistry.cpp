#include "PhysicsBackendRegistry.h"
#include "backends/SimplePhysicsBackend.h"

#ifdef CSE_WITH_JOLT
#include "backends/JoltPhysicsBackend.h"
#endif
#ifdef CSE_WITH_PHYSX
#include "backends/PhysXPhysicsBackend.h"
#endif

#include <algorithm>
#include <unordered_map>

namespace MyCoreEngine {

    namespace {
        // Function-local statics: constructed on first use, so there is no
        // static-initialization-order dependency between translation units.
        struct RegistryState {
            std::unordered_map<std::string, PhysicsBackendRegistry::Factory> factories;
            std::vector<std::string> order; // registration order for the UI
        };
        RegistryState& state() {
            static RegistryState s;
            return s;
        }
    } // namespace

    void PhysicsBackendRegistry::Register(std::string name, Factory factory) {
        if (name.empty() || !factory) return;
        auto& s = state();
        if (s.factories.find(name) == s.factories.end()) {
            s.order.push_back(name);
        }
        s.factories[std::move(name)] = std::move(factory);
    }

    std::unique_ptr<IPhysicsBackend> PhysicsBackendRegistry::Create(const std::string& name) {
        auto& s = state();
        const auto it = s.factories.find(name);
        return (it != s.factories.end()) ? it->second() : nullptr;
    }

    bool PhysicsBackendRegistry::IsRegistered(const std::string& name) {
        auto& s = state();
        return s.factories.find(name) != s.factories.end();
    }

    std::vector<std::string> PhysicsBackendRegistry::Available() {
        return state().order;
    }

    void PhysicsBackendRegistry::Clear() {
        auto& s = state();
        s.factories.clear();
        s.order.clear();
    }

    const char* DefaultPhysicsBackendName() {
        // Prefer a real engine when one is compiled in; "Simple" is the
        // always-available fallback so a build without either SDK still runs.
#ifdef CSE_WITH_JOLT
        return "Jolt";
#elif defined(CSE_WITH_PHYSX)
        return "PhysX";
#else
        return "Simple";
#endif
    }

    void RegisterBuiltinPhysicsBackends() {
        // Idempotent: re-registering replaces the factory with an identical
        // one, so calling this from both the editor and the player is fine.
        PhysicsBackendRegistry::Register("Simple", [] {
            return std::unique_ptr<IPhysicsBackend>(new SimplePhysicsBackend());
        });
#ifdef CSE_WITH_JOLT
        PhysicsBackendRegistry::Register("Jolt", [] {
            return std::unique_ptr<IPhysicsBackend>(new JoltPhysicsBackend());
        });
#endif
#ifdef CSE_WITH_PHYSX
        PhysicsBackendRegistry::Register("PhysX", [] {
            return std::unique_ptr<IPhysicsBackend>(new PhysXPhysicsBackend());
        });
#endif
    }

} // namespace MyCoreEngine
