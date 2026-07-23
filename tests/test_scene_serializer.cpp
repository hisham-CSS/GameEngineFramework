// Scene serialization round-trip tests (CPU only — no GL context needed).
#include <gtest/gtest.h>
#include "Engine.h"
#include "../Engine/src/core/PathSandbox.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

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

// Physics components must survive save/load, and must do so WITHOUT naming a
// backend: a scene authored while Jolt was active has to load under PhysX.
TEST(SceneSerializer, RoundTripPhysicsComponents) {
    const char* path = "test_scene_physics.json";

    Scene a;
    Entity body = a.createEntity();
    body.addComponent<Name>(Name{ "Crate" });
    body.addComponent<Transform>(Transform{});
    RigidBody rb;
    rb.type = BodyType::Dynamic;
    rb.mass = 7.5f;
    rb.friction = 0.33f;
    rb.restitution = 0.62f;
    rb.linearDamping = 0.11f;
    rb.angularDamping = 0.22f;
    rb.isTrigger = true;
    rb.initialLinearVelocity = { 1.f, 2.f, 3.f };
    body.addComponent<RigidBody>(rb);
    body.addComponent<BoxCollider>(BoxCollider{ glm::vec3(2.f, 3.f, 4.f), glm::vec3(0.5f, 0.f, -0.5f) });

    Entity ground = a.createEntity();
    ground.addComponent<Name>(Name{ "Ground" });
    ground.addComponent<Transform>(Transform{});
    ground.addComponent<RigidBody>(RigidBody{ BodyType::Static });
    ground.addComponent<PlaneCollider>(PlaneCollider{ glm::vec3(0.f, -1.f, 0.f) });

    Entity ball = a.createEntity();
    ball.addComponent<Name>(Name{ "Ball" });
    ball.addComponent<Transform>(Transform{});
    ball.addComponent<RigidBody>(RigidBody{});
    ball.addComponent<SphereCollider>(SphereCollider{ 1.25f, glm::vec3(0.f, 1.f, 0.f) });

    Entity cap = a.createEntity();
    cap.addComponent<Name>(Name{ "Cap" });
    cap.addComponent<Transform>(Transform{});
    cap.addComponent<RigidBody>(RigidBody{});
    cap.addComponent<CapsuleCollider>(CapsuleCollider{ 0.4f, 0.9f, glm::vec3(0.f) });

    AssetManager assets;
    SceneSerializer save(a, assets);
    ASSERT_TRUE(save.Save(path));

    Scene b;
    SceneSerializer load(b, assets);
    ASSERT_TRUE(load.Load(path));

    // Look entities up by name without comparing against entt::null (gtest's
    // printer can't format a null entity handle).
    bool found = false;
    auto byName = [&](const char* want) {
        auto view = b.registry.view<Name>();
        found = false;
        auto result = *view.begin();
        for (auto e : view) {
            if (view.get<Name>(e).value == want) { result = e; found = true; break; }
        }
        return result;
    };

    const auto eBody = byName("Crate");
    ASSERT_TRUE(found) << "Crate missing after load";
    const auto* rb2 = b.registry.try_get<RigidBody>(eBody);
    ASSERT_NE(rb2, nullptr) << "RigidBody dropped by save/load";
    EXPECT_EQ(rb2->type, BodyType::Dynamic);
    EXPECT_FLOAT_EQ(rb2->mass, 7.5f);
    EXPECT_FLOAT_EQ(rb2->friction, 0.33f);
    EXPECT_FLOAT_EQ(rb2->restitution, 0.62f);
    EXPECT_FLOAT_EQ(rb2->linearDamping, 0.11f);
    EXPECT_FLOAT_EQ(rb2->angularDamping, 0.22f);
    EXPECT_TRUE(rb2->isTrigger);
    EXPECT_FLOAT_EQ(rb2->initialLinearVelocity.z, 3.f);
    const auto* box = b.registry.try_get<BoxCollider>(eBody);
    ASSERT_NE(box, nullptr);
    EXPECT_FLOAT_EQ(box->halfExtents.y, 3.f);
    EXPECT_FLOAT_EQ(box->offset.x, 0.5f);

    const auto eGround = byName("Ground");
    ASSERT_TRUE(found) << "Ground missing after load";
    ASSERT_NE(b.registry.try_get<RigidBody>(eGround), nullptr);
    EXPECT_EQ(b.registry.get<RigidBody>(eGround).type, BodyType::Static);
    ASSERT_NE(b.registry.try_get<PlaneCollider>(eGround), nullptr);
    EXPECT_FLOAT_EQ(b.registry.get<PlaneCollider>(eGround).offset.y, -1.f);

    const auto eBall = byName("Ball");
    ASSERT_TRUE(found) << "Ball missing after load";
    const auto* sph = b.registry.try_get<SphereCollider>(eBall);
    ASSERT_NE(sph, nullptr);
    EXPECT_FLOAT_EQ(sph->radius, 1.25f);
    EXPECT_FLOAT_EQ(sph->offset.y, 1.f);

    const auto eCap = byName("Cap");
    ASSERT_TRUE(found) << "Cap missing after load";
    const auto* capc = b.registry.try_get<CapsuleCollider>(eCap);
    ASSERT_NE(capc, nullptr);
    EXPECT_FLOAT_EQ(capc->radius, 0.4f);
    EXPECT_FLOAT_EQ(capc->halfHeight, 0.9f);

    // no backend name anywhere in the file: scenes are backend-agnostic
    std::ifstream in(path);
    const std::string text((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    EXPECT_EQ(text.find("Jolt"), std::string::npos);
    EXPECT_EQ(text.find("PhysX"), std::string::npos);

    std::remove(path);
}

// The shipped player renders through FindActiveCamera. A startup scene with
// no enabled CameraComponent makes it silently fall back to a free-fly debug
// camera, which reads as "the build ignored my camera" — the exact bug the
// committed demo scene had (401 entities, zero cameras). This pins BOTH that
// a camera survives the round trip AND that the seed scene ships with one.
TEST(SceneSerializer, SceneCameraSurvivesRoundTripAndDrivesThePlayer) {
    const char* path = "test_scene_camera.json";

    Scene a;
    Entity cam = a.createEntity();
    cam.addComponent<Name>(Name{ "Main Camera" });
    Transform t{};
    t.position = { 0.f, 6.f, 30.f };
    t.rotation = { -11.f, 0.f, 0.f };
    cam.addComponent<Transform>(t);
    CameraComponent cc;
    cc.fovDeg = 60.f;
    cc.enabled = true;
    a.registry.emplace<CameraComponent>(cam, cc);

    AssetManager assets;
    SceneSerializer save(a, assets);
    ASSERT_TRUE(save.Save(path));

    Scene b;
    SceneSerializer load(b, assets);
    ASSERT_TRUE(load.Load(path));
    b.UpdateTransforms();

    // the player's own selection call must find it
    const entt::entity active = FindActiveCamera(b.registry);
    ASSERT_TRUE(b.registry.valid(active))
        << "a saved camera must be selectable by FindActiveCamera";
    EXPECT_EQ(b.registry.get<Name>(active).value, "Main Camera");

    // ...and the pose must actually reach the render camera
    Camera rendered;
    ASSERT_TRUE(SyncCameraFromEntity(b.registry, active, rendered));
    EXPECT_FLOAT_EQ(rendered.Position.z, 30.f);
    EXPECT_FLOAT_EQ(rendered.Zoom, 60.f);

    // a DISABLED camera must not be picked (it would look like the same bug)
    b.registry.get<CameraComponent>(active).enabled = false;
    EXPECT_FALSE(b.registry.valid(FindActiveCamera(b.registry)))
        << "a disabled camera must not drive the view";

    std::remove(path);
}

TEST(SceneSerializer, RoundTripScriptComponent) {
    const char* path = "test_scene_script.json";

    Scene a;
    Entity spinner = a.createEntity();
    spinner.addComponent<Name>(Name{ "Spinner" });
    spinner.addComponent<Transform>(Transform{});
    a.registry.emplace<ScriptComponent>(spinner, ScriptComponent{ "spinner.lua", true });

    // A disabled script must stay disabled across a save: reloading a scene
    // and having every muted script start running is a nasty surprise.
    Entity muted = a.createEntity();
    muted.addComponent<Name>(Name{ "Muted" });
    muted.addComponent<Transform>(Transform{});
    a.registry.emplace<ScriptComponent>(muted, ScriptComponent{ "noisy.lua", false });

    // Half-authored: the component exists but no file is chosen yet. It must
    // survive rather than silently vanishing when the author saves mid-edit.
    Entity empty = a.createEntity();
    empty.addComponent<Name>(Name{ "Empty" });
    empty.addComponent<Transform>(Transform{});
    a.registry.emplace<ScriptComponent>(empty, ScriptComponent{});

    AssetManager assets;
    SceneSerializer save(a, assets);
    ASSERT_TRUE(save.Save(path));

    Scene b;
    SceneSerializer load(b, assets);
    ASSERT_TRUE(load.Load(path));

    entt::entity eSpin = entt::null, eMute = entt::null, eEmpty = entt::null;
    for (auto [e, n] : b.registry.view<Name>().each()) {
        if (n.value == "Spinner") eSpin = e;
        else if (n.value == "Muted") eMute = e;
        else if (n.value == "Empty") eEmpty = e;
    }
    // != rather than ASSERT_NE: gtest tries to print the operands, and
    // entt::null_t does not survive that instantiation (see the note above).
    ASSERT_TRUE(eSpin != entt::null);
    ASSERT_TRUE(eMute != entt::null);
    ASSERT_TRUE(eEmpty != entt::null);

    const auto* s1 = b.registry.try_get<ScriptComponent>(eSpin);
    ASSERT_NE(s1, nullptr) << "ScriptComponent dropped by save/load";
    EXPECT_EQ(s1->path, "spinner.lua");
    EXPECT_TRUE(s1->enabled);

    const auto* s2 = b.registry.try_get<ScriptComponent>(eMute);
    ASSERT_NE(s2, nullptr);
    EXPECT_FALSE(s2->enabled) << "a muted script came back enabled";

    const auto* s3 = b.registry.try_get<ScriptComponent>(eEmpty);
    ASSERT_NE(s3, nullptr) << "empty script slot lost on reload";
    EXPECT_TRUE(s3->path.empty());

    std::remove(path);
}

TEST(SceneSerializer, RoundTripLightComponent) {
    const char* path = "test_scene_light.json";

    Scene a;
    Entity spot = a.createEntity();
    spot.addComponent<Name>(Name{ "Lamp" });
    spot.addComponent<Transform>(Transform{});
    LightComponent lc;
    lc.type = LightType::Spot;
    lc.color = { 0.9f, 0.4f, 0.1f };
    lc.intensity = 42.5f;
    lc.range = 12.25f;
    lc.innerAngleDeg = 15.f;
    lc.outerAngleDeg = 35.f;
    a.registry.emplace<LightComponent>(spot, lc);

    // an inverted cone must come back clamped, not inverted: the shader's
    // smoothstep would otherwise light everything OUTSIDE the cone
    Entity bad = a.createEntity();
    bad.addComponent<Name>(Name{ "Bad" });
    bad.addComponent<Transform>(Transform{});
    LightComponent inv;
    inv.type = LightType::Spot;
    inv.innerAngleDeg = 40.f;
    inv.outerAngleDeg = 5.f;
    a.registry.emplace<LightComponent>(bad, inv);

    AssetManager assets;
    SceneSerializer save(a, assets);
    ASSERT_TRUE(save.Save(path));

    Scene b;
    SceneSerializer load(b, assets);
    ASSERT_TRUE(load.Load(path));

    bool foundLamp = false, foundBad = false;
    auto view = b.registry.view<Name>();
    for (auto e : view) {
        const auto& n = view.get<Name>(e).value;
        if (n == "Lamp") {
            foundLamp = true;
            const auto* l = b.registry.try_get<LightComponent>(e);
            ASSERT_NE(l, nullptr) << "LightComponent dropped by save/load";
            EXPECT_EQ(l->type, LightType::Spot);
            EXPECT_FLOAT_EQ(l->intensity, 42.5f);
            EXPECT_FLOAT_EQ(l->range, 12.25f);
            EXPECT_FLOAT_EQ(l->color.r, 0.9f);
            EXPECT_FLOAT_EQ(l->innerAngleDeg, 15.f);
            EXPECT_FLOAT_EQ(l->outerAngleDeg, 35.f);
            EXPECT_TRUE(l->enabled);
        }
        else if (n == "Bad") {
            foundBad = true;
            const auto* l = b.registry.try_get<LightComponent>(e);
            ASSERT_NE(l, nullptr);
            EXPECT_GE(l->outerAngleDeg, l->innerAngleDeg)
                << "an inverted spot cone must be clamped on load";
        }
    }
    EXPECT_TRUE(foundLamp);
    EXPECT_TRUE(foundBad);

    std::remove(path);
}

TEST(SceneSerializer, RoundTripAudioComponents) {
    const char* path = "test_scene_audio.json";

    Scene a;
    // A fully-tuned 3D source.
    Entity speaker = a.createEntity();
    speaker.addComponent<Name>(Name{ "Speaker" });
    speaker.addComponent<Transform>(Transform{});
    AudioSourceComponent as;
    as.clip = "Exported/Audio/hum.wav";
    as.volume = 0.6f;
    as.pitch = 1.5f;
    as.loop = true;
    as.spatial = true;
    as.playOnStart = false;
    as.minDistance = 2.f;
    as.maxDistance = 40.f;
    a.registry.emplace<AudioSourceComponent>(speaker, as);

    // A 2D music bed: spatial off must round-trip (a music track that reloads
    // as a positional 3D source would go silent off-camera).
    Entity music = a.createEntity();
    music.addComponent<Name>(Name{ "Music" });
    music.addComponent<Transform>(Transform{});
    AudioSourceComponent bed;
    bed.clip = "Exported/Audio/theme.ogg";
    bed.spatial = false;
    bed.loop = true;
    a.registry.emplace<AudioSourceComponent>(music, bed);

    // Half-authored: the component exists but no clip is chosen yet. It must
    // survive rather than vanish when the author saves mid-edit.
    Entity pending = a.createEntity();
    pending.addComponent<Name>(Name{ "Pending" });
    pending.addComponent<Transform>(Transform{});
    a.registry.emplace<AudioSourceComponent>(pending, AudioSourceComponent{});

    // The listener tag: presence is the whole state.
    Entity ears = a.createEntity();
    ears.addComponent<Name>(Name{ "Ears" });
    ears.addComponent<Transform>(Transform{});
    a.registry.emplace<AudioListenerComponent>(ears);

    AssetManager assets;
    SceneSerializer save(a, assets);
    ASSERT_TRUE(save.Save(path));

    Scene b;
    SceneSerializer load(b, assets);
    ASSERT_TRUE(load.Load(path));

    entt::entity eSpk = entt::null, eMus = entt::null,
                 ePend = entt::null, eEars = entt::null;
    for (auto [e, n] : b.registry.view<Name>().each()) {
        if (n.value == "Speaker") eSpk = e;
        else if (n.value == "Music") eMus = e;
        else if (n.value == "Pending") ePend = e;
        else if (n.value == "Ears") eEars = e;
    }
    ASSERT_TRUE(eSpk != entt::null);
    ASSERT_TRUE(eMus != entt::null);
    ASSERT_TRUE(ePend != entt::null);
    ASSERT_TRUE(eEars != entt::null);

    const auto* s1 = b.registry.try_get<AudioSourceComponent>(eSpk);
    ASSERT_NE(s1, nullptr) << "AudioSourceComponent dropped by save/load";
    EXPECT_EQ(s1->clip, "Exported/Audio/hum.wav");
    EXPECT_FLOAT_EQ(s1->volume, 0.6f);
    EXPECT_FLOAT_EQ(s1->pitch, 1.5f);
    EXPECT_TRUE(s1->loop);
    EXPECT_TRUE(s1->spatial);
    EXPECT_FALSE(s1->playOnStart);
    EXPECT_FLOAT_EQ(s1->minDistance, 2.f);
    EXPECT_FLOAT_EQ(s1->maxDistance, 40.f);

    const auto* s2 = b.registry.try_get<AudioSourceComponent>(eMus);
    ASSERT_NE(s2, nullptr);
    EXPECT_FALSE(s2->spatial) << "a 2D source came back spatial";
    EXPECT_TRUE(s2->loop);

    const auto* s3 = b.registry.try_get<AudioSourceComponent>(ePend);
    ASSERT_NE(s3, nullptr) << "empty audio source lost on reload";
    EXPECT_TRUE(s3->clip.empty());

    EXPECT_TRUE(b.registry.any_of<AudioListenerComponent>(eEars))
        << "listener tag dropped by save/load";
    EXPECT_FALSE(b.registry.any_of<AudioListenerComponent>(eSpk));

    std::remove(path);
}

// A hand-edited file with max <= min distance must load with a strictly
// ordered span, or the 3D attenuation curve divides by a zero/negative range.
TEST(SceneSerializer, AudioInvertedDistancesLoadOrdered) {
    const char* path = "test_scene_audio_bad.json";
    {
        std::ofstream out(path);
        out << R"({"version":1,"entities":[
            {"name":"Bad","transform":{"position":[0,0,0]},
             "audioSource":{"clip":"x.wav","minDistance":10.0,"maxDistance":3.0}}
        ]})";
    }
    Scene s;
    AssetManager assets;
    SceneSerializer load(s, assets);
    ASSERT_TRUE(load.Load(path));

    entt::entity e = entt::null;
    for (auto [ent, n] : s.registry.view<Name>().each())
        if (n.value == "Bad") e = ent;
    ASSERT_TRUE(e != entt::null);
    const auto* as = s.registry.try_get<AudioSourceComponent>(e);
    ASSERT_NE(as, nullptr);
    EXPECT_GT(as->maxDistance, as->minDistance)
        << "an inverted distance span must be clamped on load";

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
    CameraComponent cc;
    cc.fovDeg = 85.f;
    cc.nearClip = 0.25f;
    cc.farClip = 500.f;
    cc.priority = 7;
    cc.enabled = false;
    a.registry.emplace<CameraComponent>((entt::entity)cam, cc);

    AssetManager assets;
    SceneSerializer save(a, assets);
    ASSERT_TRUE(save.Save(path));

    Scene b;
    SceneSerializer load(b, assets);
    ASSERT_TRUE(load.Load(path));

    bool found = false;
    for (auto [e, c] : b.registry.view<CameraComponent>().each()) {
        found = true;
        EXPECT_FLOAT_EQ(c.fovDeg, 85.f);
        EXPECT_FLOAT_EQ(c.nearClip, 0.25f);
        EXPECT_FLOAT_EQ(c.farClip, 500.f);
        EXPECT_EQ(c.priority, 7);
        EXPECT_FALSE(c.enabled); // disabled must round-trip (it is not selectable)
    }
    EXPECT_TRUE(found);
    // disabled camera: nothing to render from
    EXPECT_TRUE(FindActiveCamera(b.registry) == entt::null);

    std::remove(path);
}

