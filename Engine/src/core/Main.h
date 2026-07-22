// Engine/src/core/Main.h
#pragma once
#include "Application.h"

// Default: do not emit main unless explicitly opted in
#ifdef MYCE_DEFINE_ENTRY
int main(int argc, char** argv) {
    auto* app = MyCoreEngine::CreateApplication();
    app->SetCommandLine(argc, argv); // portable argv (replaces Win32 __argv)
    app->Run();
    delete app;
    return 0;
}
#endif
