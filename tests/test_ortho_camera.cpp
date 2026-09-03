// M3.2g: the orthographic camera mode (ADR-019 D4). Headless -- the
// projection matrix, the culling frustum, the component-to-camera sync and
// the director's blend are all pure math over a registry. The pixel-level
// proof (OrthoCamera.AUnitQuadCoversThePredictedPixels) lives with the GL
// fixtures in test_render_passes.cpp.
#include <gtest/gtest.h>

#include "Engine.h"

#include <glm/gtc/matrix_transform.hpp>

using namespace MyCoreEngine;

namespace {

glm::vec3 ndcOf(const glm::mat4& proj, const glm::vec3& viewPos) {
    const glm::vec4 c = proj * glm::vec4(viewPos, 1.0f);
    return glm::vec3(c) / c.w;
}

Transform placedAt(const glm::vec3& p) {
    Transform t{};
    t.position = p;
    t.updateMatrix();
    return t;
}

} // namespace

// The one camera-side field ADR-019 D4 admits: an orthographic camera whose
// half-height reaches the viewport's top and bottom edge exactly, whose
// half-width is that times the aspect, and whose image does not shrink with
// distance. The perspective matrix is byte-for-byte what the renderer built
// before the mode existed.
TEST(OrthoCamera, TheProjectionMapsHalfHeightToTheViewportEdge) {
    Camera cam;
    cam.Projection = CameraProjection::Orthographic;
    cam.OrthoHalfHeight = 3.0f;
    cam.NearClip = 0.5f;
    cam.FarClip = 40.0f;
    const float aspect = 2.0f;
    const glm::mat4 P = cam.GetProjectionMatrix(aspect);

    // view space looks down -Z: +-halfHeight lands on y = +-1, +-halfHeight*aspect on x = +-1
    EXPECT_NEAR(ndcOf(P, { 0.f, 3.f, -10.f }).y, 1.f, 1e-5f);
    EXPECT_NEAR(ndcOf(P, { 0.f, -3.f, -10.f }).y, -1.f, 1e-5f);
    EXPECT_NEAR(ndcOf(P, { 6.f, 0.f, -10.f }).x, 1.f, 1e-5f);
    EXPECT_NEAR(ndcOf(P, { -6.f, 0.f, -10.f }).x, -1.f, 1e-5f);
    // parallel: the same point twice as far is the same pixel
    EXPECT_NEAR(ndcOf(P, { 1.5f, 1.5f, -2.f }).x, ndcOf(P, { 1.5f, 1.5f, -30.f }).x, 1e-5f);
    EXPECT_NEAR(ndcOf(P, { 1.5f, 1.5f, -2.f }).y, ndcOf(P, { 1.5f, 1.5f, -30.f }).y, 1e-5f);
    // the clip planes still bound the depth range
    EXPECT_NEAR(ndcOf(P, { 0.f, 0.f, -0.5f }).z, -1.f, 1e-5f);
    EXPECT_NEAR(ndcOf(P, { 0.f, 0.f, -40.f }).z, 1.f, 1e-4f);
    EXPECT_EQ(P, glm::ortho(-6.f, 6.f, -3.f, 3.f, 0.5f, 40.f));

    Camera persp;
    EXPECT_EQ(persp.Projection, CameraProjection::Perspective) << "a default camera is perspective";
    // The reference is built the way the renderer always built it: radians of
    // a RUNTIME fov. A literal here lets GCC fold glm::radians(60.f) at higher
    // precision and the two differ by one ULP in one element (measured on the
    // Linux CI job) -- which would be a fact about the test, not the camera.
    volatile float fovDeg = 60.f;
    persp.Zoom = fovDeg;
    persp.NearClip = 0.1f;
    persp.FarClip = 1000.f;
    const float fovRuntime = fovDeg;
    EXPECT_EQ(persp.GetProjectionMatrix(1.5f), glm::perspective(glm::radians(fovRuntime), 1.5f, 0.1f, 1000.f))
        << "the perspective matrix must not change: every scene rendered before this mode existed relies on it";
}

// Culling reads the same box the projection draws: side planes parallel to
// the view axis at +-halfHeight and +-halfHeight*aspect. The two placements a
// perspective frustum decides differently -- wide and far (perspective sees
// it), wide and near (perspective culls it) -- pin the difference.
TEST(OrthoCamera, TheCullingFrustumIsAParallelBox) {
    Camera cam; // default: at the origin, looking down -Z, Right +X, Up +Y
    cam.Projection = CameraProjection::Orthographic;
    cam.OrthoHalfHeight = 4.0f;
    cam.NearClip = 0.1f;
    cam.FarClip = 100.0f;
    const float aspect = 2.0f; // half-width 8
    const Frustum f = createFrustumFromCamera(cam, aspect, glm::radians(cam.Zoom), cam.NearClip, cam.FarClip);
    const AABB unit{ glm::vec3(-0.5f), glm::vec3(0.5f) };

    EXPECT_TRUE(unit.isOnFrustum(f, placedAt({ 7.f, 3.f, -90.f })))  << "inside the box near the far plane";
    EXPECT_TRUE(unit.isOnFrustum(f, placedAt({ 7.f, 3.f, -1.f })))   << "inside the box next to the camera: a perspective frustum would cull this";
    EXPECT_FALSE(unit.isOnFrustum(f, placedAt({ 9.f, 0.f, -90.f })))  << "right of the box: a perspective frustum would see this";
    EXPECT_FALSE(unit.isOnFrustum(f, placedAt({ 0.f, 5.f, -50.f })))  << "above the box";
    EXPECT_FALSE(unit.isOnFrustum(f, placedAt({ 0.f, 0.f, -101.f }))) << "past the far plane";
    EXPECT_FALSE(unit.isOnFrustum(f, placedAt({ 0.f, 0.f, 1.f })))    << "behind the camera";

    // the perspective frustum is untouched by the mode's existence
    Camera persp;
    persp.Zoom = 45.f;
    const Frustum pf = createFrustumFromCamera(persp, 1.0f, glm::radians(45.f), 0.1f, 100.f);
    EXPECT_TRUE(unit.isOnFrustum(pf, placedAt({ 9.f, 0.f, -90.f })));
    EXPECT_FALSE(unit.isOnFrustum(pf, placedAt({ 7.f, 3.f, -1.f })));
}

