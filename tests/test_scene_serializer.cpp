// Scene serialization round-trip tests (CPU only — no GL context needed).
#include <gtest/gtest.h>
#include "Engine.h"

#include <cstdio>
#include <fstream>

using namespace MyCoreEngine;

namespace {
    int CountNamed(Scene& s) {
        int count = 0;
        for (auto e : s.registry.view<Name>()) { (void)e; ++count; }
        return count;
    }
}

TEST(SceneSerializer, RoundTripEntitiesAndSettings) {
    const char* path = "test_scene_roundtrip.json";

    Scene a;
    a.LightDir() = glm::vec3(0.1f, -0.9f, 0.3f);
    a.LightColor() = glm::vec3(1.f, 0.5f, 0.25f);
    a.LightIntensity() = 4.5f;
    a.SetPBREnabled(false);
    a.SetIBLIntensity(0.7f);

    Entity e1 = a.createEntity();
    e1.addComponent<Name>(Name{ "Hero" });
    Transform t{};
    t.position = { 1.f, 2.f, 3.f };
    t.rotation = { 10.f, 20.f, 30.f };
    t.scale = { 2.f, 2.f, 2.f };
    e1.addComponent<Transform>(t);
    a.registry.emplace<NoShadow>(e1);

    Entity e2 = a.createEntity();
    e2.addComponent<Name>(Name{ "Thing 2" });

    AssetManager assets;
    SceneSerializer save(a, assets);
    ASSERT_TRUE(save.Save(path));

    Scene b;
    SceneSerializer load(b, assets);
    ASSERT_TRUE(load.Load(path));

    // scene settings survived
    EXPECT_FLOAT_EQ(b.LightIntensity(), 4.5f);
    EXPECT_FALSE(b.GetPBREnabled());
    EXPECT_FLOAT_EQ(b.GetIBLIntensity(), 0.7f);
    EXPECT_FLOAT_EQ(b.LightDir().y, -0.9f);
    EXPECT_FLOAT_EQ(b.LightColor().b, 0.25f);

    // entities survived with their components
    bool foundHero = false, foundThing = false;
    auto view = b.registry.view<Name>();
    for (auto e : view) {
        const auto& n = view.get<Name>(e);
        if (n.value == "Hero") {
            foundHero = true;
            auto* tr = b.registry.try_get<Transform>(e);
            ASSERT_NE(tr, nullptr);
            EXPECT_FLOAT_EQ(tr->position.x, 1.f);
            EXPECT_FLOAT_EQ(tr->position.z, 3.f);
            EXPECT_FLOAT_EQ(tr->rotation.z, 30.f);
            EXPECT_FLOAT_EQ(tr->scale.y, 2.f);
            EXPECT_TRUE(b.registry.any_of<NoShadow>(e));
        }
        else if (n.value == "Thing 2") {
            foundThing = true;
            EXPECT_FALSE(b.registry.any_of<NoShadow>(e));
        }
    }
    EXPECT_EQ(CountNamed(b), 2);
    EXPECT_TRUE(foundHero);
    EXPECT_TRUE(foundThing);

    std::remove(path);
}

// A ModelComponent with no model loaded (added via the Inspector, model
// picked later) is a legitimate authoring state: it must survive a
// save/load round trip as a present-but-empty component, exactly like it
// survives undo/redo and play-stop restores.
TEST(SceneSerializer, EmptyModelComponentRoundTrip) {
    const char* path = "test_scene_empty_model.json";

    Scene a;
    Entity e1 = a.createEntity();
    e1.addComponent<Name>(Name{ "Pending" });
    e1.addComponent<Transform>(Transform{});
    e1.addComponent<ModelComponent>(ModelComponent{}); // no model yet

    AssetManager assets;
    SceneSerializer save(a, assets);
    ASSERT_TRUE(save.Save(path));

    Scene b;
    SceneSerializer load(b, assets);
    ASSERT_TRUE(load.Load(path));

    bool found = false;
    auto view = b.registry.view<Name>();
    for (auto e : view) {
        if (view.get<Name>(e).value != "Pending") continue;
        found = true;
        auto* mc = b.registry.try_get<ModelComponent>(e);
        ASSERT_NE(mc, nullptr) << "empty ModelComponent dropped by save/load";
        EXPECT_EQ(mc->model, nullptr);
        EXPECT_FALSE(b.registry.any_of<AABB>(e));
    }
    EXPECT_TRUE(found);

    std::remove(path);
}

