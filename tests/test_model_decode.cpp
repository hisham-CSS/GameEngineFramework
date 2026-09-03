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
