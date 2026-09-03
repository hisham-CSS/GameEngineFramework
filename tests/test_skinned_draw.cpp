// THE SKINNED GPU PATH, PROBED PIXEL BY PIXEL (ROADMAP M3.2e; ADR-019 D1).
//
// Needs a GL context (a hidden GLFW window; CI runs it under Mesa llvmpipe),
// so it is labelled `gl`. What it pins is the half of skinning that lives on
// the card: a mesh's joints and weights reach attributes 5 and 6, the palette
// reaches the `uBones` block, the SKINNED variant of vertex.glsl moves the
// vertices a joint owns and nothing else, the SKINNED prepass and colour
// programs agree on gl_Position bit for bit (GL_EQUAL), and a posed joint moves
// the normal with it. Every assertion compares two renders in the same context
// (changed versus unchanged, or equal buffers) rather than an absolute colour,
// because llvmpipe and the dev GPU rasterise the same triangle a texel apart.
//
// The fixture is tests/fixtures/models/two_bone_strip.gltf: a strip 0.5 wide
// from y = 0 to y = 2 in the XY plane, facing +Z, bottom row on `root`, top row
// on `tip`, middle row half and half. Drawn under an orthographic camera at 16
// pixels per unit, x in [-2, 2], y in [-1, 3].
#include <gtest/gtest.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "Engine.h"
#include "../src/render/passes/ForwardOpaquePass.h"

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace MyCoreEngine;

