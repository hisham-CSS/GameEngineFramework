#include "PhysXPhysicsBackend.h"

#include <PxPhysicsAPI.h>

#include <mutex>
#include <unordered_set>

namespace MyCoreEngine {

    using namespace physx;

    namespace {

        // PhysX allows only ONE PxFoundation per process, so foundation and
        // PxPhysics are shared, reference-counted globals rather than
        // per-world objects. (The editor can hold a world while a test or a
        // second world exists; per-instance foundations would fail the second
        // PxCreateFoundation.)
        struct PxGlobals {
            PxDefaultAllocator     allocator;
            PxDefaultErrorCallback errorCallback;
            PxFoundation*          foundation = nullptr;
            PxPhysics*             physics = nullptr;
            int                    refCount = 0;
            std::mutex             mutex;
        };
        PxGlobals& globals() {
            static PxGlobals g;
            return g;
        }

        bool acquireGlobals() {
            auto& g = globals();
            std::lock_guard<std::mutex> lock(g.mutex);
            if (g.refCount == 0) {
                g.foundation = PxCreateFoundation(PX_PHYSICS_VERSION, g.allocator,
                                                  g.errorCallback);
                if (!g.foundation) return false;
                g.physics = PxCreatePhysics(PX_PHYSICS_VERSION, *g.foundation,
                                            PxTolerancesScale());
                if (!g.physics) {
                    g.foundation->release();
                    g.foundation = nullptr;
                    return false;
                }
            }
            ++g.refCount;
            return true;
        }

        void releaseGlobals() {
            auto& g = globals();
            std::lock_guard<std::mutex> lock(g.mutex);
            if (g.refCount == 0) return;
            if (--g.refCount == 0) {
                if (g.physics) { g.physics->release(); g.physics = nullptr; }
                if (g.foundation) { g.foundation->release(); g.foundation = nullptr; }
            }
        }

        inline PxVec3 toPx(const glm::vec3& v) { return PxVec3(v.x, v.y, v.z); }
        inline glm::vec3 toGlm(const PxVec3& v) { return { v.x, v.y, v.z }; }
        inline PxQuat toPx(const glm::quat& q) { return PxQuat(q.x, q.y, q.z, q.w); }
        inline glm::quat toGlm(const PxQuat& q) { return glm::quat(q.w, q.x, q.y, q.z); }

    } // namespace

    struct PhysXPhysicsBackend::Impl {
        PxScene*                 scene = nullptr;
        PxDefaultCpuDispatcher*  dispatcher = nullptr;
        PxMaterial*              defaultMaterial = nullptr;
        std::unordered_set<PxActor*> actors;
        PhysicsSettings          settings{};
        bool                     acquired = false;
    };

    PhysXPhysicsBackend::PhysXPhysicsBackend() : impl_(std::make_unique<Impl>()) {}

    PhysXPhysicsBackend::~PhysXPhysicsBackend() { shutdown(); }

    bool PhysXPhysicsBackend::initialize(const PhysicsSettings& settings) {
        auto& I = *impl_;
        if (!acquireGlobals()) return false;
        I.acquired = true;
        I.settings = settings;

        PxPhysics* physics = globals().physics;
        PxSceneDesc desc(physics->getTolerancesScale());
        desc.gravity = toPx(settings.gravity);
        // 0 worker threads => PhysX simulates on the calling (fixed-tick)
        // thread, matching the engine's determinism requirement.
        I.dispatcher = PxDefaultCpuDispatcherCreate(settings.workerThreads);
        if (!I.dispatcher) { shutdown(); return false; }
        desc.cpuDispatcher = I.dispatcher;
        desc.filterShader = PxDefaultSimulationFilterShader;

        I.scene = physics->createScene(desc);
        if (!I.scene) { shutdown(); return false; }

        I.defaultMaterial = physics->createMaterial(0.5f, 0.5f, 0.0f);
        if (!I.defaultMaterial) { shutdown(); return false; }
        return true;
    }

    void PhysXPhysicsBackend::shutdown() {
        auto& I = *impl_;
        destroyAllBodies();
        if (I.defaultMaterial) { I.defaultMaterial->release(); I.defaultMaterial = nullptr; }
        if (I.scene) { I.scene->release(); I.scene = nullptr; }
        if (I.dispatcher) { I.dispatcher->release(); I.dispatcher = nullptr; }
        if (I.acquired) { releaseGlobals(); I.acquired = false; }
    }

