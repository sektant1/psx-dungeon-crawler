#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inColour;
layout(location = 0) out vec2 uv;
layout(location = 1) out vec4 colour;

layout(push_constant) uniform ImGuiConstants {
    vec2 scale;
    vec2 translate;
} drawData;

void main() {
    uv = inUv;
    colour = inColour;
    gl_Position = vec4(inPosition * drawData.scale + drawData.translate,
                       0.0, 1.0);
}
