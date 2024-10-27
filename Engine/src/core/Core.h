#pragma once

#ifdef _WIN32
	#ifdef ENGINE_DLL_EXPORTS
		#define ENGINE_API __declspec(dllexport)
	#else
		#define ENGINE_API __declspec(dllimport)
	#endif
#else
	#error Only supports windows currently!
#endif