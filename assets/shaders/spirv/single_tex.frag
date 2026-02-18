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

// GPU params UBO (set 2, dynamic uniform buffer) — same layout as vertex shader
layout(std140, set = 2, binding = 0) uniform GPUParams {
    uint  gpuFlags;
    float backlerp;
    float identityLight;
    float ubo_pad0;
    vec4  viewOrigin;
    vec4  ambientLight;
    vec4  directedLight;
    vec4  entityLightDir;
    vec4  fogDistVec;
    vec4  fogDepthVec;
    vec4  fogColor;
    float fogEyeT;
    float fogEyeOutside;
    float ubo_pad1;
    float ubo_pad2;
    vec4  dlightOrigin;
    vec4  dlightColor;
    vec4  specLightOrigin;
    vec4  boneMatrices[216];
} gp;

#define GPU_FLAG_MULTI_DLIGHT_PASS  0x0400u
#define GPU_FLAG_DLIGHT_BACKSIDES   0x0100u

layout(set = 0, binding = 0) uniform sampler2D tex0;

layout(location = 0) in vec2 fragTexCoord0;
layout(location = 1) in vec2 fragTexCoord1;
layout(location = 2) in vec4 fragColor;
layout(location = 3) in float fragFogFactor;
layout(location = 4) in vec3 fragWorldPos;
layout(location = 5) in vec3 fragWorldNormal;

layout(location = 0) out vec4 outColor;

void main() {

    // ============================================================
    // Multi-Dlight Pass: per-pixel light accumulation loop.
    // Uses the dlight texture (bound as tex0) for XY radial attenuation
    // and piecewise-linear Z-axis modulation — matches original exactly.
    // ============================================================
    if ((gp.gpuFlags & GPU_FLAG_MULTI_DLIGHT_PASS) != 0u) {
        int numDlights = int(gp.ubo_pad0);
        vec3 totalLight = vec3(0.0);
        vec3 normal = normalize(fragWorldNormal);

        for (int i = 0; i < numDlights; i++) {
            vec4 lightPosR  = gp.boneMatrices[i * 2];      // xyz=origin, w=radius
            vec4 lightColorA = gp.boneMatrices[i * 2 + 1];  // rgb=color, w=additive

            vec3 dist = lightPosR.xyz - fragWorldPos;
            float radius = lightPosR.w;
            float invRadius = 1.0 / radius;

            // Backface check
            float faceDot = dot(dist, normal);
            bool backface = (faceDot < 0.0) && ((gp.gpuFlags & GPU_FLAG_DLIGHT_BACKSIDES) == 0u);

            // Z-axis piecewise linear modulation (matches original CPU code)
            float absDz = abs(dist.z);
            float zMod;
            if (absDz > radius || backface) {
                zMod = 0.0;
            } else if (absDz < radius * 0.5) {
                zMod = 1.0;
            } else {
                zMod = 2.0 * (radius - absDz) * invRadius;
            }

            // XY attenuation: sample dlight texture exactly like original
            vec2 dlTC = vec2(0.5 + dist.x * invRadius, 0.5 + dist.y * invRadius);
            float xyAtten = texture(tex0, dlTC).r;

            totalLight += lightColorA.rgb * zMod * xyAtten;
        }

        outColor = vec4(totalLight, 1.0);

        // Alpha test (unlikely for dlight pass but respect it)
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

        return;
    }

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
}
