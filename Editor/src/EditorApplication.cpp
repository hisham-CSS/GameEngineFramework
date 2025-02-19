#include "Engine.h"
#include "EditorApplication.h"

//for the future for any initalization things that are required
void EditorApplication::Initialize()
{

}

void EditorApplication::Run()
{
    // Load resources once during initialization:
    stbi_set_flip_vertically_on_load(true);
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
    myRenderer.run(scene, shader);
}

MyCoreEngine::Application* MyCoreEngine::CreateApplication()
{
    EditorApplication* app = new EditorApplication();
    app->Initialize();
    return app;
}