// Scenes written before the camera director existed marked the rendered
// camera with "primary": true — that must map onto a priority bump so old
// multi-camera scenes keep their selection.
TEST(SceneSerializer, LegacyPrimaryCameraMapsToPriority) {
    const char* path = "test_scene_legacy_camera.json";
    {
        std::ofstream out(path);
        out << R"({
  "version": 1,
  "entities": [
    { "name": "Other",  "transform": { "position": [0,0,0] }, "camera": { "fovDeg": 60.0, "primary": false } },
    { "name": "Chosen", "transform": { "position": [1,0,0] }, "camera": { "fovDeg": 70.0, "primary": true } }
  ]
})";
    }

    Scene s;
    AssetManager assets;
    SceneSerializer load(s, assets);
    ASSERT_TRUE(load.Load(path));

    const entt::entity active = FindActiveCamera(s.registry);
    ASSERT_TRUE(active != entt::null);
    EXPECT_EQ(s.registry.get<Name>(active).value, "Chosen");
    EXPECT_EQ(s.registry.get<CameraComponent>(active).priority, 1);
    // legacy files carry no clip planes: defaults apply
    EXPECT_FLOAT_EQ(s.registry.get<CameraComponent>(active).nearClip, 0.1f);
    EXPECT_FLOAT_EQ(s.registry.get<CameraComponent>(active).farClip, 1000.f);

    std::remove(path);
}