    BodyId PhysXPhysicsBackend::createBody(const BodyDesc& desc) {
        auto& I = *impl_;
        if (!I.scene) return BodyId{};
        PxPhysics* physics = globals().physics;

        // Per-body material so friction/restitution are honoured per entity.
        PxMaterial* mat = physics->createMaterial(desc.material.friction,
                                                  desc.material.friction,
                                                  desc.material.restitution);
        if (!mat) mat = I.defaultMaterial;

        const glm::vec3 p = desc.position + desc.shape.offset;
        const PxTransform pose(toPx(p), toPx(desc.rotation));

        PxRigidActor* actor = nullptr;

        if (desc.shape.type == ShapeType::Plane) {
            // A real infinite plane: normal +Y through the requested height.
            actor = PxCreatePlane(*physics, PxPlane(PxVec3(0, 1, 0), -p.y), *mat);
        }
        else {
            PxGeometryHolder geom;
            switch (desc.shape.type) {
            case ShapeType::Box: {
                const glm::vec3 h = glm::max(desc.shape.halfExtents, glm::vec3(0.001f));
                geom = PxGeometryHolder(PxBoxGeometry(h.x, h.y, h.z));
                break;
            }
            case ShapeType::Sphere:
                geom = PxGeometryHolder(PxSphereGeometry(std::max(desc.shape.radius, 0.001f)));
                break;
            case ShapeType::Capsule:
                geom = PxGeometryHolder(PxCapsuleGeometry(std::max(desc.shape.radius, 0.001f),
                                                          std::max(desc.shape.halfHeight, 0.001f)));
                break;
            default:
                break;
            }

            PxShape* shape = physics->createShape(geom.any(), *mat);
            if (!shape) {
                if (mat != I.defaultMaterial) mat->release();
                return BodyId{};
            }
            if (desc.isTrigger) {
                shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, false);
                shape->setFlag(PxShapeFlag::eTRIGGER_SHAPE, true);
            }
            // PhysX capsules are X-aligned by default; the engine's convention
            // (and every other backend here) is Y-up, so rotate the shape.
            if (desc.shape.type == ShapeType::Capsule) {
                shape->setLocalPose(PxTransform(PxQuat(PxHalfPi, PxVec3(0, 0, 1))));
            }

            if (desc.type == BodyType::Static) {
                PxRigidStatic* s = physics->createRigidStatic(pose);
                if (s) s->attachShape(*shape);
                actor = s;
            }
            else {
                PxRigidDynamic* d = physics->createRigidDynamic(pose);
                if (d) {
                    d->attachShape(*shape);
                    if (desc.type == BodyType::Kinematic) {
                        d->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
                    }
                    else {
                        if (desc.mass > 0.f) {
                            PxRigidBodyExt::setMassAndUpdateInertia(*d, desc.mass);
                        }
                        else {
                            PxRigidBodyExt::updateMassAndInertia(*d, 1000.f);
                        }
                        d->setLinearDamping(desc.linearDamping);
                        d->setAngularDamping(desc.angularDamping);
                        d->setLinearVelocity(toPx(desc.linearVelocity));
                        d->setAngularVelocity(toPx(desc.angularVelocity));
                    }
                }
                actor = d;
            }
            shape->release(); // the actor holds its own reference now
        }

        if (mat != I.defaultMaterial) mat->release();
        if (!actor) return BodyId{};

