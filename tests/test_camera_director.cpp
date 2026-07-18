// CameraDirector (Cinemachine-style stub): headless tests — selection,
// override, and blending are pure registry/math code, no GL needed.
#include <gtest/gtest.h>

#include <algorithm>

#include "Engine.h"

using namespace MyCoreEngine;

namespace {

entt::entity makeCamera(Scene& scene, glm::vec3 pos, int priority,
                        float fovDeg = 60.f, const char* name = "Cam") {
    Entity e = scene.createEntity();
    e.addComponent<Name>(Name{ name });
    Transform t{};
    t.position = pos;
    e.addComponent<Transform>(t);
    CameraComponent cc;
    cc.fovDeg = fovDeg;
    cc.priority = priority;
    scene.registry.emplace<CameraComponent>((entt::entity)e, cc);
    return e;
}

void expectVec3Near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f) {
    EXPECT_NEAR(a.x, b.x, eps);
    EXPECT_NEAR(a.y, b.y, eps);
    EXPECT_NEAR(a.z, b.z, eps);
}

} // namespace

TEST(CameraDirector, NoCameraReturnsFalseAndLeavesCameraAlone) {
    Scene scene;
    CameraDirector d;
    Camera cam;
    cam.Position = { 7.f, 8.f, 9.f };
    EXPECT_FALSE(d.Update(scene.registry, 0.016f, cam));
    expectVec3Near(cam.Position, { 7.f, 8.f, 9.f }); // fallback view untouched
    EXPECT_TRUE(d.activeCamera() == entt::null);

    // a camera without a Transform is not usable either
    auto bare = scene.createEntity();
    scene.registry.emplace<CameraComponent>((entt::entity)bare, CameraComponent{});
    EXPECT_FALSE(d.Update(scene.registry, 0.016f, cam));
}

TEST(CameraDirector, CutsToHighestPriorityEnabledCamera) {
    Scene scene;
    makeCamera(scene, { 0.f, 0.f, 0.f }, 0, 60.f, "Low");
    auto hi = makeCamera(scene, { 5.f, 1.f, 2.f }, 10, 85.f, "High");
    auto& hc = scene.registry.get<CameraComponent>(hi);
    hc.nearClip = 0.5f;
    hc.farClip = 200.f;
    scene.UpdateTransforms();

    CameraDirector d; // blends off: every switch is a hard cut
    Camera cam;
    ASSERT_TRUE(d.Update(scene.registry, 0.016f, cam));
    EXPECT_EQ(d.activeCamera(), hi);
    EXPECT_FALSE(d.blending());
    expectVec3Near(cam.Position, { 5.f, 1.f, 2.f });
    expectVec3Near(cam.Front, { 0.f, 0.f, -1.f }); // identity looks down -Z
    EXPECT_FLOAT_EQ(cam.Zoom, 85.f);
    EXPECT_FLOAT_EQ(cam.NearClip, 0.5f);
    EXPECT_FLOAT_EQ(cam.FarClip, 200.f);
}

TEST(CameraDirector, OverrideBeatsPriorityAndInvalidOverrideFallsBack) {
    Scene scene;
    auto lo = makeCamera(scene, { 0.f, 0.f, 0.f }, 0, 60.f, "Low");
    auto hi = makeCamera(scene, { 5.f, 0.f, 0.f }, 10, 60.f, "High");
    scene.UpdateTransforms();

    CameraDirector d;
    Camera cam;
    d.setOverride(lo);
    ASSERT_TRUE(d.Update(scene.registry, 0.016f, cam));
    EXPECT_EQ(d.activeCamera(), lo);
    expectVec3Near(cam.Position, { 0.f, 0.f, 0.f });

    // override loses its CameraComponent: automatic selection resumes,
    // but the override itself is remembered (not cleared)
    scene.registry.remove<CameraComponent>(lo);
    ASSERT_TRUE(d.Update(scene.registry, 0.016f, cam));
    EXPECT_EQ(d.activeCamera(), hi);
    EXPECT_EQ(d.overrideCamera(), lo);

    // ...and wins again the moment it is valid
    scene.registry.emplace<CameraComponent>(lo, CameraComponent{});
    ASSERT_TRUE(d.Update(scene.registry, 0.016f, cam));
    EXPECT_EQ(d.activeCamera(), lo);
}

TEST(CameraDirector, BlendInterpolatesThenLandsExactlyOnTarget) {
    Scene scene;
    auto a = makeCamera(scene, { 0.f, 0.f, 0.f }, 0, 60.f, "A");
    auto b = makeCamera(scene, { 10.f, 0.f, 0.f }, 0, 40.f, "B");
    scene.UpdateTransforms();
    (void)a;

    CameraDirector d;
    d.setDefaultBlendSeconds(1.f);
    Camera cam;
    ASSERT_TRUE(d.Update(scene.registry, 0.016f, cam)); // settle on A (cut: first camera)
    expectVec3Near(cam.Position, { 0.f, 0.f, 0.f });

    scene.registry.get<CameraComponent>(b).priority = 5; // gameplay-style switch
    float lastX = 0.f;
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(d.Update(scene.registry, 0.25f, cam));
        EXPECT_TRUE(d.blending());
        EXPECT_GT(cam.Position.x, lastX);   // monotonic toward the target
        EXPECT_LT(cam.Position.x, 10.f);    // strictly between the poses
        EXPECT_GT(cam.Zoom, 40.f);          // fov blends too
        EXPECT_LT(cam.Zoom, 60.f + 1e-3f);
        lastX = cam.Position.x;
    }
    ASSERT_TRUE(d.Update(scene.registry, 0.25f, cam)); // t reaches 1
    EXPECT_FALSE(d.blending());
    expectVec3Near(cam.Position, { 10.f, 0.f, 0.f }); // exactly on target
    EXPECT_FLOAT_EQ(cam.Zoom, 40.f);
    EXPECT_EQ(d.activeCamera(), b);
}

