#include <gtest/gtest.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <cstring>
#include <filesystem>
#include "Engine.h"
#include "../src/core/Scene.h"
#include "../src/core/Shader.h"
#include "../src/core/Renderer.h" // for EnsureGLADLoaded?
#include <glm/gtc/matrix_transform.hpp>

using namespace MyCoreEngine;

namespace {

#include <fstream>

    struct SceneFixture : ::testing::Test {
        static GLFWwindow* win;
        static void SetUpTestSuite() {
            ASSERT_TRUE(glfwInit());
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
            win = glfwCreateWindow(64, 64, "headless", nullptr, nullptr);
            ASSERT_NE(nullptr, win);
            glfwMakeContextCurrent(win);
            ASSERT_TRUE(MyCoreEngine::EnsureGLADLoaded()); // Engine.dll's table
            // GLAD tables are PER-MODULE: raw gl* calls from THIS test exe
            // need the exe's own table loaded too, or they're null pointers
            ASSERT_TRUE(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress));
            
            // Create dummy.obj for testing
            std::ofstream val("dummy.obj");
            val << "v -0.5 0 0.5\n"
                << "v  0.5 0 0.5\n"
                << "v  0.5 0 -0.5\n"
                << "f 1 2 3\n";
            val.close();
        }
        static void TearDownTestSuite() {
            glfwDestroyWindow(win);
            glfwTerminate();
            // remove("dummy.obj"); // optional cleanup
        }
    };
    GLFWwindow* SceneFixture::win = nullptr;

    // Helper to create a dummy mesh
    std::shared_ptr<Model> createCubeModel() {
        // We can't easily create a full Model without files, but we can hack a Mesh?
        // Actually, Mesh is RAII.
        // Let's rely on the fact that Scene checks "if (!mc.model) continue;"
        // We need a valid Model.
        // We can create a Model with 0 meshes if we assume the culling happens before mesh iteration?
        // No, Scene.cpp iterates meshes to push DrawItems.
        // So we need at least 1 mesh.
        // Since we are testing logic, we can try to use a "Mock" model if possible,
        // but Model is a concrete class.
        // Ideally we load a tiny obj from disk, but we might not have one guaranteed.
        // Let's skip the "Add to bucket" check for strictly culling if we can't make a model?
        // Wait, "test_render_passes.cpp" uses a "MiniDepthScene" which overrides RenderDepthCascade.
        // Here we want to test Scene::RenderShadowsCombined relative to Registry.
        return nullptr; // TODO
    }
}

// Subclass Scene to inspect protected/private state if necessary
// OR just check the callback invocation count.
class TestableScene : public Scene {
public:
    // We can't access shadowCascadeItems_ easily as it is private and no getter.
    // But we can check if the CALLBACK is called.
    // If bucket is empty, callback is NOT called.
    // So we can verify culling by counting callback invocations for specific cascades.
};

// We need a valid Model to pass the "if (!mc.model)" check.
// We can use the existing "AssetManager" or manual Model construction if headers allow.
// Model.h has "void AddMesh(Mesh&& mesh)".
// Mesh has ctor from vertices.

