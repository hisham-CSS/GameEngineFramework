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

    myRenderer.SetOnContextReady([this, &scene]() {
        // GL context + GLAD are ready here
        ui_.Init(myRenderer.GetNativeWindow());


        // SAFE: capture 'this' (EditorApplication) whose lifetime spans the run loop
        myRenderer.SetUICaptureProvider([this] {
            return std::pair<bool, bool>{
                ui_.WantCaptureKeyboard(),
                    ui_.WantCaptureMouse()
            };
        });
    });

    myRenderer.SetUIDraw([this, &scene](float dt) {
        ui_.BeginFrame();
        //Information Panel
        DrawInformationPanel(scene, dt);
            
        //rendering window
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
    myRenderer.InitGL();
    assets_ = std::make_unique<AssetManager>(); // create after GL is ready
    std::unique_ptr<Shader> shader = std::make_unique<Shader>("Exported/Shaders/vertex.glsl",
        "Exported/Shaders/frag.glsl");

    assert(glfwGetCurrentContext() != nullptr);
    // --- Load or reuse a model by path ---
    auto modelHandle = assets_->GetModel("Exported/Model/backpack.obj");  // shared
    AABB localBV = generateAABB(*modelHandle); // if you still use local-space AABB
    
    {
        Entity firstEntity = scene.createEntity();
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

    myRenderer.run(scene, *shader);
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

    float exposure = myRenderer.exposure();
    if (ImGui::SliderFloat("Exposure", &exposure, 0.2f, 5.0f)) myRenderer.setExposure(exposure);
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

    bool useYawPitch = myRenderer.getUseSunYawPitch();
    if (ImGui::Checkbox("Rotate Sun (Yaw/Pitch)", &useYawPitch)) myRenderer.setUseSunYawPitch(useYawPitch);

    if (useYawPitch) {
        float yaw, pitch; myRenderer.getSunYawPitchDegrees(yaw, pitch);
        if (ImGui::SliderFloat("Yaw", &yaw, -180.f, 180.f) ||
            ImGui::SliderFloat("Pitch", &pitch, -89.f, 89.f)) {
            myRenderer.setSunYawPitchDegrees(yaw, pitch);
        }
    }
    else {
        glm::vec3 dir = myRenderer.sunDir();
        if (ImGui::DragFloat3("Sun dir", &dir.x, 0.01f, -1.0f, 1.0f)) {
            if (glm::length(dir) > 1e-6f) dir = glm::normalize(dir);
            myRenderer.setSunDir(dir);
        }
    }

    // Optionally sync scene shading light with sun
    bool useSunForShading = true;
    if (ImGui::Checkbox("Use Sun Dir for Shading Light", &useSunForShading)) {
        if (useSunForShading) scene.LightDir() = myRenderer.sunDir();
    }
    //if (useSunForShading) scene.LightDir() = myRenderer.sunDir();
    // --- CSM Controls ---
    ImGui::SeparatorText("Cascaded Shadows");

    bool on = myRenderer.getCSMEnabled();
    if (ImGui::Checkbox("CSM Enabled", &on)) myRenderer.setCSMEnabled(on);

    int casc = myRenderer.getCSMNumCascades();
    if (ImGui::SliderInt("Cascades", &casc, 1, 4)) myRenderer.setCSMNumCascades(casc);

    int res = myRenderer.getCSMBaseResolution();
    if (ImGui::SliderInt("Base Resolution", &res, 512, 4096)) myRenderer.setCSMBaseResolution(res);

    float lambda = myRenderer.getCSMLambda();
    if (ImGui::SliderFloat("Split Lambda", &lambda, 0.f, 1.f)) myRenderer.setCSMLambda(lambda);

    float maxDist = myRenderer.getCSMMaxShadowDistance();
    if (ImGui::SliderFloat("Max Shadow Distance", &maxDist, 10.f, 2000.f)) myRenderer.setCSMMaxShadowDistance(maxDist);

    float pad = myRenderer.getCSMCascadePadding();
    if (ImGui::SliderFloat("Cascade Padding (m)", &pad, 0.f, 50.f)) myRenderer.setCSMCascadePadding(pad);

    float margin = myRenderer.getCSMDepthMargin();
    if (ImGui::SliderFloat("Depth Margin (m)", &margin, 0.f, 50.f)) myRenderer.setCSMDepthMargin(margin);

    float posEps, angEps; myRenderer.getCSMEpsilons(posEps, angEps);
    if (ImGui::SliderFloat("Stability Pos Epsilon (m)", &posEps, 0.f, 0.5f) ||
        ImGui::SliderFloat("Stability Ang Epsilon (deg)", &angEps, 0.f, 5.f)) {
        myRenderer.setCSMEpsilons(posEps, angEps);
    }

    int budget = myRenderer.getCSMCascadeBudget();
    if (ImGui::SliderInt("Update Budget (cascades/frame)", &budget, 0, casc)) myRenderer.setCSMCascadeBudget(budget);
    ImGui::SameLine(); ImGui::TextDisabled("(0 = all)");

    // Bias / culling
    ImGui::SeparatorText("Shadow Acne Controls");
    float slope = myRenderer.getCSMSlopeDepthBias();
    float cbias = myRenderer.getCSMConstantDepthBias();
    bool cullFront = myRenderer.getCSMCullFrontFaces();

    if (ImGui::SliderFloat("Slope Depth Bias", &slope, 0.f, 8.f)) myRenderer.setCSMSlopeDepthBias(slope);
    if (ImGui::SliderFloat("Constant Depth Bias", &cbias, 0.f, 16.f)) myRenderer.setCSMConstantDepthBias(cbias);
    if (ImGui::Checkbox("Cull Front Faces", &cullFront)) myRenderer.setCSMCullFrontFaces(cullFront);
    if (ImGui::Button("Force Rebuild CSM")) myRenderer.forceCSMUpdate();
    // Debug
    if (ImGui::CollapsingHeader("CSM Debug", ImGuiTreeNodeFlags_None)) {
        static const char* kModes[] = {
            "Off", "Cascade index", "Shadow factor", "Light depth", "Sampled depth", "Projected UV"
        };
        int dbg = myRenderer.csmDebugMode();
        if (ImGui::Combo("Mode", &dbg, kModes, IM_ARRAYSIZE(kModes))) myRenderer.setCSMDebugMode(dbg);

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
        ImGui::Text("Cascades: %d, res: %d", myRenderer.getCSMNumCascades(), myRenderer.getCSMBaseResolution());
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

