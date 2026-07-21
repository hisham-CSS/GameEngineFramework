#include "NullScriptBackend.h"

namespace MyCoreEngine {

    bool NullScriptBackend::initialize(const ScriptSettings&, IScriptHost* host) {
        host_ = host;
        return true;
    }

    void NullScriptBackend::shutdown() {
        instances_.clear();
        counts_.clear();
        host_ = nullptr;
    }

    ScriptId NullScriptBackend::loadScript(const std::string& debugName,
                                           const std::string& source,
                                           ScriptError& err) {
        err = {};
        const uint64_t id = nextId_++;
        instances_[id] = Instance{ debugName, source };
        return static_cast<ScriptId>(id);
    }

    void NullScriptBackend::destroyScript(ScriptId id) {
        instances_.erase(static_cast<uint64_t>(id));
    }

    void NullScriptBackend::destroyAllScripts() {
        instances_.clear();
    }

    size_t NullScriptBackend::scriptCount() const {
        return instances_.size();
    }

    bool NullScriptBackend::hasCallback(ScriptId id, ScriptCallback cb) const {
        const auto it = instances_.find(static_cast<uint64_t>(id));
        if (it == instances_.end()) return false;
        // Naive scan, not parsing — see the header note.
        const std::string needle = std::string("function ") + ScriptCallbackName(cb);
        return it->second.source.find(needle) != std::string::npos;
    }

    bool NullScriptBackend::bump_(ScriptId id, ScriptCallback cb, ScriptError& err) {
        err = {};
        const auto it = instances_.find(static_cast<uint64_t>(id));
        if (it == instances_.end()) {
            err.message = "invalid script id";
            return false;
        }
        ++counts_[static_cast<int>(cb)];
        return true;
    }

    bool NullScriptBackend::callStart(ScriptId id, ScriptEntity, ScriptError& err) {
        return bump_(id, ScriptCallback::Start, err);
    }
    bool NullScriptBackend::callUpdate(ScriptId id, ScriptEntity, float, ScriptError& err) {
        return bump_(id, ScriptCallback::Update, err);
    }
    bool NullScriptBackend::callFixedUpdate(ScriptId id, ScriptEntity, float, ScriptError& err) {
        return bump_(id, ScriptCallback::FixedUpdate, err);
    }
    bool NullScriptBackend::callDestroy(ScriptId id, ScriptEntity, ScriptError& err) {
        return bump_(id, ScriptCallback::Destroy, err);
    }
    bool NullScriptBackend::callCollision(ScriptId id, ScriptEntity, const ScriptCollision&,
                                          ScriptError& err) {
        return bump_(id, ScriptCallback::Collision, err);
    }

    int NullScriptBackend::callCount(ScriptCallback cb) const {
        const auto it = counts_.find(static_cast<int>(cb));
        return (it != counts_.end()) ? it->second : 0;
    }

    void NullScriptBackend::resetCounts() {
        counts_.clear();
    }

} // namespace MyCoreEngine
