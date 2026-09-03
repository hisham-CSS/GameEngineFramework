#include "FightScene.h"

#include <algorithm>
#include <cstring>

namespace untitledfighter {

using namespace MyCoreEngine;

LookSnapshot ApplyFightLook(Scene& scene, Renderer* renderer, const cse::presentation::FightLook& look) {
    LookSnapshot s;
    s.taken = true;
    s.lightDir = scene.LightDir();
    s.lightColor = scene.LightColor();
    s.lightIntensity = scene.LightIntensity();
    s.iblEnabled = scene.GetIBLEnabled();
    s.iblIntensity = scene.GetIBLIntensity();
    s.outline = scene.PostFX().outline;

    scene.LightDir() = glm::normalize(look.sunDir);
    scene.LightColor() = look.sunColor;
    scene.LightIntensity() = look.sunIntensity;
    scene.SetIBLEnabled(look.ibl);
    scene.SetIBLIntensity(look.iblIntensity);
    auto& outline = scene.PostFX().outline;
    outline.enabled = look.outline;
    outline.thickness = look.outlineThickness;
    outline.threshold = look.outlineThreshold;
    outline.strength = look.outlineStrength;

    if (renderer) {
        s.hadRenderer = true;
        s.sunDir = renderer->sunDir();
        s.exposure = renderer->exposure();
        s.shadowDistance = renderer->getCSMMaxShadowDistance();
        s.cascades = renderer->getCSMNumCascades();
        // The renderer's sun casts the shadows and bakes the procedural sky;
        // the scene's sun shades. Two fields, set together, so a fighter's
        // shadow falls where its shading says the light is.
        renderer->setSunDir(look.sunDir);
        renderer->setExposure(look.exposure);
        renderer->setCSMMaxShadowDistance(look.shadowDistancePx);
        renderer->setCSMNumCascades(look.cascades);
    }
    return s;
}

void RestoreLook(Scene& scene, Renderer* renderer, const LookSnapshot& s) {
    if (!s.taken) return;
    scene.LightDir() = s.lightDir;
    scene.LightColor() = s.lightColor;
    scene.LightIntensity() = s.lightIntensity;
    scene.SetIBLEnabled(s.iblEnabled);
    scene.SetIBLIntensity(s.iblIntensity);
    scene.PostFX().outline = s.outline;
    if (renderer && s.hadRenderer) {
        renderer->setSunDir(s.sunDir);
        renderer->setExposure(s.exposure);
        renderer->setCSMMaxShadowDistance(s.shadowDistance);
        renderer->setCSMNumCascades(s.cascades);
    }
}

int FightCameraPriorityFor(const entt::registry& reg, const cse::presentation::FightLook& look) {
    int highest = look.cameraPriority - 1;
    for (auto [e, cc] : reg.view<const CameraComponent>().each())
        if (cc.enabled) highest = std::max(highest, cc.priority);
    return std::max(look.cameraPriority, highest + 1);
}

entt::entity CreateFightCamera(Scene& scene, const cse::presentation::FightLook& look) {
    const int priority = FightCameraPriorityFor(scene.registry, look);
    Entity e = scene.createEntity();
    e.addComponent<Name>(Name{ "Fight Camera" });
    Transform t{};
    t.position = { 0.0f, cse::presentation::kCameraHeightPx, look.cameraDistancePx };
    t.dirty = true;
    e.addComponent<Transform>(t);
    CameraComponent cc{};
    cc.projection = CameraProjection::Orthographic;
    cc.orthoHalfHeight = cse::presentation::kViewHalfWidthPx * (9.0f / 16.0f); // until the first frame says the aspect
    cc.nearClip = look.nearPx;
    cc.farClip = std::max(look.farPx, MinFarClipFor(look.nearPx));
    cc.priority = priority;
    cc.enabled = true;
    scene.registry.emplace<CameraComponent>((entt::entity)e, cc);
    return (entt::entity)e;
}

void FillPalette(const Skeleton& skeleton, const ClipSet& clips,
                 const cse::presentation::ClipRef* clip, std::uint32_t frame, SkinnedPose& out) {
    const Clip* c = clip ? clips.Find(clip->name) : nullptr;
    if (c == nullptr || skeleton.joints.empty()) {
        out.valid = false;
        return;
    }
    out.palette.resize(skeleton.joints.size());
    SamplePalette(skeleton, *c, frame, out.palette.data());
    out.valid = true;
}

bool FightScene::Create(Scene& scene, const std::shared_ptr<Model>& model,
                        const cse::presentation::FightLook& look, std::string& error) {
    Destroy(scene);
    if (!model) { error = "no model"; return false; }
    if (!model->IsSkinned()) { error = "the presentation model has no skeleton, so it cannot wear a pose"; return false; }
    if (model->GetClips().Empty()) { error = "the presentation model carries no clips"; return false; }
    model_ = model;

    for (int slot = 0; slot < cse::kernel::kMaxFighters; ++slot) {
        // One toon material per slot, over every mesh of the shared model: the
        // tint is how a viewer tells P1 from P2 on one body.
        auto mat = std::make_shared<Material>();
        mat->shadingModel = ShadingModel::Toon;
        mat->baseColor = look.tint[slot];
        materials_[slot] = mat;

        Entity e = scene.createEntity();
        e.addComponent<Name>(Name{ slot == 0 ? "Fighter P1" : "Fighter P2" });
        Transform t{};
        t.position = { 0.0f, 0.0f, look.slotZ[slot] };
        t.dirty = true;
        e.addComponent<Transform>(t);
        e.addComponent<ModelComponent>(ModelComponent{ model });
        e.addComponent<AABB>(generateAABB(*model));
        scene.registry.emplace<SkinnedPose>((entt::entity)e);
        MaterialOverrides overrides;
        for (const Mesh& mesh : model->Meshes()) overrides.byIndex[mesh.MaterialIndex()] = mat;
        scene.registry.emplace<MaterialOverrides>((entt::entity)e, std::move(overrides));
        fighters_[slot] = (entt::entity)e;
    }
    camera_ = CreateFightCamera(scene, look);
    created_ = true;
    return true;
}

void FightScene::Destroy(Scene& scene) {
    for (entt::entity& f : fighters_) {
        if (f != entt::null && scene.registry.valid(f)) scene.registry.destroy(f);
        f = entt::null;
    }
    if (camera_ != entt::null && scene.registry.valid(camera_)) scene.registry.destroy(camera_);
    camera_ = entt::null;
    for (auto& m : materials_) m.reset();
    model_.reset();
    created_ = false;
}

bool FightScene::Valid(const Scene& scene) const {
    if (!created_) return false;
    for (const entt::entity f : fighters_)
        if (f == entt::null || !scene.registry.valid(f) || !scene.registry.all_of<Transform, SkinnedPose>(f)) return false;
    return camera_ != entt::null && scene.registry.valid(camera_) &&
           scene.registry.all_of<Transform, CameraComponent>(camera_);
}

void FightScene::SetOpacity(float opacity) {
    const float o = std::clamp(opacity, 0.0f, 1.0f);
    for (auto& mat : materials_) {
        if (!mat) continue;
        mat->alphaMode = (o < 1.0f) ? AlphaMode::Blend : AlphaMode::Opaque;
        mat->opacity = o;
    }
}

void FightScene::Apply(Scene& scene, const cse::presentation::FrameComposition& frame) {
    if (!Valid(scene) || !model_) return;
    for (int slot = 0; slot < cse::kernel::kMaxFighters; ++slot) {
        const cse::presentation::FighterFrame& f = frame.fighter[slot];
        Transform& t = scene.registry.get<Transform>(fighters_[slot]);
        t.position = f.position;
        t.rotation = { 0.0f, f.yawDeg, 0.0f };   // a yaw, never a negative scale (ADR-019 D5)
        t.scale = { 1.0f, 1.0f, 1.0f };
        t.dirty = true;
        SkinnedPose& pose = scene.registry.get<SkinnedPose>(fighters_[slot]);
        FillPalette(model_->GetSkeleton(), model_->GetClips(), f.visible ? f.clip : nullptr, f.frame, pose);
    }
    Transform& ct = scene.registry.get<Transform>(camera_);
    ct.position = frame.camera.position;
    ct.rotation = { 0.0f, 0.0f, 0.0f };          // identity looks down -Z
    ct.scale = { 1.0f, 1.0f, 1.0f };
    ct.dirty = true;
    CameraComponent& cc = scene.registry.get<CameraComponent>(camera_);
    cc.projection = CameraProjection::Orthographic;
    cc.orthoHalfHeight = std::max(frame.camera.orthoHalfHeight, 1e-3f);
    cc.nearClip = std::max(frame.camera.nearClip, 1e-3f);
    cc.farClip = std::max(frame.camera.farClip, MinFarClipFor(cc.nearClip));
}

} // namespace untitledfighter
