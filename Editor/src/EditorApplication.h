#pragma once
#include "Engine.h"
#include "EditorImGuiLayer.h"
#include "panels/SceneHierarchyPanel.h"
#include "panels/InspectorPanel.h"

class EditorApplication : public MyCoreEngine::Application
{
public:
    EditorApplication() : myRenderer(1280, 720, "Cat Splat Engine") {}
    ~EditorApplication() {}

    void Initialize();

    void Run() override;

private:
    MyCoreEngine::Renderer myRenderer;
    EditorImGuiLayer ui_;                 // <-- persistent member
    SceneHierarchyPanel hierarchy_;
    InspectorPanel      inspector_;
    entt::entity        selected_ = entt::null;


    std::unique_ptr<MyCoreEngine::AssetManager> assets_;
};