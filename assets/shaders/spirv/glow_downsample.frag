#version 450

// Progressive bloom downsample — 13-tap filter with optional Karis average
// for firefly suppression on the first mip level.
//
// Based on the technique from Call of Duty: Advanced Warfare / UE4.
// Each invocation reads a source mip and writes to a half-res target.

layout(push_constant) uniform DownsamplePC {
    vec2 srcTexelSize;  // 1.0 / srcResolution
    float isFirstMip;   // 1.0 on first downsample (enables Karis), 0.0 otherwise
    float threshold;    // brightness threshold (r_DynamicGlowDelta)
} pc;

layout(set = 0, binding = 0) uniform sampler2D srcTex;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

float luminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

// Karis average: weight each sample by 1/(1+luma) to suppress fireflies
float karisWeight(vec3 c) {
    return 1.0 / (1.0 + luminance(c));
}

void main() {
    vec2 uv = fragTexCoord;
    vec2 ts = pc.srcTexelSize;

    // 13-tap downsampling filter (Jorge Jimenez, CoD:AW / UE4)
    // Sample pattern:
    //   a . b . c
    //   . d . e .
    //   f . g . h
    //   . i . j .
    //   k . l . m
    //
    // Weights: center cross (d+e+i+j)/4 * 0.5
    //          corners:  (a+b+d+e)/4 * 0.125  (b+c+e+f... etc) * 0.125 each
    //          wide:     (f+g+h+...)/4... etc

    vec3 a = texture(srcTex, uv + vec2(-2.0, -2.0) * ts).rgb;
    vec3 b = texture(srcTex, uv + vec2( 0.0, -2.0) * ts).rgb;
    vec3 c = texture(srcTex, uv + vec2( 2.0, -2.0) * ts).rgb;

    vec3 d = texture(srcTex, uv + vec2(-1.0, -1.0) * ts).rgb;
    vec3 e = texture(srcTex, uv + vec2( 1.0, -1.0) * ts).rgb;

    vec3 f = texture(srcTex, uv + vec2(-2.0,  0.0) * ts).rgb;
    vec3 g = texture(srcTex, uv).rgb;
    vec3 h = texture(srcTex, uv + vec2( 2.0,  0.0) * ts).rgb;

    vec3 i = texture(srcTex, uv + vec2(-1.0,  1.0) * ts).rgb;
    vec3 j = texture(srcTex, uv + vec2( 1.0,  1.0) * ts).rgb;

    vec3 k = texture(srcTex, uv + vec2(-2.0,  2.0) * ts).rgb;
    vec3 l = texture(srcTex, uv + vec2( 0.0,  2.0) * ts).rgb;
    vec3 m = texture(srcTex, uv + vec2( 2.0,  2.0) * ts).rgb;

    vec3 result;

    if (pc.isFirstMip > 0.5) {
        // First mip: apply Karis average to prevent fireflies from dominating
        // Group into 5 overlapping quads, weighted by inverse luminance
        vec3 g0 = (d + e + i + j);          // center quad
        vec3 g1 = (a + b + d + e) * 0.25;   // TL quad
        vec3 g2 = (b + c + e + g) * 0.25;   // TR quad... actually let's use
                                             // the standard 5-group decomposition

        // Karis-weighted groups (prevents single bright pixel from dominating)
        float w0 = karisWeight(d) + karisWeight(e) + karisWeight(i) + karisWeight(j);
        vec3 grp0 = (d * karisWeight(d) + e * karisWeight(e) +
                     i * karisWeight(i) + j * karisWeight(j)) / max(w0, 0.0001);

        float w1 = karisWeight(a) + karisWeight(b) + karisWeight(g) + karisWeight(f);
        vec3 grp1 = (a * karisWeight(a) + b * karisWeight(b) +
                     g * karisWeight(g) + f * karisWeight(f)) / max(w1, 0.0001);

        float w2 = karisWeight(b) + karisWeight(c) + karisWeight(e) + karisWeight(h);
        vec3 grp2 = (b * karisWeight(b) + c * karisWeight(c) +
                     e * karisWeight(e) + h * karisWeight(h)) / max(w2, 0.0001);

        float w3 = karisWeight(f) + karisWeight(g) + karisWeight(i) + karisWeight(k);
        vec3 grp3 = (f * karisWeight(f) + g * karisWeight(g) +
                     i * karisWeight(i) + k * karisWeight(k)) / max(w3, 0.0001);

        float w4 = karisWeight(g) + karisWeight(h) + karisWeight(j) + karisWeight(m);
        vec3 grp4 = (g * karisWeight(g) + h * karisWeight(h) +
                     j * karisWeight(j) + m * karisWeight(m)) / max(w4, 0.0001);

        // Standard decomposition weights: center=0.5, corners=0.125 each
        result = grp0 * 0.5 + (grp1 + grp2 + grp3 + grp4) * 0.125;

        // Apply brightness threshold with soft knee
        float brightness = luminance(result);
        if (brightness < pc.threshold) {
            result *= smoothstep(0.0, pc.threshold, brightness);
        }
    } else {
        // Subsequent mips: standard 13-tap (no Karis, no threshold)
        result  = (d + e + i + j) * 0.125;          // center quad: weight 0.5 / 4
        result += (a + b + g + f) * 0.03125;         // TL: 0.125 / 4
        result += (b + c + e + h) * 0.03125;         // TR
        result += (f + g + i + k) * 0.03125;         // BL
        result += (g + h + j + m) * 0.03125;         // BR
        result += (d + e + l + j) * 0.03125;         // extra cross coverage
    }

    outColor = vec4(max(result, vec3(0.0)), 1.0);
}
