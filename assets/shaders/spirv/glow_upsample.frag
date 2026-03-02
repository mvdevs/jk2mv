#version 450

// Progressive bloom upsample — 9-tap tent filter.
//
// Each invocation reads a smaller mip and applies a 3x3 tent filter
// to produce a smooth upsampled result. The existing content at the
// destination mip is preserved via hardware additive blending
// (srcFactor=ONE, dstFactor=ONE) in the pipeline, avoiding the need
// to sample the destination and eliminating read-write hazards.

layout(push_constant) uniform UpsamplePC {
    vec2 srcTexelSize;  // 1.0 / srcResolution (the smaller mip being upsampled)
    float radius;       // filter radius multiplier (controls bloom spread softness)
    float _pad;
} pc;

layout(set = 0, binding = 0) uniform sampler2D srcTex;   // smaller mip (being upsampled)

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
    vec2 uv = fragTexCoord;
    vec2 ts = pc.srcTexelSize * pc.radius;

    // 9-tap tent filter (3x3 bilinear taps)
    // Produces a smooth, wide kernel:
    //   1  2  1
    //   2  4  2  / 16
    //   1  2  1
    vec3 upsampled  = texture(srcTex, uv + vec2(-ts.x, -ts.y)).rgb;
    upsampled       += texture(srcTex, uv + vec2(  0.0, -ts.y)).rgb * 2.0;
    upsampled       += texture(srcTex, uv + vec2( ts.x, -ts.y)).rgb;
    upsampled       += texture(srcTex, uv + vec2(-ts.x,   0.0)).rgb * 2.0;
    upsampled       += texture(srcTex, uv).rgb * 4.0;
    upsampled       += texture(srcTex, uv + vec2( ts.x,   0.0)).rgb * 2.0;
    upsampled       += texture(srcTex, uv + vec2(-ts.x,  ts.y)).rgb;
    upsampled       += texture(srcTex, uv + vec2(  0.0,  ts.y)).rgb * 2.0;
    upsampled       += texture(srcTex, uv + vec2( ts.x,  ts.y)).rgb;
    upsampled /= 16.0;

    // Output just the upsampled contribution.
    // Hardware additive blend adds this to the existing framebuffer content
    // (the downsample result at this mip level).
    outColor = vec4(upsampled, 1.0);
}
