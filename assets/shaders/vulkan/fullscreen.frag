#version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColour;
layout(set = 1, binding = 0) uniform sampler2D sourceTexture;

void main() {
    outColour = texture(sourceTexture, uv);
}
