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
    EditorImGuiLayer ui_;                 // <-- persistent member
    SceneHierarchyPanel hierarchy_;
    InspectorPanel      inspector_;
    entt::entity        selected_ = entt::null;


    std::unique_ptr<MyCoreEngine::AssetManager> assets_;
};