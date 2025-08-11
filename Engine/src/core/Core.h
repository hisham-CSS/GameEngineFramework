// Core.h
#pragma once

#if defined(_WIN32) || defined(_WIN64)
#if defined(ENGINE_DLL_EXPORTS)
#define ENGINE_API __declspec(dllexport)
#elif defined(ENGINE_DLL_IMPORTS)
#define ENGINE_API __declspec(dllimport)
#else
#define ENGINE_API
#endif
#else
#if defined(ENGINE_SHARED)
#define ENGINE_API __attribute__((visibility("default")))
#else
#define ENGINE_API
#endif
#endif
