#pragma once
#include "Core.h"

namespace MyCoreEngine {
	// Call this once per process AFTER a GL context is current on the calling thread.
	// Returns true if GLAD is initialized in the Engine module.
	ENGINE_API bool EnsureGLADLoaded();
}