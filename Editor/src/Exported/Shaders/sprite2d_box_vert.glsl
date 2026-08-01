#version 330 core
// Rounded-box / bordered / image vertex stage (Renderer2D's BOX stream).
//
// Same contract as sprite2d_vert: positions arrive already transformed into the
// mode's 2D space, so this only applies the view-projection.
//
// The four extra attributes are per-QUAD constants replicated to all four
// corners. GL 3.3 core has no SSBO (4.3) and no per-instance seam here, and a
// UBO would cap a batch at the block size — so replication is the honest way to
// get per-quad data to the fragment stage without breaking the batch.
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUV;
layout (location = 2) in vec4 aColor;
layout (location = 3) in vec2 aLocal;   // px from the quad CENTRE, +y down
layout (location = 4) in vec2 aHalf;    // quad half-extent, px
layout (location = 5) in vec4 aBorder;
layout (location = 6) in vec2 aShape;   // x = radius px, y = border width px

uniform mat4 uViewProj;

out vec2 vUV;
out vec4 vColor;
out vec2 vLocal;
// flat: identical on all four corners, so the provoking vertex cannot matter
// and the interpolator does no work for them.
flat out vec2 vHalf;
flat out vec4 vBorder;
flat out vec2 vShape;

void main() {
    vUV     = aUV;
    vColor  = aColor;
    vLocal  = aLocal;
    vHalf   = aHalf;
    vBorder = aBorder;
    vShape  = aShape;
    gl_Position = uViewProj * vec4(aPos, 0.0, 1.0);
}