// The OLD default was primary = true and nothing cleared the flag on other
// cameras, so legacy files routinely hold several primaries. The old
// selection iterated newest-first — the LAST primary in the file rendered —
// and the mapping must preserve that (later primaries get higher priority).
TEST(SceneSerializer, LegacyMultiPrimaryKeepsTheLastPrimary) {
    const char* path = "test_scene_legacy_multi.json";
    {
        std::ofstream out(path);
        out << R"({
  "version": 1,
  "entities": [
    { "name": "Main Camera",  "transform": { "position": [0,0,0] }, "camera": { "fovDeg": 60.0, "primary": true } },
    { "name": "Cutscene Cam", "transform": { "position": [1,0,0] }, "camera": { "fovDeg": 45.0, "primary": true } }
  ]
})";
    }

    Scene s;
    AssetManager assets;
    SceneSerializer load(s, assets);
    ASSERT_TRUE(load.Load(path));

    const entt::entity active = FindActiveCamera(s.registry);
    ASSERT_TRUE(active != entt::null);
    EXPECT_EQ(s.registry.get<Name>(active).value, "Cutscene Cam");

    std::remove(path);
}

// Camera priority ties resolve by entity index, and entity indices come
// from file order on load — so save/load must preserve entity order or the
// rendered camera would flip on every save cycle (entt views iterate
// newest-first; the serializer reverses back to creation order).
TEST(SceneSerializer, PriorityTieSurvivesSaveLoadCycles) {
    const char* path = "test_scene_tie_stability.json";

    Scene a;
    auto addCam = [](Scene& s, const char* name, glm::vec3 pos) {
        Entity e = s.createEntity();
        e.addComponent<Name>(Name{ name });
        Transform t{};
        t.position = pos;
        e.addComponent<Transform>(t);
        s.registry.emplace<CameraComponent>((entt::entity)e, CameraComponent{});
    };
    addCam(a, "First", { 0.f, 0.f, 0.f });
    addCam(a, "Second", { 1.f, 0.f, 0.f });

    AssetManager assets;
    ASSERT_TRUE(a.registry.valid(FindActiveCamera(a.registry)));
    const std::string winner =
        a.registry.get<Name>(FindActiveCamera(a.registry)).value;

    // two full save/load cycles: the tie winner must never change
    Scene b;
    ASSERT_TRUE(SceneSerializer(a, assets).Save(path));
    ASSERT_TRUE(SceneSerializer(b, assets).Load(path));
    ASSERT_TRUE(b.registry.valid(FindActiveCamera(b.registry)));
    EXPECT_EQ(b.registry.get<Name>(FindActiveCamera(b.registry)).value, winner);

    Scene c;
    ASSERT_TRUE(SceneSerializer(b, assets).Save(path));
    ASSERT_TRUE(SceneSerializer(c, assets).Load(path));
    ASSERT_TRUE(c.registry.valid(FindActiveCamera(c.registry)));
    EXPECT_EQ(c.registry.get<Name>(FindActiveCamera(c.registry)).value, winner);

    std::remove(path);
}

