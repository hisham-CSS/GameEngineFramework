// Physics: a backend-CONFORMANCE suite plus PhysicsWorld/ECS integration.
//
// The important design property being tested: every registered backend is
// driven through the SAME assertions via IPhysicsBackend. If Jolt and PhysX
// disagree about what "a 1kg box dropped onto a ground plane" does, the
// abstraction is leaking and this suite says so. Backends are discovered at
// runtime, so a build with neither SDK still runs the whole suite against
// "Simple".
//
// Headless by design: physics touches no GL.
#include <gtest/gtest.h>

#include "Engine.h"

#include <string>
#include <vector>

using namespace MyCoreEngine;

namespace {

    std::vector<std::string> AllBackends() {
        RegisterBuiltinPhysicsBackends();
        return PhysicsBackendRegistry::Available();
    }

    // A ground plane + one dynamic sphere above it, the minimal falling test.
    struct Fixture {
        std::unique_ptr<IPhysicsBackend> be;
        BodyId ground, ball;

        bool make(const std::string& name, float startY = 5.f) {
            be = PhysicsBackendRegistry::Create(name);
            if (!be) return false;
            PhysicsSettings s{};
            s.gravity = { 0.f, -9.81f, 0.f };
            if (!be->initialize(s)) return false;

            BodyDesc g{};
            g.type = BodyType::Static;
            g.shape.type = ShapeType::Plane;
            g.position = { 0.f, 0.f, 0.f };
            g.userData = 111;
            ground = be->createBody(g);

            BodyDesc b{};
            b.type = BodyType::Dynamic;
            b.shape.type = ShapeType::Sphere;
            b.shape.radius = 0.5f;
            b.position = { 0.f, startY, 0.f };
            b.mass = 1.f;
            b.userData = 222;
            ball = be->createBody(b);
            return ground.valid() && ball.valid();
        }

        void settle(int steps = 240) {
            for (int i = 0; i < steps; ++i) be->step(1.f / 60.f);
        }
    };

} // namespace

// Every build must expose at least the dependency-free backend, and the
// default name must actually resolve.
TEST(PhysicsRegistry, HasBackendsAndDefaultResolves) {
    const auto names = AllBackends();
    ASSERT_FALSE(names.empty());
    EXPECT_NE(std::find(names.begin(), names.end(), "Simple"), names.end())
        << "the dependency-free backend must always be registered";
    EXPECT_TRUE(PhysicsBackendRegistry::IsRegistered(DefaultPhysicsBackendName()))
        << "DefaultPhysicsBackendName() must name a registered backend";
    for (const auto& n : names) {
        std::printf("[physics] backend available: %s\n", n.c_str());
    }
    // unknown names fail cleanly rather than crashing
    EXPECT_EQ(PhysicsBackendRegistry::Create("NoSuchEngine"), nullptr);
}

// ---- conformance: the same assertions for every backend -------------------

TEST(PhysicsConformance, GravityPullsBodiesDown) {
    for (const auto& name : AllBackends()) {
        Fixture f;
        ASSERT_TRUE(f.make(name)) << name;
        BodyState s0{};
        ASSERT_TRUE(f.be->getBodyState(f.ball, s0)) << name;

        for (int i = 0; i < 30; ++i) f.be->step(1.f / 60.f);

        BodyState s1{};
        ASSERT_TRUE(f.be->getBodyState(f.ball, s1)) << name;
        EXPECT_LT(s1.position.y, s0.position.y) << name << ": gravity did not move the body";
        EXPECT_LT(s1.linearVelocity.y, 0.f) << name << ": downward velocity expected";
        f.be->shutdown();
    }
}

TEST(PhysicsConformance, BodyComesToRestOnGround) {
    for (const auto& name : AllBackends()) {
        Fixture f;
        ASSERT_TRUE(f.make(name)) << name;
        f.settle();

        BodyState s{};
        ASSERT_TRUE(f.be->getBodyState(f.ball, s)) << name;
        // radius 0.5 resting on y=0 => center near 0.5. Generous tolerance:
        // solvers differ in allowed penetration/sleep thresholds.
        EXPECT_NEAR(s.position.y, 0.5f, 0.25f)
            << name << ": ball should rest on the ground plane";
        EXPECT_LT(std::fabs(s.linearVelocity.y), 1.0f)
            << name << ": resting body should have ~zero vertical velocity";
        EXPECT_GT(s.position.y, -0.5f) << name << ": body fell through the ground";
        f.be->shutdown();
    }
}

