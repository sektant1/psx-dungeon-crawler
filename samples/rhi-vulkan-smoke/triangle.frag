#version 450

layout(location = 0) in vec3 colour;
layout(location = 1) in vec2 uv;

layout(set = 1, binding = 0) uniform sampler2D colourTexture;

layout(push_constant) uniform DrawConstants {
    vec4 tint;
} drawConstants;

layout(location = 0) out vec4 outColour;

void main()
{
    vec4 sampled = texture(colourTexture, uv);
    outColour = vec4(colour, 1.0) * sampled * drawConstants.tint;
}
