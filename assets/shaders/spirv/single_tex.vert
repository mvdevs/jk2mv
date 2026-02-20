#version 450

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    float texEnvMode;
    float alphaTestFunc;
} pc;

// GPU params UBO (set 2, dynamic uniform buffer)
layout(std140, set = 2, binding = 0) uniform GPUParams {
    uint  gpuFlags;
    float backlerp;
    float identityLight;
    float ubo_pad0;
    vec4  viewOrigin;       // eye position in model space
    vec4  ambientLight;     // rgb
    vec4  directedLight;    // rgb
    vec4  entityLightDir;   // xyz = light direction
    vec4  fogDistVec;       // fog distance plane
    vec4  fogDepthVec;      // fog depth plane
    vec4  fogColor;         // rgba
    float fogEyeT;
    float fogEyeOutside;    // 0.0 or 1.0
    float ubo_pad1;
    float ubo_pad2;
    vec4  dlightOrigin;     // xyz = origin, w = radius
    vec4  dlightColor;      // rgb = color, w = additive flag
    vec4  specLightOrigin;  // xyz = specular light origin, w = useEntityLightDir flag
    // Bone matrices for GPU skeletal skinning (72 bones × mat3x4 = 216 vec4s)
    vec4  boneMatrices[216]; // boneMatrices[boneIndex*3 + row] = row of 3x4 matrix
} gp;

// GPU flag bit definitions (must match vk_local.h)
#define GPU_FLAG_DIFFUSE_LIGHTING   0x0001u
#define GPU_FLAG_SPECULAR_ALPHA     0x0002u
#define GPU_FLAG_ENVMAP_TC          0x0004u
#define GPU_FLAG_FOG                0x0008u
#define GPU_FLAG_FOG_MODULATE_RGB   0x0010u
#define GPU_FLAG_FOG_MODULATE_ALPHA 0x0020u
#define GPU_FLAG_FOG_PASS           0x0040u
#define GPU_FLAG_DLIGHT_PASS        0x0080u
#define GPU_FLAG_DLIGHT_BACKSIDES   0x0100u
#define GPU_FLAG_SKINNING           0x0200u
#define GPU_FLAG_MULTI_DLIGHT_PASS  0x0400u

layout(location = 0) in vec4 inPosition;
layout(location = 1) in vec2 inTexCoord0;
layout(location = 2) in vec2 inTexCoord1;
layout(location = 3) in vec4 inColor;
layout(location = 4) in vec4 inNormal;  // .xyz = normal, .w = packed bone weights (skinning)

layout(location = 0) out vec2 fragTexCoord0;
layout(location = 1) out vec2 fragTexCoord1;
layout(location = 2) out vec4 fragColor;
layout(location = 3) out float fragFogFactor;
layout(location = 4) out vec3 fragWorldPos;
layout(location = 5) out vec3 fragWorldNormal;

// Compute fog plane distances (s = distance, fogT = depth-adjusted)
// These are the same texcoords used to sample the fog image.
void computeFogST(vec3 pos, out float s, out float fogT) {
    s = dot(pos, gp.fogDistVec.xyz) + gp.fogDistVec.w;
    float t = dot(pos, gp.fogDepthVec.xyz) + gp.fogDepthVec.w;

    if (gp.fogEyeOutside > 0.5) {
        if (t < 1.0 || abs(t - gp.fogEyeT) < 0.001) {
            fogT = 1.0 / 32.0;
        } else {
            fogT = 1.0 / 32.0 + 30.0 / 32.0 * t / (t - gp.fogEyeT);
        }
    } else {
        if (t < 0.0) {
            fogT = 1.0 / 32.0;
        } else {
            fogT = 31.0 / 32.0;
        }
    }
}

