#pragma once
// Owns the active script backend and the entity <-> instance mapping, and is
// the ONLY place the ECS meets scripting. Mirrors PhysicsWorld deliberately —
// same lifecycle contract, same rebuild-after-restore hazard.
//
// Lifecycle contract:
// - Build()   loads one instance per entity carrying a ScriptComponent.
// - Clear()   destroys them (firing OnDestroy) and forgets the map.
// - Rebuild() is Clear+Build, and MUST run after any bulk registry restore
//   (play-stop, undo, redo, history jump, scene load, New Scene). Those paths
//   call reg.clear() and resurrect entities via create(hint), so every cached
//   entity->instance pair is stale even though the handles look identical.
//
// ERROR POLICY, which is the whole reason this class exists rather than
// callers poking the backend directly: a script is USER CONTENT and will be
// broken regularly. A failing script is reported once, marked failed, and
// never called again. It must never crash the editor, and must never spam the
// log 60 times a second with the same error.

#include "../core/Core.h"
#include "IScriptBackend.h"
#include "ScriptComponent.h"

#include <entt/entt.hpp>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace MyCoreEngine {

    class PhysicsWorld;
    class InputMap;

    class ENGINE_API ScriptWorld {
    public:
        ScriptWorld();
        ~ScriptWorld();

        ScriptWorld(const ScriptWorld&) = delete;
        ScriptWorld& operator=(const ScriptWorld&) = delete;

        // Selects and initializes a backend by registry name ("Lua", "Null").
        // Destroys any existing instances first. Returns false and leaves the
        // world EMPTY (not half-built) when the name is unknown or the backend
        // fails to initialize.
        bool SetBackend(const std::string& name, const ScriptSettings& settings = {});
        const std::string& BackendName() const { return backendName_; }
        IScriptBackend* Backend() { return backend_.get(); }
        bool HasBackend() const { return backend_ != nullptr; }

        // Optional capabilities handed to scripts. Absent ones degrade
        // gracefully: raycast/impulse return false, input reads as released.
        void SetPhysics(PhysicsWorld* physics);
        // Non-const: reading a press CONSUMES its latch (see
        // InputMap::consumePressed) so a fixed-tick script sees it once.
        void SetInput(InputMap* input);

        // How a ScriptComponent::path becomes source text. Defaults to reading
        // <scriptDirectory>/<path> from disk. Injectable so headless tests can
        // supply sources without touching the filesystem.
        using SourceResolver = std::function<bool(const std::string& path, std::string& outSource)>;
        void SetSourceResolver(SourceResolver r);

        // ---- lifecycle ----
        void Build(entt::registry& reg);
        void Clear();
        void Rebuild(entt::registry& reg) { Clear(); Build(reg); }
        bool IsBuilt() const { return built_; }

        // Runs OnStart for every instance that has not started yet. Separate
        // from Build so the editor can load scripts (surfacing syntax errors
        // immediately in the Inspector) WITHOUT running them until Play.
        void Start(entt::registry& reg);

        // Per-frame and per-fixed-tick dispatch. Safe to call with no backend
        // or before Build(): they simply do nothing.
        void Update(entt::registry& reg, float dt);
        void FixedUpdate(entt::registry& reg, float fixedDt);

        // Forwards a physics contact to both entities' OnCollision hooks.
        // Wire this to PhysicsWorld::OnCollision.
        void DispatchCollision(entt::entity a, entt::entity b, const char* phase,
                               bool isTrigger, const glm::vec3& point,
                               const glm::vec3& normal, float impulse);

        // ---- diagnostics (Inspector / console) ----
        struct ScriptStatus {
            entt::entity entity = entt::null;
            std::string  path;
            bool         loaded = false;  // compiled successfully
            bool         failed = false;  // errored; no longer called
            std::string  error;           // last message, empty when healthy
        };
        // One entry per entity with a ScriptComponent, in Build() order.
        std::vector<ScriptStatus> Statuses() const;
        // Entities whose script failed to load or threw at runtime.
        size_t FailedCount() const;
        size_t InstanceCount() const;

        // Cleared by Build/Rebuild. The editor drains this into its console.
        const std::vector<std::string>& DrainMessages() const { return messages_; }
        void ClearMessages() { messages_.clear(); }

    private:
        struct Instance {
            ScriptId    id = ScriptId::Invalid;
            std::string path;
            bool        started = false;
            bool        failed = false;
            std::string error;
            // Cached at load: a script with no OnUpdate costs nothing per
            // frame instead of a failed lookup 60x/second, per entity.
            bool has[5] = { false, false, false, false, false };
        };

        // Marks the instance failed, records the message once, and logs it.
        void fail_(entt::entity e, Instance& inst, const ScriptError& err, const char* during);
        bool defaultResolve_(const std::string& path, std::string& out) const;

        // DECLARATION ORDER IS LOAD-BEARING. Members destruct in reverse
        // declaration order, so the host must be declared BEFORE the backend
        // to be destroyed AFTER it: ~backend calls shutdown(), which may run
        // script finalizers that call back into the host. Swap these two and
        // teardown reads freed memory only when a script defines a finalizer,
        // which is exactly the kind of bug that hides until a demo.
        class EngineHost;
        std::unique_ptr<EngineHost> host_;

        std::unique_ptr<IScriptBackend> backend_;
        std::string    backendName_;
        ScriptSettings settings_{};
        bool           built_ = false;

        std::unordered_map<entt::entity, Instance> instances_;
        std::vector<entt::entity>  order_;   // stable Build() order for the UI
        std::vector<std::string>   messages_;
        SourceResolver             resolver_;
        float                      time_ = 0.f;
    };

} // namespace MyCoreEngine