TEST_F(SceneFixture, RenderShadowsCombined_CullsByZ) {
    TestableScene scene;
    
    // Create a manual model? No, Model is file-based.
    // We expect "dummy.obj" to be present (created by test setup or manually)
    // For now, we assume the test runner ensures this file exists.
    auto model = std::make_shared<Model>("dummy.obj");

    // Entity A: At Z = -10 (Camera Space)
    // Entity B: At Z = -50
    
    // Fix AABB ctor: needs min/max
    AABB b(glm::vec3(-1,-1,-1), glm::vec3(1,1,1));

    auto eA = scene.createEntity();
    eA.addComponent<Transform>().position = glm::vec3(0, 0, -10);
    eA.addComponent<ModelComponent>().model = model;
    eA.addComponent<AABB>(b);

    auto eB = scene.createEntity();
    eB.addComponent<Transform>().position = glm::vec3(0, 0, -50);
    eB.addComponent<ModelComponent>().model = model;
    eB.addComponent<AABB>(b);
    
    scene.UpdateTransforms();

    // Setup Cascades
    // Cascade 0: Near=0, Far=20  (Should contain A)
    // Cascade 1: Near=20, Far=100 (Should contain B)
    
    std::vector<Scene::CascadeParam> params(2);
    // Light VP: identity for simplicity (AABB check passes if we make it huge)
    // We want to test Z-split culling primarily.
    // Make LightVP cover everything so frustum check passes.
    glm::mat4 hugeOrtho = glm::ortho(-1000.f, 1000.f, -1000.f, 1000.f, -1000.f, 1000.f);
    params[0].lightVP = hugeOrtho;
    params[0].splitNear = 0.f;
    params[0].splitFar = 20.f;
    params[0].viewMatrix = glm::identity<glm::mat4>(); 
    
    params[1].lightVP = hugeOrtho;
    params[1].splitNear = 20.f;
    params[1].splitFar = 100.f;
    params[1].viewMatrix = glm::identity<glm::mat4>(); 

    // Dummy shader
    Shader shader("Exported/Shaders/shadow_depth_vert.glsl", "Exported/Shaders/shadow_depth_frag.glsl"); // reuse existing paths

    int calls[2] = {0, 0};
    scene.RenderShadowsCombined(shader, params, [&](int i) {
        if (i >= 0 && i < 2) calls[i]++;
    });

    // We expect both to be called because each cascade has at least 1 item.
    EXPECT_EQ(calls[0], 1);
    EXPECT_EQ(calls[1], 1);
}

// Casters must be culled by the LIGHT frustum only. An object outside a
// cascade's camera Z-slice still casts into it, so the old Z-slice caster
// cull produced pop-in/out during camera movement (removed 2026-07).
TEST_F(SceneFixture, RenderShadowsCombined_CullsByLightFrustum) {
    TestableScene scene;
    auto model = std::make_shared<Model>("dummy.obj");

    // Entity near the origin
    auto eA = scene.createEntity();
    eA.addComponent<Transform>().position = glm::vec3(0, 0, -10);
    eA.addComponent<ModelComponent>().model = model;
    AABB b(glm::vec3(-1,-1,-1), glm::vec3(1,1,1));
    eA.addComponent<AABB>(b);
    scene.UpdateTransforms();

    std::vector<Scene::CascadeParam> params(2);

    // Cascade 0: tiny ortho window centered 500 units away -> caster outside
    params[0].lightVP = glm::ortho(-1.f, 1.f, -1.f, 1.f, -1.f, 1.f)
        * glm::translate(glm::mat4(1.f), glm::vec3(-500.f, 0.f, 0.f));
    params[0].splitNear = 0.f;
    params[0].splitFar = 5.f; // must be ignored for caster culling
    params[0].viewMatrix = glm::identity<glm::mat4>();

    // Cascade 1: huge ortho covering the caster
    params[1].lightVP = glm::ortho(-1000.f, 1000.f, -1000.f, 1000.f, -1000.f, 1000.f);
    params[1].splitNear = 900.f; // way past the caster's camera depth:
    params[1].splitFar = 999.f;  // must NOT exclude it (it can cast into the slice)
    params[1].viewMatrix = glm::identity<glm::mat4>();

    Shader shader("Exported/Shaders/shadow_depth_vert.glsl", "Exported/Shaders/shadow_depth_frag.glsl");

    int calls[2] = {0, 0};
    scene.RenderShadowsCombined(shader, params, [&](int i) {
        calls[i]++;
    });

    EXPECT_EQ(calls[0], 0) << "caster outside cascade 0's light frustum was drawn";
    EXPECT_EQ(calls[1], 1) << "caster inside cascade 1's light frustum was Z-slice culled";
}

