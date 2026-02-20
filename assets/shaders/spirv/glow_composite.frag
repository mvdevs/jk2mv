#version 450

// Glow composite shader — samples the blurred glow scene, applies an
// intensity multiplier, and outputs RGB. The pipeline uses additive
// blending when compositing this on top of the main scene.

layout(push_constant) uniform GlowPC {
    float intensity;    // glow brightness multiplier (r_DynamicGlowIntensity)
} pc;

layout(set = 0, binding = 0) uniform sampler2D sceneTex;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 glow = texture(sceneTex, fragTexCoord).rgb;
    // Apply intensity multiplier to control glow brightness
    outColor = vec4(glow * pc.intensity, 1.0);
}
