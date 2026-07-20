// tests/test_perf_render.cpp — automated render performance harness (P4-8).
//
// Headless benchmark scenes rendered through the REAL pipeline
// (Renderer::RenderFrame: CSM -> forward PBR -> tonemap into an offscreen
// 1080p RenderTarget), with frame-time budget assertions so perf
// regressions surface in ctest instead of manual editor A/B sessions.
//
// Methodology:
// - warmup frames first (shader/LOD/CSM/instance-buffer high-water marks
//   settle), then glFinish() and time N frames of
//   UpdateTransforms + RenderFrame + glFinish (full CPU+GPU cost, no
//   pipelining overlap — stable regression metric, not a FPS estimate).
// - assertions are on the MEDIAN (robust against OS scheduling blips);
//   p95 is printed for eyeballing stutter.
// - budgets are ~2x the 2026-07-12 baseline medians measured on the
//   shipping target (i5-11400H + RTX 3050 Laptop, 1920x1080) — loose
//   enough not to flake, tight enough to catch a real regression.
//   On other machines set CSE_PERF_BUDGET_SCALE (e.g. 2.0) to loosen.
// - scenes must keep doing real work: each scenario also asserts a
//   minimum submitted-item count so a culling/scene bug can't silently
//   turn the benchmark into an empty-view no-op that always "passes".
//
// ctest: labeled "perf", RUN_SERIAL (other tests must not pollute timing).

#include <gtest/gtest.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "Engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <vector>

using namespace MyCoreEngine;

// Hybrid-graphics laptops route new GL contexts to the power-saving iGPU
// by default; these exports ask the NVIDIA/AMD drivers for the discrete
// GPU (the shipping target), same as a real game exe would. Without them
// the whole benchmark silently measures the Intel iGPU (~5-10x slower).
extern "C" {
    __declspec(dllexport) unsigned long NvOptimusEnablement = 1;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformanceGpu = 1;
}

namespace {

    // ---- budgets (median ms), ~2x the measured baseline medians. ----
    // Baselines 2026-07-18 (aiProcess_JoinIdenticalVertices: OBJ imports
    // finally produce indexed geometry, so LOD1/2 actually accept — wide
    // views render mostly LOD1/2 index buffers now), RTX 3050 @1920x1080.
    // "cpu" = median time until RenderFrame returns (pre-glFinish): the
    // main-thread build/cull/sort/submit share of the frame.
    //   at-rest 20x20 spawn view    6.1 ms (cpu  1.3)  ~12.3k instances
    //   sustained camera orbit      7.4 ms (cpu  2.1)
    //   wide view 25x25            16.9 ms (cpu  6.5)  ~40k instances
    //   wide view 25x25 moving     23.8 ms (cpu 11.7)  CSM movement cadence
    //   wide 25x25 move+spin       32.7 ms (cpu  9.5)  p95 ~55: far-cascade
    //     dynamic re-renders amortize every 4 frames (bounded spike)
    //   dynamic caster (spin)      16.5 ms (cpu  3.3)
    // (2026-07-13 pre-LOD-fix medians for reference: 14.6 / 17.3 / 45.7 /
    //  50.0 / 51.4 / 24.6 — a regression to those means LODs went inert
    //  again and MUST fail these budgets.)
    // If a failure is an intentional cost (new feature), re-measure and
    // update the budget + baseline lines here.
    constexpr double kBudgetAtRestMs = 12.0;
    constexpr double kBudgetCameraMoveMs = 15.0;
    constexpr double kBudgetWideViewMs = 35.0;
    constexpr double kBudgetWideMovingMs = 50.0;
    constexpr double kBudgetWideMoveSpinMs = 65.0;
    constexpr double kBudgetDynamicCasterMs = 35.0;

    double budgetScale() {
        if (const char* s = std::getenv("CSE_PERF_BUDGET_SCALE")) {
            char* end = nullptr;
            const double v = std::strtod(s, &end);
            if (end != s && v > 0.0) {
                std::printf("[PERF] budget scale: %.2f (CSE_PERF_BUDGET_SCALE)\n", v);
                return v;
            }
            // a typo'd knob must not silently tighten budgets back to 1.0 —
            // that's exactly the machine where the run would then fail
            std::printf("[PERF] WARNING: ignoring unparseable "
                        "CSE_PERF_BUDGET_SCALE='%s', using 1.0\n", s);
        }
        return 1.0;
    }

    struct PerfFixture : ::testing::Test {
        static GLFWwindow* win;
        static std::unique_ptr<AssetManager> assets;
        static std::unique_ptr<Shader> shader;
        // pin the models for the whole suite: the AssetManager cache is
        // weak, so without these each test reloads them from disk after
        // the previous test's scene releases its handles
        static std::shared_ptr<Model> backpack;
        static std::shared_ptr<Model> plane;

