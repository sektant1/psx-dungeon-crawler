#version 450

layout(location = 0) out vec2 uv;

// Per-pass UV transform. Only sampling is affected, so a pass can mirror its
// source without touching vertex winding (a clip-space flip would reverse
// winding and get solid geometry back-face culled).
layout(push_constant) uniform BlitConstants {
    vec2 uvScale;
    vec2 uvOffset;
} blit;

void main() {
    vec2 position = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    uv = position * blit.uvScale + blit.uvOffset;
    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
}
