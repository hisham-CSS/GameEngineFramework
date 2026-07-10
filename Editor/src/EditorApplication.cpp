#include "EditorApplication.h"

#include "Engine.h"                 // Renderer/Scene/Shader headers aggregated or include individually
#include "EditorImGuiLayer.h"
#include "panels/SceneHierarchyPanel.h"
#include "panels/InspectorPanel.h"

#include "imgui.h"

//for the future for any initalization things that are required
void EditorApplication::Initialize()
{
    // Load resources once during initialization:
    MyCoreEngine::SetImageFlipVerticallyOnLoad(true); // or false
}

void EditorApplication::Run() {
    using namespace MyCoreEngine;

    Scene scene;

    SetOnContextReady([this, &scene]() {
        // GL context + GLAD are ready here
        ui_.Init(GetNativeWindow());


        // SAFE: capture 'this' (EditorApplication) whose lifetime spans the run loop
        SetUICaptureProvider([this] {
            return std::pair<bool, bool>{
                ui_.WantCaptureKeyboard(),
                    ui_.WantCaptureMouse()
            };
        });
    });

    SetUIDraw([this, &scene](float dt) {
        ui_.BeginFrame();
        //Information Panel
        DrawInformationPanel(scene, dt);
            
        //rendering window
        DrawScenePersistence(scene);
        DrawTimeControls();
        DrawInputPanel();
        DrawRenderingToggles(scene);
        DrawLightControls(scene);
        DrawSunShadowControls(scene);
        DrawMaterialControls(scene);
        DrawIBLHDRControls(scene);

		//scene hierarchy
        hierarchy_.Draw(scene.registry, selected_);
            
		//inspector
        inspector_.Draw(scene.registry, selected_);

        ui_.EndFrame();
    });

    // Make GL ready before creating any GL objects (Shaders, Models)
    InitGL();
    assets_ = std::make_unique<AssetManager>(); // create after GL is ready
    std::unique_ptr<Shader> shader = std::make_unique<Shader>("Exported/Shaders/vertex.glsl",
        "Exported/Shaders/frag.glsl");

    assert(glfwGetCurrentContext() != nullptr);
    // --- Load or reuse a model by path ---
    auto modelHandle = assets_->GetModel("Exported/Model/backpack.obj");  // shared
    AABB localBV = generateAABB(*modelHandle); // if you still use local-space AABB
    
    {
        Entity firstEntity = scene.createEntity();
        firstEntity.addComponent<Name>(Name{ "Hero" });
        Transform t{};
        t.position = glm::vec3(0.f, 0.f, 0.f);
        firstEntity.addComponent<Transform>(t);
        firstEntity.addComponent<ModelComponent>(ModelComponent{ modelHandle });
		firstEntity.addComponent<AABB>(localBV); // if you still use local-space AABB
    }
    
    for (unsigned int x = 0; x < 20; ++x) {
        for (unsigned int z = 0; z < 20; ++z) {
            Entity e = scene.createEntity();
            Transform t{};
            t.position = glm::vec3(x * 10.f - 100.f, 0.f, z * 10.f - 100.f);
            e.addComponent<Transform>(t);
            e.addComponent<ModelComponent>(ModelComponent{ modelHandle });
			e.addComponent<AABB>(localBV); // if you still use local-space AABB
        }
    }

    // Ground plane: receives the sun shadows (NoShadow = doesn't cast its own)
    {
        auto groundHandle = assets_->GetModel("Exported/Model/plane.obj");
        Entity ground = scene.createEntity();
        ground.addComponent<Name>(Name{ "Ground" });
        Transform t{};
        t.position = glm::vec3(0.f, -3.f, 0.f);
        t.scale = glm::vec3(300.f, 1.f, 300.f);
        ground.addComponent<Transform>(t);
        ground.addComponent<ModelComponent>(ModelComponent{ groundHandle });
        ground.addComponent<AABB>(generateAABB(*groundHandle));
        // registry directly: Entity::addComponent can't return a reference for
        // empty flag components (EnTT emplace returns void for them)
        scene.registry.emplace<NoShadow>(ground);
    }

    // Demo gameplay on the fixed tick: spin every entity named "Hero".
    // (Real games will hang systems/scripts off this hook — see roadmap P3-9.)
    SetFixedUpdate([&scene](float fixedDt) {
        auto view = scene.registry.view<Name, Transform>();
        for (auto e : view) {
            if (view.get<Name>(e).value != "Hero") continue;
            auto& t = view.get<Transform>(e);
            t.rotation.y += 45.f * fixedDt; // deg/sec, framerate-independent
            if (t.rotation.y >= 360.f) t.rotation.y -= 360.f;
            t.dirty = true;
        }
    });

    RunLoop(scene, *shader);
}