// The component's mode and half-height reach the Camera through the same
// sync the lens already uses, clamped the way near is: a zero half-height is
// a division by zero in the ortho matrix.
TEST(OrthoCamera, SyncCopiesTheProjectionIntoTheCamera) {
    Scene scene;
    Entity e = scene.createEntity();
    e.addComponent<Transform>(placedAt({ 1.f, 2.f, 3.f }));
    CameraComponent cc;
    cc.projection = CameraProjection::Orthographic;
    cc.orthoHalfHeight = 0.0f; // hand-edited nonsense
    scene.registry.emplace<CameraComponent>((entt::entity)e, cc);
    scene.UpdateTransforms();

    Camera cam;
    ASSERT_TRUE(SyncCameraFromEntity(scene.registry, (entt::entity)e, cam));
    EXPECT_EQ(cam.Projection, CameraProjection::Orthographic);
    EXPECT_GE(cam.OrthoHalfHeight, 1e-3f) << "a degenerate half-height must be clamped, not projected";

    Entity p = scene.createEntity();
    p.addComponent<Transform>(placedAt({ 0.f, 0.f, 0.f }));
    scene.registry.emplace<CameraComponent>((entt::entity)p, CameraComponent{});
    scene.UpdateTransforms();
    Camera cam2;
    cam2.Projection = CameraProjection::Orthographic; // stale from a previous camera
    ASSERT_TRUE(SyncCameraFromEntity(scene.registry, (entt::entity)p, cam2));
    EXPECT_EQ(cam2.Projection, CameraProjection::Perspective) << "the sync must write the mode every time, not only when it is orthographic";
    EXPECT_FLOAT_EQ(cam2.OrthoHalfHeight, CameraComponent{}.orthoHalfHeight);
}

// A projection mode is discrete, so a blend between a perspective and an
// orthographic camera keeps the outgoing mode until the midpoint and the
// incoming one after it, while the half-height (and the fov) blend on their
// own so whichever mode is live lands smoothly on its target.
TEST(OrthoCamera, TheDirectorSwitchesModeAtTheBlendMidpoint) {
    Scene scene;
    Entity persp = scene.createEntity();
    persp.addComponent<Transform>(placedAt({ 0.f, 0.f, 10.f }));
    CameraComponent pc;
    pc.priority = 0;
    pc.orthoHalfHeight = 12.f; // irrelevant to a perspective camera, but the blend source for the half-height
    scene.registry.emplace<CameraComponent>((entt::entity)persp, pc);

    Entity ortho = scene.createEntity();
    ortho.addComponent<Transform>(placedAt({ 0.f, 0.f, 20.f }));
    CameraComponent oc;
    oc.priority = 0;
    oc.enabled = false;
    oc.projection = CameraProjection::Orthographic;
    oc.orthoHalfHeight = 8.f;
    scene.registry.emplace<CameraComponent>((entt::entity)ortho, oc);
    scene.UpdateTransforms();

    CameraDirector d;
    d.setDefaultBlendSeconds(1.0f);
    Camera cam;
    ASSERT_TRUE(d.Update(scene.registry, 0.016f, cam)); // first camera: a cut
    EXPECT_EQ(cam.Projection, CameraProjection::Perspective);
    EXPECT_FLOAT_EQ(cam.OrthoHalfHeight, 12.f);

    auto& occ = scene.registry.get<CameraComponent>((entt::entity)ortho);
    occ.enabled = true;
    occ.priority = 10;
    ASSERT_TRUE(d.Update(scene.registry, 0.25f, cam)); // t = 0.25
    EXPECT_TRUE(d.blending());
    EXPECT_EQ(cam.Projection, CameraProjection::Perspective) << "before the midpoint the outgoing mode stays";
    EXPECT_GT(cam.OrthoHalfHeight, 8.f);
    EXPECT_LT(cam.OrthoHalfHeight, 12.f);

    ASSERT_TRUE(d.Update(scene.registry, 0.25f, cam)); // t = 0.5
    EXPECT_TRUE(d.blending());
    EXPECT_EQ(cam.Projection, CameraProjection::Orthographic) << "from the midpoint on, the incoming mode";
    EXPECT_NEAR(cam.OrthoHalfHeight, 10.f, 1e-4f) << "smoothstep(0.5) is 0.5: the half-height is halfway";

    ASSERT_TRUE(d.Update(scene.registry, 1.0f, cam)); // done
    EXPECT_FALSE(d.blending());
    EXPECT_EQ(cam.Projection, CameraProjection::Orthographic);
    EXPECT_FLOAT_EQ(cam.OrthoHalfHeight, 8.f);
}
