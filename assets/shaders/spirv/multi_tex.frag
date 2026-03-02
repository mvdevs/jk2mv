#version 450

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    float texEnvMode;
    float alphaTestFunc;
} pc;

layout(set = 0, binding = 0) uniform sampler2D tex0;
layout(set = 1, binding = 0) uniform sampler2D tex1;

layout(location = 0) in vec2 fragTexCoord0;
layout(location = 1) in vec2 fragTexCoord1;
layout(location = 2) in vec4 fragColor;
layout(location = 3) in float fragFogFactor;
layout(location = 4) in vec3 fragWorldPos;
layout(location = 5) in vec3 fragWorldNormal;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 texColor0 = texture(tex0, fragTexCoord0);
    vec4 texColor1 = texture(tex1, fragTexCoord1);

    // First stage: modulate tex0 with vertex color
    vec4 base = texColor0 * fragColor;

    // Second stage: texEnvMode for combining
    // 0 = GL_MODULATE: base * tex1
    // 3 = GL_ADD: base + tex1
    if (pc.texEnvMode < 0.5) {
        outColor.rgb = base.rgb * texColor1.rgb;
        outColor.a = base.a * texColor1.a;
    } else {
        // For ADD, keep alpha driven by the base stage; clamping the sum would
        // otherwise force alpha toward 1.0 and break alpha-driven compositing.
        outColor.rgb = clamp(base.rgb + texColor1.rgb, 0.0, 1.0);
        outColor.a = base.a;
    }

    // Alpha test
    if (pc.alphaTestFunc > 0.5) {
        if (pc.alphaTestFunc < 1.5) {
            if (outColor.a <= 0.0) discard;
        } else if (pc.alphaTestFunc < 2.5) {
            if (outColor.a >= 0.5) discard;
        } else if (pc.alphaTestFunc < 3.5) {
            if (outColor.a < 0.5) discard;
        } else {
            if (outColor.a < 0.75) discard;
        }
    }
}
