#include "LuaScriptBackend.h"

// The ONLY translation unit in the engine that sees sol2 or Lua.
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <cstdlib>
#include <unordered_map>

namespace MyCoreEngine {

    namespace {

        // Entity handle as scripts see it. Carries the host so methods need
        // no upvalues and a stale handle degrades to a no-op rather than a
        // crash (every host accessor revalidates).
        struct LuaEntity {
            ScriptEntity id = kInvalidScriptEntity;
            IScriptHost* host = nullptr;

            bool valid() const { return host && host->isValid(id); }
            std::string name() const {
                const char* n = host ? host->getName(id) : nullptr;
                return n ? n : "";
            }
            glm::vec3 position() const {
                glm::vec3 v{ 0.f };
                if (host) host->getPosition(id, v);
                return v;
            }
            glm::vec3 rotation() const {
                glm::vec3 v{ 0.f };
                if (host) host->getEulerDegrees(id, v);
                return v;
            }
            glm::vec3 scale() const {
                glm::vec3 v{ 1.f };
                if (host) host->getScale(id, v);
                return v;
            }
            void setPosition(const glm::vec3& v) { if (host) host->setPosition(id, v); }
            void setRotation(const glm::vec3& v) { if (host) host->setEulerDegrees(id, v); }
            void setScale(const glm::vec3& v)    { if (host) host->setScale(id, v); }
            void translate(const glm::vec3& d)   { setPosition(position() + d); }
            void rotate(const glm::vec3& d)      { setRotation(rotation() + d); }
            void applyImpulse(const glm::vec3& v){ if (host) host->applyImpulse(id, v); }
            void setVelocity(const glm::vec3& v) { if (host) host->setLinearVelocity(id, v); }
        };

        // The instruction-limit hook. Fires every N VM instructions and
        // aborts the running chunk. This is what stops `while true do end` in
        // a user's script from hanging the editor with no recovery.
        //
        // It does NOT self-disarm. An earlier version cleared the hook before
        // raising, which pcall then permanently defeated: the first overflow
        // fired, disarmed, and raised; a surrounding pcall swallowed the
        // error; and the rest of the callback ran with no limit at all. Left
        // armed, the hook re-fires every N instructions, so a plain runaway
        // loop still aborts out of the callback. (A script that both wraps its
        // loop in pcall AND loops around that can still peg one core until the
        // frame's watchdog would kill it; a true fix for that pathological
        // case needs an out-of-band watchdog thread and is tracked separately.)
        void InstructionLimitHook(lua_State* L, lua_Debug*) {
            luaL_error(L, "script exceeded the instruction limit "
                          "(infinite loop?) - script disabled");
        }

        // Capping allocator. Wraps the default realloc and refuses to grow
        // past a byte ceiling, so an oversized allocation becomes a Lua error
        // instead of a process OOM. ud points at a LuaMemory.
        struct LuaMemory {
            size_t used = 0;
            size_t limit = 0; // 0 = unlimited
        };
        void* LimitedAlloc(void* ud, void* ptr, size_t osize, size_t nsize) {
            auto* mem = static_cast<LuaMemory*>(ud);
            if (nsize == 0) {
                if (ptr) { std::free(ptr); mem->used -= osize; }
                return nullptr;
            }
            // osize is meaningful only when ptr != null.
            const size_t prev = ptr ? osize : 0;
            if (mem->limit && nsize > prev && mem->used + (nsize - prev) > mem->limit) {
                return nullptr; // Lua raises "not enough memory"
            }
            void* np = std::realloc(ptr, nsize);
            if (!np) return nullptr;
            mem->used += nsize - prev;
            return np;
        }

    } // namespace

    struct LuaScriptBackend::Impl {
        sol::state   lua;
        IScriptHost* host = nullptr;
        ScriptSettings settings{};
        uint64_t     nextId = 1;
        LuaMemory    mem{};

        struct Instance {
            sol::environment env;
            std::string debugName;
            // Callback handles resolved ONCE at load. Re-looking these up per
            // frame per entity is a hash lookup into the env table 60x/second
            // for every scripted object.
            sol::protected_function fn[5];
        };
        std::unordered_map<uint64_t, Instance> instances;

