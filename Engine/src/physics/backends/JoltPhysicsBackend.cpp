#include "JoltPhysicsBackend.h"

// Jolt requires this exact include order and its own new/delete hooks.
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <unordered_set>

namespace MyCoreEngine {

    namespace {

        // ---- collision layers -------------------------------------------
        // Two object layers (non-moving / moving) mapped onto two broadphase
        // layers. This is the minimal viable setup from Jolt's own sample
        // docs; richer filtering is a natural extension.
        namespace Layers {
            static constexpr JPH::ObjectLayer NON_MOVING = 0;
            static constexpr JPH::ObjectLayer MOVING = 1;
            static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
        }
        namespace BroadPhaseLayers {
            static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
            static constexpr JPH::BroadPhaseLayer MOVING(1);
            static constexpr unsigned NUM_LAYERS = 2;
        }

        class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
        public:
            BPLayerInterfaceImpl() {
                objectToBroadPhase_[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
                objectToBroadPhase_[Layers::MOVING] = BroadPhaseLayers::MOVING;
            }
            unsigned GetNumBroadPhaseLayers() const override {
                return BroadPhaseLayers::NUM_LAYERS;
            }
            JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
                return objectToBroadPhase_[layer];
            }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
            const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer) const override {
                return "layer";
            }
#endif
        private:
            JPH::BroadPhaseLayer objectToBroadPhase_[Layers::NUM_LAYERS]{
                BroadPhaseLayers::NON_MOVING, BroadPhaseLayers::MOVING };
        };

        class ObjectVsBroadPhaseFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter {
        public:
            bool ShouldCollide(JPH::ObjectLayer layer1, JPH::BroadPhaseLayer layer2) const override {
                // static things never need to test against other static things
                if (layer1 == Layers::NON_MOVING) return layer2 == BroadPhaseLayers::MOVING;
                return true;
            }
        };

        class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter {
        public:
            bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
                if (a == Layers::NON_MOVING) return b == Layers::MOVING;
                return true;
            }
        };

        // Jolt's global Factory/type registry is process-wide, so it must be
        // set up exactly once even if several worlds are created.
        std::once_flag gJoltInitOnce;
        void initJoltGlobals() {
            std::call_once(gJoltInitOnce, [] {
                JPH::RegisterDefaultAllocator();
                JPH::Factory::sInstance = new JPH::Factory();
                JPH::RegisterTypes();
            });
        }

        inline JPH::Vec3 toJolt(const glm::vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
        inline glm::vec3 toGlm(const JPH::Vec3& v) { return { v.GetX(), v.GetY(), v.GetZ() }; }
        inline JPH::Quat toJolt(const glm::quat& q) { return JPH::Quat(q.x, q.y, q.z, q.w); }
        inline glm::quat toGlm(const JPH::Quat& q) {
            return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
        }

    } // namespace

    struct JoltPhysicsBackend::Impl {
        std::unique_ptr<JPH::PhysicsSystem>        system;
        std::unique_ptr<JPH::TempAllocatorImpl>    tempAllocator;
        std::unique_ptr<JPH::JobSystemThreadPool>  jobSystem;
        BPLayerInterfaceImpl                       bpLayers;
        ObjectVsBroadPhaseFilterImpl               objVsBp;
        ObjectLayerPairFilterImpl                  objPair;
        std::unordered_set<uint32_t>               bodies; // live BodyID indices
        PhysicsSettings                            settings{};
    };

    JoltPhysicsBackend::JoltPhysicsBackend() : impl_(std::make_unique<Impl>()) {}

    JoltPhysicsBackend::~JoltPhysicsBackend() { shutdown(); }

    bool JoltPhysicsBackend::initialize(const PhysicsSettings& settings) {
        initJoltGlobals();
        auto& I = *impl_;
        I.settings = settings;

        // 10 MB scratch is Jolt's documented default for a mid-size world.
        I.tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);
        // 0 worker threads => everything runs on the calling (fixed-tick)
        // thread, which is what the engine wants for determinism.
        const int workers = static_cast<int>(settings.workerThreads);
        I.jobSystem = std::make_unique<JPH::JobSystemThreadPool>(
            JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, workers);

        I.system = std::make_unique<JPH::PhysicsSystem>();
        I.system->Init(settings.maxBodies, /*numBodyMutexes*/ 0,
                       settings.maxBodyPairs, settings.maxContactConstraints,
                       I.bpLayers, I.objVsBp, I.objPair);
        I.system->SetGravity(toJolt(settings.gravity));
        return true;
    }

    void JoltPhysicsBackend::shutdown() {
        auto& I = *impl_;
        if (I.system) {
            destroyAllBodies();
            I.system.reset();
        }
        I.jobSystem.reset();
        I.tempAllocator.reset();
        // Deliberately NOT tearing down the global Factory/type registry:
        // it is process-wide and another world may still be alive.
    }

    BodyId JoltPhysicsBackend::createBody(const BodyDesc& desc) {
        auto& I = *impl_;
        if (!I.system) return BodyId{};

        JPH::ShapeRefC shape;
        switch (desc.shape.type) {
        case ShapeType::Box: {
            const glm::vec3 h = glm::max(desc.shape.halfExtents, glm::vec3(0.001f));
            shape = new JPH::BoxShape(toJolt(h));
            break;
        }
        case ShapeType::Sphere:
            shape = new JPH::SphereShape(std::max(desc.shape.radius, 0.001f));
            break;
        case ShapeType::Capsule:
            shape = new JPH::CapsuleShape(std::max(desc.shape.halfHeight, 0.001f),
                                          std::max(desc.shape.radius, 0.001f));
            break;
        case ShapeType::Plane: {
            // Jolt has no infinite plane: a very large, very thin static box
            // is the standard substitute and behaves identically for a ground.
            shape = new JPH::BoxShape(JPH::Vec3(1000.0f, 0.1f, 1000.0f));
            break;
        }
        }
        if (shape == nullptr) return BodyId{};

        const bool isStatic = (desc.type == BodyType::Static);
        const JPH::EMotionType motion =
            isStatic ? JPH::EMotionType::Static
                     : (desc.type == BodyType::Kinematic ? JPH::EMotionType::Kinematic
                                                         : JPH::EMotionType::Dynamic);
        const JPH::ObjectLayer layer = isStatic ? Layers::NON_MOVING : Layers::MOVING;

        // Plane substitution puts the box top at the requested height.
        glm::vec3 pos = desc.position + desc.shape.offset;
        if (desc.shape.type == ShapeType::Plane) pos.y -= 0.1f;

        JPH::BodyCreationSettings bcs(shape, toJolt(pos), toJolt(desc.rotation),
                                      motion, layer);
        bcs.mFriction = desc.material.friction;
        bcs.mRestitution = desc.material.restitution;
        bcs.mLinearDamping = desc.linearDamping;
        bcs.mAngularDamping = desc.angularDamping;
        bcs.mIsSensor = desc.isTrigger;
        bcs.mUserData = desc.userData;
        if (desc.mass > 0.f && motion == JPH::EMotionType::Dynamic) {
            bcs.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            bcs.mMassPropertiesOverride.mMass = desc.mass;
        }

        JPH::BodyInterface& bi = I.system->GetBodyInterface();
        JPH::Body* body = bi.CreateBody(bcs);
        if (!body) return BodyId{}; // body budget exhausted

        bi.AddBody(body->GetID(), isStatic ? JPH::EActivation::DontActivate
                                           : JPH::EActivation::Activate);
        if (motion == JPH::EMotionType::Dynamic &&
            glm::length(desc.linearVelocity) > 0.f) {
            bi.SetLinearVelocity(body->GetID(), toJolt(desc.linearVelocity));
        }

        const uint32_t idx = body->GetID().GetIndexAndSequenceNumber();
        I.bodies.insert(idx);
        return BodyId{ static_cast<uint64_t>(idx) };
    }

    void JoltPhysicsBackend::destroyBody(BodyId id) {
        auto& I = *impl_;
        if (!I.system || !id.valid()) return;
        const JPH::BodyID bid(static_cast<uint32_t>(id.value));
        JPH::BodyInterface& bi = I.system->GetBodyInterface();
        bi.RemoveBody(bid);
        bi.DestroyBody(bid);
        I.bodies.erase(static_cast<uint32_t>(id.value));
    }

    void JoltPhysicsBackend::destroyAllBodies() {
        auto& I = *impl_;
        if (!I.system) { I.bodies.clear(); return; }
        JPH::BodyInterface& bi = I.system->GetBodyInterface();
        for (uint32_t idx : I.bodies) {
            const JPH::BodyID bid(idx);
            bi.RemoveBody(bid);
            bi.DestroyBody(bid);
        }
        I.bodies.clear();
    }

    size_t JoltPhysicsBackend::bodyCount() const { return impl_->bodies.size(); }

    void JoltPhysicsBackend::step(float fixedDt) {
        auto& I = *impl_;
        if (!I.system || fixedDt <= 0.f) return;
        // 1 collision step per update: the engine already supplies a fixed,
        // small dt, so Jolt does not need to subdivide it further.
        I.system->Update(fixedDt, 1, I.tempAllocator.get(), I.jobSystem.get());
    }

    bool JoltPhysicsBackend::getBodyState(BodyId id, BodyState& out) const {
        auto& I = *impl_;
        if (!I.system || !id.valid()) return false;
        const JPH::BodyID bid(static_cast<uint32_t>(id.value));
        const JPH::BodyInterface& bi = I.system->GetBodyInterface();
        if (!bi.IsAdded(bid)) return false;
        JPH::RVec3 p = bi.GetPosition(bid);
        out.position = { float(p.GetX()), float(p.GetY()), float(p.GetZ()) };
        out.rotation = toGlm(bi.GetRotation(bid));
        out.linearVelocity = toGlm(bi.GetLinearVelocity(bid));
        out.angularVelocity = toGlm(bi.GetAngularVelocity(bid));
        return true;
    }

    void JoltPhysicsBackend::setBodyTransform(BodyId id, const glm::vec3& position,
                                              const glm::quat& rotation) {
        auto& I = *impl_;
        if (!I.system || !id.valid()) return;
        I.system->GetBodyInterface().SetPositionAndRotation(
            JPH::BodyID(static_cast<uint32_t>(id.value)),
            JPH::RVec3(position.x, position.y, position.z), toJolt(rotation),
            JPH::EActivation::Activate);
    }

    void JoltPhysicsBackend::setLinearVelocity(BodyId id, const glm::vec3& v) {
        auto& I = *impl_;
        if (!I.system || !id.valid()) return;
        I.system->GetBodyInterface().SetLinearVelocity(
            JPH::BodyID(static_cast<uint32_t>(id.value)), toJolt(v));
    }

    void JoltPhysicsBackend::setAngularVelocity(BodyId id, const glm::vec3& v) {
        auto& I = *impl_;
        if (!I.system || !id.valid()) return;
        I.system->GetBodyInterface().SetAngularVelocity(
            JPH::BodyID(static_cast<uint32_t>(id.value)), toJolt(v));
    }

    void JoltPhysicsBackend::applyImpulse(BodyId id, const glm::vec3& impulse) {
        auto& I = *impl_;
        if (!I.system || !id.valid()) return;
        I.system->GetBodyInterface().AddImpulse(
            JPH::BodyID(static_cast<uint32_t>(id.value)), toJolt(impulse));
    }

    void JoltPhysicsBackend::wakeBody(BodyId id) {
        auto& I = *impl_;
        if (!I.system || !id.valid()) return;
        I.system->GetBodyInterface().ActivateBody(
            JPH::BodyID(static_cast<uint32_t>(id.value)));
    }

    bool JoltPhysicsBackend::raycast(const glm::vec3& origin, const glm::vec3& direction,
                                     float maxDistance, RayHit& out) const {
        out = RayHit{};
        auto& I = *impl_;
        if (!I.system) return false;
        const float len = glm::length(direction);
        if (len <= 1e-6f || maxDistance <= 0.f) return false;
        const glm::vec3 dir = direction / len;

        const JPH::RRayCast ray{ JPH::RVec3(origin.x, origin.y, origin.z),
                                 toJolt(dir * maxDistance) };
        JPH::RayCastResult hit;
        if (!I.system->GetNarrowPhaseQuery().CastRay(ray, hit)) return false;

        out.hit = true;
        out.body = BodyId{ static_cast<uint64_t>(hit.mBodyID.GetIndexAndSequenceNumber()) };
        out.distance = hit.mFraction * maxDistance;
        const JPH::RVec3 pt = ray.GetPointOnRay(hit.mFraction);
        out.point = { float(pt.GetX()), float(pt.GetY()), float(pt.GetZ()) };

        const JPH::BodyInterface& bi = I.system->GetBodyInterface();
        out.userData = bi.GetUserData(hit.mBodyID);
        // surface normal at the hit, via the body's shape
        JPH::BodyLockRead lock(I.system->GetBodyLockInterface(), hit.mBodyID);
        if (lock.Succeeded()) {
            out.normal = toGlm(lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, pt));
        }
        return true;
    }

    void JoltPhysicsBackend::setGravity(const glm::vec3& g) {
        impl_->settings.gravity = g;
        if (impl_->system) impl_->system->SetGravity(toJolt(g));
    }

    glm::vec3 JoltPhysicsBackend::gravity() const {
        if (impl_->system) return toGlm(impl_->system->GetGravity());
        return impl_->settings.gravity;
    }

} // namespace MyCoreEngine