        static void SetUpTestSuite() {
            ASSERT_TRUE(glfwInit());
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
            win = glfwCreateWindow(64, 64, "perf-headless", nullptr, nullptr);
            ASSERT_NE(nullptr, win);
            glfwMakeContextCurrent(win);
            ASSERT_TRUE(MyCoreEngine::EnsureGLADLoaded());
            ASSERT_EQ(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress), 1);
            glfwSwapInterval(0);

            // budgets are only meaningful against the GPU they were measured
            // on — make every run say which one it used
            std::printf("[PERF] GL_RENDERER: %s\n", (const char*)glGetString(GL_RENDERER));
            std::fflush(stdout);

            assets = std::make_unique<AssetManager>();
            shader = std::make_unique<Shader>("Exported/Shaders/vertex.glsl",
                                              "Exported/Shaders/frag.glsl");
            backpack = assets->GetModel("Exported/Model/backpack.obj");
            plane = assets->GetModel("Exported/Model/plane.obj");
        }

        static void TearDownTestSuite() {
            // GL objects must die while the context is still current
            plane.reset();
            backpack.reset();
            shader.reset();
            assets.reset();
            glfwMakeContextCurrent(nullptr);
            glfwDestroyWindow(win);
            glfwTerminate();
        }

        void SetUp() override { glfwMakeContextCurrent(win); }

        // ---- scene building (mirrors the editor's demo content) ----
        struct BuiltScene {
            entt::entity hero = entt::null;
        };

        static BuiltScene buildGrid(Scene& scene, int nx, int nz, float spacing = 10.f) {
            BuiltScene out;
            auto model = assets->GetModel("Exported/Model/backpack.obj");
            if (!model || model->Meshes().empty()) {
                ADD_FAILURE() << "backpack.obj failed to load";
                return out;
            }
            const AABB localBV = generateAABB(*model);
            const float ox = (nx - 1) * spacing * 0.5f;
            const float oz = (nz - 1) * spacing * 0.5f;
            for (int x = 0; x < nx; ++x) {
                for (int z = 0; z < nz; ++z) {
                    Entity e = scene.createEntity();
                    Transform t{};
                    t.position = glm::vec3(x * spacing - ox, 0.f, z * spacing - oz);
                    e.addComponent<Transform>(t);
                    e.addComponent<ModelComponent>(ModelComponent{ model });
                    e.addComponent<AABB>(localBV);
                    if (x == nx / 2 && z == nz / 2) {
                        e.addComponent<Name>(Name{ "Hero" });
                        out.hero = e;
                    }
                }
            }
            // ground plane, receives shadows without casting (as in the editor)
            auto ground = assets->GetModel("Exported/Model/plane.obj");
            if (ground && !ground->Meshes().empty()) {
                Entity g = scene.createEntity();
                Transform t{};
                t.position = glm::vec3(0.f, -3.f, 0.f);
                t.scale = glm::vec3(300.f, 1.f, 300.f);
                g.addComponent<Transform>(t);
                g.addComponent<ModelComponent>(ModelComponent{ ground });
                g.addComponent<AABB>(generateAABB(*ground));
                scene.registry.emplace<NoShadow>(g);
            }
            return out;
        }

        static void aim(Camera& cam, glm::vec3 pos, glm::vec3 target) {
            cam.Position = pos;
            cam.Front = glm::normalize(target - pos);
            // full orthonormal basis: frustum culling reads Right/Up directly
            // (createFrustumFromCamera), not just the view matrix
            cam.Right = glm::normalize(glm::cross(cam.Front, cam.WorldUp));
            cam.Up = glm::normalize(glm::cross(cam.Right, cam.Front));
            cam.Zoom = 45.f;
        }

        // ---- measurement ----
        struct PerfResult {
            double medianMs = 0.0;
            double p95Ms = 0.0;
            double cpuMedianMs = 0.0; // until RenderFrame returns (pre-glFinish)
            RenderStats stats{};
        };

