// Transform hierarchy (P2-8): headless tests — no GL needed, Scene's
// UpdateTransforms and the reparenting helpers are pure registry/math code.
#include <gtest/gtest.h>

#include "Engine.h"

using namespace MyCoreEngine;

namespace {

entt::entity makeNode(Scene& scene, glm::vec3 pos, const char* name = "Node") {
    Entity e = scene.createEntity();
    e.addComponent<Name>(Name{ name });
    Transform t{};
    t.position = pos;
    e.addComponent<Transform>(t);
    return e;
}

glm::vec3 worldPos(Scene& scene, entt::entity e) {
    return glm::vec3(scene.registry.get<Transform>(e).modelMatrix[3]);
}

void expectVec3Near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f) {
    EXPECT_NEAR(a.x, b.x, eps);
    EXPECT_NEAR(a.y, b.y, eps);
    EXPECT_NEAR(a.z, b.z, eps);
}

} // namespace

TEST(Hierarchy, ChildWorldFollowsParent) {
    Scene scene;
    auto parent = makeNode(scene, { 10.f, 0.f, 0.f }, "P");
    auto child = makeNode(scene, { 0.f, 5.f, 0.f }, "C");
    scene.registry.emplace<Parent>(child, Parent{ parent });

    scene.UpdateTransforms();
    expectVec3Near(worldPos(scene, child), { 10.f, 5.f, 0.f });

    // move the parent only: the child's LOCAL is untouched (not dirty) but
    // its world must follow
    auto& pt = scene.registry.get<Transform>(parent);
    pt.position = { 20.f, 0.f, 0.f };
    pt.dirty = true;
    scene.UpdateTransforms();
    expectVec3Near(worldPos(scene, child), { 20.f, 5.f, 0.f });
}

TEST(Hierarchy, RotationPropagates) {
    Scene scene;
    auto parent = makeNode(scene, { 0.f, 0.f, 0.f }, "P");
    auto child = makeNode(scene, { 1.f, 0.f, 0.f }, "C");
    scene.registry.emplace<Parent>(child, Parent{ parent });

    auto& pt = scene.registry.get<Transform>(parent);
    pt.rotation.y = 90.f; // +X rotates toward -Z
    pt.dirty = true;
    scene.UpdateTransforms();
    expectVec3Near(worldPos(scene, child), { 0.f, 0.f, -1.f });
}

TEST(Hierarchy, GrandchildChainPropagates) {
    Scene scene;
    auto a = makeNode(scene, { 1.f, 0.f, 0.f }, "A");
    auto b = makeNode(scene, { 0.f, 2.f, 0.f }, "B");
    auto c = makeNode(scene, { 0.f, 0.f, 3.f }, "C");
    scene.registry.emplace<Parent>(b, Parent{ a });
    scene.registry.emplace<Parent>(c, Parent{ b });

    scene.UpdateTransforms();
    expectVec3Near(worldPos(scene, c), { 1.f, 2.f, 3.f });

    auto& at = scene.registry.get<Transform>(a);
    at.position.x = 11.f;
    at.dirty = true;
    scene.UpdateTransforms();
    expectVec3Near(worldPos(scene, c), { 11.f, 2.f, 3.f });
}

TEST(Hierarchy, SetParentKeepsWorldPose) {
    Scene scene;
    auto parent = makeNode(scene, { 5.f, 0.f, 0.f }, "P");
    scene.registry.get<Transform>(parent).rotation.y = 45.f;
    auto child = makeNode(scene, { 3.f, 4.f, 5.f }, "C");
    scene.registry.get<Transform>(child).rotation = { 10.f, 20.f, 30.f };
    scene.UpdateTransforms();
    const glm::mat4 before = scene.registry.get<Transform>(child).modelMatrix;

    ASSERT_TRUE(SetParentKeepWorld(scene.registry, child, parent));
    scene.UpdateTransforms();
    const glm::mat4 after = scene.registry.get<Transform>(child).modelMatrix;
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            EXPECT_NEAR(before[c][r], after[c][r], 1e-3f) << "col " << c << " row " << r;

    // ...and unparenting restores a root with the same world pose
    ASSERT_TRUE(SetParentKeepWorld(scene.registry, child, entt::null));
    EXPECT_FALSE(scene.registry.any_of<Parent>(child));
    scene.UpdateTransforms();
    const glm::mat4 unparented = scene.registry.get<Transform>(child).modelMatrix;
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            EXPECT_NEAR(before[c][r], unparented[c][r], 1e-3f);
}

