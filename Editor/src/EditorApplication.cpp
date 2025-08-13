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

            ImGui::Begin("Stats", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::Text("dt: %.3f ms (%.1f FPS)", dt * 1000.f, dt > 0.f ? 1.f / dt : 0.f);
            ImGui::End();

            if (hierarchy_.Draw(scene.registry, selected_)) { /* optional */ }
            inspector_.Draw(scene.registry, selected_);

            ui_.EndFrame();
        });
    });

    // Make GL ready before creating any GL objects (Shaders, Models)
    myRenderer.InitGL();

    Shader shader("Exported/Shaders/vertex.glsl",
        "Exported/Shaders/frag.glsl");

    assert(glfwGetCurrentContext() != nullptr);
    // ---- Create the model once, share it across entities ----
    auto modelHandle = std::make_shared<MyCoreEngine::Model>("Exported/Model/backpack.obj");
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

    myRenderer.run(scene, shader);
}

MyCoreEngine::Application* MyCoreEngine::CreateApplication()
{
    EditorApplication* app = new EditorApplication();
    app->Initialize();
    return app;
}

