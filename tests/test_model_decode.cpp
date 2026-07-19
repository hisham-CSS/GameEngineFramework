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
