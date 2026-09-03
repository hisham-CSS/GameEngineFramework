// Model::Decode (P4-3 phase 2, CPU stage): headless tests — the decode
// half must never touch GL, so a valid model file can be decoded with no
// context at all. The GL half (finalize) is covered in test_scene_details
// under its GL fixture.
#include <gtest/gtest.h>

#include <atomic>
#include <cfloat>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

#include "Engine.h"

using namespace MyCoreEngine;

namespace {

const char* kObj = "decode_test.obj";

class ModelDecodeTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        std::ofstream f(kObj);
        f << "v -0.5 0 0.5\n"
          << "v  0.5 0 0.5\n"
          << "v  0.5 0 -0.5\n"
          << "v -0.5 0 -0.5\n"
          << "f 1 2 3\n"
          << "f 1 3 4\n";
    }
    static void TearDownTestSuite() { std::remove(kObj); }
};

} // namespace

TEST_F(ModelDecodeTest, MissingFileDecodesInvalid) {
    const ModelCPUData cpu = Model::Decode("no_such_model_file.obj");
    EXPECT_FALSE(cpu.valid);
    EXPECT_TRUE(cpu.meshes.empty());
    EXPECT_EQ(cpu.sourcePath, "no_such_model_file.obj"); // path still recorded
}

TEST_F(ModelDecodeTest, DecodeProducesGeometryWithoutGL) {
    const ModelCPUData cpu = Model::Decode(kObj);
    ASSERT_TRUE(cpu.valid);
    ASSERT_EQ(cpu.meshes.size(), 1u);
    const auto& m = cpu.meshes[0];
    EXPECT_EQ(m.indices.size(), 6u); // two triangles
    EXPECT_GE(m.vertices.size(), 3u);
    // tiny mesh: below the simplification floor, every LOD level falls back
    for (int l = 1; l < Mesh::kLodCount; ++l) {
        EXPECT_TRUE(m.lodIndices[l].empty());
    }
    // Assimp synthesizes a default material; the mesh references one
    ASSERT_GE(cpu.materials.size(), 1u);
    ASSERT_GE(m.materialIndex, 0);
    EXPECT_LT((size_t)m.materialIndex, cpu.materials.size());
    EXPECT_TRUE(cpu.textures.empty()); // no textures referenced by the obj
}

TEST_F(ModelDecodeTest, ComputeLodIndicesKeepsAcceptedLevelsShrinking) {
    // synthetic dense grid: enough triangles to clear the simplify floor
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    constexpr int N = 24; // (N-1)^2 * 2 = 1058 triangles
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            Vertex v{};
            v.Position = { (float)x, 0.f, (float)y };
            vertices.push_back(v);
        }
    }
    for (int y = 0; y + 1 < N; ++y) {
        for (int x = 0; x + 1 < N; ++x) {
            const unsigned a = y * N + x, b = a + 1, c = a + N, d = c + 1;
            indices.insert(indices.end(), { a, b, c, b, d, c });
        }
    }

    const auto lods = Mesh::ComputeLodIndices(vertices, indices);
    size_t prev = indices.size();
    for (int l = 1; l < Mesh::kLodCount; ++l) {
        if (lods[l].empty()) continue; // legal: fall back to previous level
        EXPECT_EQ(lods[l].size() % 3, 0u) << "level " << l << " not triangles";
        EXPECT_GE(lods[l].size(), 3u);
        EXPECT_LT(lods[l].size(), prev * 9 / 10)
            << "accepted level " << l << " must meaningfully shrink";
        prev = lods[l].size();
    }
    // a flat dense grid is trivially simplifiable: expect at least one
    // accepted level, or the LOD pipeline silently stopped working
    EXPECT_FALSE(lods[1].empty() && lods[2].empty());
}

