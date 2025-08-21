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

        myRenderer.SetUIDraw([this, &scene](float dt) {
            ui_.BeginFrame();

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
            
            
            if (ImGui::CollapsingHeader("Rendering Toggles", ImGuiTreeNodeFlags_None)) {
                bool inst = scene.GetInstancingEnabled();
                if (ImGui::Checkbox("Enable instancing", &inst)) {
                    scene.SetInstancingEnabled(inst);
                }

                bool nm = scene.GetNormalMapEnabled();
                if (ImGui::Checkbox("Enable normal mapping", &nm)) {
                    scene.SetNormalMapEnabled(nm);
                }

                bool pbr = scene.GetPBREnabled();
                if (ImGui::Checkbox("Enable PBR (Cook-Torrance)", &pbr)) {
                    scene.SetPBREnabled(pbr);
                }
            }
            // ---- Sun / Shadow frustum controls ----
            if (ImGui::CollapsingHeader("Sun / Shadows Controls", ImGuiTreeNodeFlags_None)) {
                // Sun direction (normalized)

                bool on = myRenderer.getCSMEnabled();
                if (ImGui::Checkbox("CSM Enabled", &on)) myRenderer.setCSMEnabled(on);

                glm::vec3 dir = myRenderer.sunDir();
                if (ImGui::DragFloat3("Sun dir", &dir.x, 0.01f, -1.0f, 1.0f)) {
                    if (glm::length(dir) > 1e-6f) dir = glm::normalize(dir);
                    myRenderer.setSunDir(dir);
                }
                float pad = myRenderer.getCSMCascadePadding();
                float sNear = myRenderer.getCSMDepthMargin();
				float sFar = myRenderer.getCSMMaxShadowDistance();
                if (ImGui::SliderFloat("Cascade Padding", &pad, 0, 50.f)) myRenderer.setCSMCascadePadding(pad);
				if (ImGui::SliderFloat("Shadow Depth Margin", &sNear, 0.0f, 50.f)) myRenderer.setCSMDepthMargin(sNear);
				if (ImGui::SliderFloat("Max Shadow Distance", &sFar, 10.f, 2000.f)) myRenderer.setCSMMaxShadowDistance(sFar);

                int res = myRenderer.getCSMBaseResolution();
                if (ImGui::SliderInt("CSM Base Resolution", &res, 512, 4096)) myRenderer.setCSMBaseResolution(res);

                int casc = myRenderer.getCSMNumCascades();
                if (ImGui::SliderInt("Cascades", &casc, 1, 4)) myRenderer.setCSMNumCascades(casc);

                float lambda = myRenderer.getCSMLambda();
                if (ImGui::SliderFloat("CSM Lambda", &lambda, 0.f, 1.f)) myRenderer.setCSMLambda(lambda);

                float posEps, angEps; myRenderer.getCSMEpsilons(posEps, angEps);
                if (ImGui::SliderFloat("CSM Pos Epsilon (m)", &posEps, 0.f, 0.5f) ||
                    ImGui::SliderFloat("CSM Ang Epsilon (deg)", &angEps, 0.f, 5.f)) {
                    myRenderer.setCSMEpsilons(posEps, angEps);
                }

                int budget = myRenderer.getCSMCascadeBudget();
                if (ImGui::SliderInt("Cascade Budget", &budget, 0, 4)) myRenderer.setCSMCascadeBudget(budget);
                // 0 = update all cascades when movement happens

                // Force one-off refresh button:
                if (ImGui::Button("Force Rebuild CSM")) myRenderer.forceCSMUpdate();


                if (ImGui::CollapsingHeader("CSM Debug", ImGuiTreeNodeFlags_None)) {
                    static const char* kModes[] = { "Off", "Cascade index", "Shadow factor", "Light depth", "Sampled depth", "Projected UV"};
                    int dbg = myRenderer.csmDebugMode();
                    if (ImGui::Combo("CSM Debug", &dbg, kModes, IM_ARRAYSIZE(kModes))) {
                        myRenderer.setCSMDebugMode(dbg);
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled("(?)");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                        "Off: normal shading\n"
                        "Cascade index: colors by split (0/1/2)\n"
                        "Shadow factor: PCF result (white=lit)\n"
                        "Light depth: light-space depth 0..1");
                }
            }
            

            // Material sliders
            if (ImGui::CollapsingHeader("Materials", ImGuiTreeNodeFlags_None)) {
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


            if (ImGui::CollapsingHeader("IBL/HDR", ImGuiTreeNodeFlags_None)) {
                bool ibl = scene.GetIBLEnabled();
                if (ImGui::Checkbox("Enable IBL", &ibl)) {
                    scene.SetIBLEnabled(ibl);
                }

                float iblInt = scene.GetIBLIntensity();
                if (ImGui::SliderFloat("IBL Intensity", &iblInt, 0.0f, 4.0f)) {
                    scene.SetIBLIntensity(iblInt);
                }

                // ---- HDR exposure ----
                float exposure = myRenderer.exposure();
                ImGui::SliderFloat("Exposure", &exposure, 0.2f, 5.0f);
                myRenderer.setExposure(exposure);
            }

            

            if (ImGui::CollapsingHeader("Lights", ImGuiTreeNodeFlags_None)) {
                // Light controls
                auto& Ld = scene.LightDir();
                auto& Lc = scene.LightColor();
                auto& Li = scene.LightIntensity();
                ImGui::SeparatorText("Light");
                ImGui::DragFloat3("Dir", &Ld.x, 0.01f);
                ImGui::ColorEdit3("Color", &Lc.x);
                ImGui::SliderFloat("Intensity", &Li, 0.0f, 10.0f);
            }
            

            ImGui::End();

            if (hierarchy_.Draw(scene.registry, selected_)) { /* optional */ }
            
            inspector_.Draw(scene.registry, selected_);

            ui_.EndFrame();
        });
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

MyCoreEngine::Application* MyCoreEngine::CreateApplication()
{
    EditorApplication* app = new EditorApplication();
    app->Initialize();
    return app;
}

