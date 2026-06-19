#version 450

// ============================================================
//  particles3d.vert
//  Vertex shader for CustomShaderPoints3d.
//  Input:  world-space position (vec3)
//  Output: clip-space position + gl_PointSize
// ============================================================

layout(location = 0) in vec3 inPosition;

layout(binding = 0) uniform UBO {
    mat4 mvp;        // proj * view  (no model — positions already in world space)
    mat4 model;      // unused for points
    vec4 light_pos;  // unused for points
    vec4 cam_pos;    // can be used for distance-based size in fragment shader
} ubo;

layout(location = 0) out float outLifetimeFraction; // 1.0 = alive, passed as dummy for now

void main() {
    gl_Position  = ubo.mvp * vec4(inPosition, 1.0);

    // Scale point size by distance — closer = bigger
    float dist      = length(ubo.cam_pos.xyz - inPosition);
    gl_PointSize = clamp(160.0 / dist, 6.0, 32.0);  // was 80.0 / dist, 2.0, 16.0


    outLifetimeFraction = 1.0;
}