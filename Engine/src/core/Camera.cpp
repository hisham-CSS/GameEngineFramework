#include "Camera.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

Camera::Camera(glm::vec3 position, glm::vec3 up, float yaw, float pitch)
    : Position(position), WorldUp(up), Yaw(yaw), Pitch(pitch) {
    updateCameraVectors();
}

Camera::Camera(float posX, float posY, float posZ,
    float upX, float upY, float upZ,
    float yaw, float pitch)
    : Position(posX, posY, posZ),
    WorldUp(upX, upY, upZ),
    Yaw(yaw), Pitch(pitch) {
    updateCameraVectors();
}

glm::mat4 Camera::GetViewMatrix() const {
    return glm::lookAt(Position, Position + Front, Up);
}

void Camera::ProcessKeyboard(Camera_Movement direction, float deltaTime) {
    const float velocity = MovementSpeed * deltaTime;
    switch (direction) {
    case FORWARD:  Position += Front * velocity; break;
    case BACKWARD: Position -= Front * velocity; break;
    case LEFT:     Position -= Right * velocity; break;
    case RIGHT:    Position += Right * velocity; break;
    }
}

void Camera::ProcessMouseMovement(float xoffset, float yoffset, bool constrainPitch) {
    xoffset *= MouseSensitivity;
    yoffset *= MouseSensitivity;

    Yaw += xoffset;
    Pitch += yoffset;

    if (constrainPitch) {
        if (Pitch > 89.0f) Pitch = 89.0f;
        if (Pitch < -89.0f) Pitch = -89.0f;
    }
    updateCameraVectors();
}

glm::mat4 Camera::ProjectionFor(float aspect, float zNear, float zFar) const {
    if (Projection == CameraProjection::Orthographic) {
        // a half-height of zero is a division by zero in glm::ortho, the same
        // way near == far is in glm::perspective: clamp here as well as at
        // every sync, so a Camera poked directly still projects something
        const float h = (OrthoHalfHeight > 1e-3f) ? OrthoHalfHeight : 1e-3f;
        const float w = h * aspect;
        return glm::ortho(-w, w, -h, h, zNear, zFar);
    }
    return glm::perspective(glm::radians(Zoom), aspect, zNear, zFar);
}

void Camera::ProcessMouseScroll(float yoffset) {
    Zoom -= static_cast<float>(yoffset);
    if (Zoom < 1.0f)  Zoom = 1.0f;
    if (Zoom > 45.0f) Zoom = 45.0f;
}

void Camera::updateCameraVectors() {
    // Front
    const float cy = std::cos(glm::radians(Yaw));
    const float sy = std::sin(glm::radians(Yaw));
    const float cp = std::cos(glm::radians(Pitch));
    const float sp = std::sin(glm::radians(Pitch));

    glm::vec3 front;
    front.x = cy * cp;
    front.y = sp;
    front.z = sy * cp;
    Front = glm::normalize(front);

    // Right & Up
    Right = glm::normalize(glm::cross(Front, WorldUp));
    Up = glm::normalize(glm::cross(Right, Front));
}
