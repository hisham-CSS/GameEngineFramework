// THE SKINNING PALETTE ON THE GPU (ROADMAP M3.2e; ADR-019 D1).
//
// One std140 uniform buffer of kMaxSkeletonJoints mat4 -- 8 KB, under the
// 16 KB minimum every GL 3.3 core driver guarantees for a uniform block. The
// skinned shader variants declare the block `uBones` (vertex.glsl,
// shadow_depth_vert.glsl); a program routes it to a binding point once with
// Shader::bindUniformBlock, and the palette binds the buffer there before each
// skinned draw. Plain uniform arrays were the alternative and cannot hold a
// humanoid's joints beside uLightVP[4] inside the 1024-component minimum;
// shader storage buffers are GL 4.3 and not available here.
//
// MAIN THREAD ONLY, like every GL object. Upload takes the matrices
// SamplePalette produced (Engine/src/anim/ClipSampler.h) and nothing else --
// the sampler is the only thing that decides a pose, and this class only
// carries its answer to the card.
#pragma once

#include "../core/Core.h"
#include "../anim/Skeleton.h"

#include <glm/glm.hpp>

#include <cstddef>

namespace MyCoreEngine {

class ENGINE_API SkinPaletteUBO {
public:
    static constexpr unsigned    kBinding   = 0;         // glBindBufferBase index
    static constexpr const char* kBlockName = "uBones";  // the block name in the GLSL

    SkinPaletteUBO();
    ~SkinPaletteUBO();
    SkinPaletteUBO(const SkinPaletteUBO&) = delete;
    SkinPaletteUBO& operator=(const SkinPaletteUBO&) = delete;
    SkinPaletteUBO(SkinPaletteUBO&& other) noexcept;
    SkinPaletteUBO& operator=(SkinPaletteUBO&& other) noexcept;

    // Copy `count` matrices (clamped to kMaxSkeletonJoints) into the buffer.
    void Upload(const glm::mat4* palette, std::size_t count) const;
    // Bind the buffer at kBinding so the current program's `uBones` reads it.
    void Bind() const;

    bool     Valid() const { return ubo_ != 0; }
    unsigned Id() const    { return ubo_; }

private:
    unsigned ubo_ = 0;
};

} // namespace MyCoreEngine