// Real-asset LOD pin: backpack.obj through the ACTUAL import flags. The
// synthetic in-memory grids above can't catch an import-flag regression —
// without aiProcess_JoinIdenticalVertices Assimp's OBJ importer emits
// per-face vertices (disconnected triangle soup), meshopt_simplify can't
// collapse a single edge, and every LOD level is silently rejected for
// every OBJ asset. Decoded via a geometry-only copy (no .mtl next to it):
// texture decode isn't under test and the real maps are multi-MB.
TEST_F(ModelDecodeTest, BackpackObjAcceptsLodLevels) {
    namespace fs = std::filesystem;
    const char* src = "Exported/Model/backpack.obj";
    ASSERT_TRUE(fs::exists(src)) << "staged asset missing — run from the tests binary dir";
    fs::copy_file(src, "lod_backpack.obj", fs::copy_options::overwrite_existing);

    const ModelCPUData cpu = Model::Decode("lod_backpack.obj");
    std::error_code ec;
    fs::remove("lod_backpack.obj", ec);
    ASSERT_TRUE(cpu.valid);
    ASSERT_FALSE(cpu.meshes.empty());

    size_t accepted = 0;
    for (size_t i = 0; i < cpu.meshes.size(); ++i) {
        const auto& m = cpu.meshes[i];
        glm::vec3 lo{ FLT_MAX }, hi{ -FLT_MAX };
        for (const auto& v : m.vertices) {
            lo = glm::min(lo, v.Position);
            hi = glm::max(hi, v.Position);
        }
        std::printf("[lod] mesh %zu: verts=%zu idx=%zu lod1=%zu lod2=%zu "
                    "aabb(%.3f %.3f %.3f)-(%.3f %.3f %.3f)\n",
                    i, m.vertices.size(), m.indices.size(),
                    m.lodIndices[1].size(), m.lodIndices[2].size(),
                    lo.x, lo.y, lo.z, hi.x, hi.y, hi.z);
        for (int l = 1; l < Mesh::kLodCount; ++l) accepted += !m.lodIndices[l].empty();
        if (m.indices.size() >= 3 * 64) {
            // joined import = indexed geometry with real vertex reuse; the
            // pre-fix soup had vertices.size() == indices.size() exactly
            EXPECT_LT(m.vertices.size(), m.indices.size())
                << "mesh " << i << " decoded as unindexed triangle soup";
        }
    }
    EXPECT_GT(accepted, 0u)
        << "no LOD level accepted on any backpack mesh — the OBJ import "
           "produces unsimplifiable geometry and the LOD system is inert";
}

TEST_F(ModelDecodeTest, ConcurrentDecodesOnWorkersMatch) {
    JobSystem jobs(4);
    constexpr int kN = 8;
    struct Slot { ModelCPUData cpu; };
    std::vector<std::shared_ptr<Slot>> slots;
    for (int i = 0; i < kN; ++i) {
        auto s = std::make_shared<Slot>();
        slots.push_back(s);
        jobs.submit([s] { s->cpu = Model::Decode(kObj); });
    }
    jobs.waitIdle();

    for (const auto& s : slots) {
        ASSERT_TRUE(s->cpu.valid);
        ASSERT_EQ(s->cpu.meshes.size(), 1u);
        EXPECT_EQ(s->cpu.meshes[0].indices.size(), 6u);
        EXPECT_EQ(s->cpu.meshes[0].vertices.size(),
                  slots[0]->cpu.meshes[0].vertices.size());
    }
}

