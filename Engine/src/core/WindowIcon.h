#pragma once
#include "Core.h"

struct GLFWwindow;

namespace MyCoreEngine {

    // Best-effort window/taskbar icon from an image file (png/jpg/bmp via stb).
    // Returns false (silently) when the file is missing or undecodable — a
    // build without the icon asset must still boot.
    //
    // Windows barely needs this: the exe's own icon resource (resources/app.rc,
    // named GLFW_ICON so GLFW picks it up as the window-class icon) already
    // covers Explorer, the taskbar, and alt-tab. On Linux this call is the ONLY
    // way the window manager gets an icon, so hosts call it unconditionally
    // right after GL init with the staged Exported/Icon/icon.png.
    ENGINE_API bool TrySetWindowIconFromFile(GLFWwindow* window, const char* path);

} // namespace MyCoreEngine