TEST(PhysicsConformance, StaticBodiesDoNotMove) {
    for (const auto& name : AllBackends()) {
        Fixture f;
        ASSERT_TRUE(f.make(name)) << name;
        BodyState before{};
        ASSERT_TRUE(f.be->getBodyState(f.ground, before)) << name;
        f.settle(60);
        BodyState after{};
        ASSERT_TRUE(f.be->getBodyState(f.ground, after)) << name;
        EXPECT_NEAR(before.position.y, after.position.y, 1e-3f) << name;
        f.be->shutdown();
    }
}

TEST(PhysicsConformance, BodyCountTracksCreateDestroy) {
    for (const auto& name : AllBackends()) {
        Fixture f;
        ASSERT_TRUE(f.make(name)) << name;
        EXPECT_EQ(f.be->bodyCount(), 2u) << name;
        f.be->destroyBody(f.ball);
        EXPECT_EQ(f.be->bodyCount(), 1u) << name;
        f.be->destroyAllBodies();
        EXPECT_EQ(f.be->bodyCount(), 0u) << name;
        // stepping an empty world must be safe
        f.be->step(1.f / 60.f);
        f.be->shutdown();
    }
}

TEST(PhysicsConformance, RaycastHitsGroundAndReportsUserData) {
    for (const auto& name : AllBackends()) {
        Fixture f;
        ASSERT_TRUE(f.make(name)) << name;
        f.settle(120); // let the ball rest so the scene is deterministic

        RayHit hit{};
        // straight down from high above, must hit something
        const bool got = f.be->raycast({ 0.f, 50.f, 0.f }, { 0.f, -1.f, 0.f }, 200.f, hit);
        EXPECT_TRUE(got) << name << ": downward ray should hit the ball or ground";
        if (got) {
            EXPECT_TRUE(hit.hit) << name;
            EXPECT_GT(hit.distance, 0.f) << name;
            EXPECT_TRUE(hit.userData == 111 || hit.userData == 222)
                << name << ": userData must round-trip (got " << hit.userData << ")";
        }

        // a ray pointing away from everything must miss
        RayHit miss{};
        EXPECT_FALSE(f.be->raycast({ 0.f, 50.f, 0.f }, { 0.f, 1.f, 0.f }, 10.f, miss)) << name;
        f.be->shutdown();
    }
}

TEST(PhysicsConformance, ImpulseChangesVelocity) {
    for (const auto& name : AllBackends()) {
        Fixture f;
        ASSERT_TRUE(f.make(name, 20.f)) << name; // high up, away from the ground
        f.be->setLinearVelocity(f.ball, { 0.f, 0.f, 0.f });
        f.be->applyImpulse(f.ball, { 10.f, 0.f, 0.f }); // 10 Ns on 1 kg => ~10 m/s
        f.be->step(1.f / 60.f);

        BodyState s{};
        ASSERT_TRUE(f.be->getBodyState(f.ball, s)) << name;
        EXPECT_GT(s.linearVelocity.x, 1.f)
            << name << ": impulse should produce +X motion";
        f.be->shutdown();
    }
}

TEST(PhysicsConformance, SetTransformTeleportsBody) {
    for (const auto& name : AllBackends()) {
        Fixture f;
        ASSERT_TRUE(f.make(name, 20.f)) << name;
        f.be->setBodyTransform(f.ball, { 3.f, 12.f, -4.f }, glm::quat(1.f, 0.f, 0.f, 0.f));
        f.be->wakeBody(f.ball);

        BodyState s{};
        ASSERT_TRUE(f.be->getBodyState(f.ball, s)) << name;
        EXPECT_NEAR(s.position.x, 3.f, 1e-2f) << name;
        EXPECT_NEAR(s.position.y, 12.f, 1e-2f) << name;
        EXPECT_NEAR(s.position.z, -4.f, 1e-2f) << name;
        f.be->shutdown();
    }
}