void EditorApplication::DrawInputPanel()
{
    if (!ImGui::CollapsingHeader("Input", ImGuiTreeNodeFlags_None)) return;

    auto& in = input();
    ImGui::Text("Gamepad: %s", in.gamepadConnected() ? "connected" : "not connected");
    ImGui::Text("MoveForward: %+.2f", in.axis("MoveForward"));
    ImGui::Text("MoveRight:   %+.2f", in.axis("MoveRight"));
    ImGui::Text("Look X/Y:    %+.2f / %+.2f", in.axis("LookX"), in.axis("LookY"));
    ImGui::TextDisabled("Defaults: WASD + left stick move, right stick looks,");
    ImGui::TextDisabled("ESC / Back quits. Rebind via Application::input().");
}

void EditorApplication::DrawTimeControls()
{
    if (!ImGui::CollapsingHeader("Time", ImGuiTreeNodeFlags_None)) return;

    bool isPaused = paused();
    if (ImGui::Checkbox("Paused", &isPaused)) setPaused(isPaused);

    float scale = timeScale();
    if (ImGui::SliderFloat("Time Scale", &scale, 0.f, 4.f)) setTimeScale(scale);

    float hz = fixedTimestepHz();
    if (ImGui::SliderFloat("Fixed Tick (Hz)", &hz, 15.f, 240.f, "%.0f")) setFixedTimestepHz(hz);
}

void EditorApplication::DrawScenePersistence(MyCoreEngine::Scene& scene)
{
    if (!ImGui::CollapsingHeader("Scene", ImGuiTreeNodeFlags_None)) return;

    static char scenePath[260] = "Exported/scene.json";
    static std::string lastStatus;
    ImGui::InputText("Scene file", scenePath, sizeof(scenePath));

    MyCoreEngine::SceneSerializer serializer(scene, *assets_);
    if (ImGui::Button("Save Scene")) {
        lastStatus = serializer.Save(scenePath)
            ? std::string("Saved to ") + scenePath
            : std::string("Save FAILED (see console)");
    }
    ImGui::SameLine();
    if (ImGui::Button("Load Scene")) {
        if (serializer.Load(scenePath)) {
            selected_ = entt::null; // old entity handles are invalid after a load
            lastStatus = std::string("Loaded ") + scenePath;
        }
        else {
            lastStatus = "Load FAILED (see console)";
        }
    }
    if (!lastStatus.empty()) ImGui::TextDisabled("%s", lastStatus.c_str());
}

void EditorApplication::DrawLightControls(MyCoreEngine::Scene& scene)
{
    if (!ImGui::CollapsingHeader("Lights (Shading)", ImGuiTreeNodeFlags_None)) return;
    auto& Ld = scene.LightDir();
    auto& Lc = scene.LightColor();
    auto& Li = scene.LightIntensity();
    ImGui::SeparatorText("Direct Light");
    ImGui::DragFloat3("Dir", &Ld.x, 0.01f);
    ImGui::ColorEdit3("Color", &Lc.x);
    ImGui::SliderFloat("Intensity", &Li, 0.0f, 10.0f);
}

void EditorApplication::DrawIBLHDRControls(MyCoreEngine::Scene& scene)
{
    if (!ImGui::CollapsingHeader("IBL/HDR", ImGuiTreeNodeFlags_None)) return;

    bool ibl = scene.GetIBLEnabled();
    if (ImGui::Checkbox("Enable IBL", &ibl)) scene.SetIBLEnabled(ibl);

    float iblInt = scene.GetIBLIntensity();
    if (ImGui::SliderFloat("IBL Intensity", &iblInt, 0.0f, 4.0f)) scene.SetIBLIntensity(iblInt);

    float exposure = renderer().exposure();
    if (ImGui::SliderFloat("Exposure", &exposure, 0.2f, 5.0f)) renderer().setExposure(exposure);
}

void EditorApplication::DrawMaterialControls(MyCoreEngine::Scene& scene)
{
    if (!ImGui::CollapsingHeader("Materials", ImGuiTreeNodeFlags_None)) return;
    float metallic = scene.GetMetallic();
    float roughness = scene.GetRoughness();
    float ao = scene.GetAO();
    if (ImGui::SliderFloat("Metallic", &metallic, 0.0f, 1.0f)) scene.SetMetallic(metallic);
    if (ImGui::SliderFloat("Roughness", &roughness, 0.0f, 1.0f)) scene.SetRoughness(roughness);
    if (ImGui::SliderFloat("AO", &ao, 0.0f, 1.0f)) scene.SetAO(ao);
    bool enMetal = scene.GetMetallicMapEnabled();
    bool enRough = scene.GetRoughnessMapEnabled();
    bool enAO = scene.GetAOMapEnabled();
    if (ImGui::Checkbox("Use Metallic Map", &enMetal))  scene.SetMetallicMapEnabled(enMetal);
    if (ImGui::Checkbox("Use Roughness Map", &enRough)) scene.SetRoughnessMapEnabled(enRough);
    if (ImGui::Checkbox("Use AO Map", &enAO))           scene.SetAOMapEnabled(enAO);
}

