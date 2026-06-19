#version 450

// ============================================================
//  postprocess.vert
//  Fullscreen triangle — no vertex buffer needed.
//  Generates a triangle that covers the entire screen using
//  gl_VertexIndex only. Draw with vkCmdDraw(cmd, 3, 1, 0, 0).
// ============================================================

layout(location = 0) out vec2 outUV;

void main() {
    // Fullscreen triangle trick:
    // vertex 0: (-1, -1) uv (0, 0)
    // vertex 1: ( 3, -1) uv (2, 0)
    // vertex 2: (-1,  3) uv (0, 2)
    outUV       = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(outUV * 2.0 - 1.0, 0.0, 1.0);
}
