#version 330 core
// Rounded-box / bordered / image fragment stage (Renderer2D's BOX stream).
//
// ONE signed-distance evaluation gives the silhouette AND the border: the
// border is the band of the field within `bw` of the edge. That is why a border
// needs no second quad and no second layer — a child element's layer can never
// slip between an element's fill and its own border.
//
// Same LDR contract as sprite2d_frag: the UI pass runs AFTER tonemapping and
// post-processing, so these colours are final and must not be graded again.
in vec2 vUV;
in vec4 vColor;
in vec2 vLocal;
flat in vec2 vHalf;
flat in vec4 vBorder;
flat in vec2 vShape;

uniform sampler2D uTex;

out vec4 FragColor;

// Exact signed distance to an axis-aligned rounded box centred at the origin.
// Negative inside.
float sdRoundBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, vec2(0.0))) - r;
}

void main() {
    // Clamped HERE rather than on the CPU. An unclamped radius larger than half
    // the box erodes and then erases the element; clamping per-fragment is free
    // and gives `border-radius: 9999px` one obviously-right meaning — a stadium
    // — instead of an error the author has to compute around.
    float r  = clamp(vShape.x, 0.0, min(vHalf.x, vHalf.y));
    float bw = clamp(vShape.y, 0.0, min(vHalf.x, vHalf.y));

    float d = sdRoundBox(vLocal, vHalf, r);

    // fwidth OF THE DISTANCE FIELD, which stays correct under any transform the
    // batcher can bake into the positions, rotation included. Deriving a pixel
    // extent from fwidth(vUV) instead is wrong by up to 41% on a rotated quad.
    float aa = max(fwidth(d), 1e-5);

    float outer = 1.0 - smoothstep(-aa * 0.5, aa * 0.5, d);
    float inner = 1.0 - smoothstep(-aa * 0.5, aa * 0.5, d + bw);

    // The fill covers the WHOLE silhouette, border band included — CSS's
    // background-clip: border-box, and the only version in which a translucent
    // border tints the panel edge instead of showing the scene through it.
    // Clipping the fill to `inner` instead looks identical for an opaque
    // border and punches a hole for every other kind.
    vec4 fill = texture(uTex, vUV) * vColor;
    fill.a *= outer;

    // `inner` exists only to carve the band: everything within bw of the edge.
    vec4 bord = vBorder;
    bord.a *= max(outer - inner, 0.0);

    // Border OVER fill, source-over, in STRAIGHT alpha. Deliberately not a
    // mix(): mix would replace the fill's alpha with the border's, so a
    // translucent border would punch a hole clean through an opaque panel
    // instead of tinting its edge.
    float a = bord.a + fill.a * (1.0 - bord.a);
    if (a <= 0.0) discard;   // the same early-out sprite2d_frag makes
    vec3 rgb = (bord.rgb * bord.a + fill.rgb * fill.a * (1.0 - bord.a)) / a;
    FragColor = vec4(rgb, a);
}
