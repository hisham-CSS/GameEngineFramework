#pragma once
// Name -> factory registry for audio backends. Same explicit-registration
// design as PhysicsBackendRegistry (self-registering statics in a DLL are
// fragile), so adding an audio library means one IAudioBackend subclass plus a
// line here.
#include "../core/Core.h"
#include "IAudioBackend.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace MyCoreEngine {

    class ENGINE_API AudioBackendRegistry {
    public:
        using Factory = std::function<std::unique_ptr<IAudioBackend>()>;

        static void Register(std::string name, Factory factory);
        static std::unique_ptr<IAudioBackend> Create(const std::string& name);
        static bool IsRegistered(const std::string& name);
        static std::vector<std::string> Available(); // registration order
        static void Clear();                          // tests
    };

    // Registers "Null" always and "Miniaudio" (the real device backend).
    // Idempotent -- safe from both the editor and the player.
    ENGINE_API void RegisterBuiltinAudioBackends();

    // Preferred backend when nothing names one (falls back to Null if its
    // device init fails).
    ENGINE_API const char* DefaultAudioBackendName();

} // namespace MyCoreEngine
