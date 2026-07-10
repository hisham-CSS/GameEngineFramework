// Editor/src/EditorMain.cpp
#define MYCE_DEFINE_ENTRY
#include "../src/core/Main.h"

#ifdef _WIN32
// On hybrid-GPU laptops (NVIDIA Optimus / AMD PowerXpress) these exports ask
// the driver to run us on the discrete GPU instead of the integrated one.
// They must be exported from the executable itself, not a DLL.
extern "C" {
    __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif
