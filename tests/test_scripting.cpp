// Scripting seam tests: ScriptWorld's bookkeeping and error policy, plus the
// Lua backend's isolation and safety guarantees.
//
// Sources are injected through SetSourceResolver rather than written to disk,
// so these are pure-CPU and touch no filesystem.

#include <gtest/gtest.h>

#include "../Engine/src/core/Components.h"
#include "../Engine/src/script/ScriptBackendRegistry.h"
#include "../Engine/src/script/ScriptComponent.h"
#include "../Engine/src/script/ScriptWorld.h"

#include <entt/entt.hpp>

#include <map>
#include <string>

using namespace MyCoreEngine;

namespace {

    // Lua is optional at build time, so availability is a RUNTIME question:
    // CSE_WITH_LUA is private to the Engine target and invisible here.
    bool luaAvailable() {
        RegisterBuiltinScriptBackends();
        return ScriptBackendRegistry::IsRegistered("Lua");
    }

    // Fixture holding a registry, a world, and a name -> source table.
    struct ScriptFixture {
        entt::registry reg;
        ScriptWorld    world;
        std::map<std::string, std::string> sources;

        ScriptFixture() {
            RegisterBuiltinScriptBackends();
            world.SetSourceResolver([this](const std::string& p, std::string& out) {
                auto it = sources.find(p);
                if (it == sources.end()) return false;
                out = it->second;
                return true;
            });
        }

        entt::entity makeEntity(const char* name, const char* scriptPath) {
            const entt::entity e = reg.create();
            reg.emplace<Name>(e, Name{ name });
            reg.emplace<Transform>(e);
            if (scriptPath) reg.emplace<ScriptComponent>(e, ScriptComponent{ scriptPath, true });
            return e;
        }

        glm::vec3 pos(entt::entity e) { return reg.get<Transform>(e).position; }
    };

} // namespace

// ---------------------------------------------------------------- registry

TEST(ScriptRegistry, NullBackendIsAlwaysAvailable) {
    RegisterBuiltinScriptBackends();
    // The whole point of the Null backend: a build with no language runtime
    // still has a working scripting subsystem that does nothing.
    EXPECT_TRUE(ScriptBackendRegistry::IsRegistered("Null"));
    EXPECT_NE(ScriptBackendRegistry::Create("Null"), nullptr);
}

TEST(ScriptRegistry, UnknownBackendFailsCleanly) {
    ScriptWorld w;
    EXPECT_FALSE(w.SetBackend("Brainfuck"));
    // Must be EMPTY, not half-built: a failed SetBackend that left a backend
    // installed would run scripts against a runtime the caller rejected.
    EXPECT_FALSE(w.HasBackend());
    EXPECT_EQ(w.InstanceCount(), 0u);
}

TEST(ScriptRegistry, RegistrationIsIdempotent) {
    RegisterBuiltinScriptBackends();
    const size_t before = ScriptBackendRegistry::Available().size();
    RegisterBuiltinScriptBackends();
    // Called from both editor and player; must not duplicate list entries.
    EXPECT_EQ(ScriptBackendRegistry::Available().size(), before);
}

// ------------------------------------------------------------- ScriptWorld

TEST(ScriptWorld, BuildsOneInstancePerScriptedEntity) {
    ScriptFixture f;
    ASSERT_TRUE(f.world.SetBackend("Null"));
    f.sources["a.lua"] = "";
    f.makeEntity("scripted1", "a.lua");
    f.makeEntity("scripted2", "a.lua");
    f.makeEntity("plain", nullptr);

    f.world.Build(f.reg);
    EXPECT_EQ(f.world.InstanceCount(), 2u);
    EXPECT_EQ(f.world.FailedCount(), 0u);
}

TEST(ScriptWorld, MissingSourceFailsThatEntityOnly) {
    ScriptFixture f;
    ASSERT_TRUE(f.world.SetBackend("Null"));
    f.sources["good.lua"] = "";
    f.makeEntity("ok", "good.lua");
    f.makeEntity("broken", "does_not_exist.lua");

    f.world.Build(f.reg);
    EXPECT_EQ(f.world.InstanceCount(), 2u);
    // One bad path must not take down the other entity's script.
    EXPECT_EQ(f.world.FailedCount(), 1u);
}

