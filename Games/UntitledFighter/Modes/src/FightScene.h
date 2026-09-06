// THE RECONCILER'S ENGINE HALF (ROADMAP M3.4c; ADR-019 D3, D5, D9).
//
// FightPresentation (the GL-free library) turns a GameState into numbers; this
// file owns the ENTITIES those numbers land in -- two fighters, a camera -- and
// the look the scene and renderer wear while a fight is on. It is the only
// place in the title that writes a Transform, a SkinnedPose or a
// CameraComponent, and it writes them from the composition and nothing else:
// no member here remembers a pose, a frame or a position between frames
// (ADR-019 D3, T0). The entities are created on adopt, destroyed on teardown
// and on Exit, and re-created if the host swapped scenes underneath the mode
// (SceneSerializer::Load clears the registry; a stored entt::entity is then
// dangling, which Valid() reports).
//
// Kept apart from UntitledFighterMode.cpp so a headless test can link it with
// Engine and prove two things the pure library cannot: the camera it creates
// outranks every camera already in a registry, and the palette it writes is
// SamplePalette's bytes at the selected frame.
#pragma once

#include "Engine.h"

#include "cse/presentation/FightPresentation.h"

#include <cstdint>
#include <memory>
#include <string>

namespace untitledfighter {

// What the fight look overwrote on the scene and the renderer, so Exit can put
// it back: the host's editor otherwise keeps a fight-lit Scene view.
struct LookSnapshot {
    bool      taken = false;
    glm::vec3 lightDir{ 0.0f };
    glm::vec3 lightColor{ 1.0f };
    float     lightIntensity = 1.0f;
    bool      iblEnabled = true;
    float     iblIntensity = 1.0f;
    MyCoreEngine::Scene::PostFXSettings::Outline outline{};
    bool      hadRenderer = false;
    glm::vec3 sunDir{ 0.0f, -1.0f, 0.0f };
    float     exposure = 1.0f;
    float     shadowDistance = 200.0f;
    int       cascades = 4;
};

// Apply the look (ADR-019 D5): the scene's shading sun AND the renderer's
// shadow/sky sun (two fields today -- both are set so they cannot disagree),
// exposure, IBL, the outline, the CSM range and cascade count. `renderer` may
// be null (a host with none); the scene half still applies.
LookSnapshot ApplyFightLook(MyCoreEngine::Scene& scene, MyCoreEngine::Renderer* renderer,
                            const cse::presentation::FightLook& look);
void         RestoreLook(MyCoreEngine::Scene& scene, MyCoreEngine::Renderer* renderer,
                         const LookSnapshot& snapshot);

// The priority the fight camera takes in THIS registry: the look's, or one
// above the highest enabled host camera if that is higher -- the director
// breaks a tie by entity index, and the host's entities were created first.
int          FightCameraPriorityFor(const entt::registry& reg, const cse::presentation::FightLook& look);
// An orthographic camera entity with that priority, positioned by Apply.
entt::entity CreateFightCamera(MyCoreEngine::Scene& scene, const cse::presentation::FightLook& look);

// The palette for (clip, frame): SamplePalette's bytes, or `valid = false`
// (the renderer's rest pose) when there is no clip or the model lacks it.
void FillPalette(const MyCoreEngine::Skeleton& skeleton, const MyCoreEngine::ClipSet& clips,
                 const cse::presentation::ClipRef* clip, std::uint32_t frame,
                 SkinnedPose& out);

class FightScene {
public:
    // The two fighters (ModelComponent + SkinnedPose + Transform + AABB + a
    // per-slot toon material over every mesh) and the camera. False, with
    // `error` filled, when the model cannot be worn: not skinned, or no clips.
    bool Create(MyCoreEngine::Scene& scene, const std::shared_ptr<MyCoreEngine::Model>& model,
                const cse::presentation::FightLook& look, std::string& error);
    void Destroy(MyCoreEngine::Scene& scene);
    // Every handle still names a live entity in this registry.
    bool Valid(const MyCoreEngine::Scene& scene) const;
    bool Active() const { return created_; }

    // Write one composed frame: transforms (position, yaw), palettes, camera.
    void Apply(MyCoreEngine::Scene& scene, const cse::presentation::FrameComposition& frame);
    // The fighters' material opacity (M3.4e): below 1 the per-slot materials
    // switch to the Blend alpha mode -- the renderer's existing transparent
    // path -- so the boxes read through the body; 1 is opaque again.
    void SetOpacity(float opacity);

    entt::entity CameraEntity() const { return camera_; }
    entt::entity FighterEntity(int slot) const { return fighters_[slot]; }
    const std::shared_ptr<MyCoreEngine::Model>& ModelHandle() const { return model_; }

private:
    std::shared_ptr<MyCoreEngine::Model> model_;
    entt::entity fighters_[cse::kernel::kMaxFighters] = { entt::null, entt::null };
    entt::entity camera_ = entt::null;
    MyCoreEngine::MaterialHandle materials_[cse::kernel::kMaxFighters];
    bool created_ = false;
};

} // namespace untitledfighter