TEST(CameraDirector, MidBlendSwitchContinuesFromCurrentOutput) {
    Scene scene;
    makeCamera(scene, { 0.f, 0.f, 0.f }, 0, 60.f, "A");
    auto b = makeCamera(scene, { 10.f, 0.f, 0.f }, 0, 60.f, "B");
    auto c = makeCamera(scene, { 0.f, 10.f, 0.f }, 0, 60.f, "C");
    scene.UpdateTransforms();

    CameraDirector d;
    d.setDefaultBlendSeconds(1.f);
    Camera cam;
    ASSERT_TRUE(d.Update(scene.registry, 0.016f, cam)); // settle on A

    scene.registry.get<CameraComponent>(b).priority = 5;
    ASSERT_TRUE(d.Update(scene.registry, 0.5f, cam)); // halfway toward B
    const glm::vec3 midPose = cam.Position;

    scene.registry.get<CameraComponent>(c).priority = 9; // switch again mid-blend
    ASSERT_TRUE(d.Update(scene.registry, 0.05f, cam));
    // the new blend starts from the on-screen pose, so one small step later
    // the view is still near it — no pop back to A or jump to C
    EXPECT_LT(glm::length(cam.Position - midPose), 1.5f);
    EXPECT_EQ(d.activeCamera(), c);
}

TEST(CameraDirector, CutSkipsBlendingAfterBulkRestore) {
    Scene scene;
    makeCamera(scene, { 0.f, 0.f, 0.f }, 0, 60.f, "A");
    auto b = makeCamera(scene, { 10.f, 0.f, 0.f }, 0, 60.f, "B");
    scene.UpdateTransforms();

    CameraDirector d;
    d.setDefaultBlendSeconds(1.f);
    Camera cam;
    ASSERT_TRUE(d.Update(scene.registry, 0.016f, cam)); // settle on A

    d.cut(); // e.g. play-stop restored the scene wholesale
    scene.registry.get<CameraComponent>(b).priority = 5;
    ASSERT_TRUE(d.Update(scene.registry, 0.016f, cam));
    EXPECT_FALSE(d.blending());
    expectVec3Near(cam.Position, { 10.f, 0.f, 0.f }); // hard cut, no blend
}

TEST(CameraDirector, DisabledActiveCameraHandsOffToNext) {
    Scene scene;
    auto a = makeCamera(scene, { 0.f, 0.f, 0.f }, 5, 60.f, "A");
    auto b = makeCamera(scene, { 4.f, 0.f, 0.f }, 0, 60.f, "B");
    scene.UpdateTransforms();

    CameraDirector d;
    Camera cam;
    ASSERT_TRUE(d.Update(scene.registry, 0.016f, cam));
    EXPECT_EQ(d.activeCamera(), a);

    scene.registry.get<CameraComponent>(a).enabled = false;
    ASSERT_TRUE(d.Update(scene.registry, 0.016f, cam));
    EXPECT_EQ(d.activeCamera(), b);
    expectVec3Near(cam.Position, { 4.f, 0.f, 0.f });
}

TEST(CameraDirector, TieBreakUsesEntityIndexNotRawHandle) {
    Scene scene;
    // recycle a slot so one camera carries a bumped version: raw-handle
    // comparison would put version bits above the index and flip the winner
    // relative to a freshly-loaded scene (where versions reset to 0)
    Entity tmp = scene.createEntity();
    scene.registry.destroy(tmp);
    auto recycled = makeCamera(scene, { 0.f, 0.f, 0.f }, 0, 60.f, "Recycled");
    auto fresh = makeCamera(scene, { 1.f, 0.f, 0.f }, 0, 60.f, "Fresh");
    scene.UpdateTransforms();
    ASSERT_EQ(entt::to_entity(recycled), entt::to_entity((entt::entity)tmp))
        << "precondition: the destroyed slot was reused";
    ASSERT_LT(entt::to_entity(recycled), entt::to_entity(fresh));
    EXPECT_EQ(CameraDirector::SelectCamera(scene.registry), recycled)
        << "lowest entity INDEX wins the tie, whatever the version bits say";
}

TEST(CameraDirector, ResetForgetsActiveOverrideAndBlend) {
    Scene scene;
    auto a = makeCamera(scene, { 0.f, 0.f, 0.f }, 0, 60.f, "A");
    scene.UpdateTransforms();

    CameraDirector d;
    d.setOverride(a);
    d.setDefaultBlendSeconds(2.f);
    Camera cam;
    ASSERT_TRUE(d.Update(scene.registry, 0.016f, cam));

    d.reset();
    EXPECT_TRUE(d.activeCamera() == entt::null);
    EXPECT_TRUE(d.overrideCamera() == entt::null);
    EXPECT_FALSE(d.blending());
    // blend duration is configuration, not state: it survives
    EXPECT_FLOAT_EQ(d.defaultBlendSeconds(), 2.f);
}