// Absolute epsilons round away above ~32k (ULP(50000) ≈ 0.004): a file
// holding near == far must still load with a strictly ordered lens or the
// projection divides by zero and renders NaN.
TEST(SceneSerializer, HugeEqualClipPlanesLoadStrictlyOrdered) {
    const char* path = "test_scene_huge_clips.json";
    {
        std::ofstream out(path);
        out << R"({
  "version": 1,
  "entities": [
    { "name": "Cam", "transform": { "position": [0,0,0] },
      "camera": { "fovDeg": 60.0, "nearClip": 50000.0, "farClip": 50000.0 } }
  ]
})";
    }

    Scene s;
    AssetManager assets;
    SceneSerializer load(s, assets);
    ASSERT_TRUE(load.Load(path));

    const entt::entity cam = FindActiveCamera(s.registry);
    ASSERT_TRUE(cam != entt::null);
    const auto& cc = s.registry.get<CameraComponent>(cam);
    EXPECT_GT(cc.farClip, cc.nearClip) << "near == far must not survive the load clamp";

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

TEST(SceneSerializer, RoundTripEnvironmentSettings) {
    const char* path = "test_scene_env.json";

    Scene a;
    EnvironmentSettings e;
    e.source = EnvironmentSettings::Source::HDRi;
    e.hdriPath = "Exported/Env/studio.hdr";
    e.skyIntensity = 0.75f;
    e.drawSkybox = false;
    e.zenith = { 0.05f, 0.10f, 0.30f };
    e.horizon = { 0.80f, 0.60f, 0.40f };
    e.ground = { 0.10f, 0.09f, 0.08f };
    e.sunIntensity = 7.5f;
    a.SetEnvironment(e);
    a.SetIBLIntensity(2.25f); // lives in settings, NOT in EnvironmentSettings

    AssetManager assets;
    SceneSerializer save(a, assets);
    ASSERT_TRUE(save.Save(path));

    Scene b;
    SceneSerializer load(b, assets);
    ASSERT_TRUE(load.Load(path));

    const EnvironmentSettings& r = b.Environment();
    EXPECT_EQ(r.source, EnvironmentSettings::Source::HDRi);
    EXPECT_EQ(r.hdriPath, "Exported/Env/studio.hdr");
    EXPECT_FLOAT_EQ(r.skyIntensity, 0.75f);
    EXPECT_FALSE(r.drawSkybox);
    EXPECT_FLOAT_EQ(r.zenith.z, 0.30f);
    EXPECT_FLOAT_EQ(r.horizon.r, 0.80f);
    EXPECT_FLOAT_EQ(r.sunIntensity, 7.5f);
    // The two intensities are deliberately separate knobs; make sure the
    // environment block did not swallow the scene-level one.
    EXPECT_FLOAT_EQ(b.GetIBLIntensity(), 2.25f);

    std::remove(path);
}

