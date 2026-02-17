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

layout(location = 0) in vec4 inPosition;
layout(location = 1) in vec2 inTexCoord0;
layout(location = 2) in vec2 inTexCoord1;
layout(location = 3) in vec4 inColor;

layout(location = 0) out vec2 fragTexCoord0;
layout(location = 1) out vec2 fragTexCoord1;
layout(location = 2) out vec4 fragColor;

void main() {
    gl_Position = pc.mvp * vec4(inPosition.xyz, 1.0);
    fragTexCoord0 = inTexCoord0;
    fragTexCoord1 = inTexCoord1;
    fragColor = inColor;
}