        Instance* find(ScriptId id) {
            auto it = instances.find(static_cast<uint64_t>(id));
            return (it != instances.end()) ? &it->second : nullptr;
        }
        const Instance* find(ScriptId id) const {
            auto it = instances.find(static_cast<uint64_t>(id));
            return (it != instances.end()) ? &it->second : nullptr;
        }

        void armHook() {
            if (settings.instructionLimit > 0) {
                lua_sethook(lua.lua_state(), InstructionLimitHook, LUA_MASKCOUNT,
                            static_cast<int>(settings.instructionLimit));
            }
        }
        void disarmHook() {
            lua_sethook(lua.lua_state(), nullptr, 0, 0);
        }

        // Every invocation funnels through here so the hook is always armed
        // and always cleared, and so error extraction is written once.
        template <typename... Args>
        bool invoke(ScriptId id, ScriptCallback cb, ScriptEntity self,
                    ScriptError& err, Args&&... args) {
            err = {};
            Instance* inst = find(id);
            if (!inst) { err.message = "invalid script id"; return false; }

            sol::protected_function& f = inst->fn[static_cast<int>(cb)];
            if (!f.valid()) return true; // hook not defined: not an error

            // Rebound every call rather than at load: entity ids are recycled
            // by entt, so a cached `self` could outlive its entity.
            inst->env["self"] = LuaEntity{ self, host };

            armHook();
            sol::protected_function_result r = f(std::forward<Args>(args)...);
            disarmHook();

            if (!r.valid()) {
                sol::error e = r;
                err.message = std::string(inst->debugName) + ": " + e.what();
                return false;
            }
            return true;
        }

        void bindApi();
    };

    void LuaScriptBackend::Impl::bindApi() {
        // ---- vec3 ----
        // Bound through property lambdas rather than &glm::vec3::x: glm
        // stores components in an anonymous union, and taking member pointers
        // into it is not portable across glm's SIMD configurations.
        lua.new_usertype<glm::vec3>(
            "vec3",
            sol::constructors<glm::vec3(), glm::vec3(float, float, float)>(),
            "x", sol::property([](const glm::vec3& v) { return v.x; },
                               [](glm::vec3& v, float f) { v.x = f; }),
            "y", sol::property([](const glm::vec3& v) { return v.y; },
                               [](glm::vec3& v, float f) { v.y = f; }),
            "z", sol::property([](const glm::vec3& v) { return v.z; },
                               [](glm::vec3& v, float f) { v.z = f; }),
            sol::meta_function::addition,
            [](const glm::vec3& a, const glm::vec3& b) { return a + b; },
            sol::meta_function::subtraction,
            [](const glm::vec3& a, const glm::vec3& b) { return a - b; },
            sol::meta_function::multiplication,
            [](const glm::vec3& a, float s) { return a * s; },
            sol::meta_function::to_string,
            [](const glm::vec3& v) {
                return "vec3(" + std::to_string(v.x) + ", " + std::to_string(v.y)
                     + ", " + std::to_string(v.z) + ")";
            },
            "length", [](const glm::vec3& v) { return glm::length(v); },
            "normalized", [](const glm::vec3& v) {
                const float len = glm::length(v);
                return (len > 0.f) ? v / len : glm::vec3(0.f);
            });

        // ---- entity ----
        lua.new_usertype<LuaEntity>(
            "Entity", sol::no_constructor,
            "valid", &LuaEntity::valid,
            "name", &LuaEntity::name,
            "position", &LuaEntity::position,
            "rotation", &LuaEntity::rotation,
            "scale", &LuaEntity::scale,
            "setPosition", &LuaEntity::setPosition,
            "setRotation", &LuaEntity::setRotation,
            "setScale", &LuaEntity::setScale,
            "translate", &LuaEntity::translate,
            "rotate", &LuaEntity::rotate,
            "applyImpulse", &LuaEntity::applyImpulse,
            "setVelocity", &LuaEntity::setVelocity);

        IScriptHost* h = host;

        // ---- globals available to every script ----
        lua.set_function("log", [h](sol::this_state s, const std::string& m) {
            sol::state_view v(s);
            LuaEntity self = v["self"].get_or(LuaEntity{});
            if (h) h->log(self.id, "info", m.c_str());
        });
        lua.set_function("logWarn", [h](sol::this_state s, const std::string& m) {
            sol::state_view v(s);
            LuaEntity self = v["self"].get_or(LuaEntity{});
            if (h) h->log(self.id, "warn", m.c_str());
        });
        lua.set_function("logError", [h](sol::this_state s, const std::string& m) {
            sol::state_view v(s);
            LuaEntity self = v["self"].get_or(LuaEntity{});
            if (h) h->log(self.id, "error", m.c_str());
        });

        // Lua's own print goes to stdout, which a shipped game has no console
        // for. Route it to the engine log so print() works the way an author
        // expects and shows up in the editor console.
        lua.set_function("print", [h](sol::this_state s, sol::variadic_args va) {
            std::string out;
            for (auto v : va) {
                if (!out.empty()) out += "\t";
                out += sol::state_view(v.lua_state())["tostring"](v).get<std::string>();
            }
            sol::state_view v(s);
            LuaEntity self = v["self"].get_or(LuaEntity{});
            if (h) h->log(self.id, "info", out.c_str());
        });

        lua.set_function("find", [h](const std::string& n) {
            return LuaEntity{ h ? h->findByName(n.c_str()) : kInvalidScriptEntity, h };
        });

        lua.set_function("time", [h]() { return h ? h->timeSeconds() : 0.f; });

        // Returns a table {entity=, point=, normal=, distance=} or nil, so a
        // miss is falsy in Lua rather than a sentinel the author must know.
        lua.set_function("raycast", [h](sol::this_state s, const glm::vec3& o,
                                        const glm::vec3& d, float maxDist) {
            sol::state_view v(s);
            ScriptRayHit hit{};
            if (!h || !h->raycast(o, d, maxDist, hit)) return sol::object(sol::nil);
            sol::table t = v.create_table();
            t["entity"]   = LuaEntity{ hit.entity, h };
            t["point"]    = hit.point;
            t["normal"]   = hit.normal;
            t["distance"] = hit.distance;
            return sol::object(t);
        });

        sol::table input = lua.create_named_table("input");
        input.set_function("down", [h](const std::string& a) {
            return h && h->isActionDown(a.c_str());
        });
        input.set_function("pressed", [h](const std::string& a) {
            return h && h->wasActionPressed(a.c_str());
        });
        input.set_function("axis", [h](const std::string& a) {
            return h ? h->actionAxis(a.c_str()) : 0.f;
        });
    }

