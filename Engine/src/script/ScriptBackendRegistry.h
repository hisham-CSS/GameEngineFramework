#pragma once
// Name -> factory registry for script backends, mirroring
// PhysicsBackendRegistry exactly. Adding a language means writing one
// IScriptBackend subclass and registering it here; nothing else changes.
//
// Registration is EXPLICIT rather than via static initializers, for the same
// reason as physics: the linker may drop an object file nothing references,
// and a self-registering backend then silently vanishes from the list.

#include "../core/Core.h"
#include "IScriptBackend.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace MyCoreEngine {

    class ENGINE_API ScriptBackendRegistry {
    public:
        using Factory = std::function<std::unique_ptr<IScriptBackend>()>;

        // Later registrations under the same name replace earlier ones, so a
        // game can substitute its own build of a backend.
        static void Register(std::string name, Factory factory);

        // nullptr when the name is unknown (caller falls back / reports).
        static std::unique_ptr<IScriptBackend> Create(const std::string& name);

        static bool IsRegistered(const std::string& name);
        // Registration order, for the editor's backend picker.
        static std::vector<std::string> Available();

        static void Clear(); // tests
    };

    // Registers every backend compiled into this build: "Null" always, plus
    // "Lua" when CSE_WITH_LUA is defined. Idempotent.
    ENGINE_API void RegisterBuiltinScriptBackends();

    // The backend used when a project does not name one.
    ENGINE_API const char* DefaultScriptBackendName();

} // namespace MyCoreEngine
