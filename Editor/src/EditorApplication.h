#include "Engine.h"

class EditorApplication : public MyCoreEngine::Application
{
public:
    EditorApplication() {}
    ~EditorApplication() {}

    void Initialize();

    void Run() override;

private:
    Renderer myRenderer;
};