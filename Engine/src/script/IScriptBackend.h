#pragma once
// The OUTWARD half of the scripting seam: every language runtime (Lua now,
// C#/Wren/AngelScript later) implements exactly this, and no language type
// escapes a backend's own .cpp.
//
// Contract notes for implementers:
// - loadScript() COMPILES and creates an isolated instance environment. It
//   must NOT run lifecycle callbacks; ScriptWorld decides when Start happens
//   (which differs between editor-play and the shipped player).
// - Every call* returns false and fills ScriptError instead of throwing or
//   aborting. A syntax error in a user's file is normal input, not a crash.
// - Each ScriptId owns a SEPARATE global environment. Two entities running
//   the same file must not share variables.
// - initialize() may fail; return false and leave the object destructible.

#include "../core/Core.h"
#include "IScriptHost.h"
#include "ScriptTypes.h"

#include <string>

namespace MyCoreEngine {

    // Lifecycle hooks a script file may define. Kept as an enum (not raw
    // strings at call sites) so a typo is a compile error and so backends can
    // pre-resolve the lookup once at load instead of per frame.
    enum class ScriptCallback {
        Start,      // once, before the first Update
        Update,     // every frame, receives dt
        FixedUpdate,// every fixed physics tick, receives the fixed dt
        Destroy,    // entity or world torn down
        Collision,  // physics contact involving this entity
    };

    ENGINE_API const char* ScriptCallbackName(ScriptCallback cb);

    // Passed to the Collision hook.
    struct ScriptCollision {
        ScriptEntity other = kInvalidScriptEntity;
        const char*  phase = "begin"; // "begin" | "end"
        bool         isTrigger = false;
        glm::vec3    point{ 0.f };
        glm::vec3    normal{ 0.f };
        float        impulse = 0.f;
    };

    class IScriptBackend {
    public:
        virtual ~IScriptBackend() = default;

        // Registry key and human-facing language name ("Lua", "Null").
        virtual const char* name() const = 0;
        virtual const char* language() const = 0;

        // ---- runtime lifecycle ----
        // `host` outlives the backend and is never null.
        virtual bool initialize(const ScriptSettings& settings, IScriptHost* host) = 0;
        virtual void shutdown() = 0;

        // ---- script instances ----
        // debugName appears in error messages (use the source path).
        virtual ScriptId loadScript(const std::string& debugName,
                                    const std::string& source,
                                    ScriptError& err) = 0;
        virtual void   destroyScript(ScriptId id) = 0;
        virtual void   destroyAllScripts() = 0;
        virtual size_t scriptCount() const = 0;

        // True when the loaded file actually defines the hook. ScriptWorld
        // checks this once at load so a script with no OnUpdate costs nothing
        // per frame rather than paying a failed global lookup 60x/second.
        virtual bool hasCallback(ScriptId id, ScriptCallback cb) const = 0;

        // ---- invocation ----
        // `self` is the owning entity, exposed to the script as `self`.
        virtual bool callStart(ScriptId id, ScriptEntity self, ScriptError& err) = 0;
        virtual bool callUpdate(ScriptId id, ScriptEntity self, float dt, ScriptError& err) = 0;
        virtual bool callFixedUpdate(ScriptId id, ScriptEntity self, float fixedDt, ScriptError& err) = 0;
        virtual bool callDestroy(ScriptId id, ScriptEntity self, ScriptError& err) = 0;
        virtual bool callCollision(ScriptId id, ScriptEntity self,
                                   const ScriptCollision& hit, ScriptError& err) = 0;

        // Optional capability advertisement, mirroring IPhysicsBackend: the
        // engine warns rather than silently doing nothing when a build's
        // backend cannot honour a setting.
        virtual bool supportsInstructionLimit() const { return false; }
        virtual bool supportsHotReload() const { return false; }
    };

} // namespace MyCoreEngine
