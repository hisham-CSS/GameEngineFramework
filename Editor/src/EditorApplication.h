#pragma once
#include "Engine.h"
#include "EditorImGuiLayer.h"
#include "panels/SceneHierarchyPanel.h"
#include "panels/InspectorPanel.h"

class EditorApplication : public MyCoreEngine::Application
{
public:
    EditorApplication() : Application(1280, 720, "Cat Splat Engine") {}
    ~EditorApplication() {}

    void Initialize();

    void Run() override;

    // Scene viewport: renders the offscreen scene texture, hosts the
    // transform gizmo, and click-picks entities via ray-AABB.
    void DrawViewport(MyCoreEngine::Scene& scene);

    void DrawInputPanel();

    void DrawTimeControls();

    void DrawScenePersistence(MyCoreEngine::Scene& scene);

    void DrawLightControls(MyCoreEngine::Scene& scene);

    void DrawIBLHDRControls(MyCoreEngine::Scene& scene);

    void DrawMaterialControls(MyCoreEngine::Scene& scene);

    void DrawSunShadowControls(MyCoreEngine::Scene& scene);

    void DrawRenderingToggles(MyCoreEngine::Scene& scene);

    void DrawInformationPanel(const MyCoreEngine::Scene& scene, float dt);

private:
    void pickEntity_(MyCoreEngine::Scene& scene, float mouseU, float mouseV,
                     const glm::mat4& view, const glm::mat4& proj);

    EditorImGuiLayer ui_;                 // <-- persistent member
    SceneHierarchyPanel hierarchy_;
    InspectorPanel      inspector_;
    entt::entity        selected_ = entt::null;

    MyCoreEngine::RenderTarget sceneTarget_;
    bool viewportHovered_ = false;
    int  gizmoOp_ = 0; // 0 translate, 1 rotate, 2 scale

    std::unique_ptr<MyCoreEngine::AssetManager> assets_;
};