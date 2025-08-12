#include "EditorApplication.h"

#include "Engine.h"                 // Renderer/Scene/Shader headers aggregated or include individually
#include "EditorImGuiLayer.h"
#include "panels/SceneHierarchyPanel.h"
#include "panels/InspectorPanel.h"

#include "imgui.h"

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

    /*Model loadedModel("Exported/Model/backpack.obj");
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
    }*/
}

//for the future for any initalization things that are required
void EditorApplication::Initialize()
{
    // Load resources once during initialization:
    MyCoreEngine::SetImageFlipVerticallyOnLoad(true); // or false
}

void EditorApplication::Run() {
    using namespace MyCoreEngine;

    Scene  scene;
    Shader shader("Exported/Shaders/vertex.glsl",
        "Exported/Shaders/frag.glsl");

    // Register the context-ready hook BEFORE run()
    // This fires once, right after GLAD has successfully initialized.
    myRenderer.SetOnContextReady([this, &scene]() {
        // a) Initialize ImGui now (context is current, GLAD is loaded)
        EditorImGuiLayer ui;   // static ensures it outlives the lambda frame
        ui.Init(myRenderer.GetNativeWindow());

        // b) Provide capture flags so renderer can skip input when UI has focus
        myRenderer.SetUICaptureProvider([&ui] {
            return std::pair{ ui.WantCaptureKeyboard(), ui.WantCaptureMouse() };
        });

        // c) Create any GL resources here (safe): models, textures, framebuffers, etc.
        // Example (optional):
        // myRenderer.EnqueueModel("Assets/Models/sponza/sponza.obj");

        // d) Provide per-frame UI
        myRenderer.SetUIDraw([&](float dt) {
            ui.BeginFrame();

            ImGui::Begin("Stats", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::Text("dt: %.3f ms (%.1f FPS)", dt * 1000.f, dt > 0.f ? 1.f / dt : 0.f);
            ImGui::End();

            // Panels (these assume you created members or locals as needed)
            static SceneHierarchyPanel hierarchy;
            static InspectorPanel      inspector;
            static entt::entity        selected = entt::null;

            if (hierarchy.Draw(scene.registry, selected)) {
                // selection changed hooks (optional)
            }
            inspector.Draw(scene.registry, selected);

            ui.EndFrame();
        });
    });

    // Hand control to the renderer (will call the ready hook internally)
    myRenderer.run(scene, shader);
}

MyCoreEngine::Application* MyCoreEngine::CreateApplication()
{
    EditorApplication* app = new EditorApplication();
    app->Initialize();
    return app;
}

