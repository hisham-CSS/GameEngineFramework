#pragma once
// Lua backend, built on sol2 + Lua 5.4.
//
// This header is deliberately SDK-FREE — no sol/, no lua.h, not even a
// forward declaration of lua_State. Everything lives behind Impl, exactly as
// the Jolt and PhysX backends hide their SDKs. That is not tidiness: the
// vcpkg Lua target exports LUA_BUILD_AS_DLL as an INTERFACE compile
// definition, and letting SDK targets propagate definitions into Engine is
// what previously rebuilt the whole engine without exception support.

#include "../IScriptBackend.h"

#include <memory>
#include <string>

namespace MyCoreEngine {

    class LuaScriptBackend final : public IScriptBackend {
    public:
        LuaScriptBackend();
        ~LuaScriptBackend() override;

        const char* name() const override { return "Lua"; }
        const char* language() const override { return "Lua 5.4"; }

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

        bool supportsInstructionLimit() const override { return true; }

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace MyCoreEngine
