// THE RECONCILER'S ENGINE HALF, HEADLESS (ROADMAP M3.4c; ADR-019 D4, D9).
//
// Two of M3.4c's eight properties need Engine types -- a registry with camera
// components for the director to choose from, and SamplePalette for the bytes
// a fighter's palette must equal -- so they live here, linked with Engine and
// the title's FightScene, with no window: Model::Decode is the GL-free half
// of the model pipeline and CameraDirector::SelectCamera is pure registry math.
// The other six are arithmetic over GameState and live in
// test_fight_presentation, which links no Engine at all.
#include <gtest/gtest.h>

#include "Engine.h"
#include "FightScene.h"
#include "cse/presentation/FightPresentation.h"

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace MyCoreEngine;

namespace {

std::string modelFixturesDir() {
    namespace fs = std::filesystem;
    fs::path here = fs::current_path();
    for (int i = 0; i < 8; ++i) {
        const fs::path candidate = here / "tests" / "fixtures" / "models";
        if (fs::exists(candidate / "two_bone_strip.gltf")) return candidate.string();
        if (!here.has_parent_path() || here.parent_path() == here) break;
        here = here.parent_path();
    }
    return "tests/fixtures/models";
}

entt::entity hostCamera(Scene& scene, int priority, bool enabled) {
    Entity e = scene.createEntity();
    Transform t{};
    t.updateMatrix();
    e.addComponent<Transform>(t);
    CameraComponent cc{};
    cc.priority = priority;
    cc.enabled = enabled;
    scene.registry.emplace<CameraComponent>((entt::entity)e, cc);
    return (entt::entity)e;
}

} // namespace

// ADR-019 D5: a camera priority that outranks every camera in the host scene.
// The shipped scenes author 0; a host that authored the look's own number
// would win the director's tie by entity index, so the fight camera takes one
// more than the highest enabled host camera when that is higher.
TEST(FightPresentation, TheFightCameraOutranksEveryCameraInTheHostScene) {
    Scene scene;
    hostCamera(scene, 0, true);
    hostCamera(scene, 5, true);
    hostCamera(scene, 99, true);
    hostCamera(scene, 100, true);    // the look's own priority, created FIRST
    hostCamera(scene, 500, false);   // disabled cameras are never selected and do not count

    cse::presentation::FightLook look{};
    ASSERT_EQ(look.cameraPriority, 100);
    const entt::entity cam = untitledfighter::CreateFightCamera(scene, look);
    scene.UpdateTransforms();

    EXPECT_EQ(CameraDirector::SelectCamera(scene.registry), cam) << "the director picked a host camera";
    EXPECT_EQ(FindActiveCamera(scene.registry), cam);
    const auto& cc = scene.registry.get<CameraComponent>(cam);
    EXPECT_EQ(cc.priority, 101) << "one above the highest enabled host camera";
    EXPECT_EQ(cc.projection, CameraProjection::Orthographic);
    EXPECT_TRUE(cc.enabled);
    EXPECT_GT(cc.farClip, cc.nearClip);

    // in a scene with no cameras the look's own priority stands
    Scene empty;
    const entt::entity solo = untitledfighter::CreateFightCamera(empty, look);
    EXPECT_EQ(empty.registry.get<CameraComponent>(solo).priority, 100);
    // and a camera created into the look's registry AFTER the fight camera at a
    // higher priority is the host's business: the reconciler re-asserts on the
    // next Create, which is what a scene swap triggers
    Scene swapped;
    const entt::entity first = untitledfighter::CreateFightCamera(swapped, look);
    hostCamera(swapped, 250, true);
    EXPECT_NE(CameraDirector::SelectCamera(swapped.registry), first);
    const entt::entity again = untitledfighter::CreateFightCamera(swapped, look);
    EXPECT_EQ(CameraDirector::SelectCamera(swapped.registry), again);
    EXPECT_EQ(swapped.registry.get<CameraComponent>(again).priority, 251);
}

// ADR-019 D3: the palette a fighter wears is SamplePalette's bytes at the
// selected frame -- no blend, no remembered pose, nothing between the clip and
// the UBO. No clip, or a clip the model lacks, is the rest pose.
TEST(FightPresentation, ThePaletteBytesEqualSamplePaletteAtTheSelectedFrame) {
    const ModelCPUData cpu = Model::Decode(modelFixturesDir() + "/two_bone_strip.gltf");
    ASSERT_TRUE(cpu.valid) << cpu.importError;
    ASSERT_FALSE(cpu.skeleton.Empty());
    const Clip* fourteen = cpu.clips.Find("fourteen");
    ASSERT_NE(fourteen, nullptr);

    const cse::presentation::ClipRef ref{ "fourteen", fourteen->frames };
    for (std::uint32_t frame = 0; frame < fourteen->frames + 2; ++frame) {   // two past the end: clamped like the sampler
        SkinnedPose pose;
        untitledfighter::FillPalette(cpu.skeleton, cpu.clips, &ref, frame, pose);
        ASSERT_TRUE(pose.valid) << "frame " << frame;
        ASSERT_EQ(pose.palette.size(), cpu.skeleton.joints.size());
        std::vector<glm::mat4> expected(cpu.skeleton.joints.size());
        SamplePalette(cpu.skeleton, *fourteen, frame, expected.data());
        EXPECT_EQ(std::memcmp(pose.palette.data(), expected.data(), expected.size() * sizeof(glm::mat4)), 0)
            << "frame " << frame << ": the palette is not SamplePalette's bytes";
    }

    SkinnedPose rest;
    rest.valid = true;
    untitledfighter::FillPalette(cpu.skeleton, cpu.clips, nullptr, 3, rest);
    EXPECT_FALSE(rest.valid) << "no clip must read as the rest pose, not as last frame's palette";

    const cse::presentation::ClipRef missing{ "no_such_clip", 4 };
    SkinnedPose m;
    m.valid = true;
    untitledfighter::FillPalette(cpu.skeleton, cpu.clips, &missing, 0, m);
    EXPECT_FALSE(m.valid) << "a clip the model lacks must read as the rest pose";
}

// The look is applied to both suns and can be taken back off: a host's editor
// must not stay fight-lit after the mode exits.
TEST(FightPresentation, TheLookIsAppliedToBothSunsAndRestoredAfterwards) {
    Scene scene;
    const glm::vec3 before = scene.LightDir();
    const bool outlineBefore = scene.PostFX().outline.enabled;
    cse::presentation::FightLook look{};
    look.sunDir = glm::normalize(glm::vec3(0.2f, -1.0f, 0.3f));
    look.outline = !outlineBefore;
    const untitledfighter::LookSnapshot snap = untitledfighter::ApplyFightLook(scene, nullptr, look);
    EXPECT_NEAR(glm::dot(scene.LightDir(), look.sunDir), 1.0f, 1e-5f);
    EXPECT_EQ(scene.PostFX().outline.enabled, !outlineBefore);
    EXPECT_EQ(scene.GetIBLEnabled(), look.ibl);
    untitledfighter::RestoreLook(scene, nullptr, snap);
    EXPECT_EQ(scene.LightDir(), before);
    EXPECT_EQ(scene.PostFX().outline.enabled, outlineBefore);
}