namespace {

std::string modelFixturesDir() {
    namespace fs = std::filesystem;
    fs::path here = fs::current_path();
    for (int i = 0; i < 8; ++i) {
        const fs::path candidate = here / "tests" / "fixtures" / "models";
        if (fs::exists(candidate / "two_bone_strip.gltf")) return candidate.string();
        if (!here.has_parent_path() || here.parent_path() == here) break;
        here = here.parent_path();
    }
    return "tests/fixtures/models";
}

constexpr int kW = 64, kH = 64;

// Test-local fragment stages, written beside the executable because Shader
// reads files. Both declare vertex.glsl's VS_OUT block.
const char* kFlatFrag =
    "#version 330 core\n"
    "in VS_OUT { vec2 uv; mat3 TBN; vec3 worldPos; float viewDepth; } fs_in;\n"
    "out vec4 FragColor;\n"
    "void main() { FragColor = vec4(1.0, 0.0, 0.0, 1.0); }\n";
const char* kNormalFrag =
    "#version 330 core\n"
    "in VS_OUT { vec2 uv; mat3 TBN; vec3 worldPos; float viewDepth; } fs_in;\n"
    "out vec4 FragColor;\n"
    "void main() { FragColor = vec4(abs(normalize(fs_in.TBN[2])), 1.0); }\n";

struct SkinnedDrawFixture : ::testing::Test {
    static GLFWwindow* win;
    static void SetUpTestSuite() {
        ASSERT_TRUE(glfwInit());
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        win = glfwCreateWindow(kW, kH, "headless", nullptr, nullptr);
        ASSERT_NE(nullptr, win);
        glfwMakeContextCurrent(win);
        ASSERT_TRUE(MyCoreEngine::EnsureGLADLoaded()) << "Engine GLAD init failed";
        ASSERT_EQ(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress), 1) << "Test GLAD init failed";
        { std::ofstream f("skinned_test_flat_frag.glsl");   f << kFlatFrag; }
        { std::ofstream f("skinned_test_normal_frag.glsl"); f << kNormalFrag; }
    }
    static void TearDownTestSuite() {
        std::remove("skinned_test_flat_frag.glsl");
        std::remove("skinned_test_normal_frag.glsl");
        glfwMakeContextCurrent(nullptr);
        glfwDestroyWindow(win);
        glfwTerminate();
    }
    void SetUp() override {
        glfwMakeContextCurrent(win);
        ASSERT_TRUE(MyCoreEngine::EnsureGLADLoaded());
        ASSERT_EQ(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress), 1);
        makeTarget();
        model = std::make_unique<Model>(modelFixturesDir() + "/two_bone_strip.gltf");
        ASSERT_TRUE(model->IsSkinned()) << "two_bone_strip.gltf did not load as a skinned model";
        ASSERT_EQ(model->GetSkeleton().joints.size(), 2u);
        ASSERT_EQ(model->Meshes().size(), 1u);
        ASSERT_TRUE(model->Meshes()[0].Skinned()) << "the mesh has no skin VBO";
        palette = std::make_unique<SkinPaletteUBO>();
        ASSERT_TRUE(palette->Valid());
    }
    void TearDown() override {
        model.reset();
        palette.reset();
        if (fbo) glDeleteFramebuffers(1, &fbo);
        if (color) glDeleteTextures(1, &color);
        if (depth) glDeleteTextures(1, &depth);
        fbo = color = depth = 0;
    }

    void makeTarget() {
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glGenTextures(1, &color);
        glBindTexture(GL_TEXTURE_2D, color);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kW, kH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glGenTextures(1, &depth);
        glBindTexture(GL_TEXTURE_2D, depth);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, kW, kH, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth, 0);
        ASSERT_EQ(glCheckFramebufferStatus(GL_FRAMEBUFFER), GL_FRAMEBUFFER_COMPLETE);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // A skinned program from vertex.glsl and a test-local fragment stage, its
    // uBones block routed to the palette's binding point.
    std::unique_ptr<Shader> program(const char* fragPath) {
        auto s = std::make_unique<Shader>("Exported/Shaders/vertex.glsl", fragPath, "#define SKINNED 1");
        EXPECT_TRUE(s->isValid()) << "the SKINNED variant of vertex.glsl with " << fragPath << " did not build";
        EXPECT_TRUE(s->bindUniformBlock(SkinPaletteUBO::kBlockName, SkinPaletteUBO::kBinding))
            << "the SKINNED variant has no uBones block";
        return s;
    }

    void setCamera(Shader& s) {
        s.use();
        s.setMat4("model", glm::mat4(1.0f));
        s.setMat4("view", glm::mat4(1.0f));
        s.setMat4("projection", glm::ortho(-2.0f, 2.0f, -1.0f, 3.0f, -3.0f, 3.0f));
        s.setInt("uUseInstancing", 0);
    }

    void beginFrame() {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, kW, kH);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void drawStrip(Shader& s, const glm::mat4* mats, std::size_t count) {
        s.use();
        palette->Upload(mats, count);
        palette->Bind();
        glBindVertexArray(model->Meshes()[0].VAO());
        model->Meshes()[0].IssueDraw(0);
        glBindVertexArray(0);
    }

    std::array<std::uint8_t, 4> pixel(int x, int y) {
        std::array<std::uint8_t, 4> px{ 0, 0, 0, 0 };
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        return px;
    }
    std::vector<std::uint8_t> frame() {
        std::vector<std::uint8_t> buf(static_cast<std::size_t>(kW) * kH * 4);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        glReadPixels(0, 0, kW, kH, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
        return buf;
    }

    GLuint fbo = 0, color = 0, depth = 0;
    std::unique_ptr<Model>          model;
    std::unique_ptr<SkinPaletteUBO> palette;
};
GLFWwindow* SkinnedDrawFixture::win = nullptr;

// Pixel coordinates of a strip point at 16 px per unit: x in [-2, 2] -> 0..64,
// y in [-1, 3] -> 0..64.
int px(float x) { return static_cast<int>((x + 2.0f) * 16.0f); }
int py(float y) { return static_cast<int>((y + 1.0f) * 16.0f); }

} // namespace

