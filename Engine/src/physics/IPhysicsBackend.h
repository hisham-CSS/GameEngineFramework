#pragma once
// The physics seam: every library (Jolt, PhysX, or anything added later)
// implements exactly this interface, and nothing outside a backend's own .cpp
// ever sees a library type.
//
// Contract notes for implementers:
// - step() is called from the FIXED tick with a constant dt. Backends must not
//   substitute their own variable timestep; determinism is the point.
// - Handles issued by createBody() must stay valid until destroyBody() or
//   shutdown(). They are opaque to callers.
// - initialize() may fail (missing SDK runtime, allocation failure); return
//   false and leave the object safely destructible.

#include "PhysicsTypes.h"

#include <string>
#include <vector>

namespace MyCoreEngine {

    class IPhysicsBackend {
    public:
        virtual ~IPhysicsBackend() = default;

        // Stable identifier used by the registry, the project settings, and
        // the editor's backend picker (e.g. "Null", "Jolt", "PhysX").
        virtual const char* name() const = 0;

        // ---- world lifecycle ----
        virtual bool initialize(const PhysicsSettings& settings) = 0;
        virtual void shutdown() = 0;

        // ---- bodies ----
        virtual BodyId createBody(const BodyDesc& desc) = 0;
        virtual void   destroyBody(BodyId id) = 0;
        virtual void   destroyAllBodies() = 0;
        virtual size_t bodyCount() const = 0;

        // ---- simulation ----
        // fixedDt is the engine's fixed timestep (seconds), always > 0.
        virtual void step(float fixedDt) = 0;

        // ---- state access ----
        virtual bool getBodyState(BodyId id, BodyState& out) const = 0;
        virtual void setBodyTransform(BodyId id, const glm::vec3& position,
                                      const glm::quat& rotation) = 0;
        virtual void setLinearVelocity(BodyId id, const glm::vec3& v) = 0;
        virtual void setAngularVelocity(BodyId id, const glm::vec3& v) = 0;
        virtual void applyImpulse(BodyId id, const glm::vec3& impulse) = 0;
        // Wake a body the solver has put to sleep (after teleporting it, etc).
        virtual void wakeBody(BodyId id) = 0;

        // ---- queries ----
        // direction need not be normalized. Returns false when nothing is hit.
        virtual bool raycast(const glm::vec3& origin, const glm::vec3& direction,
                             float maxDistance, RayHit& out) const = 0;

        // ---- collision / trigger events ----
        // Transitions recorded during the most recent step(), in no
        // particular order. The buffer is cleared at the START of each step,
        // so a caller reads it after step() and before the next one.
        //
        // Backends MUST accumulate these on their own lock: Jolt invokes
        // contact callbacks concurrently from its job threads (with all
        // bodies locked, read-only). Collecting during the step and exposing
        // only afterwards is what lets listener code stay single-threaded.
        virtual const std::vector<ContactEvent>& contactEvents() const {
            static const std::vector<ContactEvent> kNone;
            return kNone;
        }
        virtual bool supportsContactEvents() const { return false; }

        // ---- world settings ----
        virtual void      setGravity(const glm::vec3& g) = 0;
        virtual glm::vec3 gravity() const = 0;

        // Optional capability advertisement. The engine uses this to warn when
        // a scene asks for something the active backend cannot do, rather than
        // silently simulating the wrong thing.
        virtual bool supportsShape(ShapeType) const { return true; }
        virtual bool supportsTriggers() const { return true; }
    };

} // namespace MyCoreEngine
