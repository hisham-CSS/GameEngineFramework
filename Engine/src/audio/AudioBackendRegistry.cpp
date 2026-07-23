#include "AudioBackendRegistry.h"

#include "backends/NullAudioBackend.h"
#include "backends/MiniaudioBackend.h"

#include <map>

namespace MyCoreEngine {

    namespace {
        std::vector<std::string>& order() { static std::vector<std::string> o; return o; }
        std::map<std::string, AudioBackendRegistry::Factory>& table() {
            static std::map<std::string, AudioBackendRegistry::Factory> t; return t;
        }
    }

    void AudioBackendRegistry::Register(std::string name, Factory factory) {
        if (table().find(name) == table().end()) order().push_back(name);
        table()[std::move(name)] = std::move(factory);
    }

    std::unique_ptr<IAudioBackend> AudioBackendRegistry::Create(const std::string& name) {
        auto it = table().find(name);
        return it != table().end() ? it->second() : nullptr;
    }

    bool AudioBackendRegistry::IsRegistered(const std::string& name) {
        return table().find(name) != table().end();
    }

    std::vector<std::string> AudioBackendRegistry::Available() { return order(); }

    void AudioBackendRegistry::Clear() { order().clear(); table().clear(); }

    void RegisterBuiltinAudioBackends() {
        AudioBackendRegistry::Register("Null",
            [] { return std::unique_ptr<IAudioBackend>(new NullAudioBackend()); });
        AudioBackendRegistry::Register("Miniaudio",
            [] { return std::unique_ptr<IAudioBackend>(new MiniaudioBackend()); });
    }

    const char* DefaultAudioBackendName() { return "Miniaudio"; }

} // namespace MyCoreEngine