TEST(Hierarchy, DecomposeTRSRoundTripsCompoundRotations) {
    // ImGuizmo's decompose uses a different euler order than localMatrix's
    // Y*X*Z rebuild — compound rotations visibly re-oriented on any gizmo
    // drag until the engine grew its own convention-matched decompose.
    Transform t{};
    t.position = { 3.f, -2.f, 7.f };
    t.rotation = { 25.f, 40.f, 30.f }; // compound: all three axes
    t.scale = { 2.f, 0.5f, 1.5f };
    const glm::mat4 m = t.localMatrix();

    glm::vec3 pos, rotDeg, scale;
    DecomposeTRS(m, pos, rotDeg, scale);
    Transform rebuilt{};
    rebuilt.position = pos;
    rebuilt.rotation = rotDeg;
    rebuilt.scale = scale;
    const glm::mat4 m2 = rebuilt.localMatrix();

    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            EXPECT_NEAR(m[c][r], m2[c][r], 1e-4f) << "col " << c << " row " << r;
}

TEST(Hierarchy, RefusesCycles) {
    Scene scene;
    auto a = makeNode(scene, { 0.f, 0.f, 0.f }, "A");
    auto b = makeNode(scene, { 1.f, 0.f, 0.f }, "B");
    auto c = makeNode(scene, { 2.f, 0.f, 0.f }, "C");
    ASSERT_TRUE(SetParentKeepWorld(scene.registry, b, a));
    ASSERT_TRUE(SetParentKeepWorld(scene.registry, c, b));

    EXPECT_FALSE(SetParentKeepWorld(scene.registry, a, a)); // self
    EXPECT_FALSE(SetParentKeepWorld(scene.registry, a, c)); // ancestor under descendant
    EXPECT_FALSE(scene.registry.any_of<Parent>(a));
}

TEST(Hierarchy, DanglingParentActsAsRoot) {
    Scene scene;
    auto parent = makeNode(scene, { 10.f, 0.f, 0.f }, "P");
    auto child = makeNode(scene, { 1.f, 2.f, 3.f }, "C");
    scene.registry.emplace<Parent>(child, Parent{ parent });
    scene.UpdateTransforms();

    scene.registry.destroy(parent);
    auto& ct = scene.registry.get<Transform>(child);
    ct.dirty = true;
    scene.UpdateTransforms(); // must not crash; child treated as root
    expectVec3Near(worldPos(scene, child), { 1.f, 2.f, 3.f });
}

TEST(Hierarchy, MovedParentInvalidatesChildCasterShadows) {
    Scene scene;
    auto parent = makeNode(scene, { 0.f, 0.f, -50.f }, "P");
    auto child = makeNode(scene, { 0.f, 1.f, 0.f }, "C");
    scene.registry.emplace<Parent>(child, Parent{ parent });
    // component-complete caster (null model is fine — flags only)
    scene.registry.emplace<ModelComponent>(child, ModelComponent{});
    scene.registry.emplace<AABB>(child, AABB{ glm::vec3(-1.f), glm::vec3(1.f) });
    scene.UpdateTransforms();

    // only the PARENT moves; the child caster's world changes and must
    // register with the dynamic-shadow invalidation
    auto& pt = scene.registry.get<Transform>(parent);
    pt.position.z = -60.f;
    pt.dirty = true;
    scene.UpdateTransforms();

    const glm::vec3 camPos(0.f);
    const glm::vec3 camFwd(0.f, 0.f, -1.f);
    const glm::vec3 sun = glm::normalize(glm::vec3(-0.3f, -1.f, -0.2f));
    EXPECT_TRUE(scene.HasDynamicCasterInViewRange(camPos, camFwd, 0.1f, 200.f, sun))
        << "child caster moved via parent but no dirty-caster sphere was recorded";
}