// ---- P4-3 phase 2: decode/finalize split (GL half) ----------------------

// The split pipeline must produce the same model the old monolithic
// constructor did — the sync ctor now delegates through it, so this pins
// Decode -> finalize against structural expectations.
TEST_F(SceneFixture, DecodeThenFinalizeMatchesSyncLoad) {
    Model sync("dummy.obj");

    ModelCPUData cpu = Model::Decode("dummy.obj");
    ASSERT_TRUE(cpu.valid);
    Model split(std::move(cpu));

    ASSERT_EQ(split.Meshes().size(), sync.Meshes().size());
    ASSERT_EQ(split.Meshes().size(), 1u);
    const Mesh& a = split.Meshes()[0];
    const Mesh& b = sync.Meshes()[0];
    EXPECT_EQ(a.Vertices().size(), b.Vertices().size());
    EXPECT_EQ(a.IndexCount(), b.IndexCount());
    EXPECT_NE(a.VAO(), 0u) << "finalize must create real GL objects";
    EXPECT_NE(a.VAO(), b.VAO()) << "two models must not share buffers";
    for (int l = 0; l < Mesh::kLodCount; ++l) {
        EXPECT_EQ(a.Lod(l).indexCount, b.Lod(l).indexCount) << "LOD level " << l;
    }
    EXPECT_EQ(split.SourcePath(), sync.SourcePath());
    EXPECT_EQ(split.Materials().size(), sync.Materials().size());
}

// The production shape for phase 3: decode on a WORKER, finalize in the
// main-thread completion with the GL context current.
TEST_F(SceneFixture, WorkerDecodeMainThreadFinalize) {
    JobSystem jobs(2);
    struct State {
        ModelCPUData cpu;
        std::shared_ptr<Model> model;
        std::thread::id decodeThread{};
    };
    auto st = std::make_shared<State>();

    jobs.submit(
        [st] {
            st->decodeThread = std::this_thread::get_id();
            st->cpu = Model::Decode("dummy.obj");
        },
        [st] {
            st->model = std::make_shared<Model>(std::move(st->cpu));
        });
    jobs.waitIdle();
    EXPECT_EQ(st->model, nullptr) << "finalize must wait for the main-thread pump";
    jobs.pumpCompletions(1e6f);

    ASSERT_NE(st->model, nullptr);
    EXPECT_NE(st->decodeThread, std::this_thread::get_id()) << "decode ran on main";
    ASSERT_EQ(st->model->Meshes().size(), 1u);
    EXPECT_NE(st->model->Meshes()[0].VAO(), 0u)
        << "GL objects must come from the main-thread finalize";
    EXPECT_EQ(st->model->SourcePath(), "dummy.obj");
}