// The REAL concurrency surface: parallel stb decode (per-thread flip flag —
// the reason the global stbi flag was replaced) and parallel meshoptimizer
// simplification. A textured dense grid exercises both on every worker;
// results must be bit-identical across all decodes.
TEST_F(ModelDecodeTest, ConcurrentTexturedGridDecodesAreIdentical) {
    namespace fs = std::filesystem;
    fs::create_directories("decode_hl");
    {
        // 2x2 24bpp BMP, bottom-up rows: image bottom {red, green}, top {blue, white}
        const unsigned char px[2][2][3] = { { {255,0,0}, {0,255,0} },
                                            { {0,0,255}, {255,255,255} } };
        std::ofstream bmp("decode_hl/grid.bmp", std::ios::binary);
        unsigned char hdr[54] = {};
        hdr[0]='B'; hdr[1]='M';
        auto put32 = [&](int off, unsigned int v) {
            hdr[off]=(unsigned char)v; hdr[off+1]=(unsigned char)(v>>8);
            hdr[off+2]=(unsigned char)(v>>16); hdr[off+3]=(unsigned char)(v>>24);
        };
        put32(2, 54 + 16); put32(10, 54); put32(14, 40);
        put32(18, 2); put32(22, 2); hdr[26]=1; hdr[28]=24; put32(34, 16);
        bmp.write((const char*)hdr, 54);
        for (int y = 0; y < 2; ++y) {
            unsigned char row[8] = {};
            for (int x = 0; x < 2; ++x) {
                row[x*3+0]=px[y][x][2]; row[x*3+1]=px[y][x][1]; row[x*3+2]=px[y][x][0];
            }
            bmp.write((const char*)row, 8);
        }
    }
    {
        std::ofstream mtl("decode_hl/grid.mtl");
        mtl << "newmtl gridmat\nmap_Kd grid.bmp\n";
    }
    {
        std::ofstream obj("decode_hl/grid.obj");
        obj << "mtllib grid.mtl\nusemtl gridmat\n";
        constexpr int N = 24; // over the LOD simplify floor
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x)
                obj << "v " << x << " 0 " << y << "\n";
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x)
                obj << "vt " << (float)x / (N - 1) << " " << (float)y / (N - 1) << "\n";
        for (int y = 0; y + 1 < N; ++y) {
            for (int x = 0; x + 1 < N; ++x) {
                const int a = y * N + x + 1, b = a + 1, c = a + N, d = c + 1;
                obj << "f " << a << "/" << a << " " << b << "/" << b << " " << c << "/" << c << "\n";
                obj << "f " << b << "/" << b << " " << d << "/" << d << " " << c << "/" << c << "\n";
            }
        }
    }

    JobSystem jobs(4);
    constexpr int kN = 8;
    struct Slot { ModelCPUData cpu; };
    std::vector<std::shared_ptr<Slot>> slots;
    for (int i = 0; i < kN; ++i) {
        auto s = std::make_shared<Slot>();
        slots.push_back(s);
        jobs.submit([s] { s->cpu = Model::Decode("decode_hl/grid.obj"); });
    }
    jobs.waitIdle();

    const auto& first = slots[0]->cpu;
    ASSERT_TRUE(first.valid);
    ASSERT_EQ(first.textures.size(), 1u);
    ASSERT_TRUE(first.textures[0].decoded);
    // per-thread flip: the image BOTTOM row {red,...} must come first
    ASSERT_GE(first.textures[0].pixels.size(), 4u);
    EXPECT_EQ(first.textures[0].pixels[0], 255u) << "flip semantics changed on a worker";
    EXPECT_EQ(first.textures[0].pixels[1], 0u);
    EXPECT_EQ(first.textures[0].pixels[2], 0u);

    for (const auto& s : slots) {
        ASSERT_TRUE(s->cpu.valid);
        ASSERT_EQ(s->cpu.textures.size(), 1u);
        ASSERT_TRUE(s->cpu.textures[0].decoded);
        EXPECT_EQ(s->cpu.textures[0].pixels, first.textures[0].pixels)
            << "concurrent stb decodes must be byte-identical";
        ASSERT_EQ(s->cpu.meshes.size(), first.meshes.size());
        for (int l = 0; l < Mesh::kLodCount; ++l) {
            EXPECT_EQ(s->cpu.meshes[0].lodIndices[l].size(),
                      first.meshes[0].lodIndices[l].size())
                << "concurrent meshopt runs diverged at LOD " << l;
        }
    }
    std::error_code ec;
    std::filesystem::remove_all("decode_hl", ec);
}

// ============================================================================
// glTF enters the static model path (ROADMAP M3.2a; ADR-019 D1)
// ============================================================================
//
// Two things a Blender export does that OBJ never did, each pinned on a
// committed fixture that tests/fixtures/models/make_fixtures.py writes with the
// standard library alone (no Blender, no GPU, nothing an artist's machine has to
// provide): meshes sit under TRANSFORMED nodes, and UVs are authored with the
// origin at the image's top-left. The first used to be silently ignored -- the
// vertices were copied verbatim and every part of a hierarchy landed at the
// origin; the second is the one where "fix" it and you break it: Assimp's glTF2
// importer already flips V on import, aiProcess_FlipUVs flips it again, and the
// OBJ importer flips once through the same flag, so BOTH formats arrive in one
// convention. The second test exists so nobody "corrects" one flip and ships
// upside-down textures for one format.
#include <cmath>
#include <string>

