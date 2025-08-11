#include "ImageIO.h"
#include "stb_image.h"   // no define here!

namespace MyCoreEngine {
    void SetImageFlipVerticallyOnLoad(bool enable) {
        stbi_set_flip_vertically_on_load(enable ? 1 : 0);
    }
}
