#pragma once
// The INWARD half of the scripting seam: what a script is allowed to do to
// the world. The engine implements this; each backend binds it into its own
// language. IScriptBackend is the outward half (engine calls into scripts).
//
// Two halves rather than one is what keeps a backend genuinely swappable: a
// new language implements IScriptBackend and calls this same host, so the
// engine-side capability set is written ONCE and every language inherits it.
//
// Everything is plain integers / glm / C strings — no entt, no engine
// components — so a backend .cpp never includes engine internals.

#include "ScriptTypes.h"

#include <glm/glm.hpp>

namespace MyCoreEngine {

    // Result of a script-issued raycast.
    struct ScriptRayHit {
        ScriptEntity entity = kInvalidScriptEntity;
        glm::vec3    point{ 0.f };
        glm::vec3    normal{ 0.f };
        float        distance = 0.f;
    };

    class IScriptHost {
    public:
        virtual ~IScriptHost() = default;

        // ---- diagnostics ----
        // level is "info" | "warn" | "error". Routed to the engine log with
        // the originating script named, so a message from a misbehaving
        // object is traceable back to the entity.
        virtual void log(ScriptEntity self, const char* level, const char* message) = 0;

        // ---- identity ----
        // Returns nullptr when the entity is gone. Scripts hold entity ids
        // across frames, so every accessor must tolerate a stale one rather
        // than assuming liveness.
        virtual const char*  getName(ScriptEntity e) const = 0;
        virtual bool         isValid(ScriptEntity e) const = 0;
        virtual ScriptEntity findByName(const char* name) const = 0;

        // ---- transform ----
        // All in WORLD space intent but written to the local Transform, which
        // is what the hierarchy propagates from. Setters mark the transform
        // dirty so parenting and physics see the change.
        virtual bool getPosition(ScriptEntity e, glm::vec3& out) const = 0;
        virtual bool setPosition(ScriptEntity e, const glm::vec3& v) = 0;
        virtual bool getEulerDegrees(ScriptEntity e, glm::vec3& out) const = 0;
        virtual bool setEulerDegrees(ScriptEntity e, const glm::vec3& v) = 0;
        virtual bool getScale(ScriptEntity e, glm::vec3& out) const = 0;
        virtual bool setScale(ScriptEntity e, const glm::vec3& v) = 0;

        // ---- physics ----
        // No-ops returning false when the entity has no rigid body, or when
        // no physics backend is active.
        virtual bool applyImpulse(ScriptEntity e, const glm::vec3& impulse) = 0;
        virtual bool setLinearVelocity(ScriptEntity e, const glm::vec3& v) = 0;
        virtual bool raycast(const glm::vec3& origin, const glm::vec3& direction,
                             float maxDistance, ScriptRayHit& out) const = 0;

        // ---- input ----
        // Named actions from the InputMap rather than raw key codes, so
        // scripts survive rebinding and gamepad/keyboard differences.
        // Unknown names read as released / 0 rather than erroring: input
        // bindings are project data, and a script referencing one that has
        // not been bound yet should not kill the frame.
        virtual bool  isActionDown(const char* action) const = 0;
        // Edge-triggered: true only on the frame the action went down. Needed
        // for "jump" — polling isActionDown fires every frame it is held.
        virtual bool  wasActionPressed(const char* action) const = 0;
        virtual float actionAxis(const char* axis) const = 0;

        // ---- time ----
        virtual float timeSeconds() const = 0;
    };

} // namespace MyCoreEngine