namespace {

std::string modelFixturesDir() {
    namespace fs = std::filesystem;
    fs::path here = fs::current_path();
    for (int i = 0; i < 8; ++i) {
        // Committed fixtures, deliberately NOT staged next to an executable:
        // they are test evidence, not content. Walking up keeps this runnable
        // from a build tree or a shell.
        const fs::path candidate = here / "tests" / "fixtures" / "models";
        if (fs::exists(candidate / "child_offset.gltf")) return candidate.string();
        if (!here.has_parent_path() || here.parent_path() == here) break;
        here = here.parent_path();
    }
    return "tests/fixtures/models";
}

const Vertex* findAt(const std::vector<Vertex>& vs, float x, float y, float z) {
    for (const Vertex& v : vs)
        if (std::fabs(v.Position.x - x) < 1e-4f && std::fabs(v.Position.y - y) < 1e-4f &&
            std::fabs(v.Position.z - z) < 1e-4f)
            return &v;
    return nullptr;
}

} // namespace

TEST(ModelDecodeGltf, AChildNodesTransformLandsItsVerticesInWorldSpace) {
    const ModelCPUData cpu = Model::Decode(modelFixturesDir() + "/child_offset.gltf");
    ASSERT_TRUE(cpu.valid) << "child_offset.gltf did not decode from " << modelFixturesDir();
    ASSERT_EQ(cpu.meshes.size(), 1u);
    const std::vector<Vertex>& vs = cpu.meshes[0].vertices;
    ASSERT_EQ(vs.size(), 4u) << "JoinIdenticalVertices should leave the quad its four corners";

    // Authored at +-0.5 under a node translated (10, 0, 0) and scaled 2: the
    // corners must land at x in {9, 11}, z in {-1, 1}, y = 0.
    for (float x : { 9.f, 11.f })
        for (float z : { -1.f, 1.f })
            EXPECT_NE(findAt(vs, x, 0.f, z), nullptr)
                << "no vertex at (" << x << ", 0, " << z
                << "): the node's transform did not reach the vertices";
    for (const Vertex& v : vs) {
        EXPECT_NEAR(v.Normal.y, 1.f, 1e-4f)
            << "a translate plus a uniform scale must leave the normal +Y";
        EXPECT_NEAR(glm::length(v.Normal), 1.f, 1e-4f) << "the baked normal is not unit";
    }

    // Assimp's glTF2 importer prepends a default material of its own, so the
    // authored one is found by NAME -- which is the whole point of carrying it
    // -- and the mesh must point at that one, not at the importer's default.
    ASSERT_GE(cpu.materials.size(), 1u);
    int authored = -1;
    for (size_t i = 0; i < cpu.materials.size(); ++i)
        if (cpu.materials[i].name == "grid_heavy") authored = (int)i;
    ASSERT_NE(authored, -1) << "no material named grid_heavy survived Decode; the authored "
                               "name was lost (materials: " << cpu.materials.size() << ")";
    EXPECT_EQ(cpu.meshes[0].materialIndex, authored)
        << "the quad is bound to material " << cpu.meshes[0].materialIndex
        << " but the authored material is " << authored;
}

TEST(ModelDecodeGltf, AGltfAndAnObjOfTheSameQuadSampleTheSameTexel) {
    const ModelCPUData g = Model::Decode(modelFixturesDir() + "/uv_quad.gltf");
    const ModelCPUData o = Model::Decode(modelFixturesDir() + "/uv_quad.obj");
    ASSERT_TRUE(g.valid) << "uv_quad.gltf did not decode";
    ASSERT_TRUE(o.valid) << "uv_quad.obj did not decode";
    ASSERT_EQ(g.meshes.size(), 1u);
    ASSERT_EQ(o.meshes.size(), 1u);
    ASSERT_EQ(g.meshes[0].vertices.size(), 4u);
    ASSERT_EQ(o.meshes[0].vertices.size(), 4u);

    int matched = 0;
    for (const Vertex& gv : g.meshes[0].vertices) {
        const Vertex* ov = findAt(o.meshes[0].vertices, gv.Position.x, gv.Position.y, gv.Position.z);
        ASSERT_NE(ov, nullptr) << "the OBJ has no vertex at the glTF corner ("
                               << gv.Position.x << ", " << gv.Position.z << ")";
        EXPECT_NEAR(gv.TexCoords.x, ov->TexCoords.x, 1e-5f)
            << "u differs at (" << gv.Position.x << ", " << gv.Position.z << ")";
        EXPECT_NEAR(gv.TexCoords.y, ov->TexCoords.y, 1e-5f)
            << "v differs at (" << gv.Position.x << ", " << gv.Position.z
            << "): the importers' flips and aiProcess_FlipUVs no longer cancel into "
               "one convention, and one format's textures are upside down";
        ++matched;
    }
    EXPECT_EQ(matched, 4);

    // The convention itself, so a change cannot flip BOTH and pass: the
    // far-left corner carries the image's TOP-left texel, and the decoded v
    // there is 0 -- top-left origin, against the rows stbi flips on load.
    const Vertex* farLeft = findAt(g.meshes[0].vertices, -0.5f, 0.f, -0.5f);
    ASSERT_NE(farLeft, nullptr);
    EXPECT_NEAR(farLeft->TexCoords.x, 0.f, 1e-5f);
    EXPECT_NEAR(farLeft->TexCoords.y, 0.f, 1e-5f)
        << "the far-left corner must decode to v = 0; the renderer's convention moved";
}

