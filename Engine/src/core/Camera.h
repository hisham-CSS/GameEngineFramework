#pragma once
#ifndef CAMERA_H
#define CAMERA_H

#include "Core.h"
#include <glm/glm.hpp>

// Movement options (window-system agnostic)
enum Camera_Movement {
    FORWARD,
    BACKWARD,
    LEFT,
    RIGHT
};

// How a camera projects (ROADMAP M3.2g, ADR-019 D4). Int-backed on purpose:
// CameraComponent serialises it as a number appended to the camera block.
enum class CameraProjection : int { Perspective = 0, Orthographic = 1 };

class ENGINE_API Camera {
public:
    // Defaults
    inline static constexpr float YAW_DEFAULT = -90.0f;
    inline static constexpr float PITCH_DEFAULT = 0.0f;
    inline static constexpr float SPEED_DEFAULT = 20.0f;
    inline static constexpr float SENSITIVITY_DEFAULT = 0.1f;
    inline static constexpr float ZOOM_DEFAULT = 45.0f;

    // Camera attributes
    glm::vec3 Position{};
    glm::vec3 Front{ 0.0f, 0.0f, -1.0f };
    glm::vec3 Up{};
    glm::vec3 Right{};
    glm::vec3 WorldUp{ 0.0f, 1.0f, 0.0f };

    // Euler angles
    float Yaw = YAW_DEFAULT;
    float Pitch = PITCH_DEFAULT;

    // Options
    float MovementSpeed = SPEED_DEFAULT;
    float MouseSensitivity = SENSITIVITY_DEFAULT;
    float Zoom = ZOOM_DEFAULT;

    // Clip planes. The renderer, frustum culling, and CSM splits all read
    // these (they used to hardcode 0.1/1000 in three places). Synced from
    // CameraComponent when rendering through a scene camera; the editor's
    // god camera keeps the defaults.
    inline static constexpr float NEAR_DEFAULT = 0.1f;
    inline static constexpr float FAR_DEFAULT = 1000.0f;
    float NearClip = NEAR_DEFAULT;
    float FarClip = FAR_DEFAULT;

    // Projection (M3.2g). Perspective reads Zoom as the vertical FOV.
    // Orthographic shows exactly +-OrthoHalfHeight world units from the view
    // centre to the top and bottom edge (half-width = half-height * aspect)
    // and does not shrink with distance; the fight camera is one, so the box
    // overlay and the fist agree off the z = 0 plane. Both modes keep the
    // clip planes. Synced from CameraComponent like the lens.
    CameraProjection Projection = CameraProjection::Perspective;
    float OrthoHalfHeight = 10.0f; // > 0

    // Constructors
    Camera(glm::vec3 position = { 0.0f, 0.0f, 0.0f },
        glm::vec3 up = { 0.0f, 1.0f, 0.0f },
        float yaw = YAW_DEFAULT,
        float pitch = PITCH_DEFAULT);

    Camera(float posX, float posY, float posZ,
        float upX, float upY, float upZ,
        float yaw, float pitch);

    // API
    glm::mat4 GetViewMatrix() const;
    // The projection this camera renders, culls and fits its shadows with --
    // ONE builder, so the renderer, the culling frustum and the CSM slice fit
    // cannot disagree about the mode. ProjectionFor takes its own clip planes
    // for the per-cascade slice.
    glm::mat4 GetProjectionMatrix(float aspect) const { return ProjectionFor(aspect, NearClip, FarClip); }
    glm::mat4 ProjectionFor(float aspect, float zNear, float zFar) const;
    void ProcessKeyboard(Camera_Movement direction, float deltaTime);
    void ProcessMouseMovement(float xoffset, float yoffset, bool constrainPitch = true);
    void ProcessMouseScroll(float yoffset);

private:
    void updateCameraVectors();
};

#endif // CAMERA_H