    // -----------------------------------------------------------------

    LuaScriptBackend::LuaScriptBackend() : impl_(new Impl()) {}
    LuaScriptBackend::~LuaScriptBackend() { shutdown(); }

    bool LuaScriptBackend::initialize(const ScriptSettings& settings, IScriptHost* host) {
        if (!host) return false;
        impl_->host = host;
        impl_->settings = settings;

        // Swap in the capping allocator BEFORE anything substantial is loaded,
        // so the ceiling covers library and script allocations too.
        impl_->mem.limit = settings.memoryLimitBytes;
        lua_setallocf(impl_->lua.lua_state(), LimitedAlloc, &impl_->mem);

        // Deliberately WITHOUT io/os/package/debug unless asked: a shipped
        // game may run scripts it did not author, and those libraries hand
        // over the filesystem and process control.
        //
        // coroutine is ALSO gated. The instruction-limit hook is per-thread
        // and a new coroutine starts with no hook, so a loop inside one runs
        // unbounded. Safely hooking every coroutine thread means wrapping
        // create/wrap; until that exists, coroutines belong to trusted content
        // only. (We already advertise no coroutine scheduler, so nothing that
        // shipped depended on them by default.)
        impl_->lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math,
                                  sol::lib::table);
        if (settings.allowUnsafeLibraries) {
            impl_->lua.open_libraries(sol::lib::io, sol::lib::os, sol::lib::package,
                                      sol::lib::debug, sol::lib::coroutine);
            if (!settings.scriptDirectory.empty()) {
                const std::string dir = settings.scriptDirectory;
                impl_->lua["package"]["path"] =
                    dir + "/?.lua;" + dir + "/?/init.lua;"
                    + impl_->lua["package"]["path"].get<std::string>();
            }
        } else {
            // Remove the base-library CHUNK LOADERS from the sandbox. `load`
            // accepts unverified BINARY bytecode (a memory-corruption
            // primitive in Lua 5.4), and loadfile/dofile additionally read and
            // execute arbitrary files off disk regardless of io being closed.
            // Withholding io/os is meaningless while these remain reachable.
            const char* kLoaders[] = { "load", "loadstring", "loadfile", "dofile" };
            for (const char* n : kLoaders) impl_->lua[n] = sol::nil;
        }