TEST_F(SkinnedDrawFixture, ThePaletteMovesTheVerticesTheJointOwns) {
    auto flat = program("skinned_test_flat_frag.glsl");
    if (::testing::Test::HasFailure()) return;
    setCamera(*flat);

    // Rest: identity palette. The strip covers x in [-0.25, 0.25] for all y.
    const glm::mat4 rest[2] = { glm::mat4(1.0f), glm::mat4(1.0f) };
    beginFrame();
    drawStrip(*flat, rest, 2);
    const auto topRest    = pixel(px(0.0f), py(1.875f));
    const auto bottomRest = pixel(px(0.0f), py(0.125f));
    const auto rightRest  = pixel(px(0.95f), py(1.875f));
    EXPECT_GT(topRest[0], 128)    << "rest: the top of the strip is not drawn";
    EXPECT_GT(bottomRest[0], 128) << "rest: the bottom of the strip is not drawn";
    EXPECT_LT(rightRest[0], 128)  << "rest: nothing should be drawn one unit to the right";

    // Move tip by +1 in x: the top row (tip's alone) shifts a full unit, the
    // middle row (half and half) half a unit, and the bottom row (root's
    // alone) must not move at all.
    glm::mat4 posed[2] = { glm::mat4(1.0f), glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f)) };
    beginFrame();
    drawStrip(*flat, posed, 2);
    const auto topPosed    = pixel(px(0.0f), py(1.875f));
    const auto bottomPosed = pixel(px(0.0f), py(0.125f));
    const auto rightPosed  = pixel(px(0.95f), py(1.875f));
    EXPECT_LT(topPosed[0], 128)    << "posed: the top of the strip did not leave its rest position";
    EXPECT_GT(rightPosed[0], 128)  << "posed: the top of the strip did not arrive one unit to the right";
    EXPECT_GT(bottomPosed[0], 128) << "posed: root's vertices moved although only tip was posed";
}

TEST_F(SkinnedDrawFixture, PrepassOnAndOffDrawTheSamePixelsForASkinnedMesh) {
    auto flat    = program("skinned_test_flat_frag.glsl");
    auto prepass = std::make_unique<Shader>("Exported/Shaders/vertex.glsl", "Exported/Shaders/prepass_frag.glsl",
                                            "#define SKINNED 1");
    ASSERT_TRUE(prepass->isValid()) << "the SKINNED prepass program did not build";
    ASSERT_TRUE(prepass->bindUniformBlock(SkinPaletteUBO::kBlockName, SkinPaletteUBO::kBinding));
    if (::testing::Test::HasFailure()) return;
    setCamera(*flat);
    setCamera(*prepass);

    // A non-trivial pose, so the two programs have real arithmetic to agree on.
    const glm::mat4 pivot = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 posed[2] = { glm::mat4(1.0f),
                           pivot * glm::rotate(glm::mat4(1.0f), glm::radians(30.0f), glm::vec3(0, 0, 1)) * glm::inverse(pivot) };

    // Without a prepass.
    beginFrame();
    drawStrip(*flat, posed, 2);
    const std::vector<std::uint8_t> plain = frame();

    // With: depth only through the SKINNED prepass, then colour under GL_EQUAL.
    beginFrame();
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    drawStrip(*prepass, posed, 2);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthFunc(GL_EQUAL);
    glDepthMask(GL_FALSE);
    drawStrip(*flat, posed, 2);
    const std::vector<std::uint8_t> prepassed = frame();

    ASSERT_EQ(plain.size(), prepassed.size());
    std::size_t differing = 0, lit = 0;
    for (std::size_t i = 0; i < plain.size(); i += 4) {
        if (plain[i] > 128) ++lit;
        if (plain[i] != prepassed[i]) ++differing;
    }
    EXPECT_GT(lit, 0u) << "nothing was drawn at all";
    EXPECT_EQ(differing, 0u)
        << differing << " pixels differ between the prepassed and the plain draw: the SKINNED prepass "
           "and colour programs no longer agree on gl_Position (invariant gl_Position lost, or the "
           "skin arithmetic diverged between the two)";
}