        actor->userData = reinterpret_cast<void*>(static_cast<uintptr_t>(desc.userData));
        I.scene->addActor(*actor);
        I.actors.insert(actor);
        return BodyId{ reinterpret_cast<uint64_t>(actor) };
    }

    void PhysXPhysicsBackend::destroyBody(BodyId id) {
        auto& I = *impl_;
        if (!I.scene || !id.valid()) return;
        auto* actor = reinterpret_cast<PxActor*>(id.value);
        if (I.actors.erase(actor) == 0) return; // not ours / already gone
        I.scene->removeActor(*actor);
        actor->release();
    }

    void PhysXPhysicsBackend::destroyAllBodies() {
        auto& I = *impl_;
        if (I.scene) {
            for (PxActor* a : I.actors) {
                I.scene->removeActor(*a);
                a->release();
            }
        }
        I.actors.clear();
    }

    size_t PhysXPhysicsBackend::bodyCount() const { return impl_->actors.size(); }

    void PhysXPhysicsBackend::step(float fixedDt) {
        auto& I = *impl_;
        if (!I.scene || fixedDt <= 0.f) return;
        I.scene->simulate(fixedDt);
        I.scene->fetchResults(true); // block: the engine wants the result now
    }

    bool PhysXPhysicsBackend::getBodyState(BodyId id, BodyState& out) const {
        auto& I = *impl_;
        if (!id.valid()) return false;
        auto* actor = reinterpret_cast<PxActor*>(id.value);
        if (I.actors.find(actor) == I.actors.end()) return false;
        auto* rigid = actor->is<PxRigidActor>();
        if (!rigid) return false;

        const PxTransform t = rigid->getGlobalPose();
        out.position = toGlm(t.p);
        out.rotation = toGlm(t.q);
        if (auto* dyn = actor->is<PxRigidDynamic>()) {
            out.linearVelocity = toGlm(dyn->getLinearVelocity());
            out.angularVelocity = toGlm(dyn->getAngularVelocity());
        }
        return true;
    }

    void PhysXPhysicsBackend::setBodyTransform(BodyId id, const glm::vec3& position,
                                               const glm::quat& rotation) {
        auto& I = *impl_;
        if (!id.valid()) return;
        auto* actor = reinterpret_cast<PxActor*>(id.value);
        if (I.actors.find(actor) == I.actors.end()) return;
        auto* rigid = actor->is<PxRigidActor>();
        if (!rigid) return;
        const PxTransform pose(toPx(position), toPx(rotation));
        // Kinematic actors must be moved with setKinematicTarget so the solver
        // sweeps them and they push dynamics instead of teleporting through.
        if (auto* dyn = actor->is<PxRigidDynamic>();
            dyn && (dyn->getRigidBodyFlags() & PxRigidBodyFlag::eKINEMATIC)) {
            dyn->setKinematicTarget(pose);
        }
        else {
            rigid->setGlobalPose(pose);
        }
    }

    void PhysXPhysicsBackend::setLinearVelocity(BodyId id, const glm::vec3& v) {
        if (!id.valid()) return;
        auto* actor = reinterpret_cast<PxActor*>(id.value);
        if (impl_->actors.find(actor) == impl_->actors.end()) return;
        if (auto* dyn = actor->is<PxRigidDynamic>()) dyn->setLinearVelocity(toPx(v));
    }

    void PhysXPhysicsBackend::setAngularVelocity(BodyId id, const glm::vec3& v) {
        if (!id.valid()) return;
        auto* actor = reinterpret_cast<PxActor*>(id.value);
        if (impl_->actors.find(actor) == impl_->actors.end()) return;
        if (auto* dyn = actor->is<PxRigidDynamic>()) dyn->setAngularVelocity(toPx(v));
    }

    void PhysXPhysicsBackend::applyImpulse(BodyId id, const glm::vec3& impulse) {
        if (!id.valid()) return;
        auto* actor = reinterpret_cast<PxActor*>(id.value);
        if (impl_->actors.find(actor) == impl_->actors.end()) return;
        if (auto* dyn = actor->is<PxRigidDynamic>()) {
            dyn->addForce(toPx(impulse), PxForceMode::eIMPULSE);
        }
    }

    void PhysXPhysicsBackend::wakeBody(BodyId id) {
        if (!id.valid()) return;
        auto* actor = reinterpret_cast<PxActor*>(id.value);
        if (impl_->actors.find(actor) == impl_->actors.end()) return;
        if (auto* dyn = actor->is<PxRigidDynamic>()) {
            if (!(dyn->getRigidBodyFlags() & PxRigidBodyFlag::eKINEMATIC)) dyn->wakeUp();
        }
    }

    bool PhysXPhysicsBackend::raycast(const glm::vec3& origin, const glm::vec3& direction,
                                      float maxDistance, RayHit& out) const {
        out = RayHit{};
        auto& I = *impl_;
        if (!I.scene) return false;
        const float len = glm::length(direction);
        if (len <= 1e-6f || maxDistance <= 0.f) return false;

        PxRaycastBuffer buf;
        if (!I.scene->raycast(toPx(origin), toPx(direction / len), maxDistance, buf)) {
            return false;
        }
        if (!buf.hasBlock) return false;

        out.hit = true;
        out.body = BodyId{ reinterpret_cast<uint64_t>(buf.block.actor) };
        out.point = toGlm(buf.block.position);
        out.normal = toGlm(buf.block.normal);
        out.distance = buf.block.distance;
        out.userData = buf.block.actor
            ? static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buf.block.actor->userData))
            : 0ull;
        return true;
    }

    void PhysXPhysicsBackend::setGravity(const glm::vec3& g) {
        impl_->settings.gravity = g;
        if (impl_->scene) impl_->scene->setGravity(toPx(g));
    }

    glm::vec3 PhysXPhysicsBackend::gravity() const {
        if (impl_->scene) return toGlm(impl_->scene->getGravity());
        return impl_->settings.gravity;
    }

} // namespace MyCoreEngine
