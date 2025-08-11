// Engine/src/core/Main.h
#pragma once
#include "Application.h"

#ifndef MYCE_ENABLE_ENTRY
#define MYCE_ENABLE_ENTRY 0
#endif

#if MYCE_ENABLE_ENTRY
int main() {
    auto* app = MyCoreEngine::CreateApplication();
    app->Run();
    delete app;
    return 0;
}
#endif
