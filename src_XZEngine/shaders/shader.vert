#version 450

layout(binding = 0) uniform UniformData {
    mat4 mvp;
    mat4 model;
    vec4 light_pos;
    vec4 cam_pos;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec3 outWorldPos;

void main() {
    vec4 world_pos = ubo.model * vec4(inPosition, 1.0);
    gl_Position    = ubo.mvp   * vec4(inPosition, 1.0);
    outUV          = inUV;
    outNormal      = mat3(transpose(inverse(ubo.model))) * inNormal;
    outWorldPos    = world_pos.xyz;
}