// Analytical R_FogFactor: converts fog texcoords (s, fogT) to [0..1] density.
// Replicates the CPU-side R_FogFactor + fogTable lookup.
// fogTable[i] = sqrt(i/255), so the final lookup is just sqrt(adjustedS).
float computeFogFactor(float s, float fogT) {
    float fogS = s - 1.0 / 512.0;
    if (fogS < 0.0 || fogT < 1.0 / 32.0) return 0.0;
    if (fogT < 31.0 / 32.0) {
        fogS *= (fogT - 1.0 / 32.0) * (32.0 / 30.0);
    }
    fogS = clamp(fogS * 8.0, 0.0, 1.0);
    return sqrt(fogS);
}

void main() {
    vec3 position = inPosition.xyz;
    vec3 normal = inNormal.xyz;

    uint flags = gp.gpuFlags;

    // ============================================================
    // GPU Skeletal Skinning (Ghoul2 models)
    // Bone indices packed in inPosition.w, weights in inNormal.w
    // ============================================================
    if ((flags & GPU_FLAG_SKINNING) != 0u) {
        uint packedIndices = floatBitsToUint(inPosition.w);
        uint packedWeights = floatBitsToUint(inNormal.w);

        vec4 srcPos = vec4(position, 1.0);
        vec3 srcNorm = normal;
        vec3 skinnedPos = vec3(0.0);
        vec3 skinnedNorm = vec3(0.0);

        for (int w = 0; w < 4; w++) {
            float weight = float((packedWeights >> (w * 8)) & 0xFFu) / 255.0;
            if (weight < 0.001) continue;

            int bi = int((packedIndices >> (w * 8)) & 0xFFu) * 3;
            vec4 row0 = gp.boneMatrices[bi + 0];
            vec4 row1 = gp.boneMatrices[bi + 1];
            vec4 row2 = gp.boneMatrices[bi + 2];

            skinnedPos += weight * vec3(dot(row0, srcPos), dot(row1, srcPos), dot(row2, srcPos));
            skinnedNorm += weight * vec3(dot(row0.xyz, srcNorm), dot(row1.xyz, srcNorm), dot(row2.xyz, srcNorm));
        }

        position = skinnedPos;
        normal = normalize(skinnedNorm);
    }

    gl_Position = pc.mvp * vec4(position, 1.0);

    // Default outputs — pass through CPU-computed data
    fragTexCoord0 = inTexCoord0;
    fragTexCoord1 = inTexCoord1;
    fragColor = inColor;
    fragFogFactor = 0.0;
    fragWorldPos = position;
    fragWorldNormal = normal;

    // ============================================================
    // P1.1: GPU Diffuse Lighting (replaces RB_CalcDiffuseColor)
    // Only overrides RGB — alpha is left for alphaGen (CPU or GPU).
    // ============================================================
    if ((flags & GPU_FLAG_DIFFUSE_LIGHTING) != 0u) {
        float incoming = dot(normal, gp.entityLightDir.xyz);
        incoming = max(incoming, 0.0);

        // ambientLight and directedLight are in [0..255] range
        vec3 litColor;
        litColor.r = min(gp.ambientLight.x + incoming * gp.directedLight.x, 255.0);
        litColor.g = min(gp.ambientLight.y + incoming * gp.directedLight.y, 255.0);
        litColor.b = min(gp.ambientLight.z + incoming * gp.directedLight.z, 255.0);

        fragColor.rgb = litColor / 255.0;
    }

    // ============================================================
    // P1.2: GPU Specular Alpha (replaces RB_CalcSpecularAlpha)
    // Only overrides alpha channel.
    // ============================================================
    if ((flags & GPU_FLAG_SPECULAR_ALPHA) != 0u) {
        vec3 lightDir;
        if (gp.specLightOrigin.w > 0.5) {
            // Entity with model: use pre-computed entity light direction
            lightDir = gp.entityLightDir.xyz;
        } else {
            // World geometry: per-vertex light direction from fixed lightOrigin
            lightDir = normalize(gp.specLightOrigin.xyz - position);
        }

        float d = 2.0 * dot(normal, lightDir);
        vec3 reflected = normal * d - lightDir;

        vec3 viewer = gp.viewOrigin.xyz - position;
        float ilength = inversesqrt(max(dot(viewer, viewer), 0.0001));
        float l = dot(reflected, viewer) * ilength;

        float spec;
        if (l < 0.0) {
            spec = 0.0;
        } else {
            l = l * l;
            l = l * l;
            spec = min(l, 1.0);
        }
        fragColor.a = spec;
    }

    // ============================================================
    // P1.3: GPU Environment Map Texcoords (replaces RB_CalcEnvironmentTexCoords)
    // ============================================================
    if ((flags & GPU_FLAG_ENVMAP_TC) != 0u) {
        vec3 viewer = gp.viewOrigin.xyz - position;
        float ilen = inversesqrt(max(dot(viewer, viewer), 0.0001));
        viewer *= ilen;

        float d = dot(normal, viewer);
        vec3 reflected = normal * 2.0 * d - viewer;

        fragTexCoord0.x = 0.5 + reflected.y * 0.5;
        fragTexCoord0.y = 0.5 - reflected.z * 0.5;
    }

    // ============================================================
    // Fog modulation: compute analytical fog density and attenuate
    // vertex colors. This replaces the CPU-side
    // RB_CalcModulateColorsByFog / RB_CalcModulateAlphasByFog.
    // ============================================================
    if ((flags & (GPU_FLAG_FOG_MODULATE_RGB | GPU_FLAG_FOG_MODULATE_ALPHA)) != 0u) {
        float s, fogT;
        computeFogST(position, s, fogT);
        float fogDensity = computeFogFactor(s, fogT);
        fragFogFactor = fogDensity;

        float invFog = 1.0 - fogDensity;
        if ((flags & GPU_FLAG_FOG_MODULATE_RGB) != 0u) {
            fragColor.rgb *= invFog;
        }
        if ((flags & GPU_FLAG_FOG_MODULATE_ALPHA) != 0u) {
            fragColor.a *= invFog;
        }
    }

    // ============================================================
    // Dynamic Light Pass (single dlight, legacy path)
    // GPU computes texcoords from distance and attenuation color.
    // ============================================================
    if ((flags & GPU_FLAG_DLIGHT_PASS) != 0u) {
        vec3 dist = gp.dlightOrigin.xyz - position;
        float radius = gp.dlightOrigin.w;
        float invRadius = 1.0 / radius;

        fragTexCoord0.x = 0.5 + dist.x * invRadius;
        fragTexCoord0.y = 0.5 + dist.y * invRadius;

        float faceDot = dot(dist, normal);
        bool backface = (faceDot < 0.0) && ((flags & GPU_FLAG_DLIGHT_BACKSIDES) == 0u);

        float modulate;
        float absDz = abs(dist.z);
        if (absDz > radius || backface) {
            modulate = 0.0;
        } else if (absDz < radius * 0.5) {
            modulate = 1.0;
        } else {
            modulate = 2.0 * (radius - absDz) * invRadius;
        }

        fragColor = vec4(gp.dlightColor.xyz * modulate, 1.0);
    }

    // ============================================================
    // Multi-Dlight Pass: vertex shader just passes position/normal.
    // Fragment shader does the actual dlight accumulation loop.
    // ============================================================
    if ((flags & GPU_FLAG_MULTI_DLIGHT_PASS) != 0u) {
        // Pass-through: fragWorldPos/fragWorldNormal already set above.
        // fragColor stays white (0xff from CPU), fragment shader overrides output.
        fragTexCoord0 = vec2(0.5, 0.5);
    }

    // ============================================================
    // Fog Pass: vertex color = fog color, texcoords = fog ST for
    // sampling the fog image. This replaces RB_FogPass CPU path.
    // ============================================================
    if ((flags & GPU_FLAG_FOG_PASS) != 0u) {
        fragColor = gp.fogColor;

        float s, fogT;
        computeFogST(position, s, fogT);
        fragTexCoord0.x = clamp(s, 0.0, 1.0);
        fragTexCoord0.y = clamp(fogT, 0.0, 1.0);
    }
}
