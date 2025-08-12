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
    void ProcessKeyboard(Camera_Movement direction, float deltaTime);
    void ProcessMouseMovement(float xoffset, float yoffset, bool constrainPitch = true);
    void ProcessMouseScroll(float yoffset);

private:
    void updateCameraVectors();
};

#endif // CAMERA_H