        static PerfResult measure(const char* name, Scene& scene, Camera& cam,
                                  const std::function<void(int)>& perFrame = nullptr,
                                  int warmup = 40, int frames = 120) {
            Renderer renderer;
            RenderTarget rt;
            rt.Create(1920, 1080);
            renderer.Setup(rt.width(), rt.height());

            const float dt = 1.f / 60.f; // fixed: deterministic CSM cadence

            for (int i = 0; i < warmup; ++i) {
                if (perFrame) perFrame(i);
                scene.UpdateTransforms();
                renderer.RenderFrame(scene, *shader, cam,
                                     rt.width(), rt.height(), dt, rt.fbo());
            }
            glFinish();

            std::vector<double> ms, cpuMs;
            ms.reserve(frames);
            cpuMs.reserve(frames);
            using clock = std::chrono::steady_clock;
            for (int i = 0; i < frames; ++i) {
                if (perFrame) perFrame(warmup + i); // game logic: untimed
                const auto t0 = clock::now();
                scene.UpdateTransforms();
                renderer.RenderFrame(scene, *shader, cam,
                                     rt.width(), rt.height(), dt, rt.fbo());
                const auto tCpu = clock::now(); // submission done, GPU may still run
                glFinish();
                const auto t1 = clock::now();
                ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
                cpuMs.push_back(std::chrono::duration<double, std::milli>(tCpu - t0).count());
            }

            std::sort(ms.begin(), ms.end());
            std::sort(cpuMs.begin(), cpuMs.end());
            PerfResult r;
            r.medianMs = ms[ms.size() / 2];
            // nearest-rank p95: smallest sample >= 95% of the distribution
            r.p95Ms = ms[(size_t)std::ceil(0.95 * (double)ms.size()) - 1];
            r.cpuMedianMs = cpuMs[cpuMs.size() / 2];
            r.stats = scene.GetRenderStats();
            std::printf("[PERF] %-28s median %7.2f ms (cpu %6.2f)  p95 %7.2f ms  "
                        "(submitted %u, instances %u, culled %u, lod %u/%u/%u)\n",
                        name, r.medianMs, r.cpuMedianMs, r.p95Ms,
                        r.stats.submitted, r.stats.instances, r.stats.culled,
                        r.stats.lodInstances[0], r.stats.lodInstances[1],
                        r.stats.lodInstances[2]);
            std::fflush(stdout);
            return r;
        }
    };
    GLFWwindow* PerfFixture::win = nullptr;
    std::unique_ptr<AssetManager> PerfFixture::assets;
    std::unique_ptr<Shader> PerfFixture::shader;
    std::shared_ptr<Model> PerfFixture::backpack;
    std::shared_ptr<Model> PerfFixture::plane;

} // namespace

// Projected-size culling: on a wide shot with a near->far size gradient, the
// cull must drop a meaningful chunk of the distant (small) instances and must
// NOT be slower than the un-culled baseline. This is the wide-view lever —
// the frame is vertex/instance-bound (measured: shadows/PCF/fill are ~free),
// so fewer submitted instances is the only thing that helps. Functional
// assertions on the counts; timing printed for the log, budget-checked
// elsewhere.
TEST_F(PerfFixture, SmallObjectCull_DropsDistantInstances) {
    Camera cam;
    aim(cam, { 0.f, 40.f, 160.f }, { 0.f, 0.f, -40.f }); // low oblique: size gradient

    Scene off;
    buildGrid(off, 25, 25);
    off.SetSmallCullEnabled(false);
    const auto rOff = measure("size-cull OFF", off, cam);

    Scene on;
    buildGrid(on, 25, 25);
    on.SetSmallCullEnabled(true);
    // This low oblique view spans a large near->far size gradient (near
    // backpacks are ~150px, the far rows ~30px). A 48px floor culls the far
    // portion — enough to prove the mechanism drops a real fraction. (In a
    // real bird's-eye "fly up", far smaller thresholds cull just as hard as
    // everything shrinks uniformly.)
    on.SetSmallCullPixels(48.f);
    const auto rOn = measure("size-cull 48px", on, cam);

    EXPECT_EQ(rOff.stats.culledSmall, 0u) << "cull disabled must drop nothing by size";
    EXPECT_GT(rOn.stats.culledSmall, 0u) << "48px floor must cull the distant small backpacks";
    EXPECT_LT(rOn.stats.submitted, rOff.stats.submitted)
        << "culling must reduce submitted instances";
    // sanity: it removed a real fraction, not one stray entity
    EXPECT_LT(rOn.stats.submitted, (rOff.stats.submitted * 9u) / 10u)
        << "expected the far rows (>10% of instances) to drop";
    // and it must not cost MORE than drawing everything (allow noise headroom)
    EXPECT_LT(rOn.medianMs, rOff.medianMs * 1.25 * budgetScale())
        << "size cull should never be slower than the un-culled frame";
}

// Scenario 1: static camera over the editor's default 20x20 spawn grid.
TEST_F(PerfFixture, AtRest_SpawnView) {
    Scene scene;
    buildGrid(scene, 20, 20);
    Camera cam;
    aim(cam, { 0.f, 10.f, 40.f }, { 0.f, 0.f, 0.f });

    const auto r = measure("at-rest spawn view", scene, cam);
    EXPECT_GT(r.stats.submitted, 2000u) << "benchmark view is nearly empty — scene/culling bug?";
    EXPECT_LT(r.medianMs, kBudgetAtRestMs * budgetScale())
        << "at-rest frame regressed vs baseline (see budgets atop test_perf_render.cpp)";
}

