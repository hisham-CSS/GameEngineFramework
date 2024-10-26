#include <iostream>
#include "Engine.h"

int main() {
    MyCoreEngine::Engine engine;
    engine.Run();

    std::cout << "Editor is running!" << std::endl;
    return 0;
}