namespace {

// hand-rolled 2x2 24bpp BMP (bottom-up rows padded to 4 bytes). Image-space
// colors: TOP row {blue, white}, BOTTOM row {red, green}.
void writeTinyBMP(const std::string& path) {
    const unsigned char px[2][2][3] = { { {255,0,0}, {0,255,0} },    // file row 0 = image BOTTOM
                                        { {0,0,255}, {255,255,255} } }; // file row 1 = image TOP
    std::ofstream bmp(path, std::ios::binary);
    unsigned char hdr[54] = {};
    hdr[0]='B'; hdr[1]='M';
    auto put32 = [&](int off, unsigned int v) {
        hdr[off]=(unsigned char)v; hdr[off+1]=(unsigned char)(v>>8);
        hdr[off+2]=(unsigned char)(v>>16); hdr[off+3]=(unsigned char)(v>>24);
    };
    put32(2, 54 + 16); put32(10, 54); put32(14, 40);
    put32(18, 2); put32(22, 2); // width, height
    hdr[26]=1; hdr[28]=24;      // planes, bpp
    put32(34, 16);
    bmp.write((const char*)hdr, 54);
    for (int y = 0; y < 2; ++y) { // BGR, rows padded to 8 bytes
        unsigned char row[8] = {};
        for (int x = 0; x < 2; ++x) {
            row[x*3+0]=px[y][x][2]; row[x*3+1]=px[y][x][1]; row[x*3+2]=px[y][x][0];
        }
        bmp.write((const char*)row, 8);
    }
}

// obj + mtl + tiny BMP under `dir` with UNIQUE names per call: the engine
// texture cache is process-static and keys by path, so reruns of a test in
// one process (--gtest_repeat) must not cache-hit ids from a dead context.
std::string writeTexturedTriangle(const std::string& dir, const char* texOverride = nullptr) {
    static int sUnique = 0;
    const std::string base = "texmodel_" + std::to_string(++sUnique);
    std::filesystem::create_directories(dir);
    const std::string tex = texOverride ? texOverride : (base + ".bmp");
    if (!texOverride) writeTinyBMP(dir + "/" + tex);
    {
        std::ofstream mtl(dir + "/" + base + ".mtl");
        mtl << "newmtl texmat\nmap_Kd " << tex << "\n";
    }
    {
        std::ofstream obj(dir + "/" + base + ".obj");
        obj << "mtllib " << base << ".mtl\n"
            << "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
            << "vt 0 0\nvt 1 0\nvt 0 1\n"
            << "usemtl texmat\n"
            << "f 1/1 2/2 3/3\n";
    }
    return dir + "/" + base + ".obj";
}

} // namespace

// End-to-end texture path: stb decode into pixels (flip semantics pinned by
// CONTENT, not just dimensions), finalize uploads a REAL GL texture wired to
// the mesh's shared material.
TEST_F(SceneFixture, TexturedModelUploadsRealGLTexture) {
    const std::string objPath = writeTexturedTriangle("texmodels");

    ModelCPUData cpu = Model::Decode(objPath);
    ASSERT_TRUE(cpu.valid);
    ASSERT_EQ(cpu.textures.size(), 1u);
    EXPECT_TRUE(cpu.textures[0].decoded) << "worker stage must deliver pixels";
    EXPECT_EQ(cpu.textures[0].width, 2);
    EXPECT_EQ(cpu.textures[0].height, 2);
    EXPECT_TRUE(cpu.textures[0].srgb); // albedo slot

    // flip-on-load semantics: stb returns the image BOTTOM row first (GL
    // convention, matching the old global-flag loader). Bottom row of the
    // BMP is {red, green}; top is {blue, white}. A dropped/misplaced flip
    // or channel-order regression changes these bytes.
    const auto& p = cpu.textures[0].pixels;
    ASSERT_EQ(p.size(), 16u);
    const unsigned char expect[16] = { 255,0,0,255,  0,255,0,255,     // bottom row first
                                       0,0,255,255,  255,255,255,255 }; // then top row
    EXPECT_EQ(0, memcmp(p.data(), expect, 16)) << "decoded pixel content/orientation changed";

    Model m(std::move(cpu));
    ASSERT_EQ(m.Meshes().size(), 1u);
    const auto& mat = m.Meshes()[0].GetMaterial();
    ASSERT_TRUE(mat) << "mesh must share the imported material";
    ASSERT_NE(mat->albedoTex, 0u) << "finalize must upload a real texture";
    GLint texW = 0;
    glBindTexture(GL_TEXTURE_2D, mat->albedoTex);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &texW);
    glBindTexture(GL_TEXTURE_2D, 0);
    EXPECT_EQ(texW, 2) << "uploaded texture must carry the decoded pixels' size";
}

