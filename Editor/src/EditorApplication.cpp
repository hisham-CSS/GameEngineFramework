#include "Engine.h"
#include "EditorApplication.h"


void EditorApplication::Initialize() 
{
    myRenderer = Renderer();
    myRenderer.Init();
}

void EditorApplication::Run() 
{
    myRenderer.Update();
}

MyCoreEngine::Application* MyCoreEngine::CreateApplication()
{
	EditorApplication* app = new EditorApplication();
    app->Initialize();
    return app;
}
