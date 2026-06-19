#version 450

// ============================================================
//  postprocess.frag
//  Full-screen post-processing pass.
//  Reads the scene color target and applies:
//    1. Radial blur   — samples along a line from screen centre
//    2. Chromatic aberration — splits R/G/B channels by UV offset
//  Both effects are toggled and parameterised via UBO.
// ============================================================

layout(location = 0) in  vec2 inUV;
layout(location = 0) out vec4 outColor;

// binding 0 — scene color texture (rendered offscreen)
layout(binding = 0) uniform sampler2D colorTarget;

// binding 1 — post-process parameters
layout(binding = 1) uniform PostProcessData {
    int   chromatic_aberration_enabled;  // 0 or 1
    float chromatic_aberration_strength; // e.g. 0.005
    int   radial_blur_enabled;           // 0 or 1
    float radial_blur_strength;          // e.g. 0.02
    int   radial_blur_samples;           // e.g. 10
    float pad0;
    float pad1;
    float pad2;
} pp;

void main() {
    vec2 uv = inUV;

    // --------------------------------------------------------
    //  1. Radial blur
    //     Samples the scene along a line from the screen centre
    //     toward the pixel, averaging them. Gives a hyperspeed
    //     / tunnel-vision feel.
    // --------------------------------------------------------
    vec4 blurred = texture(colorTarget, uv);

    if (pp.radial_blur_enabled != 0) {
        vec2  centre    = vec2(0.5, 0.5);
        vec2  dir       = uv - centre;           // vector from centre to pixel
        float dist      = length(dir);
        vec2  step_uv   = dir * pp.radial_blur_strength / float(pp.radial_blur_samples);

        vec4  accum     = vec4(0.0);
        vec2  sample_uv = uv;
        for (int i = 0; i < pp.radial_blur_samples; i++) {
            accum     += texture(colorTarget, sample_uv);
            sample_uv -= step_uv;                // walk toward centre
        }
        blurred = accum / float(pp.radial_blur_samples);
    }

    // --------------------------------------------------------
    //  2. Chromatic aberration
    //     Samples R, G, B channels at slightly offset UVs,
    //     offset direction is radially outward from centre.
    // --------------------------------------------------------
    vec4 result = blurred;

    if (pp.chromatic_aberration_enabled != 0) {
        vec2  centre    = vec2(0.5, 0.5);
        vec2  dir       = normalize(uv - centre);
        float str       = pp.chromatic_aberration_strength;

        // Use blurred image as base — sample each channel offset
        // R shifted outward, B shifted inward, G unchanged
        float r = texture(colorTarget, uv + dir * str * 1.0).r;
        float g = blurred.g;
        float b = texture(colorTarget, uv - dir * str * 1.0).b;

        result = vec4(r, g, b, blurred.a);
    }

    outColor = result;
}