void EditorApplication::DrawSunShadowControls(MyCoreEngine::Scene& scene)
{
    if (!ImGui::CollapsingHeader("Sun / Shadows Controls", ImGuiTreeNodeFlags_None)) return;
    // --- Directional light (Unity-style) ---
    ImGui::SeparatorText("Directional Light");

    bool useYawPitch = renderer().getUseSunYawPitch();
    if (ImGui::Checkbox("Rotate Sun (Yaw/Pitch)", &useYawPitch)) renderer().setUseSunYawPitch(useYawPitch);

    if (useYawPitch) {
        float yaw, pitch; renderer().getSunYawPitchDegrees(yaw, pitch);
        if (ImGui::SliderFloat("Yaw", &yaw, -180.f, 180.f) ||
            ImGui::SliderFloat("Pitch", &pitch, -89.f, 89.f)) {
            renderer().setSunYawPitchDegrees(yaw, pitch);
        }
    }
    else {
        glm::vec3 dir = renderer().sunDir();
        if (ImGui::DragFloat3("Sun dir", &dir.x, 0.01f, -1.0f, 1.0f)) {
            if (glm::length(dir) > 1e-6f) dir = glm::normalize(dir);
            renderer().setSunDir(dir);
        }
    }

    // Optionally sync scene shading light with sun
    bool useSunForShading = true;
    if (ImGui::Checkbox("Use Sun Dir for Shading Light", &useSunForShading)) {
        if (useSunForShading) scene.LightDir() = renderer().sunDir();
    }
    //if (useSunForShading) scene.LightDir() = renderer().sunDir();
    // --- CSM Controls ---
    ImGui::SeparatorText("Cascaded Shadows");

    bool on = renderer().getCSMEnabled();
    if (ImGui::Checkbox("CSM Enabled", &on)) renderer().setCSMEnabled(on);

    int casc = renderer().getCSMNumCascades();
    if (ImGui::SliderInt("Cascades", &casc, 1, 4)) renderer().setCSMNumCascades(casc);

    int res = renderer().getCSMBaseResolution();
    if (ImGui::SliderInt("Base Resolution", &res, 512, 4096)) renderer().setCSMBaseResolution(res);

    float lambda = renderer().getCSMLambda();
    if (ImGui::SliderFloat("Split Lambda", &lambda, 0.f, 1.f)) renderer().setCSMLambda(lambda);

    float maxDist = renderer().getCSMMaxShadowDistance();
    if (ImGui::SliderFloat("Max Shadow Distance", &maxDist, 10.f, 2000.f)) renderer().setCSMMaxShadowDistance(maxDist);

    float pad = renderer().getCSMCascadePadding();
    if (ImGui::SliderFloat("Cascade Padding (m)", &pad, 0.f, 50.f)) renderer().setCSMCascadePadding(pad);

    float margin = renderer().getCSMDepthMargin();
    if (ImGui::SliderFloat("Depth Margin (m)", &margin, 0.f, 50.f)) renderer().setCSMDepthMargin(margin);

    float posEps, angEps; renderer().getCSMEpsilons(posEps, angEps);
    if (ImGui::SliderFloat("Stability Pos Epsilon (m)", &posEps, 0.f, 0.5f) ||
        ImGui::SliderFloat("Stability Ang Epsilon (deg)", &angEps, 0.f, 5.f)) {
        renderer().setCSMEpsilons(posEps, angEps);
    }

    int budget = renderer().getCSMCascadeBudget();
    if (ImGui::SliderInt("Update Budget (cascades/frame)", &budget, 0, casc)) renderer().setCSMCascadeBudget(budget);
    ImGui::SameLine(); ImGui::TextDisabled("(0 = all)");

    // Bias / culling
    ImGui::SeparatorText("Shadow Acne Controls");
    float slope = renderer().getCSMSlopeDepthBias();
    float cbias = renderer().getCSMConstantDepthBias();
    bool cullFront = renderer().getCSMCullFrontFaces();

    if (ImGui::SliderFloat("Slope Depth Bias", &slope, 0.f, 8.f)) renderer().setCSMSlopeDepthBias(slope);
    if (ImGui::SliderFloat("Constant Depth Bias", &cbias, 0.f, 16.f)) renderer().setCSMConstantDepthBias(cbias);
    if (ImGui::Checkbox("Cull Front Faces", &cullFront)) renderer().setCSMCullFrontFaces(cullFront);

    ImGui::SeparatorText("Shadow Filtering (PCF)");
    float rbc = renderer().getShadowBiasConst();
    if (ImGui::SliderFloat("Receiver Bias Const (texels)", &rbc, 0.f, 8.f)) renderer().setShadowBiasConst(rbc);
    float rbs = renderer().getShadowBiasSlope();
    if (ImGui::SliderFloat("Receiver Bias Slope (texels)", &rbs, 0.f, 8.f)) renderer().setShadowBiasSlope(rbs);
    static const char* kKernelLabels[4] = {
        "PCF Radius (cascade 0)", "PCF Radius (cascade 1)",
        "PCF Radius (cascade 2)", "PCF Radius (cascade 3)"
    };
    for (int i = 0; i < casc && i < 4; ++i) {
        int r = renderer().getCascadeKernel(i);
        if (ImGui::SliderInt(kKernelLabels[i], &r, 0, 4)) renderer().setCascadeKernel(i, r);
    }

    if (ImGui::Button("Force Rebuild CSM")) renderer().forceCSMUpdate();
    // Debug
    if (ImGui::CollapsingHeader("CSM Debug", ImGuiTreeNodeFlags_None)) {
        static const char* kModes[] = {
            "Off", "Cascade index", "Shadow factor", "Light depth", "Sampled depth", "Projected UV"
        };
        int dbg = renderer().csmDebugMode();
        if (ImGui::Combo("Mode", &dbg, kModes, IM_ARRAYSIZE(kModes))) renderer().setCSMDebugMode(dbg);

        ImGui::SameLine();
        ImGui::TextDisabled("(?)");

        if (ImGui::IsItemHovered()) ImGui::SetTooltip(
            "Off: normal shading\n"
            "Cascade index: color per cascade\n"
            "Shadow factor: PCF result (white=lit)\n"
            "Light depth: light-space depth 0..1\n"
            "Sampled depth / Projected UV: debug sampling");
    }
}