// ============================================================================
// Skeleton and skin weights (ROADMAP M3.2b; ADR-019 D1/D2)
// ============================================================================
//
// Skinning is the one renderer feature the showcase freeze admits, and this is
// its CPU half: one Skeleton per model from the node tree (parent-first), and
// per-vertex joints/weights BESIDE the vertices so the static Vertex, the LOD
// stride and every shipped OBJ stay byte for byte what they were.

namespace {

const Vertex* firstAtY(const std::vector<Vertex>& vs, float y) {
    for (const Vertex& v : vs)
        if (std::fabs(v.Position.y - y) < 1e-4f) return &v;
    return nullptr;
}

std::size_t indexOf(const std::vector<Vertex>& vs, const Vertex* v) {
    return static_cast<std::size_t>(v - vs.data());
}

} // namespace

TEST(ModelDecodeSkin, TwoBoneStripYieldsTwoNamedJointsWithNormalisedWeights) {
    const ModelCPUData cpu = Model::Decode(modelFixturesDir() + "/two_bone_strip.gltf");
    ASSERT_TRUE(cpu.valid) << "two_bone_strip.gltf did not decode: " << cpu.importError;

    ASSERT_EQ(cpu.skeleton.joints.size(), 2u) << "the strip binds to exactly root and tip";
    EXPECT_EQ(cpu.skeleton.joints[0].name, "root");
    EXPECT_EQ(cpu.skeleton.joints[0].parent, -1);
    EXPECT_EQ(cpu.skeleton.joints[1].name, "tip");
    EXPECT_EQ(cpu.skeleton.joints[1].parent, 0) << "tip's parent is root";
    EXPECT_TRUE(cpu.skeleton.ParentsPrecedeChildren());
    EXPECT_EQ(cpu.skeleton.Find("tip"), 1);
    EXPECT_EQ(cpu.skeleton.Find("nobody"), -1);
    // tip sits one unit above root: its local bind translates by +1 in y, and
    // its inverse bind undoes exactly that.
    EXPECT_NEAR(cpu.skeleton.joints[1].localBind[3][1], 1.0f, 1e-5f)
        << "tip's local bind pose lost its (0, 1, 0) translation";
    EXPECT_NEAR(cpu.skeleton.joints[1].inverseBind[3][1], -1.0f, 1e-5f)
        << "tip's inverse bind matrix is not the inverse of its bind pose";

    ASSERT_EQ(cpu.meshes.size(), 1u);
    const auto& m = cpu.meshes[0];
    ASSERT_FALSE(m.skin.Empty()) << "a mesh with bones decoded with no skin stream";
    ASSERT_EQ(m.skin.joints.size(), m.vertices.size());
    ASSERT_EQ(m.skin.weights.size(), m.vertices.size());
    for (std::size_t i = 0; i < m.vertices.size(); ++i) {
        const glm::vec4& w = m.skin.weights[i];
        EXPECT_NEAR(w.x + w.y + w.z + w.w, 1.0f, 1e-5f)
            << "vertex " << i << "'s weights do not sum to one";
        for (int s = 0; s < 4; ++s)
            EXPECT_LT(m.skin.joints[i][s], 2) << "vertex " << i << " names a joint that does not exist";
    }
    // The bottom row is root's alone, the top row tip's alone, and the middle
    // row -- authored 0.3 / 0.3 -- is half and half after normalisation.
    const Vertex* bottom = firstAtY(m.vertices, 0.f);
    const Vertex* middle = firstAtY(m.vertices, 1.f);
    const Vertex* top    = firstAtY(m.vertices, 2.f);
    ASSERT_NE(bottom, nullptr); ASSERT_NE(middle, nullptr); ASSERT_NE(top, nullptr);
    {
        const glm::ivec4& j = m.skin.joints[indexOf(m.vertices, bottom)];
        const glm::vec4&  w = m.skin.weights[indexOf(m.vertices, bottom)];
        EXPECT_EQ(j.x, 0); EXPECT_NEAR(w.x, 1.f, 1e-5f);
    }
    {
        const glm::ivec4& j = m.skin.joints[indexOf(m.vertices, top)];
        const glm::vec4&  w = m.skin.weights[indexOf(m.vertices, top)];
        EXPECT_EQ(j.x, 1) << "the top row must bind to tip";
        EXPECT_NEAR(w.x, 1.f, 1e-5f);
    }
    {
        const glm::vec4& w = m.skin.weights[indexOf(m.vertices, middle)];
        float wRoot = 0.f, wTip = 0.f;
        const glm::ivec4& j = m.skin.joints[indexOf(m.vertices, middle)];
        for (int s = 0; s < 4; ++s) {
            if (w[s] == 0.f) continue;
            if (j[s] == 0) wRoot += w[s]; else if (j[s] == 1) wTip += w[s];
        }
        EXPECT_NEAR(wRoot, 0.5f, 1e-5f) << "0.3/0.3 authored must normalise to 0.5/0.5";
        EXPECT_NEAR(wTip,  0.5f, 1e-5f);
    }
}

