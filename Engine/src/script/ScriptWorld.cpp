#include "ScriptWorld.h"

#include "../core/Components.h"
#include "../core/InputMap.h"
#include "../physics/PhysicsWorld.h"
#include "ScriptBackendRegistry.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace MyCoreEngine {

    namespace {
        inline ScriptEntity toScript(entt::entity e) {
            // Normalize the null handle to the script-side sentinel. Without
            // this, entt::null arrives in Lua as a large integer that looks
            // like a real entity, and `if hit.entity then` is true for a miss.
            if (e == entt::null) return kInvalidScriptEntity;
            return static_cast<ScriptEntity>(entt::to_integral(e));
        }
        // Round-trips the FULL handle including entt's version bits, so a
        // script holding an id across frames sees it correctly go invalid
        // when the entity is destroyed and the index is recycled.
        inline entt::entity fromScript(ScriptEntity s) {
            if (s == kInvalidScriptEntity) return entt::null;
            return static_cast<entt::entity>(static_cast<entt::id_type>(s));
        }
    } // namespace

    // ---------------------------------------------------------------------
    // EngineHost: the capability surface handed to every backend.
    // ---------------------------------------------------------------------
    class ScriptWorld::EngineHost final : public IScriptHost {
    public:
        entt::registry* reg = nullptr;
        PhysicsWorld*   physics = nullptr;
        const InputMap* input = nullptr;
        std::vector<std::string>* messages = nullptr;
        float time = 0.f;

        void log(ScriptEntity self, const char* level, const char* message) override {
            const char* who = getName(self);
            std::string line = std::string("[script:") + (level ? level : "info") + "] "
                             + (who ? who : "<entity>") + ": " + (message ? message : "");
            if (messages) messages->push_back(line);
            std::printf("%s\n", line.c_str());
        }

        // Every accessor tolerates a stale handle: scripts keep entity ids
        // across frames and the referenced object may have been destroyed.
        Transform* xf(ScriptEntity e) const {
            if (!reg) return nullptr;
            const entt::entity h = fromScript(e);
            if (h == entt::null || !reg->valid(h)) return nullptr;
            return reg->try_get<Transform>(h);
        }

        bool isValid(ScriptEntity e) const override {
            const entt::entity h = fromScript(e);
            return reg && h != entt::null && reg->valid(h);
        }

        const char* getName(ScriptEntity e) const override {
            if (!reg) return nullptr;
            const entt::entity h = fromScript(e);
            if (h == entt::null || !reg->valid(h)) return nullptr;
            const Name* n = reg->try_get<Name>(h);
            return n ? n->value.c_str() : nullptr;
        }

        ScriptEntity findByName(const char* name) const override {
            if (!reg || !name) return kInvalidScriptEntity;
            for (auto [e, n] : reg->view<Name>().each()) {
                if (n.value == name) return toScript(e);
            }
            return kInvalidScriptEntity;
        }

        bool getPosition(ScriptEntity e, glm::vec3& out) const override {
            if (Transform* t = xf(e)) { out = t->position; return true; }
            return false;
        }
        bool setPosition(ScriptEntity e, const glm::vec3& v) override {
            if (Transform* t = xf(e)) { t->position = v; t->dirty = true; return true; }
            return false;
        }
        bool getEulerDegrees(ScriptEntity e, glm::vec3& out) const override {
            if (Transform* t = xf(e)) { out = t->rotation; return true; }
            return false;
        }
        bool setEulerDegrees(ScriptEntity e, const glm::vec3& v) override {
            if (Transform* t = xf(e)) { t->rotation = v; t->dirty = true; return true; }
            return false;
        }
        bool getScale(ScriptEntity e, glm::vec3& out) const override {
            if (Transform* t = xf(e)) { out = t->scale; return true; }
            return false;
        }
        bool setScale(ScriptEntity e, const glm::vec3& v) override {
            if (Transform* t = xf(e)) { t->scale = v; t->dirty = true; return true; }
            return false;
        }

        bool applyImpulse(ScriptEntity e, const glm::vec3& impulse) override {
            if (!physics || !reg) return false;
            const entt::entity h = fromScript(e);
            if (h == entt::null || !reg->valid(h)) return false;
            return physics->ApplyImpulse(h, impulse);
        }
        bool setLinearVelocity(ScriptEntity e, const glm::vec3& v) override {
            if (!physics || !reg) return false;
            const entt::entity h = fromScript(e);
            if (h == entt::null || !reg->valid(h)) return false;
            return physics->SetLinearVelocity(h, v);
        }
        bool raycast(const glm::vec3& origin, const glm::vec3& direction,
                     float maxDistance, ScriptRayHit& out) const override {
            if (!physics) return false;
            RayHit hit{};
            if (!physics->Raycast(origin, direction, maxDistance, hit)) return false;
            out.entity   = toScript(physics->EntityFromHit(hit));
            out.point    = hit.point;
            out.normal   = hit.normal;
            out.distance = hit.distance;
            return true;
        }

        bool isActionDown(const char* action) const override {
            return input && action && input->isDown(action);
        }
        bool wasActionPressed(const char* action) const override {
            return input && action && input->wasPressed(action);
        }
        float actionAxis(const char* axis) const override {
            return (input && axis) ? input->axis(axis) : 0.f;
        }

        float timeSeconds() const override { return time; }
    };

    // ---------------------------------------------------------------------

    ScriptWorld::ScriptWorld() : host_(new EngineHost()) {
        host_->messages = &messages_;
    }

    ScriptWorld::~ScriptWorld() {
        Clear();
        if (backend_) backend_->shutdown();
    }

    bool ScriptWorld::SetBackend(const std::string& name, const ScriptSettings& settings) {
        Clear();
        if (backend_) {
            backend_->shutdown();
            backend_.reset();
        }
        backendName_.clear();

        auto b = ScriptBackendRegistry::Create(name);
        if (!b) {
            messages_.push_back("[script] unknown backend '" + name + "'");
            return false;
        }
        if (!b->initialize(settings, host_.get())) {
            messages_.push_back("[script] backend '" + name + "' failed to initialize");
            return false; // b destroyed here; world stays empty, not half-built
        }
        backend_  = std::move(b);
        backendName_ = name;
        settings_ = settings;
        return true;
    }

    void ScriptWorld::SetPhysics(PhysicsWorld* p) { host_->physics = p; }
    void ScriptWorld::SetInput(const InputMap* i) { host_->input = i; }
    void ScriptWorld::SetSourceResolver(SourceResolver r) { resolver_ = std::move(r); }

    bool ScriptWorld::defaultResolve_(const std::string& path, std::string& out) const {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::path full = settings_.scriptDirectory.empty()
                            ? fs::path(path)
                            : fs::path(settings_.scriptDirectory) / path;
        if (!fs::exists(full, ec)) return false;
        std::ifstream in(full, std::ios::binary);
        if (!in) return false;
        std::ostringstream ss;
        ss << in.rdbuf();
        out = ss.str();
        return true;
    }

    void ScriptWorld::Build(entt::registry& reg) {
        Clear();
        host_->reg = &reg;
        messages_.clear();
        if (!backend_) return;

        for (auto [e, sc] : reg.view<ScriptComponent>().each()) {
            Instance inst;
            inst.path = sc.path;

            if (sc.path.empty()) {
                inst.failed = true;
                inst.error  = "no script assigned";
                instances_[e] = std::move(inst);
                order_.push_back(e);
                continue;
            }

            std::string source;
            const bool got = resolver_ ? resolver_(sc.path, source)
                                       : defaultResolve_(sc.path, source);
            if (!got) {
                inst.failed = true;
                inst.error  = "could not read script '" + sc.path + "'";
                messages_.push_back("[script:error] " + inst.error);
                instances_[e] = std::move(inst);
                order_.push_back(e);
                continue;
            }

            ScriptError err{};
            const ScriptId id = backend_->loadScript(sc.path, source, err);
            if (!IsValid(id)) {
                inst.failed = true;
                inst.error  = err.message.empty() ? "compile failed" : err.message;
                messages_.push_back("[script:error] " + sc.path + ": " + inst.error);
                instances_[e] = std::move(inst);
                order_.push_back(e);
                continue;
            }

            inst.id = id;
            // Resolve hook presence ONCE, not per frame per entity.
            inst.has[int(ScriptCallback::Start)]       = backend_->hasCallback(id, ScriptCallback::Start);
            inst.has[int(ScriptCallback::Update)]      = backend_->hasCallback(id, ScriptCallback::Update);
            inst.has[int(ScriptCallback::FixedUpdate)] = backend_->hasCallback(id, ScriptCallback::FixedUpdate);
            inst.has[int(ScriptCallback::Destroy)]     = backend_->hasCallback(id, ScriptCallback::Destroy);
            inst.has[int(ScriptCallback::Collision)]   = backend_->hasCallback(id, ScriptCallback::Collision);
            instances_[e] = std::move(inst);
            order_.push_back(e);
        }
        built_ = true;
    }

    void ScriptWorld::Clear() {
        if (backend_) {
            // Fire OnDestroy for anything that actually started, so scripts
            // can release what they acquired. A throwing finalizer is
            // swallowed: we are already tearing down.
            for (auto& [e, inst] : instances_) {
                if (inst.started && !inst.failed && IsValid(inst.id)
                    && inst.has[int(ScriptCallback::Destroy)]) {
                    ScriptError err{};
                    backend_->callDestroy(inst.id, toScript(e), err);
                }
            }
            backend_->destroyAllScripts();
        }
        instances_.clear();
        order_.clear();
        built_ = false;
        time_ = 0.f;
        if (host_) host_->time = 0.f;
    }

    void ScriptWorld::fail_(entt::entity e, Instance& inst, const ScriptError& err,
                            const char* during) {
        inst.failed = true;
        inst.error  = err.message.empty() ? "script error" : err.message;
        // Reported ONCE. The instance is never called again, so a broken
        // script cannot spam the log every frame.
        std::string line = std::string("[script:error] ") + inst.path + " in "
                         + during + ": " + inst.error;
        messages_.push_back(line);
        std::printf("%s\n", line.c_str());
        (void)e;
    }

    void ScriptWorld::Start(entt::registry& reg) {
        if (!backend_) return;
        host_->reg = &reg;
        for (entt::entity e : order_) {
            auto it = instances_.find(e);
            if (it == instances_.end()) continue;
            Instance& inst = it->second;
            if (inst.failed || inst.started || !IsValid(inst.id)) continue;
            // enabled==false still loads (so errors surface) but never runs.
            const ScriptComponent* sc = reg.try_get<ScriptComponent>(e);
            if (!sc || !sc->enabled) continue;

            inst.started = true;
            if (!inst.has[int(ScriptCallback::Start)]) continue;
            ScriptError err{};
            if (!backend_->callStart(inst.id, toScript(e), err)) {
                fail_(e, inst, err, "OnStart");
            }
        }
    }

    void ScriptWorld::Update(entt::registry& reg, float dt) {
        if (!backend_) return;
        host_->reg = &reg;
        time_ += dt;
        host_->time = time_;

        for (entt::entity e : order_) {
            auto it = instances_.find(e);
            if (it == instances_.end()) continue;
            Instance& inst = it->second;
            if (inst.failed || !IsValid(inst.id)) continue;
            if (!inst.has[int(ScriptCallback::Update)]) continue;
            const ScriptComponent* sc = reg.try_get<ScriptComponent>(e);
            if (!sc || !sc->enabled) continue;
            // A script whose entity died this frame is skipped rather than
            // handed a dangling self.
            if (!reg.valid(e)) continue;

            ScriptError err{};
            if (!backend_->callUpdate(inst.id, toScript(e), dt, err)) {
                fail_(e, inst, err, "OnUpdate");
            }
        }
    }

    void ScriptWorld::FixedUpdate(entt::registry& reg, float fixedDt) {
        if (!backend_) return;
        host_->reg = &reg;
        for (entt::entity e : order_) {
            auto it = instances_.find(e);
            if (it == instances_.end()) continue;
            Instance& inst = it->second;
            if (inst.failed || !IsValid(inst.id)) continue;
            if (!inst.has[int(ScriptCallback::FixedUpdate)]) continue;
            const ScriptComponent* sc = reg.try_get<ScriptComponent>(e);
            if (!sc || !sc->enabled) continue;
            if (!reg.valid(e)) continue;

            ScriptError err{};
            if (!backend_->callFixedUpdate(inst.id, toScript(e), fixedDt, err)) {
                fail_(e, inst, err, "OnFixedUpdate");
            }
        }
    }

    void ScriptWorld::DispatchCollision(entt::entity a, entt::entity b, const char* phase,
                                        bool isTrigger, const glm::vec3& point,
                                        const glm::vec3& normal, float impulse) {
        if (!backend_) return;
        // Both sides get the hook, each seeing the OTHER as `other`.
        const entt::entity pair[2][2] = { { a, b }, { b, a } };
        for (const auto& side : pair) {
            auto it = instances_.find(side[0]);
            if (it == instances_.end()) continue;
            Instance& inst = it->second;
            if (inst.failed || !IsValid(inst.id)) continue;
            if (!inst.has[int(ScriptCallback::Collision)]) continue;

            ScriptCollision hit{};
            hit.other     = toScript(side[1]);
            hit.phase     = phase ? phase : "begin";
            hit.isTrigger = isTrigger;
            hit.point     = point;
            hit.normal    = normal;
            hit.impulse   = impulse;

            ScriptError err{};
            if (!backend_->callCollision(inst.id, toScript(side[0]), hit, err)) {
                fail_(side[0], inst, err, "OnCollision");
            }
        }
    }

    std::vector<ScriptWorld::ScriptStatus> ScriptWorld::Statuses() const {
        std::vector<ScriptStatus> out;
        out.reserve(order_.size());
        for (entt::entity e : order_) {
            const auto it = instances_.find(e);
            if (it == instances_.end()) continue;
            ScriptStatus s;
            s.entity = e;
            s.path   = it->second.path;
            s.loaded = IsValid(it->second.id);
            s.failed = it->second.failed;
            s.error  = it->second.error;
            out.push_back(std::move(s));
        }
        return out;
    }

    size_t ScriptWorld::FailedCount() const {
        size_t n = 0;
        for (const auto& [e, inst] : instances_) if (inst.failed) ++n;
        return n;
    }

    size_t ScriptWorld::InstanceCount() const { return instances_.size(); }

} // namespace MyCoreEngine