TEST(PhysicsConformance, GravitySettingIsHonoured) {
    for (const auto& name : AllBackends()) {
        Fixture f;
        ASSERT_TRUE(f.make(name, 20.f)) << name;
        f.be->setGravity({ 0.f, 0.f, 0.f });
        EXPECT_NEAR(f.be->gravity().y, 0.f, 1e-3f) << name;
        f.be->setLinearVelocity(f.ball, { 0.f, 0.f, 0.f });

        BodyState before{}, after{};
        ASSERT_TRUE(f.be->getBodyState(f.ball, before)) << name;
        for (int i = 0; i < 60; ++i) f.be->step(1.f / 60.f);
        ASSERT_TRUE(f.be->getBodyState(f.ball, after)) << name;
        EXPECT_NEAR(after.position.y, before.position.y, 0.2f)
            << name << ": zero gravity should leave a still body in place";
        f.be->shutdown();
    }
}

// ---- contact events: same assertions for every backend --------------------

TEST(PhysicsConformance, LandingReportsABeginContact) {
    for (const auto& name : AllBackends()) {
        Fixture f;
        ASSERT_TRUE(f.make(name, 3.f)) << name;
        if (!f.be->supportsContactEvents()) { f.be->shutdown(); continue; }

        bool sawBegin = false;
        bool pairedCorrectly = false;
        for (int i = 0; i < 240 && !sawBegin; ++i) {
            f.be->step(1.f / 60.f);
            for (const auto& e : f.be->contactEvents()) {
                if (e.phase != ContactPhase::Begin) continue;
                sawBegin = true;
                // the pair must be ball(222) + ground(111), in either order
                pairedCorrectly =
                    (e.userDataA == 222 && e.userDataB == 111) ||
                    (e.userDataA == 111 && e.userDataB == 222);
            }
        }
        EXPECT_TRUE(sawBegin) << name << ": landing produced no Begin contact";
        EXPECT_TRUE(pairedCorrectly)
            << name << ": contact userData did not identify both bodies";
        f.be->shutdown();
    }
}

TEST(PhysicsConformance, ContactEventsAreClearedEachStep) {
    for (const auto& name : AllBackends()) {
        Fixture f;
        ASSERT_TRUE(f.make(name, 3.f)) << name;
        if (!f.be->supportsContactEvents()) { f.be->shutdown(); continue; }

        f.settle(300); // land and come fully to rest
        // once resting, steady state must not keep re-reporting Begin: the
        // API surfaces TRANSITIONS, not a per-step "still touching" stream
        int beginsWhileResting = 0;
        for (int i = 0; i < 60; ++i) {
            f.be->step(1.f / 60.f);
            for (const auto& e : f.be->contactEvents()) {
                if (e.phase == ContactPhase::Begin) ++beginsWhileResting;
            }
        }
        EXPECT_EQ(beginsWhileResting, 0)
            << name << ": resting contact re-reported Begin (events not transition-only)";
        f.be->shutdown();
    }
}