TEST(ModelDecodeSkin, TwoMeshesSharingOneSkinShareOneSkeleton) {
    const ModelCPUData cpu = Model::Decode(modelFixturesDir() + "/two_meshes_one_skin.gltf");
    ASSERT_TRUE(cpu.valid) << cpu.importError;
    ASSERT_EQ(cpu.meshes.size(), 2u);
    ASSERT_EQ(cpu.skeleton.joints.size(), 2u)
        << "two meshes on one skin must yield ONE skeleton, not one per mesh's aiBone list";
    for (const auto& m : cpu.meshes) {
        ASSERT_FALSE(m.skin.Empty());
        const Vertex* top = firstAtY(m.vertices, 2.f);
        ASSERT_NE(top, nullptr);
        EXPECT_EQ(m.skin.joints[indexOf(m.vertices, top)].x, 1)
            << "both meshes must name tip by the same index";
    }
}

TEST(ModelDecodeSkin, AnObjDecodeCarriesNoSkeletonAndItsVertexBytesAreUnchanged) {
    static_assert(sizeof(Vertex) == 14 * sizeof(float),
                  "the static Vertex grew: skinning must live BESIDE it, not inside it "
                  "(every shipped OBJ and the LOD stride depend on this)");
    const ModelCPUData cpu = Model::Decode(modelFixturesDir() + "/uv_quad.obj");
    ASSERT_TRUE(cpu.valid);
    EXPECT_TRUE(cpu.skeleton.Empty()) << "an OBJ has no bones and must carry no skeleton";
    EXPECT_TRUE(cpu.importError.empty());
    for (const auto& m : cpu.meshes)
        EXPECT_TRUE(m.skin.Empty()) << "an unskinned mesh must carry no skin stream";
}

TEST(ModelDecodeSkin, ARigOverThePaletteCapIsRefusedNamingTheCount) {
    const ModelCPUData cpu = Model::Decode(modelFixturesDir() + "/too_many_joints.gltf");
    EXPECT_FALSE(cpu.valid) << "a 129-joint rig must be refused, never truncated";
    EXPECT_NE(cpu.importError.find("129"), std::string::npos)
        << "the refusal must name the count; it says: " << cpu.importError;
    EXPECT_NE(cpu.importError.find("128"), std::string::npos)
        << "the refusal must name the cap; it says: " << cpu.importError;
    EXPECT_TRUE(cpu.skeleton.Empty());
}

// ============================================================================
// Clips on the 60 Hz grid, integer frames only (ROADMAP M3.2c; ADR-019 D2)
// ============================================================================
//
// Sample k IS key k. The decoder asserts the grid so a clip exported at the
// wrong rate, or with Optimize Animation Size left on, is refused at import
// naming the clip and the key -- never shown a frame late.

