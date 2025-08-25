#include "GLInit.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>

namespace MyCoreEngine {
	bool EnsureGLADLoaded() {
		static bool s_loaded = false;
		if (!s_loaded) {
			// context must be current on this thread
			s_loaded = (gladLoadGLLoader((GLADloadproc)glfwGetProcAddress) != 0);
		}
		return s_loaded;
	}
}