// A trigger must report overlap but apply NO collision response — the body
// falls straight through it. Skipped on backends that declare no trigger
// support rather than asserted-around.
TEST(PhysicsConformance, TriggerReportsOverlapWithoutBlocking) {
    for (const auto& name : AllBackends()) {
        auto be = PhysicsBackendRegistry::Create(name);
        ASSERT_NE(be, nullptr) << name;
        PhysicsSettings s{};
        ASSERT_TRUE(be->initialize(s)) << name;
        if (!be->supportsTriggers() || !be->supportsContactEvents()) {
            be->shutdown();
            continue;
        }

        // static trigger volume sitting at y=2
        BodyDesc t{};
        t.type = BodyType::Static;
        t.shape.type = ShapeType::Box;
        t.shape.halfExtents = { 4.f, 1.f, 4.f };
        t.position = { 0.f, 2.f, 0.f };
        t.isTrigger = true;
        t.userData = 777;
        ASSERT_TRUE(be->createBody(t).valid()) << name;

        BodyDesc b{};
        b.type = BodyType::Dynamic;
        b.shape.type = ShapeType::Sphere;
        b.shape.radius = 0.25f;
        b.position = { 0.f, 8.f, 0.f };
        b.mass = 1.f;
        b.userData = 888;
        const BodyId ball = be->createBody(b);
        ASSERT_TRUE(ball.valid()) << name;

        bool sawTriggerBegin = false;
        for (int i = 0; i < 300; ++i) {
            be->step(1.f / 60.f);
            for (const auto& e : be->contactEvents()) {
                if (e.isTrigger && e.phase == ContactPhase::Begin) sawTriggerBegin = true;
            }
        }
        EXPECT_TRUE(sawTriggerBegin) << name << ": trigger overlap was never reported";

        BodyState st{};
        ASSERT_TRUE(be->getBodyState(ball, st)) << name;
        EXPECT_LT(st.position.y, 1.0f)
            << name << ": body was blocked by a trigger (triggers must not collide)";
        be->shutdown();
    }
}

// ---- PhysicsWorld <-> ECS integration -------------------------------------

namespace {
    // Builds a scene with a ground plane and a falling box, returns the box.
    entt::entity buildFallScene(Scene& scene) {
        Entity ground = scene.createEntity();
        Transform gt{};
        ground.addComponent<Transform>(gt);
        ground.addComponent<RigidBody>(RigidBody{ BodyType::Static });
        ground.addComponent<PlaneCollider>(PlaneCollider{});

        Entity box = scene.createEntity();
        Transform bt{};
        bt.position = { 0.f, 6.f, 0.f };
        box.addComponent<Transform>(bt);
        RigidBody rb{};
        rb.type = BodyType::Dynamic;
        rb.mass = 1.f;
        box.addComponent<RigidBody>(rb);
        box.addComponent<BoxCollider>(BoxCollider{ glm::vec3(0.5f) });
        scene.UpdateTransforms();
        return box;
    }
}

TEST(PhysicsWorldTest, StepWritesPosesBackIntoTransforms) {
    for (const auto& name : AllBackends()) {
        Scene scene;
        const entt::entity box = buildFallScene(scene);

        PhysicsWorld world;
        ASSERT_TRUE(world.SetBackend(name)) << name;
        world.Build(scene.registry);
        EXPECT_EQ(world.BodyCount(), 2u) << name;
        EXPECT_TRUE(world.SkippedEntities().empty()) << name;

        const float y0 = scene.registry.get<Transform>(box).position.y;
        for (int i = 0; i < 60; ++i) {
            world.Step(scene.registry, 1.f / 60.f);
            scene.UpdateTransforms(); // physics marks transforms dirty
        }
        const float y1 = scene.registry.get<Transform>(box).position.y;

        EXPECT_LT(y1, y0) << name << ": the box should have fallen";
        // and the world matrix must reflect it (dirty flag actually set)
        EXPECT_NEAR(scene.registry.get<Transform>(box).modelMatrix[3].y, y1, 1e-3f)
            << name << ": Transform.dirty was not set, so the hierarchy is stale";
    }
}

TEST(PhysicsWorldTest, RigidBodyWithoutColliderIsSkippedNotSimulated) {
    Scene scene;
    Entity e = scene.createEntity();
    e.addComponent<Transform>(Transform{});
    e.addComponent<RigidBody>(RigidBody{}); // no collider on purpose
    scene.UpdateTransforms();

    PhysicsWorld world;
    ASSERT_TRUE(world.SetBackend("Simple"));
    world.Build(scene.registry);
    EXPECT_EQ(world.BodyCount(), 0u);
    ASSERT_EQ(world.SkippedEntities().size(), 1u)
        << "a RigidBody with no collider must be reported, not silently ignored";
}

