#pragma once
// Engine/src/thirdparty/stb_impl.cpp
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// Font rasterisation for the 2D text layer. stb_truetype ships its own
// fallback rect packer, but the real stb_rect_pack produces a tighter atlas —
// it must be included (and implemented) BEFORE stb_truetype for the pack API to
// pick it up. Both headers already come with the existing `stb` vcpkg package,
// so text costs no new dependency.
#define STB_RECT_PACK_IMPLEMENTATION
#include "stb_rect_pack.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
