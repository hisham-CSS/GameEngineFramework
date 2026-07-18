#include "CameraDirector.h"
#include "Components.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace MyCoreEngine {

    entt::entity CameraDirector::SelectCamera(entt::registry& reg)
    {
        entt::entity best = entt::null;
        int bestPri = 0;
        for (auto [e, cam] : reg.view<CameraComponent>().each()) {
            if (!cam.enabled) continue;
            if (!reg.all_of<Transform>(e)) continue; // a camera needs a pose
            // tie-break on the entity INDEX, not the raw handle: the raw
            // value carries version bits in its high bits, and versions
            // reset on scene load — a raw compare would flip the winner
            // between a session and its reloaded save
            if (best == entt::null || cam.priority > bestPri ||
                (cam.priority == bestPri &&
                 entt::to_entity(e) < entt::to_entity(best))) {
                best = e;
                bestPri = cam.priority;
            }
        }
        return best;
    }

    bool CameraDirector::poseFromEntity_(entt::registry& reg, entt::entity e, Pose& out)
    {
        if (e == entt::null || !reg.valid(e) ||
            !reg.all_of<CameraComponent, Transform>(e)) return false;

        const auto& cc = reg.get<CameraComponent>(e);
        const glm::mat4& m = reg.get<Transform>(e).modelMatrix; // WORLD

        // strip scale so the quaternion comes from a pure rotation basis
        // (quat_cast on a scaled matrix skews the orientation). Degenerate
        // columns (zero scale) fall back to the identity axes. Negative
        // scale flips handedness and has no meaningful camera orientation —
        // not supported.
        auto column = [&](int c, glm::vec3 fallback) {
            const glm::vec3 v(m[c]);
            const float len = glm::length(v);
            return (len > 1e-6f) ? v / len : fallback;
        };
        glm::mat3 basis;
        basis[0] = column(0, { 1.f, 0.f, 0.f });
        basis[1] = column(1, { 0.f, 1.f, 0.f });
        basis[2] = column(2, { 0.f, 0.f, 1.f });

        out.position = glm::vec3(m[3]);
        out.rotation = glm::normalize(glm::quat_cast(basis));
        // clamp: fov outside (0,180) degenerates tan(fov/2) in every
        // projection; near must stay positive and far strictly past near
        // (relative separation — see MinFarClipFor)
        out.fovDeg = glm::clamp(cc.fovDeg, 1.f, 179.f);
        out.nearClip = cc.nearClip > 1e-3f ? cc.nearClip : 1e-3f;
        out.farClip = std::max(cc.farClip, MinFarClipFor(out.nearClip));
        return true;
    }

    void CameraDirector::applyPose_(const Pose& p, Camera& cam)
    {
        cam.Position = p.position;
        cam.Right = p.rotation * glm::vec3(1.f, 0.f, 0.f);
        cam.Up = p.rotation * glm::vec3(0.f, 1.f, 0.f);
        cam.Front = p.rotation * glm::vec3(0.f, 0.f, -1.f); // identity looks down -Z
        // keep the euler look state coherent with the written vectors: the
        // fly cam's ProcessMouseMovement rebuilds Front FROM Yaw/Pitch, so
        // stale values would snap the view on the first look input after a
        // fallback to the fly cam (no usable scene camera left)
        cam.Yaw = glm::degrees(std::atan2(cam.Front.z, cam.Front.x));
        cam.Pitch = glm::degrees(std::asin(glm::clamp(cam.Front.y, -1.f, 1.f)));
        cam.Zoom = p.fovDeg;
        cam.NearClip = p.nearClip;
        cam.FarClip = p.farClip;
    }

    bool CameraDirector::Update(entt::registry& reg, float dt, Camera& cam)
    {
        // resolve the winner: a valid override beats priority selection
        Pose target;
        entt::entity want = entt::null;
        if (override_ != entt::null && poseFromEntity_(reg, override_, target)) {
            want = override_;
        }
        else {
            want = SelectCamera(reg);
            if (want == entt::null || !poseFromEntity_(reg, want, target)) {
                // no usable camera: forget blend state (the next camera
                // appearing should cut, not blend from a stale pose)
                active_ = entt::null;
                blendT_ = 1.f;
                lastOutputValid_ = false;
                return false;
            }
        }

        if (want != active_) {
            if (lastOutputValid_ && defaultBlendSeconds_ > 0.f && active_ != entt::null) {
                // blend from the current OUTPUT pose so a switch mid-blend
                // continues from what is on screen instead of popping
                fromPose_ = lastOutput_;
                blendT_ = 0.f;
                blendDur_ = defaultBlendSeconds_;
            }
            else {
                blendT_ = 1.f; // first camera / blends off: hard cut
            }
            active_ = want;
        }

        Pose out = target;
        if (blendT_ < 1.f) {
            blendT_ = (blendDur_ > 1e-6f)
                ? (blendT_ + dt / blendDur_ < 1.f ? blendT_ + dt / blendDur_ : 1.f)
                : 1.f;
            const float s = blendT_ * blendT_ * (3.f - 2.f * blendT_); // smoothstep
            out.position = glm::mix(fromPose_.position, target.position, s);
            out.rotation = glm::slerp(fromPose_.rotation, target.rotation, s);
            out.fovDeg = glm::mix(fromPose_.fovDeg, target.fovDeg, s);
            out.nearClip = glm::mix(fromPose_.nearClip, target.nearClip, s);
            out.farClip = glm::mix(fromPose_.farClip, target.farClip, s);
        }

        lastOutput_ = out;
        lastOutputValid_ = true;
        applyPose_(out, cam);
        return true;
    }

    void CameraDirector::reset()
    {
        active_ = entt::null;
        override_ = entt::null;
        blendT_ = 1.f;
        blendDur_ = 0.f;
        lastOutputValid_ = false;
    }

} // namespace MyCoreEngine