// The seam phase 3 leans on: two models sharing a texture KEY must share
// one GL id — the second finalize reuses the cached id, no re-upload.
TEST_F(SceneFixture, SharedTextureFinalizesToOneGLId) {
    static int sDir = 0;
    const std::string dir = "texshare_" + std::to_string(++sDir);
    const std::string objA = writeTexturedTriangle(dir);
    // second model in the SAME directory referencing the SAME bmp file
    const std::string texName = std::filesystem::path(objA).stem().string() + ".bmp";
    const std::string objB = writeTexturedTriangle(dir, texName.c_str());

    Model a(Model::Decode(objA));
    Model b(Model::Decode(objB));
    ASSERT_EQ(a.Meshes().size(), 1u);
    ASSERT_EQ(b.Meshes().size(), 1u);
    const auto& matA = a.Meshes()[0].GetMaterial();
    const auto& matB = b.Meshes()[0].GetMaterial();
    ASSERT_TRUE(matA); ASSERT_TRUE(matB);
    ASSERT_NE(matA->albedoTex, 0u);
    EXPECT_EQ(matA->albedoTex, matB->albedoTex)
        << "shared texture key must resolve to the same cached GL id";

    // ...and the skip-set sync path: a decode WITH the cache snapshot must
    // record the slot without pixels yet still finalize to the cached id
    const auto cachedKeys = Model::CachedTextureKeys();
    ModelCPUData cpu = Model::Decode(objB, false, &cachedKeys);
    ASSERT_EQ(cpu.textures.size(), 1u);
    EXPECT_FALSE(cpu.textures[0].decoded) << "cached key must skip stb decode";
    EXPECT_TRUE(cpu.textures[0].pixels.empty());
    Model c(std::move(cpu));
    ASSERT_TRUE(c.Meshes()[0].GetMaterial());
    EXPECT_EQ(c.Meshes()[0].GetMaterial()->albedoTex, matA->albedoTex);
}

// A missing texture FILE (valid model) must yield decoded=false, a
// renderable mesh with albedoTex == 0, and a cached zero — no retry storm,
// and never a bogus 0x0 GL texture.
TEST_F(SceneFixture, MissingTextureFileFinalizesToZeroId) {
    static int sDir = 0;
    const std::string dir = "texmissing_" + std::to_string(++sDir);
    const std::string objA = writeTexturedTriangle(dir, "no_such_texture.bmp");
    const std::string objB = writeTexturedTriangle(dir, "no_such_texture.bmp");

    ModelCPUData cpu = Model::Decode(objA);
    ASSERT_TRUE(cpu.valid);
    ASSERT_EQ(cpu.textures.size(), 1u);
    EXPECT_FALSE(cpu.textures[0].decoded);

    Model a(std::move(cpu));
    ASSERT_EQ(a.Meshes().size(), 1u);
    ASSERT_TRUE(a.Meshes()[0].GetMaterial());
    EXPECT_EQ(a.Meshes()[0].GetMaterial()->albedoTex, 0u);

    // second model, same missing file: the cached zero must be reused
    Model b(Model::Decode(objB));
    ASSERT_TRUE(b.Meshes()[0].GetMaterial());
    EXPECT_EQ(b.Meshes()[0].GetMaterial()->albedoTex, 0u);
}

