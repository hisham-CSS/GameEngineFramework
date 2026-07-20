#pragma once
// Jolt implementation of IPhysicsBackend.
//
// No Jolt type appears in this header: all of it (PhysicsSystem, BodyID, the
// layer interfaces, the temp allocator and job system Jolt requires) is held
// behind a pimpl in the .cpp. That keeps <Jolt/...> out of the engine's
// include graph, so only this one translation unit depends on the SDK.

#include "../IPhysicsBackend.h"

#include <memory>

namespace MyCoreEngine {

    class JoltPhysicsBackend final : public IPhysicsBackend {
    public:
        JoltPhysicsBackend();
        ~JoltPhysicsBackend() override;

        const char* name() const override { return "Jolt"; }

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

        const std::vector<ContactEvent>& contactEvents() const override;
        bool supportsContactEvents() const override { return true; }

        void      setGravity(const glm::vec3& g) override;
        glm::vec3 gravity() const override;

        // Jolt has no infinite-plane primitive; the engine substitutes a very
        // large static box, so Plane is still "supported" from the caller's
        // point of view.
        bool supportsShape(ShapeType) const override { return true; }
        bool supportsTriggers() const override { return true; }

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace MyCoreEngine
