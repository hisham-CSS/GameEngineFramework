// Engine/src/core/Main.h
#pragma once
#include "Application.h"

// Default: do not emit main unless explicitly opted in
#ifdef MYCE_DEFINE_ENTRY
int main() {
    auto* app = MyCoreEngine::CreateApplication();
    app->Run();
    delete app;
    return 0;
}
#endif
