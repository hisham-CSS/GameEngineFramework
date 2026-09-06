#include "SkinPalette.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <utility>

namespace MyCoreEngine {

namespace {
constexpr std::size_t kPaletteBytes = static_cast<std::size_t>(kMaxSkeletonJoints) * sizeof(glm::mat4);
}

SkinPaletteUBO::SkinPaletteUBO() {
    // Same readiness guard as the texture upload: a palette created before a
    // context exists is an empty object, not a crash.
    if (glfwGetCurrentContext() == nullptr || !glad_glGenBuffers) return;
    glGenBuffers(1, &ubo_);
    glBindBuffer(GL_UNIFORM_BUFFER, ubo_);
    glBufferData(GL_UNIFORM_BUFFER, static_cast<GLsizeiptr>(kPaletteBytes), nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

SkinPaletteUBO::~SkinPaletteUBO() {
    if (ubo_ && glfwGetCurrentContext()) glDeleteBuffers(1, &ubo_);
}

SkinPaletteUBO::SkinPaletteUBO(SkinPaletteUBO&& other) noexcept
    : ubo_(std::exchange(other.ubo_, 0u)) {}

SkinPaletteUBO& SkinPaletteUBO::operator=(SkinPaletteUBO&& other) noexcept {
    if (this != &other) {
        if (ubo_ && glfwGetCurrentContext()) glDeleteBuffers(1, &ubo_);
        ubo_ = std::exchange(other.ubo_, 0u);
    }
    return *this;
}

void SkinPaletteUBO::Upload(const glm::mat4* palette, std::size_t count) const {
    if (!ubo_ || !palette || count == 0) return;
    if (count > static_cast<std::size_t>(kMaxSkeletonJoints)) count = kMaxSkeletonJoints;
    glBindBuffer(GL_UNIFORM_BUFFER, ubo_);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, static_cast<GLsizeiptr>(count * sizeof(glm::mat4)), palette);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void SkinPaletteUBO::Bind() const {
    if (!ubo_) return;
    glBindBufferBase(GL_UNIFORM_BUFFER, kBinding, ubo_);
}

} // namespace MyCoreEngine
