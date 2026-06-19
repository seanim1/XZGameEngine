#version 450

// ============================================================
//  sparkParticles.frag
//  Fragment shader for spark particles.
//  Uses gl_PointCoord to draw a soft circular glow per point.
//  Additive blending is set in the pipeline — bright overlapping
//  sparks naturally create a glowing burst effect.
// ============================================================

layout(location = 0) in  float inLifetimeFraction;  // 1.0 for now, wire up fade later
layout(location = 0) out vec4  outColor;

layout(binding = 0) uniform UBO {
    mat4 mvp;
    mat4 model;
    vec4 light_pos;
    vec4 cam_pos;
} ubo;

void main() {
    // gl_PointCoord goes 0→1 across the point sprite
    // Compute distance from centre of the point
    vec2  coord    = gl_PointCoord - vec2(0.5);
    float dist     = length(coord);

    // Discard pixels outside the circle
    if (dist > 0.5) discard;

    // Soft glow: bright centre, fades to zero at edge
    float glow     = 1.0 - smoothstep(0.0, 0.5, dist);
    glow           = pow(glow, 1.5);   // sharpen the falloff slightly

    // Spark colour: white-yellow core → orange edge
    vec3  core     = vec3(1.0, 0.95, 0.6);   // near-white yellow
    vec3  edge     = vec3(1.0, 0.35, 0.0);   // deep orange
    vec3  color    = mix(edge, core, glow);

    // Alpha drives the additive blend — zero alpha = no contribution
    float alpha    = glow * inLifetimeFraction;

    outColor = vec4(color * alpha, alpha);
}