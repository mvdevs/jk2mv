#version 450

layout(set = 0, binding = 0) uniform sampler2D sceneTex;

layout(push_constant) uniform GammaPC {
    float invGamma;
    float brightness;
    float contrast;
    float pad;
} pc;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
    vec4 color = texture(sceneTex, fragTexCoord);

    // Apply brightness, contrast, then gamma
    vec3 c = color.rgb;

    // Contrast: scale around 0.5
    c = (c - 0.5) * pc.contrast + 0.5;

    // Brightness: additive
    c += pc.brightness;

    // Clamp before gamma
    c = clamp(c, 0.0, 1.0);

    // Gamma: pow(color, invGamma)
    c = pow(c, vec3(pc.invGamma));

    outColor = vec4(c, color.a);
}