TEST(ScriptWorld, RebuildDoesNotLeaveStaleInstances) {
    ScriptFixture f;
    ASSERT_TRUE(f.world.SetBackend("Null"));
    f.sources["a.lua"] = "";
    f.makeEntity("one", "a.lua");
    f.world.Build(f.reg);
    ASSERT_EQ(f.world.InstanceCount(), 1u);

    // Mimic a bulk restore (play-stop / undo): clear and resurrect. Every
    // cached entity->instance pair is stale even though handles look alike.
    f.reg.clear();
    f.makeEntity("one", "a.lua");
    f.makeEntity("two", "a.lua");
    f.world.Rebuild(f.reg);

    EXPECT_EQ(f.world.InstanceCount(), 2u);
}

// -------------------------------------------------------------------- Lua

TEST(LuaScripting, RunsStartAndUpdate) {
    if (!luaAvailable()) GTEST_SKIP() << "Lua backend not built";
    ScriptFixture f;
    ASSERT_TRUE(f.world.SetBackend("Lua"));

    f.sources["move.lua"] = R"(
        function OnStart()
            self:setPosition(vec3.new(1, 0, 0))
        end
        function OnUpdate(dt)
            self:translate(vec3.new(0, dt, 0))
        end
    )";
    const entt::entity e = f.makeEntity("mover", "move.lua");

    f.world.Build(f.reg);
    ASSERT_EQ(f.world.FailedCount(), 0u) << "script failed to compile";

    f.world.Start(f.reg);
    EXPECT_FLOAT_EQ(f.pos(e).x, 1.0f);

    f.world.Update(f.reg, 0.5f);
    f.world.Update(f.reg, 0.5f);
    EXPECT_FLOAT_EQ(f.pos(e).y, 1.0f);
    EXPECT_EQ(f.world.FailedCount(), 0u);
}

TEST(LuaScripting, WritingATransformMarksItDirty) {
    if (!luaAvailable()) GTEST_SKIP() << "Lua backend not built";
    ScriptFixture f;
    ASSERT_TRUE(f.world.SetBackend("Lua"));
    f.sources["m.lua"] = "function OnStart() self:setPosition(vec3.new(5,5,5)) end";
    const entt::entity e = f.makeEntity("m", "m.lua");

    f.world.Build(f.reg);
    f.reg.get<Transform>(e).dirty = false; // simulate a resolved hierarchy
    f.world.Start(f.reg);

    // Without the dirty flag the hierarchy keeps the STALE world matrix, so
    // the object renders and collides at its old position while its
    // component says otherwise -- a silent desync.
    EXPECT_TRUE(f.reg.get<Transform>(e).dirty);
}

TEST(LuaScripting, EachEntityGetsItsOwnGlobals) {
    if (!luaAvailable()) GTEST_SKIP() << "Lua backend not built";
    ScriptFixture f;
    ASSERT_TRUE(f.world.SetBackend("Lua"));

    // `counter` is a plain global. If instances shared one environment, both
    // entities would increment the SAME counter and end up at 2 -- the single
    // most surprising bug in a naive Lua integration.
    f.sources["count.lua"] = R"(
        counter = 0
        function OnUpdate(dt)
            counter = counter + 1
            self:setPosition(vec3.new(counter, 0, 0))
        end
    )";
    const entt::entity a = f.makeEntity("a", "count.lua");
    const entt::entity b = f.makeEntity("b", "count.lua");

    f.world.Build(f.reg);
    f.world.Start(f.reg);
    f.world.Update(f.reg, 0.1f);

    EXPECT_FLOAT_EQ(f.pos(a).x, 1.0f);
    EXPECT_FLOAT_EQ(f.pos(b).x, 1.0f) << "entities are sharing a global environment";
}

TEST(LuaScripting, SyntaxErrorIsReportedAndDisablesOnlyThatScript) {
    if (!luaAvailable()) GTEST_SKIP() << "Lua backend not built";
    ScriptFixture f;
    ASSERT_TRUE(f.world.SetBackend("Lua"));

    f.sources["bad.lua"]  = "function OnUpdate( this is not lua";
    f.sources["good.lua"] = "function OnUpdate(dt) self:translate(vec3.new(1,0,0)) end";
    f.makeEntity("bad", "bad.lua");
    const entt::entity good = f.makeEntity("good", "good.lua");

    f.world.Build(f.reg);
    EXPECT_EQ(f.world.FailedCount(), 1u);

    f.world.Start(f.reg);
    f.world.Update(f.reg, 0.1f);
    // The healthy script still runs. A broken file is normal authoring input,
    // not a reason to stop the whole scene.
    EXPECT_FLOAT_EQ(f.pos(good).x, 1.0f);
}

