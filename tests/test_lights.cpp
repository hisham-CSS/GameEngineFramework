// Punctual light selection: the CPU half of multi-light rendering.
//
// Scene::SelectPunctualLights is deliberately a pure function of the registry
// (no GL, no renderer state) precisely so this can be tested headlessly. The
// shader array is BOUNDED, so the interesting behaviour is not "does it
// gather lights" but "when there are more lights than slots, does it keep the
// ones that actually matter".
#include <gtest/gtest.h>

#include "Engine.h"

#include <vector>

using namespace MyCoreEngine;

namespace {
    entt::entity makeLight(Scene& s, glm::vec3 pos, float intensity, float range,
                           LightType type = LightType::Point) {
        Entity e = s.createEntity();
        Transform t{};
        t.position = pos;
        e.addComponent<Transform>(t);
        LightComponent lc{};
        lc.type = type;
        lc.intensity = intensity;
        lc.range = range;
        e.addComponent<LightComponent>(lc);
        return e;
    }
}

TEST(Lights, GathersEnabledLightsInWorldSpace) {
    Scene s;
    makeLight(s, { 3.f, 4.f, 5.f }, 10.f, 20.f);
    s.UpdateTransforms();

    std::vector<PunctualLight> out;
    Scene::SelectPunctualLights(s.registry, glm::vec3(0.f), out);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_FLOAT_EQ(out[0].position.x, 3.f);
    EXPECT_FLOAT_EQ(out[0].position.z, 5.f);
    EXPECT_FLOAT_EQ(out[0].range, 20.f);
    EXPECT_EQ(out[0].type, static_cast<int>(LightType::Point));
}

TEST(Lights, DisabledAndZeroContributionLightsAreDropped) {
    Scene s;
    auto off = makeLight(s, { 0.f, 1.f, 0.f }, 10.f, 20.f);
    s.registry.get<LightComponent>(off).enabled = false;
    makeLight(s, { 1.f, 1.f, 0.f }, 0.f, 20.f);   // no intensity
    makeLight(s, { 2.f, 1.f, 0.f }, 10.f, 0.f);   // no range
    makeLight(s, { 3.f, 1.f, 0.f }, 10.f, 20.f);  // the only real one
    s.UpdateTransforms();

    std::vector<PunctualLight> out;
    unsigned culled = 0;
    Scene::SelectPunctualLights(s.registry, glm::vec3(0.f), out,
                                Scene::kMaxPunctualLights, &culled);
    EXPECT_EQ(out.size(), 1u);
    EXPECT_EQ(culled, 3u) << "disabled / zero-intensity / zero-range must be reported";
}

// The whole point of the bounded array: overflow must keep the STRONGEST
// lights, not an arbitrary prefix of registry order.
TEST(Lights, OverflowKeepsTheMostInfluentialLights) {
    Scene s;
    // 30 dim lights far away, created FIRST so registry order favours them
    for (int i = 0; i < 30; ++i) {
        makeLight(s, { 500.f + float(i), 0.f, 0.f }, 1.f, 1000.f);
    }
    // one bright light right next to the camera, created LAST
    const auto bright = makeLight(s, { 0.f, 2.f, 0.f }, 5000.f, 1000.f);
    s.UpdateTransforms();

    std::vector<PunctualLight> out;
    Scene::SelectPunctualLights(s.registry, glm::vec3(0.f), out);
    ASSERT_EQ(out.size(), Scene::kMaxPunctualLights);

    const glm::vec3 brightPos = s.registry.get<Transform>(bright).position;
    bool keptBright = false;
    for (const auto& l : out) {
        if (glm::length(l.position - brightPos) < 1e-3f) keptBright = true;
    }
    EXPECT_TRUE(keptBright)
        << "the nearby bright light was dropped in favour of distant dim ones";
}

TEST(Lights, RespectsAnExplicitLimitAndReportsTheRemainder) {
    Scene s;
    for (int i = 0; i < 10; ++i) makeLight(s, { float(i), 0.f, 0.f }, 10.f, 50.f);
    s.UpdateTransforms();

    std::vector<PunctualLight> out;
    unsigned culled = 0;
    Scene::SelectPunctualLights(s.registry, glm::vec3(0.f), out, 4, &culled);
    EXPECT_EQ(out.size(), 4u);
    EXPECT_EQ(culled, 6u);

    // a zero budget is legal and yields nothing rather than crashing
    Scene::SelectPunctualLights(s.registry, glm::vec3(0.f), out, 0, &culled);
    EXPECT_TRUE(out.empty());
}

TEST(Lights, SpotConeIsResolvedAndNeverInverted) {
    Scene s;
    const auto e = makeLight(s, { 0.f, 5.f, 0.f }, 10.f, 20.f, LightType::Spot);
    auto& lc = s.registry.get<LightComponent>(e);
    // outer SMALLER than inner: an inverted cone would make the shader's
    // smoothstep run backwards and light everything outside the cone
    lc.innerAngleDeg = 40.f;
    lc.outerAngleDeg = 10.f;
    s.UpdateTransforms();

    std::vector<PunctualLight> out;
    Scene::SelectPunctualLights(s.registry, glm::vec3(0.f), out);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].type, static_cast<int>(LightType::Spot));
    // cos decreases with angle, so outer >= inner in ANGLE means
    // cosOuter <= cosInner
    EXPECT_LE(out[0].cosOuter, out[0].cosInner)
        << "inverted cone was not clamped";
    // default identity orientation aims down -Z
    EXPECT_NEAR(out[0].spotDir.z, -1.f, 1e-3f);
}

// Same trap that put physics bodies at the origin: modelMatrix is a CACHE.
TEST(Lights, WorldPositionIsCorrectWithDirtyTransforms) {
    Scene s;
    makeLight(s, { 7.f, 8.f, 9.f }, 10.f, 50.f);
    // deliberately NO UpdateTransforms: caches are still identity

    std::vector<PunctualLight> out;
    Scene::SelectPunctualLights(s.registry, glm::vec3(0.f), out);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_FLOAT_EQ(out[0].position.x, 7.f)
        << "light built from a stale identity matrix instead of its real pose";
    EXPECT_FLOAT_EQ(out[0].position.z, 9.f);
}
