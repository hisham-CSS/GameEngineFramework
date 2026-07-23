#include "WindowIcon.h"

#include <GLFW/glfw3.h>
#include "stb_image.h"   // impl lives in thirdparty/stb_impl.cpp

namespace MyCoreEngine {

    bool TrySetWindowIconFromFile(GLFWwindow* window, const char* path) {
        if (!window || !path || !*path) return false;

        // GLFWimage wants top-down rows, which is stb's UNflipped output.
        // The global flip flag is host-owned state we can't read back, so
        // force it off for this decode — the same force-per-call-site pattern
        // IBLBaker uses for HDRi loads (which already leaves the global at 0
        // after every environment bake, so nothing depends on it being set).
        stbi_set_flip_vertically_on_load(0);

        int w = 0, h = 0, comp = 0;
        unsigned char* pixels = stbi_load(path, &w, &h, &comp, 4); // force RGBA
        if (!pixels || w <= 0 || h <= 0) {
            if (pixels) stbi_image_free(pixels);
            return false;
        }

        GLFWimage img;
        img.width = w;
        img.height = h;
        img.pixels = pixels;
        glfwSetWindowIcon(window, 1, &img);

        stbi_image_free(pixels);
        return true;
    }

} // namespace MyCoreEngine
