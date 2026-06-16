#version 450

layout(binding = 0) uniform UniformData {
    mat4 mvp;
    mat4 model;
    vec4 light_pos;
    vec4 cam_pos;
} ubo;

layout(binding = 1) uniform sampler2D texSampler;

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inWorldPos;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 N = normalize(inNormal);
    vec3 L = normalize(ubo.light_pos.xyz - inWorldPos);
    vec3 V = normalize(ubo.cam_pos.xyz   - inWorldPos);
    vec3 H = normalize(L + V);  // Blinn-Phong halfway vector

    float ambient   = 0.15;
    float diffuse   = max(dot(N, L), 0.0);
    float specular  = pow(max(dot(N, H), 0.0), 32.0);

    vec4 tex_color  = texture(texSampler, inUV);
    outColor        = tex_color * (ambient + diffuse) + vec4(1.0) * specular * 0.4;
}