// Scenario 2: sustained camera movement (orbit) — exercises CSM movement
// cadence + cascade rebuilds, the July 2026 hotspot.
TEST_F(PerfFixture, SustainedCameraMovement) {
    Scene scene;
    buildGrid(scene, 20, 20);
    Camera cam;

    const auto r = measure("sustained camera orbit", scene, cam, [&](int i) {
        const float a = 0.01f * (float)i;
        const glm::vec3 pos{ 60.f * std::sin(a), 12.f, 60.f * std::cos(a) };
        aim(cam, pos, { 0.f, 0.f, 0.f });
    });
    EXPECT_GT(r.stats.submitted, 2000u) << "benchmark view is nearly empty — scene/culling bug?";
    EXPECT_LT(r.medianMs, kBudgetCameraMoveMs * budgetScale())
        << "moving-camera frame regressed vs baseline";
}

// Scenario 3: wide view, high instance count (25x25 = 625 backpacks,
// ~35k submitted instances) — the single-threaded build/sort/submit
// stress case P4-9 is meant to fix; this is its canary.
TEST_F(PerfFixture, WideView_HighInstanceCount) {
    Scene scene;
    buildGrid(scene, 25, 25);
    Camera cam;
    aim(cam, { 0.f, 110.f, 150.f }, { 0.f, 0.f, 0.f });

    const auto r = measure("wide view 25x25", scene, cam);
    EXPECT_GT(r.stats.submitted, 15000u) << "wide view lost its instance load — scene/culling bug?";
    EXPECT_LT(r.medianMs, kBudgetWideViewMs * budgetScale())
        << "wide-view frame regressed vs baseline";
}

// Scenario 3b: wide view WHILE MOVING — the July 2026 pain case (20-30 FPS
// in the editor): every camera step triggers CSM cascade rebuilds, so the
// per-frame cost adds shadow bucketing/culling/draws on top of the wide
// forward pass. This is the primary P4-9 target.
TEST_F(PerfFixture, WideView_Moving) {
    Scene scene;
    buildGrid(scene, 25, 25);
    Camera cam;

    const auto r = measure("wide view 25x25 moving", scene, cam, [&](int i) {
        const float a = 0.006f * (float)i;
        const glm::vec3 pos{ 170.f * std::sin(a), 110.f, 170.f * std::cos(a) };
        aim(cam, pos, { 0.f, 0.f, 0.f });
    });
    EXPECT_GT(r.stats.submitted, 15000u) << "wide view lost its instance load — scene/culling bug?";
    EXPECT_LT(r.medianMs, kBudgetWideMovingMs * budgetScale())
        << "moving-wide-view frame regressed vs baseline";
}

// Scenario 3c: the full July 2026 editor reproduction — wide view, camera
// flying, AND the demo Hero spinning every frame (dynamic caster keeps
// invalidating cascades). Worst case for the whole pipeline.
TEST_F(PerfFixture, WideView_MovingWithDynamicCaster) {
    Scene scene;
    const auto built = buildGrid(scene, 25, 25);
    ASSERT_TRUE(built.hero != entt::null);
    Camera cam;

    const auto r = measure("wide 25x25 move+spin", scene, cam, [&](int i) {
        const float a = 0.006f * (float)i;
        aim(cam, { 170.f * std::sin(a), 110.f, 170.f * std::cos(a) }, { 0.f, 0.f, 0.f });
        auto& t = scene.registry.get<Transform>(built.hero);
        t.rotation.y += 45.f * (1.f / 60.f);
        t.dirty = true;
    });
    EXPECT_GT(r.stats.submitted, 15000u) << "wide view lost its instance load — scene/culling bug?";
    EXPECT_LT(r.medianMs, kBudgetWideMoveSpinMs * budgetScale())
        << "move+spin wide-view frame regressed vs baseline";
}

// Scenario 4: dynamic caster — the center entity spins every frame
// (dirty transform), driving the view-depth-scoped shadow invalidation.
TEST_F(PerfFixture, DynamicCasterShadows) {
    Scene scene;
    const auto built = buildGrid(scene, 20, 20);
    ASSERT_TRUE(built.hero != entt::null);
    Camera cam;
    aim(cam, { 0.f, 6.f, 25.f }, { 0.f, 0.f, 0.f });

    const auto r = measure("dynamic caster (spin)", scene, cam, [&](int) {
        auto& t = scene.registry.get<Transform>(built.hero);
        t.rotation.y += 45.f * (1.f / 60.f);
        if (t.rotation.y >= 360.f) t.rotation.y -= 360.f;
        t.dirty = true;
    });
    EXPECT_GT(r.stats.submitted, 2000u) << "benchmark view is nearly empty — scene/culling bug?";
    EXPECT_LT(r.medianMs, kBudgetDynamicCasterMs * budgetScale())
        << "dynamic-caster frame regressed vs baseline";
}
