#version 450

// Unified gamma/tonemap + optional FXAA post-process.
// FXAA_ENABLED (specialization constant 0) gates the entire FXAA code path at pipeline
// creation time; the no-FXAA pipeline is a single-tap gamma/dither pass.

layout(set = 0, binding = 0) uniform sampler2D sceneTex;

layout(push_constant) uniform GammaPC {
    vec4 gammaParams; // x=invGamma, y=brightness, z=contrast, w=exposure
    vec4 fxaaParams;  // x=blend quality (0=disabled), y=edgeThreshold, z=edgeThresholdMin, w=maxSpan
} pc;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

// Set to false at pipeline creation time when FXAA is disabled.
// Lets the driver prune the entire fxaaScene() code path from the no-FXAA pipeline binary.
layout(constant_id = 0) const bool FXAA_ENABLED = true;

float triangularDither(vec2 coord) {
    // Jimenez 2014 interleaved gradient noise: no transcendentals, fast on GPU.
    float r = fract(52.9829189 * fract(dot(coord, vec2(0.06711056, 0.00583715))));
    return (r < 0.5) ? (sqrt(2.0 * r) - 1.0) * 0.5 : (1.0 - sqrt(2.0 - 2.0 * r)) * 0.5;
}

vec3 acesToneMap(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Classic FXAA luma approximation (Lottes 2011)
float lumaFxaa(vec3 rgb) {
    return dot(rgb, vec3(0.299, 0.587, 0.114));
}

vec3 sceneRgb(vec2 uv) {
    return texture(sceneTex, uv).rgb;
}

// Luma for FXAA edge detection.
// In HDR mode (exposure > 0) raw scene values can exceed 1.0 near glow composites.
// Apply Reinhard compression (l / (1+l)) so edge thresholds stay perceptually
// consistent regardless of absolute HDR scene brightness.
// Cost: 1 add + 1 divide per tap — negligible vs ACES (which is intentionally NOT used here).
float sceneLuma(vec2 uv) {
    float l = lumaFxaa(texture(sceneTex, uv).rgb);
    float exposure = pc.gammaParams.w;
    if (exposure > 0.0) l = l / (1.0 + l);
    return l;
}

// FXAA 3.11-style edge-walk anti-aliasing.
//
// WHY the old direction-vector approach caused artifacts:
//   The classic FXAA v1 "dir" vector, computed from 4 diagonal corners only,
//   often points ACROSS the edge for diagonal staircases instead of along it.
//   This makes the sample taps land on the wrong side, bleeding the sky/wall
//   colour into the opposite side. Additionally a constant blend floor blurred
//   every detected edge uniformly – straight wall borders, texture seams, etc.
//
// This implementation uses the FXAA 3.11 edge-walk instead:
//   1. H/V orientation is a binary decision, never ambiguous.
//   2. An edge walk (up to FXAA_SEARCH_STEPS each way) finds where the edge ends.
//   3. Blend weight = 0.5 - nearDist/totalSpan:  0 in the middle of a straight
//      edge (touched nothing), up to 0.5 at actual staircase corners.
//   4. The blend is always with the direct perpendicular neighbour (never a
//      direction-computed tap), so no colour-bleed artefacts are possible.
//
// Result: straight edges are completely untouched; only actual aliasing
// corners are smoothed, and only just enough.
vec3 fxaaScene(vec2 uv) {
    vec2 rcpFrame = 1.0 / vec2(textureSize(sceneTex, 0));

    // Full 3x3 luma neighbourhood.
    float lumaM  = sceneLuma(uv);
    float lumaN  = sceneLuma(uv + vec2( 0.0,        -rcpFrame.y));
    float lumaS  = sceneLuma(uv + vec2( 0.0,         rcpFrame.y));
    float lumaE  = sceneLuma(uv + vec2( rcpFrame.x,  0.0));
    float lumaW  = sceneLuma(uv + vec2(-rcpFrame.x,  0.0));
    float lumaNW = sceneLuma(uv + vec2(-rcpFrame.x, -rcpFrame.y));
    float lumaNE = sceneLuma(uv + vec2( rcpFrame.x, -rcpFrame.y));
    float lumaSW = sceneLuma(uv + vec2(-rcpFrame.x,  rcpFrame.y));
    float lumaSE = sceneLuma(uv + vec2( rcpFrame.x,  rcpFrame.y));

    float lumaMin = min(lumaM, min(min(lumaN, lumaS), min(lumaE, lumaW)));
    float lumaMax = max(lumaM, max(max(lumaN, lumaS), max(lumaE, lumaW)));
    float lumaRange = lumaMax - lumaMin;

    // Early-out: not on a contrastful enough edge.
    if (lumaRange < max(pc.fxaaParams.z, lumaMax * pc.fxaaParams.y)) {
        return sceneRgb(uv);
    }

    // --- Step 1: Binary H/V edge orientation (Sobel-weighted) ---
    // edgeHorz measures vertical luma change (H edge = vertical contrast).
    float edgeHorz = abs(lumaNW - lumaSW) + 2.0 * abs(lumaN - lumaS) + abs(lumaNE - lumaSE);
    float edgeVert = abs(lumaNW - lumaNE) + 2.0 * abs(lumaW - lumaE) + abs(lumaSW - lumaSE);
    bool  horzSpan = edgeHorz >= edgeVert;

    // --- Step 2: Pick the steeper perpendicular side ---
    float lumaN1 = horzSpan ? lumaN : lumaW;   // "negative" side
    float lumaP1 = horzSpan ? lumaS : lumaE;   // "positive" side
    float gradN  = abs(lumaN1 - lumaM);
    float gradP  = abs(lumaP1 - lumaM);
    // stepSign moves toward the steeper neighbour (= across the edge).
    float stepSign    = (gradN >= gradP) ? -1.0 : 1.0;
    float lumaNeighbor = (gradN >= gradP) ? lumaN1 : lumaP1;

    vec2 perpStep = horzSpan ? vec2(0.0, stepSign * rcpFrame.y)
                             : vec2(stepSign * rcpFrame.x, 0.0);
    vec2 walkStep = horzSpan ? vec2(rcpFrame.x, 0.0)
                             : vec2(0.0, rcpFrame.y);

    // Luma at the centre of the edge cross-section.
    float edgeLuma   = (lumaM + lumaNeighbor) * 0.5;
    // Stop walking when the luma at the walk position deviates from edgeLuma
    // by at least 25% of the edge contrast.
    float edgeGradTh = max(gradN, gradP) * 0.25;

    // --- Step 3: Walk along the edge to find both endpoints ---
    // Start at the half-pixel offset onto the edge midpoint.
    vec2 uvEdge = uv + perpStep * 0.5;

    vec2  uvA    = uvEdge + walkStep;
    vec2  uvB    = uvEdge - walkStep;
    float deltaA = sceneLuma(uvA) - edgeLuma;
    float deltaB = sceneLuma(uvB) - edgeLuma;
    bool  endA   = abs(deltaA) >= edgeGradTh;
    bool  endB   = abs(deltaB) >= edgeGradTh;

    // Up to 7 additional steps (total 8 taps per side = ~8 px radius).
    for (int i = 0; i < 7; i++) {
        if (!endA) { uvA += walkStep;  deltaA = sceneLuma(uvA) - edgeLuma; endA = abs(deltaA) >= edgeGradTh; }
        if (!endB) { uvB -= walkStep;  deltaB = sceneLuma(uvB) - edgeLuma; endB = abs(deltaB) >= edgeGradTh; }
    }

    // --- Step 4: Blend weight from span fraction ---
    float distA  = horzSpan ? abs(uvA.x - uv.x) : abs(uvA.y - uv.y);
    float distB  = horzSpan ? abs(uv.x - uvB.x) : abs(uv.y - uvB.y);

    bool goodA = (deltaA < 0.0) != (lumaM < edgeLuma);
    bool goodB = (deltaB < 0.0) != (lumaM < edgeLuma);
    // At equal distance prefer the endpoint that confirms a real corner.
    // (When both are equal and both good/bad, default to A.)
    bool useA      = (distA < distB) || (!(distB < distA) && goodA);
    float distNear  = useA ? distA : distB;
    float distTotal = distA + distB;
    bool  goodSpan  = useA ? goodA : goodB;

    // Edge blend: 0 at the centre of a straight run, up to quality at a true corner.
    // Formula: (0.5 - distNear/distTotal) maps [0,0.5] -> scaled by 2*quality -> [0, quality].
    // Sub-pixel is intentionally ABSENT: it fires on every texture highlight/stripe
    // (lumaL != lumaM whenever a texel is locally bright/dark) and blurs the whole
    // scene uniformly.  The edge-walk is safe without it because symmetric patterns
    // (floor grids, straight texture seams) always give distNear==distTotal/2 -> blend==0.
    float finalBlend = goodSpan
        ? clamp((0.5 - distNear / distTotal) * 2.0 * pc.fxaaParams.x, 0.0, pc.fxaaParams.x)
        : 0.0;
    return mix(sceneRgb(uv), sceneRgb(uv + perpStep), finalBlend);
}

void main() {
    vec3 c;
    if (FXAA_ENABLED) {
        // FXAA path: blend in scene space, tone-map after.
        c = fxaaScene(fragTexCoord);
    } else {
        // Fast path: single sample.
        c = sceneRgb(fragTexCoord);
    }

    // Exposure + tone-mapping applied once on the final result.
    float exposure = pc.gammaParams.w;
    if (exposure > 0.0) {
        c = acesToneMap(c * exposure);
    }

    // Contrast + brightness
    c = (c - 0.5) * pc.gammaParams.z + 0.5;
    c += pc.gammaParams.y;
    c = clamp(c, 0.0, 1.0);

    // Gamma
    c = pow(c, vec3(pc.gammaParams.x));

    // Dither: break 8-bit banding
    float dither = triangularDither(gl_FragCoord.xy) / 255.0;
    c = clamp(c + dither, 0.0, 1.0);

    outColor = vec4(c, 1.0);
}