TEST(SceneSerializer, SceneWithoutEnvironmentBlockGetsTheProceduralSky) {
    const char* path = "test_scene_noenv.json";

    // A scene saved before environment settings existed.
    Scene a;
    AssetManager assets;
    SceneSerializer save(a, assets);
    ASSERT_TRUE(save.Save(path));

    std::ifstream in(path);
    nlohmann::json root;
    in >> root;
    in.close();
    root["settings"].erase("environment");
    std::ofstream out(path);
    out << root.dump(2);
    out.close();

    Scene b;
    SceneSerializer load(b, assets);
    ASSERT_TRUE(load.Load(path));

    // Must default to a lit scene, not an unlit one: an old file should gain
    // environment lighting, never load black. The default is the shipped sky,
    // which falls back to the procedural one if the asset is absent.
    EXPECT_EQ(b.Environment().source, EnvironmentSettings::Source::HDRi);
    EXPECT_FALSE(b.Environment().hdriPath.empty());
    EXPECT_TRUE(b.Environment().drawSkybox);

    std::remove(path);
}

// A hostile scene must not be able to aim the model loader at a file outside
// the project. Model paths flow straight from authored (possibly malicious)
// scene JSON into Assimp, whose mesh importers have a history of heap-overflow
// CVEs — so absolute paths and ".." traversal are refused BEFORE the loader
// ever opens them. The load itself still SUCCEEDS (a rejected path is not a
// parse error); only the offending model is dropped to an empty
// ModelComponent, so the rest of the scene — and the entity's parent-index
// slot — stays intact.
TEST(SceneSerializer, RejectsTraversalAndAbsoluteModelPaths) {
    const char* path = "test_scene_evil_model.json";
    for (const char* evil : { "../../evil.obj",
                              R"(..\..\evil.obj)",
                              "Exported/Model/../../../../evil.obj",
                              "C:/Windows/System32/evil.obj",
                              "/etc/passwd" }) {
        {
            nlohmann::json root;
            root["version"] = SceneSerializer::kVersion;
            nlohmann::json e;
            e["name"] = "Evil";
            e["transform"]["position"] = nlohmann::json::array({ 0, 0, 0 });
            e["model"] = evil;
            root["entities"] = nlohmann::json::array({ e });
            std::ofstream(path) << root.dump(2);
        }

        Scene s;
        AssetManager assets;
        SceneSerializer load(s, assets);
        // Rejecting a model path is graceful degradation, not a load failure.
        ASSERT_TRUE(load.Load(path)) << "load failed outright for: " << evil;

        // The entity survives, but carries NO model: the traversal/absolute
        // path was refused before it could reach the loader.
        bool found = false;
        for (auto ent : s.registry.view<Name>()) {
            if (s.registry.get<Name>(ent).value != "Evil") continue;
            found = true;
            auto* mc = s.registry.try_get<ModelComponent>(ent);
            ASSERT_NE(mc, nullptr) << "entity dropped entirely for: " << evil;
            EXPECT_EQ(mc->model, nullptr)
                << "an out-of-project model path was loaded: " << evil;
            EXPECT_FALSE(s.registry.any_of<AABB>(ent))
                << "AABB present for a rejected model: " << evil;
        }
        EXPECT_TRUE(found) << "entity missing after rejected model: " << evil;
    }
    std::remove(path);
}

