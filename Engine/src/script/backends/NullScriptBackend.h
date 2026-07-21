#pragma once
// The always-available script backend. It EXECUTES NOTHING.
//
// Two jobs:
//  1. A build with no language runtime still loads and plays scenes — scripted
//     entities simply do nothing, instead of the engine failing to start.
//  2. It makes ScriptWorld's own bookkeeping (load/destroy counts, lifecycle
//     ordering, disable-on-error) testable with no runtime dependency, via the
//     per-callback counters below.
//
// hasCallback() answers with a naive textual scan for "function OnX". That is
// not parsing and makes no claim to be — it exists so ScriptWorld's
// "skip entities that define no OnUpdate" fast path is exercised in tests.

#include "../IScriptBackend.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace MyCoreEngine {

    class NullScriptBackend final : public IScriptBackend {
    public:
        const char* name() const override { return "Null"; }
        const char* language() const override { return "none"; }

        bool initialize(const ScriptSettings& settings, IScriptHost* host) override;
        void shutdown() override;

        ScriptId loadScript(const std::string& debugName, const std::string& source,
                            ScriptError& err) override;
        void   destroyScript(ScriptId id) override;
        void   destroyAllScripts() override;
        size_t scriptCount() const override;

        bool hasCallback(ScriptId id, ScriptCallback cb) const override;

        bool callStart(ScriptId id, ScriptEntity self, ScriptError& err) override;
        bool callUpdate(ScriptId id, ScriptEntity self, float dt, ScriptError& err) override;
        bool callFixedUpdate(ScriptId id, ScriptEntity self, float fixedDt, ScriptError& err) override;
        bool callDestroy(ScriptId id, ScriptEntity self, ScriptError& err) override;
        bool callCollision(ScriptId id, ScriptEntity self, const ScriptCollision& hit,
                           ScriptError& err) override;

        // ---- test observation ----
        int callCount(ScriptCallback cb) const;
        void resetCounts();

    private:
        struct Instance {
            std::string debugName;
            std::string source;
        };
        std::unordered_map<uint64_t, Instance> instances_;
        std::unordered_map<int, int> counts_;
        uint64_t nextId_ = 1;
        IScriptHost* host_ = nullptr;

        bool bump_(ScriptId id, ScriptCallback cb, ScriptError& err);
    };

} // namespace MyCoreEngine
