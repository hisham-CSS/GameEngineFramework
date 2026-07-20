#include "PhysicsWorld.h"
#include "PhysicsBackendRegistry.h"
#include "../core/Components.h"
#include "../core/Scene.h" // DecomposeTRS / ResolveWorldMatrix

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace MyCoreEngine {

    namespace {
        // Largest absolute scale on each axis of a world matrix.
        glm::vec3 matrixScale(const glm::mat4& m) {
            return { glm::length(glm::vec3(m[0])),
                     glm::length(glm::vec3(m[1])),
                     glm::length(glm::vec3(m[2])) };
        }
        // Rotation-only quaternion from a (possibly scaled) world matrix.
        glm::quat matrixRotation(const glm::mat4& m) {
            // braces, not parens: parenthesised form is a most-vexing-parse
            // (MSVC reads it as a function declaration)
            glm::mat3 r{ glm::vec3(m[0]), glm::vec3(m[1]), glm::vec3(m[2]) };
            for (int c = 0; c < 3; ++c) {
                const float len = glm::length(r[c]);
                if (len > 1e-8f) r[c] /= len;
            }
            return glm::normalize(glm::quat_cast(r));
        }
    } // namespace

    PhysicsWorld::~PhysicsWorld() {
        Clear();
        if (backend_) {
            backend_->shutdown();
            backend_.reset();
        }
    }

    bool PhysicsWorld::SetBackend(const std::string& name, const PhysicsSettings& settings) {
        // tear the old world down first so two SDKs never hold bodies at once
        Clear();
        if (backend_) {
            backend_->shutdown();
            backend_.reset();
        }
        backendName_.clear();

        auto be = PhysicsBackendRegistry::Create(name);
        if (!be) return false;
        if (!be->initialize(settings)) {
            be->shutdown(); // leave the failed backend safely destructible
            return false;
        }
        backend_ = std::move(be);
        backendName_ = name;
        settings_ = settings;
        return true;
    }

    bool PhysicsWorld::shapeFromEntity_(entt::registry& reg, entt::entity e, ShapeDesc& out) {
        if (const auto* c = reg.try_get<BoxCollider>(e)) {
            out.type = ShapeType::Box;
            out.halfExtents = glm::max(c->halfExtents, glm::vec3(1e-3f));
            out.offset = c->offset;
            return true;
        }
        if (const auto* c = reg.try_get<SphereCollider>(e)) {
            out.type = ShapeType::Sphere;
            out.radius = std::max(c->radius, 1e-3f);
            out.offset = c->offset;
            return true;
        }
        if (const auto* c = reg.try_get<CapsuleCollider>(e)) {
            out.type = ShapeType::Capsule;
            out.radius = std::max(c->radius, 1e-3f);
            out.halfHeight = std::max(c->halfHeight, 1e-4f);
            out.offset = c->offset;
            return true;
        }
        if (const auto* c = reg.try_get<PlaneCollider>(e)) {
            out.type = ShapeType::Plane;
            out.offset = c->offset;
            return true;
        }
        return false;
    }

    void PhysicsWorld::Build(entt::registry& reg) {
        skipped_.clear();
        if (!backend_) return;

        auto view = reg.view<RigidBody, Transform>();
        for (auto e : view) {
            const auto& rb = view.get<RigidBody>(e);
            const auto& t = view.get<Transform>(e);

            ShapeDesc shape{};
            if (!shapeFromEntity_(reg, e, shape)) {
                // A RigidBody with no collider has no volume; simulating it
                // would be an invisible point mass. Record it so the editor
                // can surface the mistake instead of silently doing nothing.
                skipped_.push_back(e);
                continue;
            }
            if (!backend_->supportsShape(shape.type)) {
                skipped_.push_back(e);
                continue;
            }

            // World pose: modelMatrix is WORLD (hierarchy already applied).
            const glm::mat4& world = t.modelMatrix;
            const glm::vec3 scale = matrixScale(world);

            // Bake the entity's scale into the shape — physics shapes are
            // authored in local units but simulate in world space.
            switch (shape.type) {
            case ShapeType::Box:
                shape.halfExtents *= scale;
                break;
            case ShapeType::Sphere:
                shape.radius *= std::max({ scale.x, scale.y, scale.z });
                break;
            case ShapeType::Capsule:
                shape.radius *= std::max(scale.x, scale.z);
                shape.halfHeight *= scale.y;
                break;
            case ShapeType::Plane:
                break;
            }
            shape.offset *= scale;

            BodyDesc desc{};
            desc.type = rb.type;
            desc.shape = shape;
            desc.material.friction = rb.friction;
            desc.material.restitution = rb.restitution;
            desc.position = glm::vec3(world[3]);
            desc.rotation = matrixRotation(world);
            desc.linearVelocity = rb.initialLinearVelocity;
            desc.mass = rb.mass;
            desc.linearDamping = rb.linearDamping;
            desc.angularDamping = rb.angularDamping;
            desc.isTrigger = rb.isTrigger && backend_->supportsTriggers();
            // let a query map a hit back to the ECS entity
            desc.userData = static_cast<uint64_t>(entt::to_integral(e));

            const BodyId id = backend_->createBody(desc);
            if (!id.valid()) {
                skipped_.push_back(e);
                continue;
            }
            entityToBody_[e] = id;
            bodyToEntity_[id.value] = e;
        }
        built_ = true;
    }

    void PhysicsWorld::Clear() {
        if (backend_) backend_->destroyAllBodies();
        entityToBody_.clear();
        bodyToEntity_.clear();
        skipped_.clear();
        built_ = false;
    }

    void PhysicsWorld::Step(entt::registry& reg, float fixedDt) {
        if (!backend_ || !built_ || fixedDt <= 0.f) return;

        // 1) Push KINEMATIC poses in: they are driven by gameplay/animation
        // and must move the simulation, not be overwritten by it.
        for (const auto& [e, id] : entityToBody_) {
            if (!reg.valid(e)) continue;
            const auto* rb = reg.try_get<RigidBody>(e);
            const auto* t = reg.try_get<Transform>(e);
            if (!rb || !t || rb->type != BodyType::Kinematic) continue;
            backend_->setBodyTransform(id, glm::vec3(t->modelMatrix[3]),
                                       matrixRotation(t->modelMatrix));
        }

        // 2) Simulate one fixed step.
        backend_->step(fixedDt);

        // 3) Read DYNAMIC poses back into Transform. Static bodies are
        // authored and Kinematic ones are driven, so neither is read back.
        BodyState st{};
        for (const auto& [e, id] : entityToBody_) {
            if (!reg.valid(e)) continue;
            const auto* rb = reg.try_get<RigidBody>(e);
            if (!rb || rb->type != BodyType::Dynamic) continue;
            auto* t = reg.try_get<Transform>(e);
            if (!t || !backend_->getBodyState(id, st)) continue;

            // Rebuild a world matrix from the simulated pose, preserving the
            // entity's authored scale.
            const glm::vec3 scale = matrixScale(t->modelMatrix);
            glm::mat4 world = glm::translate(glm::mat4(1.f), st.position) *
                              glm::mat4_cast(st.rotation) *
                              glm::scale(glm::mat4(1.f), scale);

            // Transform TRS is LOCAL when the entity is parented, so convert.
            glm::mat4 local = world;
            if (const auto* p = reg.try_get<Parent>(e);
                p && p->value != entt::null && reg.valid(p->value)) {
                local = glm::inverse(ResolveWorldMatrix(reg, p->value)) * world;
            }

            // DecomposeTRS (never ImGuizmo's) — it matches localMatrix's
            // Y*X*Z euler rebuild, so this round-trips losslessly.
            DecomposeTRS(local, t->position, t->rotation, t->scale);
            // UpdateTransforms only revisits dirty nodes; without this the
            // simulated pose never reaches modelMatrix or the renderer.
            t->dirty = true;
        }

        // 4) Fan out collision/trigger events. Deliberately LAST: listeners
        // see transforms that already reflect this step.
        dispatchContacts_(reg);
    }

    PhysicsWorld::ListenerHandle PhysicsWorld::OnCollision(CollisionCallback cb) {
        if (!cb) return 0;
        const ListenerHandle h = ++nextListener_;
        listeners_.push_back({ h, std::move(cb) });
        return h;
    }

    void PhysicsWorld::RemoveCollisionListener(ListenerHandle h) {
        listeners_.erase(
            std::remove_if(listeners_.begin(), listeners_.end(),
                [h](const Listener& l) { return l.handle == h; }),
            listeners_.end());
    }

    void PhysicsWorld::ClearCollisionListeners() { listeners_.clear(); }

    bool PhysicsWorld::BackendReportsContacts() const {
        return backend_ && backend_->supportsContactEvents();
    }

    void PhysicsWorld::dispatchContacts_(entt::registry& reg) {
        if (listeners_.empty() || !backend_) return;

        for (const ContactEvent& ce : backend_->contactEvents()) {
            CollisionEvent e;
            e.phase = ce.phase;
            e.isTrigger = ce.isTrigger;
            e.point = ce.point;
            e.normal = ce.normal;

            // userData carries the entity handle; validate it, because an
            // End event can name a body whose entity was destroyed during
            // the very step that produced the event.
            auto resolve = [&reg](uint64_t ud) -> entt::entity {
                const auto e = static_cast<entt::entity>(static_cast<uint32_t>(ud));
                return reg.valid(e) ? e : entt::null;
            };
            e.a = resolve(ce.userDataA);
            e.b = resolve(ce.userDataB);
            if (e.a == entt::null && e.b == entt::null) continue;

            // Copy the list: a listener is allowed to unsubscribe itself,
            // which would otherwise invalidate the iterator mid-dispatch.
            const auto snapshot = listeners_;
            for (const auto& l : snapshot) {
                if (l.cb) l.cb(e);
            }
        }
    }

    bool PhysicsWorld::Raycast(const glm::vec3& origin, const glm::vec3& direction,
                               float maxDistance, RayHit& out) const {
        if (!backend_) { out = RayHit{}; return false; }
        return backend_->raycast(origin, direction, maxDistance, out);
    }

    entt::entity PhysicsWorld::EntityFromHit(const RayHit& hit) const {
        if (!hit.hit) return entt::null;
        const auto it = bodyToEntity_.find(hit.body.value);
        return (it != bodyToEntity_.end()) ? it->second : entt::null;
    }

    void PhysicsWorld::SetGravity(const glm::vec3& g) {
        settings_.gravity = g;
        if (backend_) backend_->setGravity(g);
    }

    glm::vec3 PhysicsWorld::Gravity() const {
        return backend_ ? backend_->gravity() : settings_.gravity;
    }

    size_t PhysicsWorld::BodyCount() const {
        return backend_ ? backend_->bodyCount() : 0u;
    }

} // namespace MyCoreEngine
