#pragma once
// Owns the active backend and the entity <-> body mapping, and is the ONLY
// place the ECS meets physics.
//
// Lifecycle contract (learned from the editor's play/undo machinery):
// - Build()   creates native bodies from RigidBody + collider + Transform.
// - Clear()   destroys them and forgets the map.
// - Rebuild() is Clear+Build, and MUST run after any bulk registry restore
//   (play-stop, undo, redo, history jump, scene load, New Scene). Those paths
//   call reg.clear() and resurrect entities via create(hint), so every cached
//   entity->body pair is stale even though the handles look identical.

#include "../core/Core.h"
#include "IPhysicsBackend.h"
#include "PhysicsComponents.h"

#include <entt/entt.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace MyCoreEngine {

    class ENGINE_API PhysicsWorld {
    public:
        PhysicsWorld() = default;
        ~PhysicsWorld();

        PhysicsWorld(const PhysicsWorld&) = delete;
        PhysicsWorld& operator=(const PhysicsWorld&) = delete;

        // Selects and initializes a backend by registry name ("Jolt",
        // "PhysX", "Simple", ...). Destroys any existing world first. Returns
        // false and leaves the world EMPTY (not half-built) when the name is
        // unknown or the backend fails to initialize.
        bool SetBackend(const std::string& name, const PhysicsSettings& settings = {});
        const std::string& BackendName() const { return backendName_; }
        IPhysicsBackend* Backend() { return backend_.get(); }
        const IPhysicsBackend* Backend() const { return backend_.get(); }
        bool HasBackend() const { return backend_ != nullptr; }

        // ---- lifecycle ----
        void Build(entt::registry& reg);
        void Clear();
        void Rebuild(entt::registry& reg) { Clear(); Build(reg); }
        bool IsBuilt() const { return built_; }

        // Steps the simulation and writes resulting poses back into Transform
        // (marking them dirty so the hierarchy propagates). Safe to call with
        // no backend / before Build(): it simply does nothing.
        void Step(entt::registry& reg, float fixedDt);

        // ---- collision / trigger events ----
        // A backend ContactEvent with both sides resolved to ECS entities.
        struct CollisionEvent {
            ContactPhase phase = ContactPhase::Begin;
            entt::entity a = entt::null;
            entt::entity b = entt::null;
            bool isTrigger = false;
            glm::vec3 point{ 0.f };
            glm::vec3 normal{ 0.f };
        };
        using CollisionCallback = std::function<void(const CollisionEvent&)>;
        using ListenerHandle = uint32_t;

        // Listeners fire from Step(), AFTER the backend has finished
        // simulating — so they run single-threaded on the fixed tick even
        // though Jolt reports contacts from its job threads. That means a
        // listener may safely touch the registry.
        // Adding/removing a body from a listener is still unsafe (it would
        // mutate the map being iterated); defer such work to the next tick.
        ListenerHandle OnCollision(CollisionCallback cb);
        void RemoveCollisionListener(ListenerHandle h);
        void ClearCollisionListeners();
        bool BackendReportsContacts() const;

        // ---- queries / control ----
        bool Raycast(const glm::vec3& origin, const glm::vec3& direction,
                     float maxDistance, RayHit& out) const;
        // Maps a query result back to the entity that owns the body.
        entt::entity EntityFromHit(const RayHit& hit) const;

        void      SetGravity(const glm::vec3& g);
        glm::vec3 Gravity() const;

        size_t BodyCount() const;
        // Entities that had a RigidBody but no usable collider (diagnostics).
        const std::vector<entt::entity>& SkippedEntities() const { return skipped_; }

    private:
        // Fills `out` from whichever collider component the entity carries.
        // false when the entity has no collider at all.
        static bool shapeFromEntity_(entt::registry& reg, entt::entity e, ShapeDesc& out);
        // Resolves backend contact events to entities and fans them out.
        void dispatchContacts_(entt::registry& reg);

        std::unique_ptr<IPhysicsBackend> backend_;
        std::string      backendName_;
        PhysicsSettings  settings_{};
        bool             built_ = false;

        std::unordered_map<entt::entity, BodyId> entityToBody_;
        std::unordered_map<uint64_t, entt::entity> bodyToEntity_;
        std::vector<entt::entity> skipped_;

        struct Listener { ListenerHandle handle; CollisionCallback cb; };
        std::vector<Listener> listeners_;
        ListenerHandle nextListener_ = 0;
    };

} // namespace MyCoreEngine
