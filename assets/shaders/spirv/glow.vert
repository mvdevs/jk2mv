#version 450

layout(push_constant) uniform BlurPC {
    vec2 texelOffset;
    float softness;
    float delta;
} pc;

layout(location = 0) out vec2 fragTexCoord;

void main() {
    // Fullscreen triangle trick
    fragTexCoord = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(fragTexCoord * 2.0 - 1.0, 0.0, 1.0);
    // No Y-flip needed: glow textures are Vulkan render targets (Y=0 at top),
    // which matches the Vulkan NDC/framebuffer convention.
}
