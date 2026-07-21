#version 330 core
in vec4 vertex;
in vec4 colour;
in vec2 uv0;
uniform mat4 worldViewProj;
out vec2 particleUV;
out vec4 particleColour;
void main()
{
    particleUV = uv0;
    particleColour = colour;
    gl_Position = worldViewProj * vertex;
}
