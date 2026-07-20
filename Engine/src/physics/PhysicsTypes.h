#pragma once
// Backend-agnostic physics vocabulary.
//
// NOTHING in this header (or IPhysicsBackend.h) may reference Jolt, PhysX, or
// any other library type. That is the whole point of the seam: the engine,
// the ECS components, the editor, and the serializer speak only these types,
// so a backend can be swapped at runtime and a new library can be added
// without touching a single call site.

#include "../core/Core.h"

#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace MyCoreEngine {

    // Opaque, backend-issued body handle. The value is meaningful ONLY to the
    // backend that produced it (Jolt body ids and PhysX actor pointers are
    // both stuffed in here), so it must never be persisted to disk or
    // compared across backends.
    struct BodyId {
        static constexpr uint64_t kInvalid = ~0ull;
        uint64_t value = kInvalid;

        bool valid() const { return value != kInvalid; }
        bool operator==(const BodyId& o) const { return value == o.value; }
        bool operator!=(const BodyId& o) const { return value != o.value; }
    };

    enum class BodyType {
        Static,    // never moves; cheapest (level geometry)
        Kinematic, // moved by code/animation, pushes dynamics, ignores forces
        Dynamic    // fully simulated
    };

    enum class ShapeType {
        Box,
        Sphere,
        Capsule,
        Plane // infinite ground plane; static only
    };

    struct ShapeDesc {
        ShapeType type = ShapeType::Box;
        glm::vec3 halfExtents{ 0.5f };   // Box
        float     radius = 0.5f;         // Sphere / Capsule
        float     halfHeight = 0.5f;     // Capsule (cylindrical half-height, excludes caps)
        glm::vec3 offset{ 0.f };         // shape origin relative to the body origin
    };

    struct MaterialDesc {
        float friction = 0.5f;
        float restitution = 0.0f; // bounciness
    };

    struct BodyDesc {
        BodyType     type = BodyType::Dynamic;
        ShapeDesc    shape{};
        MaterialDesc material{};
        glm::vec3    position{ 0.f };
        glm::quat    rotation{ 1.f, 0.f, 0.f, 0.f }; // w,x,y,z identity
        glm::vec3    linearVelocity{ 0.f };
        glm::vec3    angularVelocity{ 0.f };
        // <= 0 means "let the backend compute mass from the shape + density"
        float        mass = 1.0f;
        float        linearDamping = 0.05f;
        float        angularDamping = 0.05f;
        bool         isTrigger = false; // reports overlaps, no collision response
        // Opaque payload the caller can recover from queries; the engine
        // stores the entt entity handle here so a raycast maps back to an ECS
        // entity without the backend knowing what an entity is.
        uint64_t     userData = 0;
    };

    // Live body pose/velocity, read back after a step.
    struct BodyState {
        glm::vec3 position{ 0.f };
        glm::quat rotation{ 1.f, 0.f, 0.f, 0.f };
        glm::vec3 linearVelocity{ 0.f };
        glm::vec3 angularVelocity{ 0.f };
    };

    struct RayHit {
        bool      hit = false;
        BodyId    body{};
        glm::vec3 point{ 0.f };
        glm::vec3 normal{ 0.f };
        float     distance = 0.f;
        uint64_t  userData = 0; // mirrors BodyDesc::userData of the body hit
    };

    // World-creation parameters. Kept deliberately small and generic: anything
    // that only one library understands belongs in that backend, not here.
    struct PhysicsSettings {
        glm::vec3 gravity{ 0.f, -9.81f, 0.f };
        // Sizing hints; backends that pre-allocate (both Jolt and PhysX do)
        // use them, others ignore them.
        uint32_t  maxBodies = 8192;
        uint32_t  maxBodyPairs = 8192;
        uint32_t  maxContactConstraints = 4096;
        // Worker threads the backend may use internally. 0 = single-threaded
        // (the deterministic default; the engine's own JobSystem owns the
        // machine's cores and physics runs on the fixed tick).
        uint32_t  workerThreads = 0;
    };

} // namespace MyCoreEngine
