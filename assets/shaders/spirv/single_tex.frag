#version 450

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 color;
    float texEnvMode;
    float alphaTestFunc;
    float alphaTestValue;
    float depthNear;
    float depthFar;
    float gamma;
} pc;

layout(set = 0, binding = 0) uniform sampler2D tex0;

layout(location = 0) in vec2 fragTexCoord0;
layout(location = 1) in vec2 fragTexCoord1;
layout(location = 2) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 texColor = texture(tex0, fragTexCoord0);

    // texEnvMode: 0=modulate, 1=replace, 2=decal, 3=add
    if (pc.texEnvMode < 0.5) {
        outColor = texColor * fragColor;
    } else if (pc.texEnvMode < 1.5) {
        outColor = texColor;
    } else if (pc.texEnvMode < 2.5) {
        outColor = vec4(mix(fragColor.rgb, texColor.rgb, texColor.a), fragColor.a);
    } else {
        outColor = texColor + fragColor;
        outColor.a = texColor.a * fragColor.a;
    }

    // Alpha test: 0=none, 1=GT0, 2=LT80, 3=GE80, 4=GEC0
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
    
    // Apply gamma correction
    outColor.rgb = pow(outColor.rgb, vec3(pc.gamma));
}
