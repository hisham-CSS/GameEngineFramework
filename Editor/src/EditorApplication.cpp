
#include "Engine.h"
#include "EditorApplication.h"
#include "EditorImGuiLayer.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

//for the future for any initalization things that are required
void EditorApplication::Initialize()
{
    // Load resources once during initialization:
    stbi_set_flip_vertically_on_load(true);
}

void EditorApplication::Run()
{
    Shader shader("Exported/Shaders/vertex.glsl", "Exported/Shaders/frag.glsl");
    Model loadedModel("Exported/Model/backpack.obj");
    AABB boundingVol = generateAABB(loadedModel);

    // Setup scene
    // TODO: Move this to it's own methods
    MyCoreEngine::Scene scene;
    MyCoreEngine::Entity firstEntity = scene.createEntity();
    Transform transform;
    transform.position = glm::vec3(0.f, 0.f, 0.f);
    firstEntity.addComponent<Transform>(transform);
    firstEntity.addComponent<Model>(loadedModel);
    firstEntity.addComponent<AABB>(boundingVol);


    for (unsigned int x = 0; x < 20; ++x) {
        for (unsigned int z = 0; z < 20; ++z) {
            MyCoreEngine::Entity newEntity = scene.createEntity();
            Transform newTransform;
            newTransform.position = glm::vec3(x * 10.f - 100.f, 0.f, z * 10.f - 100.f);
            newEntity.addComponent<Transform>(newTransform);
            newEntity.addComponent<Model>(loadedModel);
            newEntity.addComponent<AABB>(boundingVol);
        }
    }

    // Init ImGui using the renderer's window
    EditorImGuiLayer ui;
    ui.Init(myRenderer.GetNativeWindow());

    // Provide capture flags to the renderer so it can skip WASD when UI is focused
    myRenderer.SetUICaptureProvider([&ui]() -> std::pair<bool, bool> {
        return { ui.WantCaptureKeyboard(), ui.WantCaptureMouse() };
    });

    // Provide the editor UI to draw each frame
    myRenderer.SetUIDraw([&ui, /*&scene,*/ this](float dt) {
        ui.BeginFrame();

        // Stats
        ImGui::Begin("Stats", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("dt: %.3f ms (%.1f FPS)", dt * 1000.f, dt > 0.f ? 1.f / dt : 0.f);
        static bool showScene = true;
        ImGui::Checkbox("Show Scene Panel", &showScene);
        ImGui::End();

        if (showScene) {
            ImGui::Begin("Scene");
            ImGui::Text("Entities: (hook count here)");
            ImGui::End();
        }

        ui.EndFrame();
    });

    myRenderer.run(scene, shader);
}

MyCoreEngine::Application* MyCoreEngine::CreateApplication()
{
    EditorApplication* app = new EditorApplication();
    app->Initialize();
    return app;
}

