#pragma once

#ifdef _WIN32
    #ifdef ENGINE_DLL_EXPORTS
        #define ENGINE_API __declspec(dllexport)
    #else
        #define ENGINE_API __declspec(dllimport)
    #endif
#else
    #define ENGINE_API
#endif

namespace MyCoreEngine
{
    class ENGINE_API Engine
    {
    public:
        void Run();
    };

}