// GL half of the LOD pipeline on a mesh that actually clears the simplify
// floor: accepted levels get their OWN EBOs with the ACCEPTED counts, the
// precomputed-LOD ctor matches the compute-inline ctor, and destruction
// stays GL-clean. Built in-memory to pin the GL branch without file I/O;
// the import side (JoinIdenticalVertices producing simplifiable indexed
// geometry from a real OBJ) is pinned by BackpackObjAcceptsLodLevels in
// test_model_decode.
TEST_F(SceneFixture, AcceptedLodLevelsGetRealEBOs) {
    while (glGetError() != GL_NO_ERROR) {} // drain stale errors from earlier tests

    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    constexpr int N = 24; // (N-1)^2*2 = 1058 triangles: over the simplify floor
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

    auto lods = Mesh::ComputeLodIndices(vertices, indices);
    bool anyAccepted = false;
    for (int l = 1; l < Mesh::kLodCount; ++l) anyAccepted |= !lods[l].empty();
    ASSERT_TRUE(anyAccepted) << "a dense connected grid must accept at least one level";
    const auto lodSizes = lods; // keep sizes; the ctor consumes the arrays

    {
        // precomputed path (worker shape) vs compute-inline path (old ctor)
        Mesh pre(std::vector<Vertex>(vertices), std::vector<unsigned int>(indices),
                 std::move(lods));
        Mesh inl(vertices, indices, std::vector<Texture>{});

        for (int l = 1; l < Mesh::kLodCount; ++l) {
            EXPECT_EQ(pre.Lod(l).indexCount, inl.Lod(l).indexCount)
                << "precomputed vs inline LOD " << l;
            if (!lodSizes[l].empty()) {
                EXPECT_EQ((size_t)pre.Lod(l).indexCount, lodSizes[l].size())
                    << "uploaded count must be the ACCEPTED count, level " << l;
                EXPECT_NE(pre.Lod(l).ebo, pre.Lod(0).ebo)
                    << "accepted level must own a distinct EBO, level " << l;
                EXPECT_NE(pre.Lod(l).ebo, 0u);
                EXPECT_LT((size_t)pre.Lod(l).indexCount,
                          (size_t)pre.Lod(l - 1).indexCount * 9 / 10 + 1)
                    << "accepted level must meaningfully shrink, level " << l;
            }
            else {
                EXPECT_EQ(pre.Lod(l).ebo, pre.Lod(l - 1).ebo)
                    << "rejected level must alias the previous, level " << l;
            }
        }
    } // meshes destroyed: unique EBOs deleted exactly once, aliases skipped
    EXPECT_EQ(glGetError(), (GLenum)GL_NO_ERROR) << "LOD teardown raised a GL error";
}

// P4-3 phase 3: the full async request round trip — decode on a worker,
// GL finalize in the main-thread pump, handle flips to Live with a real
// renderable model; a second request resolves instantly from the cache.
TEST_F(SceneFixture, RequestModelGoesLiveThroughThePump) {
    using LoadState = AssetManager::LoadState;
    JobSystem jobs(2);
    AssetManager assets;

    auto req = assets.RequestModel(jobs, "dummy.obj");
    ASSERT_TRUE(req != nullptr);
    EXPECT_TRUE(req->state == LoadState::Queued || req->state == LoadState::Decoding);
    EXPECT_EQ(req->model, nullptr) << "no model before the main-thread finalize";

    jobs.waitIdle();
    EXPECT_NE(req->state, LoadState::Live) << "Live requires the main-thread pump";
    jobs.pumpCompletions(1e6f);

    ASSERT_EQ(req->state, LoadState::Live);
    ASSERT_TRUE(req->model != nullptr);
    ASSERT_EQ(req->model->Meshes().size(), 1u);
    EXPECT_NE(req->model->Meshes()[0].VAO(), 0u) << "GL objects from the finalize";
    EXPECT_EQ(assets.pendingRequests(), 0u);

    // cache hit: instant Live, same instance, no job
    auto again = assets.RequestModel(jobs, "dummy.obj");
    EXPECT_EQ(again->state, LoadState::Live);
    EXPECT_EQ(again->model.get(), req->model.get());
    EXPECT_EQ(assets.pendingRequests(), 0u);

    // ...and the sync API sees the async-loaded model too
    EXPECT_EQ(assets.GetModel("dummy.obj").get(), req->model.get());
}

// An empty ModelComponent's null-model guard has a sibling now: a FAILED
// async decode finalizes into a valid-but-empty Model (parity with the old
// failed sync load) — downstream code already handles empty Meshes().
TEST_F(SceneFixture, FailedDecodeFinalizesEmpty) {
    ModelCPUData cpu = Model::Decode("no_such_file.obj");
    EXPECT_FALSE(cpu.valid);
    Model m(std::move(cpu));
    EXPECT_TRUE(m.Meshes().empty());
    EXPECT_EQ(m.SourcePath(), "no_such_file.obj");
}
