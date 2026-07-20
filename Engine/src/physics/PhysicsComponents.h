#pragma once
// ECS-facing physics components. Deliberately PURE DATA and completely
// backend-agnostic — no BodyId, no native handle, nothing runtime.
//
// Why no runtime handle here: the editor snapshots components wholesale for
// undo/redo and play-stop (UndoHistory::EntitySnapshot), and a restore
// resurrects entities via reg.clear() + create(hint). A native body id stored
// in a component would survive that restore as a DANGLING value pointing at a
// body the backend already destroyed. The entity -> body mapping therefore
// lives only in PhysicsWorld, which is rebuilt after every bulk restore.

#include "PhysicsTypes.h"

#include <glm/glm.hpp>

namespace MyCoreEngine {

    // Marks an entity as simulated. Needs a Transform, and a collider
    // component to give it a shape (an entity with RigidBody but no collider
    // is skipped with a warning rather than silently simulating a point).
    struct RigidBody {
        BodyType type = BodyType::Dynamic;
        float mass = 1.0f;             // <= 0 => backend computes from shape
        float friction = 0.5f;
        float restitution = 0.0f;
        float linearDamping = 0.05f;
        float angularDamping = 0.05f;
        bool  isTrigger = false;
        glm::vec3 initialLinearVelocity{ 0.f };
    };

    // Collider shapes. An entity uses the FIRST one found in this order:
    // Box, Sphere, Capsule, Plane — multiple colliders on one entity are not
    // compounded yet (a compound-shape component is the natural extension).
    struct BoxCollider {
        glm::vec3 halfExtents{ 0.5f };
        glm::vec3 offset{ 0.f };
    };

    struct SphereCollider {
        float radius = 0.5f;
        glm::vec3 offset{ 0.f };
    };

    struct CapsuleCollider {
        float radius = 0.5f;
        float halfHeight = 0.5f; // cylindrical part only, excludes the caps
        glm::vec3 offset{ 0.f };
    };

    // Infinite horizontal plane; only meaningful on a Static body (ground).
    struct PlaneCollider {
        glm::vec3 offset{ 0.f };
    };

} // namespace MyCoreEngine
