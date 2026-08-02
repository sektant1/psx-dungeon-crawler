#version 450

layout(location = 0) in vec2 uv;
layout(location = 1) in vec4 colour;
layout(location = 0) out vec4 outColour;
layout(set = 1, binding = 0) uniform sampler2D uiTexture;

void main() {
    outColour = colour * texture(uiTexture, uv);
}