void EditorApplication::DrawRenderingToggles(MyCoreEngine::Scene& scene)
{
    if (!ImGui::CollapsingHeader("Rendering Toggles", ImGuiTreeNodeFlags_None)) return;

    bool vsync = vsyncEnabled();
    if (ImGui::Checkbox("VSync", &vsync)) setVSync(vsync);
    ImGui::SameLine(); ImGui::TextDisabled("(off = uncapped, for benchmarking)");

    bool inst = scene.GetInstancingEnabled();
    if (ImGui::Checkbox("Enable instancing", &inst)) scene.SetInstancingEnabled(inst);

    bool nm = scene.GetNormalMapEnabled();
    if (ImGui::Checkbox("Enable normal mapping", &nm)) scene.SetNormalMapEnabled(nm);

    bool pbr = scene.GetPBREnabled();
    if (ImGui::Checkbox("Enable PBR (Cook-Torrance)", &pbr)) scene.SetPBREnabled(pbr);
}

void EditorApplication::DrawInformationPanel(const MyCoreEngine::Scene& scene, float dt)
{
    const auto& rs = scene.GetRenderStats();
    ImGui::Begin("Information", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    if (ImGui::CollapsingHeader("Rendering Stats", ImGuiTreeNodeFlags_None)) {
        ImGui::Text("dt: %.3f ms (%.1f FPS)", dt * 1000.f, dt > 0.f ? 1.f / dt : 0.f);
        ImGui::Text("Cascades: %d, res: %d", renderer().getCSMNumCascades(), renderer().getCSMBaseResolution());
        ImGui::Text("Draws:            %u", rs.draws);
        ImGui::Text("Instanced draws:  %u", rs.instancedDraws);
        ImGui::Text("Instances:        %u", rs.instances);
        ImGui::Separator();
        ImGui::Text("Texture binds:    %u", rs.textureBinds);
        ImGui::Text("VAO binds:        %u", rs.vaoBinds);
        ImGui::Separator();
        ImGui::Text("Built items:      %u", rs.itemsBuilt);
        ImGui::Text("Culled:           %u", rs.culled);
        ImGui::Text("Submitted:        %u", rs.submitted);
        unsigned totalCalls = rs.draws + rs.instancedDraws;
        ImGui::Text("GPU draw calls:   %u", totalCalls);
    }
    ImGui::End();
}

MyCoreEngine::Application* MyCoreEngine::CreateApplication()
{
    EditorApplication* app = new EditorApplication();
    app->Initialize();
    return app;
}

