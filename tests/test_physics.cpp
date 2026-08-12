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

// Impulse must be positive on impact and MONOTONIC in impact speed — that's
// the property gameplay relies on (louder thud / more damage from a harder
// hit). Absolute values are NOT compared across backends on purpose: PhysX
// reports the solver's applied impulse while Jolt's callback runs pre-solve
// and estimates it, so only within-backend ordering is meaningful.
TEST(PhysicsConformance, ContactImpulseScalesWithImpactSpeed) {
    for (const auto& name : AllBackends()) {
        auto firstImpact = [&](float dropHeight) -> float {
            Fixture f;
            if (!f.make(name, dropHeight)) return -1.f;
            if (!f.be->supportsContactEvents()) return -2.f;
            for (int i = 0; i < 600; ++i) {
                f.be->step(1.f / 60.f);
                for (const auto& e : f.be->contactEvents()) {
                    if (e.phase == ContactPhase::Begin) {
                        const float j = e.impulse;
                        f.be->shutdown();
                        return j;
                    }
                }
            }
            f.be->shutdown();
            return 0.f;
        };

        const float soft = firstImpact(1.5f);
        if (soft == -2.f) continue; // backend reports no events
        ASSERT_GE(soft, 0.f) << name << ": fixture failed to build";
        const float hard = firstImpact(20.f);

        EXPECT_GT(soft, 0.f) << name << ": landing reported zero impulse";
        EXPECT_GT(hard, soft)
            << name << ": a 20m drop must hit harder than a 1.5m drop ("
            << hard << " vs " << soft << ")";
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

// A trigger is a trigger on the way OUT as well as on the way in. Jolt filled
// isTrigger in OnContactAdded from the live bodies, but OnContactRemoved gets
// only a SubShapeIDPair -- no bodies to ask, possibly already destroyed -- and
// left the flag at its false default. PhysX reports true for both phases, so
// the same scene behaved differently under the two backends, and a listener
// filtering on isTrigger saw enters with no matching exits: doorways that
// opened and never closed, counters that only ever went up.
TEST(PhysicsConformance, TriggerExitIsAlsoFlaggedAsATrigger) {
    for (const auto& name : AllBackends()) {
        auto be = PhysicsBackendRegistry::Create(name);
        ASSERT_NE(be, nullptr) << name;
        PhysicsSettings s{};
        ASSERT_TRUE(be->initialize(s)) << name;
        if (!be->supportsTriggers() || !be->supportsContactEvents()) {
            be->shutdown();
            continue;
        }

        // A thin trigger slab with nothing under it, so the ball falls in,
        // falls out, and both edges are observed in one run.
        BodyDesc t{};
        t.type = BodyType::Static;
        t.shape.type = ShapeType::Box;
        t.shape.halfExtents = { 4.f, 0.5f, 4.f };
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
        ASSERT_TRUE(be->createBody(b).valid()) << name;

        int begins = 0, ends = 0, endsFlagged = 0;
        for (int i = 0; i < 400; ++i) {
            be->step(1.f / 60.f);
            for (const auto& e : be->contactEvents()) {
                if (e.phase == ContactPhase::Begin && e.isTrigger) ++begins;
                if (e.phase == ContactPhase::End) {
                    ++ends;
                    if (e.isTrigger) ++endsFlagged;
                }
            }
        }

        ASSERT_GT(begins, 0) << name << ": the ball never entered the trigger";
        ASSERT_GT(ends, 0) << name << ": the ball never left the trigger, so this "
                                      "test cannot say anything about exit events";
        EXPECT_EQ(endsFlagged, ends)
            << name << ": " << (ends - endsFlagged) << " of " << ends
            << " trigger EXIT event(s) were reported as ordinary collisions -- "
               "isTrigger is unset on the End phase";
        be->shutdown();
    }
}

// The Simple backend is the REFERENCE the conformance suite checks Jolt and
// PhysX against, so its own output must not depend on hash order.
//
// It built its support list by iterating an unordered_map, so the list arrived
// in hash order, and the resting test "if (s.topY > restY)" gave the tie to
// whichever support the hash visited first. Ties are ordinary rather than
// exotic: two ground boxes meeting, or a plane and a box both at y=0. Since
// restingOn feeds the Begin/End contact events, the coin flip was observable.
//
// The fix sorts the support list by body id, which makes the tie-break a stated
// RULE -- lowest id wins -- instead of an emergent property of std::hash. That
// rule is what this test locks down.
//
// WHAT THIS TEST CANNOT DO, stated plainly because it was MEASURED: deleting
// the sort and re-running leaves this test GREEN on MSVC. Its std::hash for
// integers is the identity, so a fresh map of small contiguous ids already
// iterates in ascending order and the sort changes nothing observable here.
//
// So this is NOT proof that the sort is load-bearing. What it does lock is the
// RULE -- lowest id wins. Change the comparison to >= (last wins) and it fails
// immediately, because that flips the winner to the highest id on any iteration
// order. The sort's real justification is cross-implementation: libstdc++ and
// libc++ do not iterate like MSVC, and nothing in the standard says they should.
// That can only be settled by the cross-platform trace comparison in
// ARCHITECTURE.md Phase 1, which is where this belongs and where it will be
// checked for real.
TEST(PhysicsConformance, SimpleBackendBreaksRestingTiesByLowestBodyId) {
    RegisterBuiltinPhysicsBackends();
    auto be = PhysicsBackendRegistry::Create("Simple");
    ASSERT_NE(be, nullptr);
    PhysicsSettings s{};
    s.gravity = { 0.f, -9.81f, 0.f };
    ASSERT_TRUE(be->initialize(s));

    // EIGHT supports, all the same height, all overlapping the drop point, so
    // every one of them is a candidate and the tie is eight-way. Enough bodies
    // that hash order is very unlikely to coincide with id order.
    std::vector<BodyId> supports;
    for (int i = 0; i < 8; ++i) {
        BodyDesc d{};
        d.type = BodyType::Static;
        d.shape.type = ShapeType::Box;
        d.shape.halfExtents = { 4.f, 0.5f, 4.f };
        d.position = { 0.f, 0.f, 0.f };          // identical top Y
        d.userData = 100ull + static_cast<uint64_t>(i);
        supports.push_back(be->createBody(d));
        ASSERT_TRUE(supports.back().valid());
    }

    // Destroy a couple so the surviving ids are NOT contiguous and the lowest
    // survivor is not simply "the one created first overall".
    be->destroyBody(supports[0]);
    be->destroyBody(supports[1]);

    uint64_t lowestSurviving = ~0ull;
    for (std::size_t i = 2; i < supports.size(); ++i)
        lowestSurviving = std::min(lowestSurviving, supports[i].value);

    BodyDesc b{};
    b.type = BodyType::Dynamic;
    b.shape.type = ShapeType::Sphere;
    b.shape.radius = 0.25f;
    b.position = { 0.f, 4.f, 0.f };
    b.mass = 1.f;
    b.userData = 999;
    ASSERT_TRUE(be->createBody(b).valid());

    uint64_t landedOnBody = 0;
    for (int i = 0; i < 300 && landedOnBody == 0; ++i) {
        be->step(1.f / 60.f);
        for (const auto& e : be->contactEvents()) {
            if (e.phase != ContactPhase::Begin) continue;
            // a is always the dynamic body here; b is the support it landed on.
            landedOnBody = e.b.value;
            break;
        }
    }

    ASSERT_NE(landedOnBody, 0u)
        << "the body never landed, so the tie was never exercised";
    EXPECT_EQ(landedOnBody, lowestSurviving)
        << "an eight-way tie at the same height resolved to body " << landedOnBody
        << " rather than the lowest surviving id " << lowestSurviving
        << " -- the support list is not sorted, so the winner is whatever"
           " std::unordered_map happened to yield first";
    be->shutdown();
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

// REGRESSION (shipped player: "the backpack fell through the floor").
// Bodies are built from WORLD poses, but Transform::modelMatrix is a CACHE
// that UpdateTransforms fills — right after a scene load it is still
// identity. Building then made a 300x-scaled ground at y=-3 into a 1x1 box
// at the origin, so dynamics fell straight past it. The editor never saw it
// because Play happens after many ticks; the player builds immediately.
// Build must therefore be correct even with dirty transforms.
TEST(PhysicsWorldTest, BuildUsesWorldPoseEvenWithDirtyTransforms) {
    for (const auto& name : AllBackends()) {
        Scene scene;

        // big, low, scaled ground — exactly the shape of the real bug
        Entity ground = scene.createEntity();
        Transform gt{};
        gt.position = { 0.f, -3.f, 0.f };
        gt.scale = { 300.f, 1.f, 300.f };
        ground.addComponent<Transform>(gt);
        ground.addComponent<RigidBody>(RigidBody{ BodyType::Static });
        ground.addComponent<BoxCollider>(BoxCollider{ glm::vec3(0.5f), glm::vec3(0.f, 0.5f, 0.f) });

        Entity box = scene.createEntity();
        Transform bt{};
        bt.position = { 0.f, 5.f, 0.f };
        box.addComponent<Transform>(bt);
        RigidBody rb{};
        rb.type = BodyType::Dynamic;
        box.addComponent<RigidBody>(rb);
        box.addComponent<BoxCollider>(BoxCollider{ glm::vec3(0.5f) });

        // DELIBERATELY no UpdateTransforms() here: this is the player's
        // load-then-build order, and the caches are identity.
        PhysicsWorld world;
        ASSERT_TRUE(world.SetBackend(name)) << name;
        world.Build(scene.registry);
        EXPECT_EQ(world.BodyCount(), 2u) << name;

        for (int i = 0; i < 300; ++i) {
            world.Step(scene.registry, 1.f / 60.f);
            scene.UpdateTransforms();
        }

        // ground top = -3 + (0.5 offset * 1) + (0.5 half * 1) = -2.0,
        // so a 0.5-half box rests near -1.5. Falling through would leave it
        // far below (it never stops).
        const float y = scene.registry.get<Transform>(box).position.y;
        EXPECT_GT(y, -3.f)
            << name << ": body fell THROUGH the ground (y=" << y
            << ") — bodies were built from stale identity transforms";
        EXPECT_NEAR(y, -1.5f, 0.6f) << name << ": did not come to rest on the ground";
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

// Step's counterpart to BuildUsesWorldPoseEvenWithDirtyTransforms above: the
// SAME cache, the same trap, on the per-tick path this time.
//
// Transform::modelMatrix is filled once per frame by Scene::UpdateTransforms,
// AFTER the whole fixed-step loop. So anything that moves a transform inside a
// tick -- a script, the gameplay fixed-update slot, a contact listener -- is
// invisible to a Step that reads the cache, and stays invisible until the next
// frame. Both phases of Step used to read it.

// 1) KINEMATIC PUSH. A kinematic body is driven BY gameplay, so gameplay moving
//    it mid-tick is the entire point of the body type.
TEST(PhysicsWorldTest, KinematicPushUsesTheLiveTransformNotTheCache) {
    for (const auto& name : AllBackends()) {
        Scene scene;
        Entity platformEnt = scene.createEntity();
        const entt::entity platform = platformEnt;
        Transform t{};
        t.position = { 0.f, 1.f, 0.f };
        platformEnt.addComponent<Transform>(t);
        platformEnt.addComponent<RigidBody>(RigidBody{ BodyType::Kinematic });
        platformEnt.addComponent<BoxCollider>(BoxCollider{ glm::vec3(1.f) });
        scene.UpdateTransforms();

        PhysicsWorld world;
        ASSERT_TRUE(world.SetBackend(name)) << name;
        world.Build(scene.registry);

        // Gameplay moves the platform mid-tick: position changes, dirty is set,
        // and modelMatrix still holds the OLD pose -- exactly the state Step
        // sees when a script drives a platform from OnFixedUpdate.
        auto& tr = scene.registry.get<Transform>(platform);
        tr.position = { 5.f, 1.f, 0.f };
        tr.dirty = true;
        ASSERT_NEAR(tr.modelMatrix[3].x, 0.f, 1e-4f)
            << name << ": the cache was refreshed, so this test proves nothing";

        world.Step(scene.registry, 1.f / 60.f);

        // Ask the simulation directly: a kinematic body is driven, never read
        // back into its Transform, so the ECS cannot answer this.
        const BodyId id = world.BodyFor(platform);
        ASSERT_TRUE(id.valid()) << name << ": no body was built for the platform";
        BodyState st{};
        ASSERT_TRUE(world.Backend()->getBodyState(id, st)) << name;
        EXPECT_NEAR(st.position.x, 5.f, 1e-3f)
            << name << ": the kinematic body was pushed to x=" << st.position.x
            << " -- Step read the stale modelMatrix cache instead of resolving "
               "the live TRS chain, so the body chased last frame's pose";
    }
}

// 2) READ-BACK. Phase 3 re-derives the entity's scale to rebuild a world
//    matrix. Taking that from the cache meant DecomposeTRS wrote a frame-old
//    scale straight back over Transform::scale, silently undoing any scale set
//    during the tick. It looked like an effect that only worked at high frame
//    rates, because only a short frame left no second step to revert it.
TEST(PhysicsWorldTest, StepPreservesAScaleSetDuringTheSameFrame) {
    for (const auto& name : AllBackends()) {
        Scene scene;
        const entt::entity box = buildFallScene(scene);

        PhysicsWorld world;
        ASSERT_TRUE(world.SetBackend(name)) << name;
        world.Build(scene.registry);

        // A listener squashes the box mid-frame. No UpdateTransforms follows --
        // that is the whole point; it runs once per FRAME, after every step.
        auto& tr = scene.registry.get<Transform>(box);
        tr.scale = { 2.f, 0.5f, 2.f };
        tr.dirty = true;

        // The next step of the SAME frame.
        world.Step(scene.registry, 1.f / 60.f);

        const glm::vec3 after = scene.registry.get<Transform>(box).scale;
        EXPECT_NEAR(after.x, 2.0f, 1e-3f) << name;
        EXPECT_NEAR(after.y, 0.5f, 1e-3f)
            << name << ": the scale set this frame was reverted to " << after.y
            << " -- Step rebuilt the world matrix from the stale modelMatrix "
               "cache and DecomposeTRS wrote the old scale back";
        EXPECT_NEAR(after.z, 2.0f, 1e-3f) << name;
    }
}