TEST(LuaScripting, RuntimeErrorDisablesScriptAfterFirstFailure) {
    if (!luaAvailable()) GTEST_SKIP() << "Lua backend not built";
    ScriptFixture f;
    ASSERT_TRUE(f.world.SetBackend("Lua"));

    // Compiles fine, explodes on the first call.
    f.sources["boom.lua"] = "function OnUpdate(dt) error('kaboom') end";
    f.makeEntity("boom", "boom.lua");

    f.world.Build(f.reg);
    EXPECT_EQ(f.world.FailedCount(), 0u) << "should compile cleanly";

    f.world.Start(f.reg);
    f.world.ClearMessages();
    f.world.Update(f.reg, 0.1f);
    EXPECT_EQ(f.world.FailedCount(), 1u);
    const size_t afterFirst = f.world.DrainMessages().size();
    EXPECT_GE(afterFirst, 1u) << "the failure must be reported";

    // A broken script must not re-log every frame; at 60fps that buries the
    // console in seconds and makes the real first error unfindable.
    for (int i = 0; i < 10; ++i) f.world.Update(f.reg, 0.1f);
    EXPECT_EQ(f.world.DrainMessages().size(), afterFirst) << "error is spamming";
}

TEST(LuaScripting, InfiniteLoopIsAbortedByInstructionLimit) {
    if (!luaAvailable()) GTEST_SKIP() << "Lua backend not built";
    ScriptFixture f;
    ScriptSettings s;
    s.instructionLimit = 100000; // small enough to trip fast
    ASSERT_TRUE(f.world.SetBackend("Lua", s));

    f.sources["hang.lua"] = "function OnUpdate(dt) while true do end end";
    f.makeEntity("hang", "hang.lua");

    f.world.Build(f.reg);
    f.world.Start(f.reg);
    // If the limit does not work this test HANGS rather than fails -- which
    // is exactly what would happen to the editor.
    f.world.Update(f.reg, 0.1f);

    EXPECT_EQ(f.world.FailedCount(), 1u) << "runaway loop was not aborted";
}

TEST(LuaScripting, DisabledScriptDoesNotRunButStillLoads) {
    if (!luaAvailable()) GTEST_SKIP() << "Lua backend not built";
    ScriptFixture f;
    ASSERT_TRUE(f.world.SetBackend("Lua"));
    f.sources["m.lua"] = "function OnUpdate(dt) self:translate(vec3.new(1,0,0)) end";
    const entt::entity e = f.makeEntity("m", "m.lua");
    f.reg.get<ScriptComponent>(e).enabled = false;

    f.world.Build(f.reg);
    f.world.Start(f.reg);
    f.world.Update(f.reg, 0.1f);

    EXPECT_FLOAT_EQ(f.pos(e).x, 0.0f) << "disabled script ran";
    // Still loaded, so syntax errors surface in the editor immediately
    // rather than only once the author remembers to tick the box.
    EXPECT_EQ(f.world.InstanceCount(), 1u);
}

TEST(LuaScripting, ScriptWithoutOnUpdateIsHarmless) {
    if (!luaAvailable()) GTEST_SKIP() << "Lua backend not built";
    ScriptFixture f;
    ASSERT_TRUE(f.world.SetBackend("Lua"));
    f.sources["startonly.lua"] = "function OnStart() self:setPosition(vec3.new(2,0,0)) end";
    const entt::entity e = f.makeEntity("s", "startonly.lua");

    f.world.Build(f.reg);
    f.world.Start(f.reg);
    for (int i = 0; i < 5; ++i) f.world.Update(f.reg, 0.1f);

    EXPECT_FLOAT_EQ(f.pos(e).x, 2.0f);
    EXPECT_EQ(f.world.FailedCount(), 0u) << "a missing hook must not be an error";
}

