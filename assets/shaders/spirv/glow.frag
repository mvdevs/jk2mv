#version 450

layout(push_constant) uniform BlurPC {
    vec2 texelOffset;   // base texel offset (1/width,0) or (0,1/height)
    float softness;     // multiplier on texelOffset for blur radius (r_DynamicGlowSoft)
    float delta;        // brightness threshold — discard pixels below this (r_DynamicGlowDelta)
} pc;

layout(set = 0, binding = 0) uniform sampler2D blurTex;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
    // 9-tap Gaussian blur (1D pass - run horizontally then vertically)
    const float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

    // Scale the texel offset by the softness factor to control blur radius
    vec2 offset = pc.texelOffset * pc.softness;

    vec3 result = texture(blurTex, fragTexCoord).rgb * weights[0];

    for (int i = 1; i < 5; i++) {
        result += texture(blurTex, fragTexCoord + offset * float(i)).rgb * weights[i];
        result += texture(blurTex, fragTexCoord - offset * float(i)).rgb * weights[i];
    }

    // Apply brightness threshold: pixels below delta are suppressed
    float brightness = dot(result, vec3(0.2126, 0.7152, 0.0722));
    if (brightness < pc.delta) {
        result *= smoothstep(0.0, pc.delta, brightness);
    }

    outColor = vec4(result, 1.0);
}
