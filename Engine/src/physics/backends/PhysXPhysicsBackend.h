#pragma once
// PhysX implementation of IPhysicsBackend.
//
// As with the Jolt backend, every PhysX type stays behind a pimpl so
// <PxPhysicsAPI.h> is included by exactly one translation unit.

#include "../IPhysicsBackend.h"

#include <memory>

namespace MyCoreEngine {

    class PhysXPhysicsBackend final : public IPhysicsBackend {
    public:
        PhysXPhysicsBackend();
        ~PhysXPhysicsBackend() override;

        const char* name() const override { return "PhysX"; }

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

        bool supportsShape(ShapeType) const override { return true; }
        bool supportsTriggers() const override { return true; }

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace MyCoreEngine
