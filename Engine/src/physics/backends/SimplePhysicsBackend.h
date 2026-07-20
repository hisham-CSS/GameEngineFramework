#pragma once
// "Simple": the dependency-free reference backend.
//
// It is a REAL (if minimal) simulator, not a no-op: semi-implicit Euler
// integration with gravity and damping, plus resting contact against static
// Plane and Box bodies. That makes the seam testable and the engine runnable
// in a build with neither SDK — and it gives the backend-conformance test
// suite something to check the Jolt/PhysX implementations against.
//
// Deliberate limitations (documented, not bugs): no dynamic-vs-dynamic
// collision, no rotation from collision response, no continuous detection.
// Use Jolt or PhysX for real gameplay.

#include "../IPhysicsBackend.h"

#include <unordered_map>
#include <vector>

namespace MyCoreEngine {

    class SimplePhysicsBackend final : public IPhysicsBackend {
    public:
        const char* name() const override { return "Simple"; }

        bool initialize(const PhysicsSettings& settings) override;
        void shutdown() override;

        BodyId createBody(const BodyDesc& desc) override;
        void   destroyBody(BodyId id) override;
        void   destroyAllBodies() override;
        size_t bodyCount() const override;

        void step(float fixedDt) override;

        bool getBodyState(BodyId id, BodyState& out) const override;
        void setBodyTransform(BodyId id, const glm::vec3& position,
                              const glm::quat& rotation) override;
        void setLinearVelocity(BodyId id, const glm::vec3& v) override;
        void setAngularVelocity(BodyId id, const glm::vec3& v) override;
        void applyImpulse(BodyId id, const glm::vec3& impulse) override;
        void wakeBody(BodyId id) override;

        bool raycast(const glm::vec3& origin, const glm::vec3& direction,
                     float maxDistance, RayHit& out) const override;

        const std::vector<ContactEvent>& contactEvents() const override { return events_; }
        bool supportsContactEvents() const override { return true; }

        void      setGravity(const glm::vec3& g) override { settings_.gravity = g; }
        glm::vec3 gravity() const override { return settings_.gravity; }

        bool supportsTriggers() const override { return false; }

    private:
        struct Body {
            BodyDesc  desc{};
            BodyState state{};
            bool      alive = true;
            // id of the support this body is currently resting on (0 = none),
            // so Begin/End transitions can be detected between steps
            uint64_t  restingOn = 0;
        };

        // Lowest point of a shape below its origin — used for resting contact.
        static float shapeBottomOffset(const ShapeDesc& s);

        PhysicsSettings settings_{};
        std::unordered_map<uint64_t, Body> bodies_;
        std::vector<ContactEvent> events_;
        uint64_t nextId_ = 1; // 0 stays free so a zeroed BodyId is never valid
    };

} // namespace MyCoreEngine
