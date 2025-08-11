#pragma once
#include "Engine.h"
#include "EditorApplication.h"
#include "EditorImGuiLayer.h"


#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <glm/glm.hpp>

// Helper: create a couple of entities for testing
static void CreateSampleScene(MyCoreEngine::Scene& scene) {
    using namespace MyCoreEngine;
//
//    // Entity 1
//    entt::entity e1 = scene.registry.create();
//    scene.registry.emplace<Transform>(e1, Transform{
//        /*position*/ {0.f, 0.f, 0.f},
//        /*rotation*/ {0.f, 0.f, 0.f},
//        /*scale*/    {1.f, 1.f, 1.f},
//        /*dirty*/    true
//        });
//#ifdef NAME_COMPONENT_ENABLED
//    scene.registry.emplace<Name>(e1, Name{ "Cube A" });
//#endif
//
//    // Entity 2
//    entt::entity e2 = scene.registry.create();
//    scene.registry.emplace<Transform>(e2, Transform{
//        {2.f, 0.f, 0.f}, {0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}, true
//        });
//#ifdef NAME_COMPONENT_ENABLED
//    scene.registry.emplace<Name>(e2, Name{ "Cube B" });
//#endif

    // If you have a Model component in your ECS, attach it here.
    // Otherwise skip; your Scene::RenderScene likely renders via a stored list.
    // scene.registry.emplace<ModelComponent>(e1, ModelComponent{/*...*/});
    // scene.registry.emplace<ModelComponent>(e2, ModelComponent{/*...*/});

    Model loadedModel("Exported/Model/backpack.obj");
    AABB boundingVol = generateAABB(loadedModel);
    
    Entity firstEntity = scene.createEntity();
    Transform transform;
    transform.position = glm::vec3(0.f, 0.f, 0.f);
    firstEntity.addComponent<Transform>(transform);
    firstEntity.addComponent<Model>(loadedModel);
    firstEntity.addComponent<AABB>(boundingVol);


    for (unsigned int x = 0; x < 20; ++x) {
        for (unsigned int z = 0; z < 20; ++z) {
            Entity newEntity = scene.createEntity();
            Transform newTransform;
            newTransform.position = glm::vec3(x * 10.f - 100.f, 0.f, z * 10.f - 100.f);
            newEntity.addComponent<Transform>(newTransform);
            newEntity.addComponent<Model>(loadedModel);
            newEntity.addComponent<AABB>(boundingVol);
        }
    }
}

//for the future for any initalization things that are required
void EditorApplication::Initialize()
{
    // Load resources once during initialization:
    MyCoreEngine::SetImageFlipVerticallyOnLoad(true); // or false
}

void EditorApplication::Run() {
    using namespace MyCoreEngine;

    // 1) Build a simple scene
    Scene scene;
    CreateSampleScene(scene);

    // 2) Load a shader (adjust paths to match your repo)
    Shader shader("Exported/Shaders/vertex.glsl",
        "Exported/Shaders/frag.glsl");
    

    // 3) Initialize editor-side ImGui with the renderer’s GLFW window
    EditorImGuiLayer ui;
    ui.Init(myRenderer.GetNativeWindow());

    // 4) Panels and selection state
    SceneHierarchyPanel hierarchy;
    InspectorPanel inspector;
    entt::entity selected = entt::null;

    // 5) Let the renderer know when ImGui wants to capture input
    myRenderer.SetUICaptureProvider([&ui]() -> std::pair<bool, bool> {
        return { ui.WantCaptureKeyboard(), ui.WantCaptureMouse() };
    });

    // 6) Provide the per-frame UI callback (ImGui lives entirely here)
    myRenderer.SetUIDraw([&](float dt) {
        ui.BeginFrame();

        // Stats (keep it tiny and useful)
        ImGui::Begin("Stats", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("dt: %.3f ms (%.1f FPS)", dt * 1000.f, (dt > 0.f ? 1.f / dt : 0.f));
        ImGui::End();

        // Scene hierarchy + inspector
        if (hierarchy.Draw(scene.registry, selected)) {
            // selection changed → optional hooks here (e.g., focus camera)
        }
        inspector.Draw(scene.registry, selected);

        ui.EndFrame(); // does ImGui::Render and OpenGL backend draw
    });

    // 7) Hand off control to the renderer’s main loop
    //    (Renderer will: compute deltaTime, handle input unless UI captures,
    //     update transforms, draw 3D scene, then call your UI callback)
    myRenderer.run(scene, shader);

    // 8) Cleanup handled by destructors (ui.Shutdown() called in ~EditorImGuiLayer)
}

MyCoreEngine::Application* MyCoreEngine::CreateApplication()
{
    EditorApplication* app = new EditorApplication();
    app->Initialize();
    return app;
}

