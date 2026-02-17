/*
===========================================================================
vk_shaders.h - Embedded SPIR-V shader bytecode for Vulkan renderer

These shaders replicate the OpenGL fixed-function pipeline behavior:
- Single texture with vertex colors (modulate)
- Multi-texture (modulate or additive combine)  
- Fullscreen quad for post-processing (gamma, glow)

The GLSL source is included in comments for reference.
The SPIR-V bytecode was generated from equivalent GLSL.
===========================================================================
*/

#ifndef VK_SHADERS_H
#define VK_SHADERS_H

/*
=== VERTEX SHADER (single_tex.vert) ===
#version 450

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 color;
    float texEnvMode;
    float alphaTestFunc;
    float alphaTestValue;
    float depthNear;
    float depthFar;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord0;
layout(location = 2) in vec2 inTexCoord1;
layout(location = 3) in vec4 inColor;

layout(location = 0) out vec2 fragTexCoord0;
layout(location = 1) out vec2 fragTexCoord1;
layout(location = 2) out vec4 fragColor;

void main() {
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    fragTexCoord0 = inTexCoord0;
    fragTexCoord1 = inTexCoord1;
    fragColor = inColor;
}

=== FRAGMENT SHADER (single_tex.frag) ===
#version 450

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 color;
    float texEnvMode;
    float alphaTestFunc;
    float alphaTestValue;
    float depthNear;
    float depthFar;
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
    
    // Alpha test
    if (pc.alphaTestFunc > 0.5) {
        if (pc.alphaTestFunc < 1.5) {
            // GT_0
            if (outColor.a <= 0.0) discard;
        } else if (pc.alphaTestFunc < 2.5) {
            // LT_80
            if (outColor.a >= 0.5) discard;
        } else if (pc.alphaTestFunc < 3.5) {
            // GE_80
            if (outColor.a < 0.5) discard;
        } else {
            // GE_C0
            if (outColor.a < 0.75) discard;
        }
    }
}

=== FRAGMENT SHADER (multi_tex.frag) ===
#version 450

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 color;
    float texEnvMode;
    float alphaTestFunc;
    float alphaTestValue;
    float depthNear;
    float depthFar;
} pc;

layout(set = 0, binding = 0) uniform sampler2D tex0;
layout(set = 1, binding = 0) uniform sampler2D tex1;

layout(location = 0) in vec2 fragTexCoord0;
layout(location = 1) in vec2 fragTexCoord1;
layout(location = 2) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 texColor0 = texture(tex0, fragTexCoord0);
    vec4 texColor1 = texture(tex1, fragTexCoord1);
    
    // First stage: modulate tex0 with vertex color
    vec4 base = texColor0 * fragColor;
    
    // Second stage: texEnvMode determines combination
    // 0 = TEXENV_MODULATE: base * tex1
    // 3 = TEXENV_ADD: base + tex1
    if (pc.texEnvMode < 0.5) {
        outColor = base * texColor1;
    } else {
        outColor = base + texColor1;
        outColor = clamp(outColor, 0.0, 1.0);
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

=== GAMMA VERTEX SHADER ===
#version 450

layout(location = 0) out vec2 fragTexCoord;

void main() {
    // Fullscreen triangle
    fragTexCoord = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(fragTexCoord * 2.0 - 1.0, 0.0, 1.0);
    fragTexCoord.y = 1.0 - fragTexCoord.y;
}

=== GAMMA FRAGMENT SHADER ===
#version 450

layout(set = 0, binding = 0) uniform sampler2D sceneTex;
layout(set = 1, binding = 0) uniform sampler3D gammaLUT;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
    vec4 color = texture(sceneTex, fragTexCoord);
    outColor = vec4(texture(gammaLUT, color.rgb).rgb, color.a);
}
*/

// ============================================================
// Shader bytecode is loaded from pre-compiled .spv files at runtime.
// The GLSL source files live in assets/shaders/spirv/ and are compiled
// to SPIR-V via glslangValidator during the build process.
//
// To manually compile:
//   glslangValidator -V single_tex.vert -o single_tex_vert.spv
//   glslangValidator -V single_tex.frag -o single_tex_frag.spv
//   glslangValidator -V multi_tex.frag  -o multi_tex_frag.spv
//   glslangValidator -V gamma.vert      -o gamma_vert.spv
//   glslangValidator -V gamma.frag      -o gamma_frag.spv
//   glslangValidator -V glow.vert       -o glow_vert.spv
//   glslangValidator -V glow.frag       -o glow_frag.spv
//   glslangValidator -V glow_composite.frag -o glow_composite_frag.spv
// ============================================================

#endif // VK_SHADERS_H
