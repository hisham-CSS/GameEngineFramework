// THE SKELETON A MODEL CARRIES, AND THE WEIGHTS THAT BIND A MESH TO IT
// (ROADMAP M3.2b; ADR-019 D1/D2; the one renderer feature the showcase freeze
// admits is skinning, and this is its CPU half).
//
// Built by Model::Decode on a worker -- no GL, no shared state -- from the
// scene's node hierarchy and the meshes' bone lists, and consumed by the
// frame-indexed sampler (M3.2d) and the skinned draw path (M3.2e). Everything
// here is plain data: a joint is a name, a parent index, its local bind pose
// and its inverse bind matrix; a skin is two per-vertex vec4 streams kept
// BESIDE the static Vertex rather than inside it, so the static vertex layout
// (and the meshoptimizer LOD stride, and every shipped OBJ) stays byte for
// byte what it was.
//
// WHY JOINTS ARE ORDERED PARENT-FIRST. The sampler composes a joint's world
// matrix as parent * local, in one pass over the array; that pass is correct
// only if every parent index is smaller than its child's. Decode walks the
// node tree depth-first, which gives exactly that order, and Skeleton asserts
// it so a hand-built one cannot be wrong quietly.
//
// WHY 128. The palette lives in a std140 uniform block on GL 3.3 core
// (M3.2e): 128 mat4 is 8 KB, under the 16 KB minimum every conforming driver
// guarantees, and a Rigify deform set for a humanoid is around 65. A rig over
// the cap is refused at import, naming the count, rather than truncated -- a
// truncated rig would skin some vertices to a joint that does not exist.
#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace MyCoreEngine {

constexpr int kMaxSkeletonJoints = 128;

// At most four joints influence a vertex (the exporter's default, and what the
// import re-normalises to via aiProcess_LimitBoneWeights).
constexpr int kMaxJointInfluences = 4;

struct Skeleton {
    struct Joint {
        std::string name;
        int         parent = -1;                 // index into joints, -1 for a root
        glm::mat4   localBind{ 1.0f };           // this joint's rest transform, relative to `parent`
        glm::mat4   inverseBind{ 1.0f };         // mesh space -> this joint's space at bind time
    };
    std::vector<Joint> joints;

    bool Empty() const { return joints.empty(); }

    // Index of the joint with this name, or -1.
    int Find(const std::string& name) const {
        for (std::size_t i = 0; i < joints.size(); ++i)
            if (joints[i].name == name) return static_cast<int>(i);
        return -1;
    }

    // Every parent precedes its child. Decode guarantees it; a caller that
    // assembled a skeleton by hand can ask.
    bool ParentsPrecedeChildren() const {
        for (std::size_t i = 0; i < joints.size(); ++i)
            if (joints[i].parent >= static_cast<int>(i)) return false;
        return true;
    }
};

// Per-vertex binding, parallel to a mesh's vertex array. Empty for an
// unskinned mesh. Weights are normalised to sum to 1 (unused slots weigh 0).
struct SkinData {
    std::vector<glm::ivec4> joints;   // indices into Skeleton::joints
    std::vector<glm::vec4>  weights;

    bool Empty() const { return joints.empty(); }
};

} // namespace MyCoreEngine