// The containment gate must keep legitimate, project-relative asset paths
// loadable — the whole point of deferring this change was to not break real
// scenes. These are the shapes the shipped scene.json actually stores.
TEST(SceneSerializer, ContainmentAcceptsRelativeExportedPaths) {
    namespace fs = std::filesystem;
    fs::path out;
    // base = "" resolves against the working directory, exactly how the model
    // loader resolves these paths at runtime.
    EXPECT_TRUE(PathIsContained("", "Exported/Model/backpack.obj", out));
    EXPECT_TRUE(PathIsContained("", "Exported/Model/plane.obj", out));
    EXPECT_TRUE(PathIsContained("", "Exported/Env/kloofendal_puresky_2k.hdr", out));
    // an explicit asset-root base with a relative remainder is contained too
    EXPECT_TRUE(PathIsContained("Exported", "Model/backpack.obj", out));

    // ...and the escapes stay rejected, including a ".." buried mid-path that
    // still climbs back out of the tree.
    EXPECT_FALSE(PathIsContained("", "../../evil.obj", out));
    EXPECT_FALSE(PathIsContained("", R"(..\..\evil.obj)", out));
    EXPECT_FALSE(PathIsContained("", "Exported/Model/../../../etc/passwd", out));
    EXPECT_FALSE(PathIsContained("", "/etc/passwd", out));
    EXPECT_FALSE(PathIsContained("", "C:/Windows/System32/evil.obj", out));
    EXPECT_FALSE(PathIsContained("", R"(\\host\share\evil.obj)", out));
}