TEST(PhysicsWorldTest, RebuildAfterRegistryResetIsSafe) {
    // Mirrors the editor's play-stop / undo path: the registry is cleared and
    // entities are resurrected, so every cached entity->body pair is stale.
    Scene scene;
    buildFallScene(scene);

    PhysicsWorld world;
    ASSERT_TRUE(world.SetBackend("Simple"));
    world.Build(scene.registry);
    EXPECT_EQ(world.BodyCount(), 2u);

    scene.registry.clear();
    world.Rebuild(scene.registry); // must not touch freed entities
    EXPECT_EQ(world.BodyCount(), 0u);

    buildFallScene(scene);
    world.Rebuild(scene.registry);
    EXPECT_EQ(world.BodyCount(), 2u);

    // stepping after a rebuild still works
    world.Step(scene.registry, 1.f / 60.f);
}

TEST(PhysicsWorldTest, SwitchingBackendsTearsDownCleanly) {
    const auto names = AllBackends();
    Scene scene;
    buildFallScene(scene);

    PhysicsWorld world;
    for (const auto& name : names) {
        ASSERT_TRUE(world.SetBackend(name)) << name;
        world.Build(scene.registry);
        EXPECT_EQ(world.BodyCount(), 2u) << name;
        world.Step(scene.registry, 1.f / 60.f);
    }
    // an unknown backend leaves the world empty rather than half-built
    EXPECT_FALSE(world.SetBackend("NoSuchEngine"));
    EXPECT_FALSE(world.HasBackend());
    EXPECT_EQ(world.BodyCount(), 0u);
    // and stepping a backend-less world is a no-op, not a crash
    world.Step(scene.registry, 1.f / 60.f);
}

// Events must reach a listener with both sides resolved to ECS entities —
// that mapping is the whole point of routing them through PhysicsWorld.
TEST(PhysicsWorldTest, CollisionListenerReceivesResolvedEntities) {
    for (const auto& name : AllBackends()) {
        Scene scene;
        const entt::entity box = buildFallScene(scene);

        PhysicsWorld world;
        ASSERT_TRUE(world.SetBackend(name)) << name;
        if (!world.BackendReportsContacts()) continue;
        world.Build(scene.registry);

        int begins = 0;
        bool boxInvolved = false;
        bool bothResolved = true;
        world.OnCollision([&](const PhysicsWorld::CollisionEvent& e) {
            if (e.phase != ContactPhase::Begin) return;
            ++begins;
            if (e.a == box || e.b == box) boxInvolved = true;
            if (e.a == entt::null || e.b == entt::null) bothResolved = false;
        });

        for (int i = 0; i < 300 && begins == 0; ++i) {
            world.Step(scene.registry, 1.f / 60.f);
            scene.UpdateTransforms();
        }
        EXPECT_GT(begins, 0) << name << ": listener never fired";
        EXPECT_TRUE(boxInvolved) << name << ": falling box was not in the contact";
        EXPECT_TRUE(bothResolved) << name << ": an event had an unresolved entity";
    }
}

TEST(PhysicsWorldTest, RemovedListenerStopsReceiving) {
    Scene scene;
    buildFallScene(scene);
    PhysicsWorld world;
    ASSERT_TRUE(world.SetBackend("Simple"));
    world.Build(scene.registry);

    int calls = 0;
    const auto h = world.OnCollision([&](const PhysicsWorld::CollisionEvent&) { ++calls; });
    world.RemoveCollisionListener(h);
    for (int i = 0; i < 300; ++i) {
        world.Step(scene.registry, 1.f / 60.f);
        scene.UpdateTransforms();
    }
    EXPECT_EQ(calls, 0) << "a removed listener must not be called";
}

TEST(PhysicsWorldTest, RaycastMapsHitBackToEntity) {
    Scene scene;
    const entt::entity box = buildFallScene(scene);

    PhysicsWorld world;
    ASSERT_TRUE(world.SetBackend("Simple"));
    world.Build(scene.registry);

    RayHit hit{};
    ASSERT_TRUE(world.Raycast({ 0.f, 20.f, 0.f }, { 0.f, -1.f, 0.f }, 100.f, hit));
    EXPECT_EQ(world.EntityFromHit(hit), box)
        << "a hit must resolve back to the owning ECS entity";
}
