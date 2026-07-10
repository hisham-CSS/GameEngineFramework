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