TEST(SceneSerializer, RoundTripParentLinks) {
    const char* path = "test_scene_parents.json";

    Scene a;
    Entity parent = a.createEntity();
    parent.addComponent<Name>(Name{ "Parent" });
    Transform pt{};
    pt.position = { 10.f, 0.f, 0.f };
    parent.addComponent<Transform>(pt);

    Entity child = a.createEntity();
    child.addComponent<Name>(Name{ "Child" });
    Transform ct{};
    ct.position = { 0.f, 5.f, 0.f }; // LOCAL to parent
    child.addComponent<Transform>(ct);
    a.registry.emplace<Parent>(child, Parent{ (entt::entity)parent });

    AssetManager assets;
    SceneSerializer save(a, assets);
    ASSERT_TRUE(save.Save(path));

    Scene b;
    SceneSerializer load(b, assets);
    ASSERT_TRUE(load.Load(path));

    entt::entity lp = entt::null, lc = entt::null;
    for (auto e : b.registry.view<Name>()) {
        const auto& n = b.registry.get<Name>(e);
        if (n.value == "Parent") lp = e;
        if (n.value == "Child") lc = e;
    }
    ASSERT_TRUE(lp != entt::null);
    ASSERT_TRUE(lc != entt::null);
    ASSERT_TRUE(b.registry.any_of<Parent>(lc));
    EXPECT_EQ(b.registry.get<Parent>(lc).value, lp);
    EXPECT_FALSE(b.registry.any_of<Parent>(lp));

    // world transforms resolve through the restored hierarchy
    b.UpdateTransforms();
    const glm::vec3 childWorld(b.registry.get<Transform>(lc).modelMatrix[3]);
    EXPECT_NEAR(childWorld.x, 10.f, 1e-4f);
    EXPECT_NEAR(childWorld.y, 5.f, 1e-4f);

    std::remove(path);
}

TEST(SceneSerializer, ParentIndicesSurviveSkippedEntries) {
    // parent refs are FILE-array indices; a malformed non-object entry must
    // not shift later links onto the wrong entities
    const char* path = "test_scene_skipshift.json";
    {
        std::ofstream out(path);
        out << R"({"version":1,"entities":[
            {"name":"A","transform":{"position":[0,0,0]}},
            42,
            {"name":"B","transform":{"position":[1,0,0]}},
            {"name":"C","transform":{"position":[2,0,0]},"parent":2}
        ]})";
    }
    Scene s;
    AssetManager assets;
    SceneSerializer load(s, assets);
    ASSERT_TRUE(load.Load(path));

    entt::entity b = entt::null, c = entt::null;
    for (auto e : s.registry.view<Name>()) {
        if (s.registry.get<Name>(e).value == "B") b = e;
        if (s.registry.get<Name>(e).value == "C") c = e;
    }
    ASSERT_TRUE(b != entt::null);
    ASSERT_TRUE(c != entt::null);
    ASSERT_TRUE(s.registry.any_of<Parent>(c)) << "parent link lost to index shift";
    EXPECT_EQ(s.registry.get<Parent>(c).value, b) << "parent link shifted to wrong entity";

    std::remove(path);
}

TEST(SceneSerializer, RefusesParentCyclesInFile) {
    const char* path = "test_scene_cycle.json";
    {
        std::ofstream out(path);
        out << R"({"version":1,"entities":[
            {"name":"A","transform":{"position":[0,0,0]},"parent":1},
            {"name":"B","transform":{"position":[1,0,0]},"parent":0}
        ]})";
    }
    Scene s;
    AssetManager assets;
    SceneSerializer load(s, assets);
    ASSERT_TRUE(load.Load(path));

    // at most one direction of the cycle may survive; a full A<->B cycle
    // would make both entities unreachable from any hierarchy root
    int parented = 0;
    for (auto e : s.registry.view<Parent>()) { (void)e; ++parented; }
    EXPECT_LE(parented, 1);

    std::remove(path);
}