TEST(LuaScripting, FindByNameReachesAnotherEntity) {
    if (!luaAvailable()) GTEST_SKIP() << "Lua backend not built";
    ScriptFixture f;
    ASSERT_TRUE(f.world.SetBackend("Lua"));
    f.sources["seek.lua"] = R"(
        function OnStart()
            local t = find("target")
            if t:valid() then self:setPosition(t:position()) end
        end
    )";
    const entt::entity seeker = f.makeEntity("seeker", "seek.lua");
    const entt::entity target = f.makeEntity("target", nullptr);
    f.reg.get<Transform>(target).position = glm::vec3(7.f, 8.f, 9.f);

    f.world.Build(f.reg);
    f.world.Start(f.reg);

    EXPECT_FLOAT_EQ(f.pos(seeker).x, 7.0f);
    EXPECT_FLOAT_EQ(f.pos(seeker).z, 9.0f);
}

TEST(LuaScripting, LookupOfMissingEntityIsSafe) {
    if (!luaAvailable()) GTEST_SKIP() << "Lua backend not built";
    ScriptFixture f;
    ASSERT_TRUE(f.world.SetBackend("Lua"));
    // Calling methods on a handle to nothing must be a no-op, not a crash:
    // scripts hold entity ids across frames and objects get destroyed.
    f.sources["ghost.lua"] = R"(
        function OnStart()
            local g = find("nobody")
            if not g:valid() then self:setPosition(vec3.new(-1,0,0)) end
            g:setPosition(vec3.new(99,99,99))
            local p = g:position()
        end
    )";
    const entt::entity e = f.makeEntity("g", "ghost.lua");

    f.world.Build(f.reg);
    f.world.Start(f.reg);

    EXPECT_EQ(f.world.FailedCount(), 0u);
    EXPECT_FLOAT_EQ(f.pos(e).x, -1.0f);
}

TEST(LuaScripting, CollisionReachesBothSidesWithTheOtherEntity) {
    if (!luaAvailable()) GTEST_SKIP() << "Lua backend not built";
    ScriptFixture f;
    ASSERT_TRUE(f.world.SetBackend("Lua"));
    // Each side records the OTHER entity's name length, proving the pairing
    // is not simply echoing `self` back.
    f.sources["hit.lua"] = R"(
        function OnCollision(c)
            self:setPosition(vec3.new(#c.other:name(), c.impulse, 0))
        end
    )";
    const entt::entity a = f.makeEntity("aaa", "hit.lua");     // 3 chars
    const entt::entity b = f.makeEntity("bbbbb", "hit.lua");   // 5 chars

    f.world.Build(f.reg);
    f.world.Start(f.reg);
    f.world.DispatchCollision(a, b, "begin", false, glm::vec3(0.f), glm::vec3(0, 1, 0), 4.5f);

    EXPECT_FLOAT_EQ(f.pos(a).x, 5.0f) << "a should see b";
    EXPECT_FLOAT_EQ(f.pos(b).x, 3.0f) << "b should see a";
    EXPECT_FLOAT_EQ(f.pos(a).y, 4.5f) << "impulse did not reach the script";
}

TEST(LuaScripting, DestroyHookFiresOnClear) {
    if (!luaAvailable()) GTEST_SKIP() << "Lua backend not built";
    ScriptFixture f;
    ASSERT_TRUE(f.world.SetBackend("Lua"));
    f.sources["d.lua"] = "function OnDestroy() self:setPosition(vec3.new(42,0,0)) end";
    const entt::entity e = f.makeEntity("d", "d.lua");

    f.world.Build(f.reg);
    f.world.Start(f.reg);
    f.world.Clear();

    // Entities outlive the script world here, so the write must have landed.
    EXPECT_FLOAT_EQ(f.pos(e).x, 42.0f);
}

TEST(LuaScripting, FixedUpdateIsSeparateFromUpdate) {
    if (!luaAvailable()) GTEST_SKIP() << "Lua backend not built";
    ScriptFixture f;
    ASSERT_TRUE(f.world.SetBackend("Lua"));
    f.sources["both.lua"] = R"(
        function OnUpdate(dt)      self:translate(vec3.new(1, 0, 0)) end
        function OnFixedUpdate(dt) self:translate(vec3.new(0, 1, 0)) end
    )";
    const entt::entity e = f.makeEntity("b", "both.lua");

    f.world.Build(f.reg);
    f.world.Start(f.reg);
    f.world.Update(f.reg, 0.016f);
    f.world.FixedUpdate(f.reg, 0.02f);
    f.world.FixedUpdate(f.reg, 0.02f);

    EXPECT_FLOAT_EQ(f.pos(e).x, 1.0f);
    EXPECT_FLOAT_EQ(f.pos(e).y, 2.0f);
}
