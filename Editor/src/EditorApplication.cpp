#include "Engine.h"
#include "EditorApplication.h"

//for the future for any initalization things that are required
void EditorApplication::Initialize()
{

}

void EditorApplication::Run()
{
    myRenderer.run();
}

MyCoreEngine::Application* MyCoreEngine::CreateApplication()
{
    EditorApplication* app = new EditorApplication();
    app->Initialize();
    return app;
}

