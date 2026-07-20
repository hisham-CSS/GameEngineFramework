#pragma once
// Name -> factory registry for physics backends.
//
// This is what makes "support any physics library" true rather than
// aspirational: adding a library means writing one IPhysicsBackend subclass
// and registering it here. Nothing else in the engine changes.
//
// Registration is EXPLICIT (RegisterBuiltinPhysicsBackends) rather than via
// static initializers. Self-registering statics inside a DLL are fragile —
// the linker is free to drop an object file nothing references, and the
// backend silently disappears from the list.

#include "../core/Core.h"
#include "IPhysicsBackend.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace MyCoreEngine {

    class ENGINE_API PhysicsBackendRegistry {
    public:
        using Factory = std::function<std::unique_ptr<IPhysicsBackend>()>;

        // Later registrations under the same name replace earlier ones, so a
        // game can override a built-in backend with its own build.
        static void Register(std::string name, Factory factory);

        // nullptr when the name is unknown (caller falls back / reports).
        static std::unique_ptr<IPhysicsBackend> Create(const std::string& name);

        static bool IsRegistered(const std::string& name);
        // Registration order, for the editor's backend picker.
        static std::vector<std::string> Available();

        static void Clear(); // tests
    };

    // Registers every backend compiled into this build: "Simple" always, plus
    // "Jolt" / "PhysX" when CSE_WITH_JOLT / CSE_WITH_PHYSX are defined.
    // Idempotent — safe to call from both the editor and the player.
    ENGINE_API void RegisterBuiltinPhysicsBackends();

    // The backend used when a scene/project does not name one.
    ENGINE_API const char* DefaultPhysicsBackendName();

} // namespace MyCoreEngine
