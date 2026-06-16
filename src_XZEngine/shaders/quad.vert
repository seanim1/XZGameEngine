#version 450

layout(binding = 0) uniform UniformData {
    mat4 mvp;
    mat4 model;
    vec4 light_pos;
    vec4 cam_pos;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec3 outWorldPos;

void main() {
    gl_Position = ubo.mvp * vec4(inPosition, 1.0);
    outUV       = inUV;
    outNormal   = vec3(0.0, 0.0, 1.0);  // unused by face.frag
    outWorldPos = vec3(0.0);
}