        impl_->bindApi();
        return true;
    }

    void LuaScriptBackend::shutdown() {
        if (!impl_) return;
        destroyAllScripts();
        impl_->host = nullptr;
    }

    ScriptId LuaScriptBackend::loadScript(const std::string& debugName,
                                          const std::string& source,
                                          ScriptError& err) {
        err = {};

        // Each instance gets its OWN environment, falling back to globals for
        // the shared API. Writes land in the environment, so two entities
        // running the same file cannot stomp each other's variables — the
        // single most surprising bug in a naive Lua integration.
        sol::environment env(impl_->lua, sol::create, impl_->lua.globals());

        impl_->armHook();
        sol::protected_function_result r = impl_->lua.safe_script(
            source, env, sol::script_pass_on_error, "@" + debugName);
        impl_->disarmHook();

        if (!r.valid()) {
            sol::error e = r;
            err.message = e.what();
            return ScriptId::Invalid;
        }

        Impl::Instance inst;
        inst.env = std::move(env);
        inst.debugName = debugName;
        for (int i = 0; i < 5; ++i) {
            sol::object o = inst.env[ScriptCallbackName(static_cast<ScriptCallback>(i))];
            if (o.valid() && o.get_type() == sol::type::function) {
                inst.fn[i] = o.as<sol::protected_function>();
            }
        }

        const uint64_t id = impl_->nextId++;
        impl_->instances.emplace(id, std::move(inst));
        return static_cast<ScriptId>(id);
    }

    void LuaScriptBackend::destroyScript(ScriptId id) {
        impl_->instances.erase(static_cast<uint64_t>(id));
    }

    void LuaScriptBackend::destroyAllScripts() {
        impl_->instances.clear();
        // Environments hold the only references to per-instance tables; drop
        // them now rather than letting a full scene's garbage sit until the
        // next incidental collection.
        impl_->lua.collect_garbage();
    }

    size_t LuaScriptBackend::scriptCount() const { return impl_->instances.size(); }

    bool LuaScriptBackend::hasCallback(ScriptId id, ScriptCallback cb) const {
        const Impl::Instance* inst = impl_->find(id);
        return inst && inst->fn[static_cast<int>(cb)].valid();
    }

    bool LuaScriptBackend::callStart(ScriptId id, ScriptEntity self, ScriptError& err) {
        return impl_->invoke(id, ScriptCallback::Start, self, err);
    }
    bool LuaScriptBackend::callUpdate(ScriptId id, ScriptEntity self, float dt,
                                      ScriptError& err) {
        return impl_->invoke(id, ScriptCallback::Update, self, err, dt);
    }
    bool LuaScriptBackend::callFixedUpdate(ScriptId id, ScriptEntity self, float fixedDt,
                                           ScriptError& err) {
        return impl_->invoke(id, ScriptCallback::FixedUpdate, self, err, fixedDt);
    }
    bool LuaScriptBackend::callDestroy(ScriptId id, ScriptEntity self, ScriptError& err) {
        return impl_->invoke(id, ScriptCallback::Destroy, self, err);
    }

    bool LuaScriptBackend::callCollision(ScriptId id, ScriptEntity self,
                                         const ScriptCollision& hit, ScriptError& err) {
        Impl::Instance* inst = impl_->find(id);
        if (!inst) { err.message = "invalid script id"; return false; }

        sol::table t = impl_->lua.create_table();
        t["other"]     = LuaEntity{ hit.other, impl_->host };
        t["phase"]     = std::string(hit.phase ? hit.phase : "begin");
        t["isTrigger"] = hit.isTrigger;
        t["point"]     = hit.point;
        t["normal"]    = hit.normal;
        t["impulse"]   = hit.impulse;
        return impl_->invoke(id, ScriptCallback::Collision, self, err, t);
    }

} // namespace MyCoreEngine