TEST_F(SkinnedDrawFixture, ALitSkinnedQuadShadesByItsPosedNormal) {
    auto normals = program("skinned_test_normal_frag.glsl");
    if (::testing::Test::HasFailure()) return;
    setCamera(*normals);

    // Rest: the strip faces +Z, so the normal-as-colour is pure blue everywhere.
    const glm::mat4 rest[2] = { glm::mat4(1.0f), glm::mat4(1.0f) };
    beginFrame();
    drawStrip(*normals, rest, 2);
    const auto topRest    = pixel(px(0.0f), py(1.5f));
    const auto bottomRest = pixel(px(0.0f), py(0.125f));
    EXPECT_GT(topRest[2], 200)    << "rest: the top of the strip does not face +Z";
    EXPECT_LT(topRest[1], 30);
    EXPECT_GT(bottomRest[2], 200) << "rest: the bottom of the strip does not face +Z";

    // Tilt tip 45 degrees about X around its own pivot (0, 1, 0): the upper
    // half folds toward the camera, its normal gains a y component, and the
    // bottom row, root's alone, keeps facing +Z.
    const glm::mat4 pivot = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 posed[2] = { glm::mat4(1.0f),
                           pivot * glm::rotate(glm::mat4(1.0f), glm::radians(45.0f), glm::vec3(1, 0, 0)) * glm::inverse(pivot) };
    beginFrame();
    drawStrip(*normals, posed, 2);
    const auto topPosed    = pixel(px(0.0f), py(1.5f));
    const auto bottomPosed = pixel(px(0.0f), py(0.125f));
    EXPECT_GT(topPosed[1], 100)
        << "posed: the tilted top of the strip still shades with its rest normal (no y component)";
    EXPECT_LT(topPosed[2], 220)
        << "posed: the tilted top of the strip still shades as if facing +Z exactly";
    EXPECT_GT(bottomPosed[2], 200) << "posed: root's normal moved although only tip was posed";
    EXPECT_LT(bottomPosed[1], 30);
}

// ============================================================================
// Submission, bounds and shadows for posed items (ROADMAP M3.2f)
// ============================================================================
//
// The Scene now routes a posed entity through the skinned program with its own
// palette, culls it on its pose bounds, and counts it as a dynamic caster every
// frame. These drive the real ForwardOpaquePass over a real Scene.

namespace {

struct HdrTarget {
    PassContext ctx{};
    GLuint depthTex = 0;
    void make() {
        glGenFramebuffers(1, &ctx.hdrFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, ctx.hdrFBO);
        glGenTextures(1, &ctx.hdrColorTex);
        glBindTexture(GL_TEXTURE_2D, ctx.hdrColorTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, kW, kH, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glGenTextures(1, &depthTex);
        glBindTexture(GL_TEXTURE_2D, depthTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, kW, kH, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ctx.hdrColorTex, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTex, 0);
        ctx.hdrDepthTex = depthTex;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        ctx.csm.enabled = false;
        ctx.csm.cascades = 0;
    }
    void destroy() {
        if (ctx.hdrFBO) glDeleteFramebuffers(1, &ctx.hdrFBO);
        if (ctx.hdrColorTex) glDeleteTextures(1, &ctx.hdrColorTex);
        if (depthTex) glDeleteTextures(1, &depthTex);
        ctx.hdrFBO = ctx.hdrColorTex = depthTex = 0;
    }
    // True when any of the 3x3 pixels around the projection of `world` is lit.
    bool litAround(const glm::vec3& world, const FrameParams& fp) const {
        const glm::vec3 p = glm::project(world, fp.view, fp.proj, glm::vec4(0, 0, kW, kH));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, ctx.hdrFBO);
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const int x = (int)p.x + dx, y = (int)p.y + dy;
                if (x < 0 || y < 0 || x >= kW || y >= kH) continue;
                float px[4] = { 0, 0, 0, 0 };
                glReadPixels(x, y, 1, 1, GL_RGBA, GL_FLOAT, px);
                if (px[0] + px[1] + px[2] > 0.005f) return true;
            }
        return false;
    }
    int litPixels() const {
        std::vector<float> buf(static_cast<std::size_t>(kW) * kH * 4);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, ctx.hdrFBO);
        glReadPixels(0, 0, kW, kH, GL_RGBA, GL_FLOAT, buf.data());
        int n = 0;
        for (std::size_t i = 0; i < buf.size(); i += 4)
            if (buf[i] + buf[i + 1] + buf[i + 2] > 0.005f) ++n;
        return n;
    }
};