TEST(ModelDecodeClips, AClipAuthoredAtFourteenFramesDecodesToFourteenFrames) {
    const ModelCPUData cpu = Model::Decode(modelFixturesDir() + "/two_bone_strip.gltf");
    ASSERT_TRUE(cpu.valid) << cpu.importError;
    ASSERT_EQ(cpu.clips.clips.size(), 2u) << "the strip carries 'fourteen' and 'held'";

    const Clip* fourteen = cpu.clips.Find("fourteen");
    ASSERT_NE(fourteen, nullptr);
    EXPECT_EQ(fourteen->frames, 14u) << "14 keys on the grid are 14 frames, no more, no fewer";
    EXPECT_EQ(fourteen->joints, 2u);
    EXPECT_EQ(fourteen->local.size(), 14u * 2u);

    const Clip* held = cpu.clips.Find("held");
    ASSERT_NE(held, nullptr);
    EXPECT_EQ(held->frames, 5u)
        << "five identical keys are five frames: a held pose is not collapsed into one";
    for (std::uint32_t k = 1; k < held->frames; ++k)
        EXPECT_EQ(held->LocalAt(k, 1), held->LocalAt(0, 1))
            << "held frame " << k << " differs from frame 0";

    // Frame k of 'fourteen' rotates tip by 5k degrees about X: frames differ,
    // and the first is the rest orientation.
    EXPECT_NE(fourteen->LocalAt(1, 1), fourteen->LocalAt(0, 1));
    EXPECT_NE(fourteen->LocalAt(13, 1), fourteen->LocalAt(12, 1));
    EXPECT_EQ(cpu.clips.Find("nobody"), nullptr);
}

TEST(ModelDecodeClips, ARotationOnlyJointIsConstantNotRefused) {
    const ModelCPUData cpu = Model::Decode(modelFixturesDir() + "/two_bone_strip.gltf");
    ASSERT_TRUE(cpu.valid) << "a joint with a rotation channel and no position/scale channel must decode: "
                           << cpu.importError;
    const Clip* fourteen = cpu.clips.Find("fourteen");
    ASSERT_NE(fourteen, nullptr);
    // root has NO channel: it wears its bind pose in every frame.
    for (std::uint32_t k = 0; k < fourteen->frames; ++k)
        EXPECT_EQ(fourteen->LocalAt(k, 0), cpu.skeleton.joints[0].localBind)
            << "root, which the clip never animates, left its bind pose at frame " << k;
    // tip's translation stays (0, 1, 0) on every frame -- the single synthesised
    // position key is a constant -- while its rotation changes.
    for (std::uint32_t k = 0; k < fourteen->frames; ++k) {
        const glm::mat4& m = fourteen->LocalAt(k, 1);
        EXPECT_NEAR(m[3][0], 0.f, 1e-5f);
        EXPECT_NEAR(m[3][1], 1.f, 1e-5f) << "tip's constant translation moved at frame " << k;
        EXPECT_NEAR(m[3][2], 0.f, 1e-5f);
    }
}

TEST(ModelDecodeClips, AClipWhoseKeysAreOffTheSixtyHertzGridIsRefusedNamingTheClipAndTheKey) {
    const ModelCPUData cpu = Model::Decode(modelFixturesDir() + "/off_grid.gltf");
    EXPECT_FALSE(cpu.valid) << "a key at 1/50 s is off the 60 Hz grid and must be refused";
    EXPECT_NE(cpu.importError.find("offgrid"), std::string::npos)
        << "the refusal must name the clip; it says: " << cpu.importError;
    EXPECT_NE(cpu.importError.find("key 1"), std::string::npos)
        << "the refusal must name the key; it says: " << cpu.importError;
    EXPECT_TRUE(cpu.clips.Empty());
}

TEST(ModelDecodeClips, AClipCarriesNoTimeOnlyFrames) {
    // The compile-time half of "the sampler has no clock": the frame count is
    // an integer, and the fixture's clips read back as integers that agree
    // with the file's key counts. A Clip with a duration in seconds could not
    // pass the second half without the first.
    static_assert(std::is_same_v<decltype(Clip::frames), std::uint32_t>, "Clip::frames is an integer");
    static_assert(!std::is_floating_point_v<decltype(Clip::frames)>, "no seconds in a Clip");
    const ModelCPUData cpu = Model::Decode(modelFixturesDir() + "/two_bone_strip.gltf");
    ASSERT_TRUE(cpu.valid);
    for (const Clip& c : cpu.clips.clips)
        EXPECT_EQ(c.local.size(), static_cast<std::size_t>(c.frames) * c.joints)
            << "clip " << c.name << " does not hold exactly frames x joints transforms";
}