TEST(SceneSerializer, MalformedFieldTypeDoesNotCrash) {
    const char* path = "test_scene_hostile.json";
    // Wrong-TYPED fields (not a syntax error): version is an object, a
    // transform position is a string. nlohmann throws type_error on these,
    // which used to escape uncaught and crash the editor/player -- a
    // denial-of-service via a hand-edited or hostile scene file.
    const char* hostile =
        "{ \"version\": {}, \"entities\": ["
        "  { \"name\": \"x\", \"transform\": { \"position\": \"north\" } } ] }";
    std::ofstream(path) << hostile;

    AssetManager assets;
    Scene scene;
    SceneSerializer load(scene, assets);
    // Must return false, not throw. (If it threw, the process would abort and
    // this test would not complete.)
    EXPECT_FALSE(load.Load(path));

    std::remove(path);
}

TEST(SceneSerializer, RoundTripMaterialTransparency) {
    const char* path = "test_scene_transparency.json";

    // A material override needs a model to attach to; the headless stub has no
    // meshes but carries a Materials() list of the right shape for slot 0.
    Scene a;
    Entity glass = a.createEntity();
    glass.addComponent<Name>(Name{ "Glass" });
    glass.addComponent<Transform>(Transform{});
    auto model = std::make_shared<Model>("missing.obj");
    glass.addComponent<ModelComponent>(ModelComponent{ model });

    MaterialOverrides ov;
    auto mat = std::make_shared<Material>();
    mat->alphaMode = AlphaMode::Blend;
    mat->opacity = 0.4f;
    mat->alphaCutoff = 0.6f;
    mat->doubleSided = true;
    mat->baseColor = { 0.2f, 0.6f, 0.9f };
    ov.byIndex[0] = mat;
    a.registry.emplace<MaterialOverrides>(glass, std::move(ov));

    AssetManager assets;
    SceneSerializer save(a, assets);
    ASSERT_TRUE(save.Save(path));

    Scene b;
    SceneSerializer load(b, assets);
    ASSERT_TRUE(load.Load(path));

    entt::entity e = entt::null;
    for (auto [ent, n] : b.registry.view<Name>().each())
        if (n.value == "Glass") e = ent;
    ASSERT_TRUE(e != entt::null);

    const auto* mo = b.registry.try_get<MaterialOverrides>(e);
    ASSERT_NE(mo, nullptr) << "material override dropped by save/load";
    auto it = mo->byIndex.find(0);
    ASSERT_NE(it, mo->byIndex.end());
    const Material& m = *it->second;
    EXPECT_EQ(m.alphaMode, AlphaMode::Blend);
    EXPECT_FLOAT_EQ(m.opacity, 0.4f);
    EXPECT_FLOAT_EQ(m.alphaCutoff, 0.6f);
    EXPECT_TRUE(m.doubleSided);
    EXPECT_FLOAT_EQ(m.baseColor.b, 0.9f) << "existing fields must still round-trip";

    std::remove(path);
}

TEST(SceneSerializer, OldMaterialOverrideDefaultsToOpaque) {
    const char* path = "test_scene_oldmat.json";
    // A material override written before transparency existed: no alphaMode /
    // opacity keys. It must load as Opaque, not as some out-of-range garbage.
    const char* old =
        "{ \"version\": 1, \"entities\": [ { \"name\": \"Old\","
        "  \"model\": \"missing.obj\","
        "  \"materialOverrides\": [ { \"slot\": 0, \"metallic\": 0.5 } ] } ] }";
    std::ofstream(path) << old;

    AssetManager assets;
    Scene b;
    SceneSerializer load(b, assets);
    ASSERT_TRUE(load.Load(path));

    entt::entity e = entt::null;
    for (auto [ent, n] : b.registry.view<Name>().each())
        if (n.value == "Old") e = ent;
    ASSERT_TRUE(e != entt::null);
    const auto* mo = b.registry.try_get<MaterialOverrides>(e);
    ASSERT_NE(mo, nullptr);
    EXPECT_EQ(mo->byIndex.at(0)->alphaMode, AlphaMode::Opaque);

    std::remove(path);
}