Entity addPosed(Scene& scene, const std::shared_ptr<Model>& model, float x, const glm::mat4& tip) {
    Entity e = scene.createEntity();
    Transform t{};
    t.position = { x, 0.f, 0.f };
    t.dirty = true;
    e.addComponent<Transform>(t);
    e.addComponent<ModelComponent>(ModelComponent{ model });
    e.addComponent<AABB>(generateAABB(*model));
    SkinnedPose sp;
    sp.palette = { glm::mat4(1.0f), tip };
    sp.valid = true;
    scene.registry.emplace<SkinnedPose>(e, std::move(sp));
    return e;
}

FrameParams frameFor(Camera& cam, float nearClip, float farClip) {
    cam.NearClip = nearClip;
    cam.FarClip = farClip;
    FrameParams fp{};
    fp.view = cam.GetViewMatrix();
    fp.proj = glm::perspective(glm::radians(cam.Zoom), 1.0f, nearClip, farClip);
    fp.viewportW = kW;
    fp.viewportH = kH;
    return fp;
}

} // namespace

TEST_F(SkinnedDrawFixture, TwoFightersSharingOneMeshNeverShareOnePose) {
    auto shared = std::make_shared<Model>(modelFixturesDir() + "/two_bone_strip.gltf");
    ASSERT_TRUE(shared->IsSkinned());
    Shader forward("Exported/Shaders/vertex.glsl", "Exported/Shaders/frag.glsl");
    ASSERT_TRUE(forward.isValid());

    Scene scene;
    // A at rest on the left; B on the right with its tip shifted half a unit right.
    addPosed(scene, shared, -1.0f, glm::mat4(1.0f));
    addPosed(scene, shared, +1.0f, glm::translate(glm::mat4(1.0f), glm::vec3(0.5f, 0.0f, 0.0f)));
    scene.UpdateTransforms();

    HdrTarget hdr;
    hdr.make();
    Camera cam;
    cam.Position = { 0.f, 1.f, 6.f };
    cam.Front = { 0.f, 0.f, -1.f };
    cam.Zoom = 45.f;
    const FrameParams fp = frameFor(cam, 0.1f, 100.f);

    hdr.ctx.sunDir = glm::normalize(glm::vec3(-0.3f, -1.0f, -0.2f));
    ForwardOpaquePass fwd(forward);
    fwd.setup(hdr.ctx);
    ASSERT_TRUE(fwd.execute(hdr.ctx, scene, cam, fp));

    // The top of A is where it rests; the top of B left its rest spot and
    // arrived half a unit right. Drawing both with the pose of A leaves the top
    // of B at rest; drawing both with the pose of B moves the top of A to -0.5.
    // Two posed entities on one mesh are two draws, never one instanced
    // draw with one palette, and neither was culled.
    const auto& st = scene.GetRenderStats();
    EXPECT_EQ(st.culled, 0u);
    EXPECT_EQ(st.draws, 2u) << "a posed run must be one item drawn on its own";
    EXPECT_EQ(st.instancedDraws, 0u) << "posed items must never instance-collapse";

    EXPECT_TRUE(hdr.litAround({ -1.0f, 1.875f, 0.f }, fp))  << "the top of A is not drawn at its rest position";
    EXPECT_FALSE(hdr.litAround({ -0.5f, 1.875f, 0.f }, fp)) << "the top of A moved: A was drawn with the pose of B";
    EXPECT_FALSE(hdr.litAround({ 1.0f, 1.875f, 0.f }, fp))  << "the top of B stayed at rest: B was drawn with the pose of A";
    EXPECT_TRUE(hdr.litAround({ 1.5f, 1.875f, 0.f }, fp))   << "the top of B did not arrive half a unit right";
    hdr.destroy();
}