TEST(SceneSerializer, RoundTripCameraComponent) {
    const char* path = "test_scene_camera.json";

    Scene a;
    Entity cam = a.createEntity();
    cam.addComponent<Name>(Name{ "Main Camera" });
    Transform t{};
    t.position = { 0.f, 6.f, 30.f };
    cam.addComponent<Transform>(t);
    a.registry.emplace<CameraComponent>((entt::entity)cam, CameraComponent{ 85.f, true });

    AssetManager assets;
    SceneSerializer save(a, assets);
    ASSERT_TRUE(save.Save(path));

    Scene b;
    SceneSerializer load(b, assets);
    ASSERT_TRUE(load.Load(path));

    const entt::entity found = FindPrimaryCamera(b.registry);
    ASSERT_TRUE(found != entt::null);
    EXPECT_FLOAT_EQ(b.registry.get<CameraComponent>(found).fovDeg, 85.f);
    EXPECT_TRUE(b.registry.get<CameraComponent>(found).primary);

    std::remove(path);
}

// "New Scene" (P2-4): everything gone, settings back to defaults
TEST(SceneReset, ResetToDefaultsClearsEntitiesAndSettings) {
    Scene s;
    Entity e = s.createEntity();
    e.addComponent<Name>(Name{ "Doomed" });
    e.addComponent<Transform>(Transform{});
    s.LightIntensity() = 9.f;
    s.LightDir() = glm::vec3(1.f, 0.f, 0.f);
    s.SetPBREnabled(false);
    s.SetIBLIntensity(3.5f);
    s.SetLODDistanceScale(2.f);
    s.SetInstancingEnabled(false);

    s.ResetToDefaults();

    size_t count = 0;
    for (auto ent : s.registry.view<entt::entity>()) { (void)ent; ++count; }
    EXPECT_EQ(count, 0u);
    EXPECT_FLOAT_EQ(s.LightIntensity(), 3.f);
    EXPECT_NEAR(s.LightDir().y, glm::normalize(glm::vec3(0.3f, -1.f, 0.2f)).y, 1e-5f);
    EXPECT_TRUE(s.GetPBREnabled());
    EXPECT_FLOAT_EQ(s.GetIBLIntensity(), 1.f);
    EXPECT_FLOAT_EQ(s.GetLODDistanceScale(), 1.f);
    EXPECT_TRUE(s.GetInstancingEnabled());

    // a fresh save/load round-trips the reset state (empty scene loads back)
    const char* path = "test_scene_reset.json";
    AssetManager assets;
    SceneSerializer save(s, assets);
    ASSERT_TRUE(save.Save(path));
    Scene b;
    SceneSerializer load(b, assets);
    ASSERT_TRUE(load.Load(path));
    size_t loaded = 0;
    for (auto ent : b.registry.view<entt::entity>()) { (void)ent; ++loaded; }
    EXPECT_EQ(loaded, 0u);
    std::remove(path);
}

TEST(SceneSerializer, LoadMissingFileFails) {
    Scene s;
    AssetManager assets;
    SceneSerializer sz(s, assets);
    EXPECT_FALSE(sz.Load("does_not_exist_12345.json"));
}

TEST(SceneSerializer, LoadInvalidJsonLeavesSceneIntact) {
    const char* path = "test_scene_bad.json";
    { std::ofstream f(path); f << "{ this is not valid json !!!"; }

    Scene s;
    AssetManager assets;
    Entity e = s.createEntity();
    e.addComponent<Name>(Name{ "Keep" });

    SceneSerializer sz(s, assets);
    EXPECT_FALSE(sz.Load(path));
    EXPECT_EQ(CountNamed(s), 1); // failed load must not clear the scene

    std::remove(path);
}

TEST(SceneSerializer, LoadUnsupportedVersionFails) {
    const char* path = "test_scene_ver.json";
    { std::ofstream f(path); f << R"({"version": 99, "entities": []})"; }

    Scene s;
    AssetManager assets;
    SceneSerializer sz(s, assets);
    EXPECT_FALSE(sz.Load(path));

    std::remove(path);
}
