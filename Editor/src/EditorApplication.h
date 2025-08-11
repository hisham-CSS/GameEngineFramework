#pragma once
#include "Engine.h"

class EditorApplication : public MyCoreEngine::Application
{
public:
    EditorApplication() : myRenderer(1280, 720) {}
    ~EditorApplication() {}

    void Initialize();

    void Run() override;

private:
    MyCoreEngine::Renderer myRenderer;
};