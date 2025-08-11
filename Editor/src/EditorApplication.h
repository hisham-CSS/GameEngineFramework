#pragma once
#include "Engine.h"
#include "panels/SceneHierarchyPanel.h"
#include "panels/InspectorPanel.h"

class EditorApplication : public MyCoreEngine::Application
{
public:
    EditorApplication() : myRenderer(1280, 720) {}
    ~EditorApplication() {}

    void Initialize();

    void Run() override;

private:
    MyCoreEngine::Renderer myRenderer;
    entt::entity selected_ = entt::null;
    // Panels
    SceneHierarchyPanel* hierarchy_ = nullptr;
    InspectorPanel* inspector_ = nullptr;
};