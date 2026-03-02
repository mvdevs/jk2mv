#version 450

// Alpha-masked variant of glow.frag for RT glow reflection blur.
//
// Two cases:
//   1. Ghoul2 center pixel (alpha < 0.5): output zero — composited separately
//      from the raw (unblurred) RT output via a second overlay pass.
//   2. BSP center pixel: 9-tap Gaussian blur, but kernel samples that fall on
//      Ghoul2 pixels are replaced with the center BSP color. This prevents
//      bright wall light from bleeding into the model silhouette while
//      keeping the blur smooth everywhere — no hard transitions.

layout(push_constant) uniform BlurPC {
    vec2 texelOffset;   // base texel offset (1/width,0) or (0,1/height)
    float softness;     // multiplier on texelOffset for blur radius
    float delta;        // unused for RT blur (set to 0)
} pc;

layout(set = 0, binding = 0) uniform sampler2D blurTex;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
    vec4 center = texture(blurTex, fragTexCoord);

    // Case 1: Ghoul2 pixel — zero it out entirely
    if (center.a < 0.5) {
        outColor = vec4(0.0);
        return;
    }

    // Case 2: BSP pixel — 9-tap Gaussian blur with Ghoul2 sample clamping.
    // Any kernel tap that lands on a Ghoul2 pixel (alpha < 0.5) is replaced
    // with the center BSP color so the blur never pulls in Ghoul2-zeroed
    // regions, preventing both halos and dark fringing.
    const float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec2 offset = pc.texelOffset * pc.softness;

    vec3 result = center.rgb * weights[0];

    for (int i = 1; i < 5; i++) {
        vec4 sPos = texture(blurTex, fragTexCoord + offset * float(i));
        vec4 sNeg = texture(blurTex, fragTexCoord - offset * float(i));

        // Replace Ghoul2 samples with center BSP color
        vec3 cPos = (sPos.a >= 0.5) ? sPos.rgb : center.rgb;
        vec3 cNeg = (sNeg.a >= 0.5) ? sNeg.rgb : center.rgb;

        result += cPos * weights[i];
        result += cNeg * weights[i];
    }

    outColor = vec4(result, 1.0);
}