TEST_F(SkinnedDrawFixture, APosedLimbOutsideTheRestBoxIsStillDrawn) {
    auto shared = std::make_shared<Model>(modelFixturesDir() + "/two_bone_strip.gltf");
    ASSERT_TRUE(shared->IsSkinned());
    const Clip* fourteen = shared->GetClips().Find("fourteen");
    ASSERT_NE(fourteen, nullptr);
    Shader forward("Exported/Shaders/vertex.glsl", "Exported/Shaders/frag.glsl");
    ASSERT_TRUE(forward.isValid());

    // Frame 13 tilts tip 65 degrees about X around (0, 1, 0): the top of the
    // strip swings out to z ~ 0.9, well outside the rest mesh, which is flat
    // at z = 0.
    std::vector<glm::mat4> palette(2);
    SamplePalette(shared->GetSkeleton(), *fourteen, 13, palette.data());

    Scene scene;
    Entity e = addPosed(scene, shared, 0.0f, palette[1]);
    scene.UpdateTransforms();

    // A camera whose FAR plane sits between the rest plane and the swung
    // limb: z = 0 is 4 units away (beyond 3.55), the limb is inside.
    HdrTarget hdr;
    hdr.make();
    Camera cam;
    cam.Position = { 0.f, 1.5f, 4.f };
    cam.Front = { 0.f, 0.f, -1.f };
    cam.Zoom = 60.f;
    const FrameParams fp = frameFor(cam, 0.1f, 3.55f);
    hdr.ctx.sunDir = glm::normalize(glm::vec3(-0.3f, -1.0f, -0.2f));
    ForwardOpaquePass fwd(forward);
    fwd.setup(hdr.ctx);

    // With the pose-bounds AABB (what generateAABB gives a skinned model), the
    // entity survives the frustum cull and its swung limb is drawn.
    ASSERT_TRUE(fwd.execute(hdr.ctx, scene, cam, fp));
    const int litPosed = hdr.litPixels();
    EXPECT_GT(litPosed, 0) << "the swung limb was culled although its pose bounds reach the camera";

    // Control: with the REST box (flat at z = 0, beyond the far plane) the
    // same entity is culled and nothing is drawn -- the bug this WP removes.
    glm::vec3 lo(FLT_MAX), hi(-FLT_MAX);
    for (const auto& m : shared->Meshes())
        for (const auto& v : m.Vertices()) { lo = glm::min(lo, v.Position); hi = glm::max(hi, v.Position); }
    scene.registry.emplace_or_replace<AABB>(e, AABB(lo, hi));
    ASSERT_TRUE(fwd.execute(hdr.ctx, scene, cam, fp));
    EXPECT_EQ(hdr.litPixels(), 0) << "the rest box should have culled the entity; the control is wrong";
    hdr.destroy();
}

TEST_F(SkinnedDrawFixture, AFighterAnimatingInPlaceRedrawsItsShadowCascade) {
    auto shared = std::make_shared<Model>(modelFixturesDir() + "/two_bone_strip.gltf");
    ASSERT_TRUE(shared->IsSkinned());

    Scene scene;
    Entity posed = addPosed(scene, shared, 0.0f, glm::mat4(1.0f));
    Entity still = scene.createEntity();
    {
        Transform t{}; t.position = { 3.f, 0.f, 0.f }; t.dirty = true;
        still.addComponent<Transform>(t);
        still.addComponent<ModelComponent>(ModelComponent{ shared });
        still.addComponent<AABB>(generateAABB(*shared));
    }
    const glm::vec3 camPos{ 0.f, 1.f, 6.f }, camFwd{ 0.f, 0.f, -1.f };
    const glm::vec3 sun = glm::normalize(glm::vec3(-0.3f, -1.0f, -0.2f));

    scene.UpdateTransforms();   // first frame: every transform is dirty anyway
    scene.UpdateTransforms();   // second frame: transforms clean, nothing moved
    EXPECT_TRUE(scene.HasDynamicCasterInViewRange(camPos, camFwd, 0.1f, 100.f, sun))
        << "a posed caster with a clean transform must still count as dynamic: its pose "
           "changes every tick and the cascade would otherwise keep a stale shadow";

    scene.registry.get<SkinnedPose>(posed).valid = false;
    scene.UpdateTransforms();
    EXPECT_FALSE(scene.HasDynamicCasterInViewRange(camPos, camFwd, 0.1f, 100.f, sun))
        << "with no valid pose and no transform change, nothing is dynamic";
}
