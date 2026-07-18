#pragma once
#include "Core.h"
#include "Camera.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace MyCoreEngine {

    // Cinemachine-style camera direction — the stub for the full camera
    // system planned later. Every frame, Update():
    //   1. picks the camera entity to render from — the manual override if
    //      one is set and still valid, else the highest-priority ENABLED
    //      CameraComponent with a Transform (ties: lowest entity index), and
    //   2. when the winner CHANGES, blends the rendered view from the
    //      previous OUTPUT pose to the new camera over defaultBlendSeconds
    //      (position lerp, orientation slerp, fov/near/far lerp, smoothstep
    //      eased). 0 seconds (the default) means hard cuts. Blending from
    //      the output pose — not the old camera's live pose — makes
    //      mid-blend switches chain smoothly instead of popping.
    //
    // Gameplay drives cameras by editing CameraComponent fields (raise
    // priority / toggle enabled) — the director notices on its own; nothing
    // needs to call into it. setOverride is for tooling (the editor's Game
    // panel picker) and scripted hard takes.
    //
    // Planned, NOT implemented (this class is the seam for all of it):
    // per-camera blend overrides + easing curves, follow/look-at targets
    // with damping, noise/shake profiles, dolly tracks.
    class ENGINE_API CameraDirector {
    public:
        // Highest-priority enabled camera with a Transform; ties broken by
        // lowest entity INDEX (not raw handle — versions reset on scene
        // load) so selection is deterministic and save/load-stable.
        // entt::null when the scene has no usable camera.
        static entt::entity SelectCamera(entt::registry& reg);

        // Force rendering from a specific camera entity; entt::null returns
        // to automatic priority selection. An override that is (or becomes)
        // invalid — destroyed entity, component removed — is ignored, not
        // cleared: selection falls back to automatic until it is valid
        // again or the override is reset.
        void setOverride(entt::entity e) { override_ = e; }
        entt::entity overrideCamera() const { return override_; }

        void  setDefaultBlendSeconds(float s) { defaultBlendSeconds_ = s > 0.f ? s : 0.f; }
        float defaultBlendSeconds() const { return defaultBlendSeconds_; }

        // Advance selection + blending and write the resulting view into
        // `cam` (Position/Right/Up/Front, Zoom, NearClip/FarClip). Returns
        // false and leaves `cam` untouched when the scene has no usable
        // camera — callers keep their own fallback view (fly cam).
        bool Update(entt::registry& reg, float dt, Camera& cam);

        // The camera entity currently driving the view (the blend target
        // while a blend is in flight).
        entt::entity activeCamera() const { return active_; }
        bool blending() const { return blendT_ < 1.f; }
        // Hard cut: finish any in-flight blend AND forget the last output
        // pose, so the next camera switch cuts instead of blending. Use
        // after teleports/bulk restores where the on-screen pose is stale.
        void cut() { blendT_ = 1.f; lastOutputValid_ = false; }
        // Forget everything (active, override, blend state). Call whenever
        // entity handles may have gone stale — scene load, new scene — or
        // when a cut is wanted after a bulk restore (play-mode stop).
        void reset();

    private:
        struct Pose {
            glm::vec3 position{ 0.f };
            glm::quat rotation{ 1.f, 0.f, 0.f, 0.f };
            float fovDeg = 60.f;
            float nearClip = Camera::NEAR_DEFAULT;
            float farClip = Camera::FAR_DEFAULT;
        };
        // World pose + lens values from a camera entity; false when it is
        // not a valid camera (missing/invalid entity, component, Transform).
        static bool poseFromEntity_(entt::registry& reg, entt::entity e, Pose& out);
        static void applyPose_(const Pose& p, Camera& cam);

        entt::entity active_ = entt::null;
        entt::entity override_ = entt::null;
        float defaultBlendSeconds_ = 0.f;

        // blend state: t in [0,1); >= 1 means no blend in flight
        Pose  fromPose_{};
        float blendT_ = 1.f;
        float blendDur_ = 0.f;
        // last frame's OUTPUT pose — the blend source on a switch
        Pose  lastOutput_{};
        bool  lastOutputValid_ = false;
    };

} // namespace MyCoreEngine
