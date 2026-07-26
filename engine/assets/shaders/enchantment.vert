#version 330 core
in vec4 vertex;
in vec2 uv0;
uniform mat4 worldViewProj;
uniform float time;
uniform vec2 enchantScroll;
out vec2 enchantUV;
out float enchantPulse;
void main()
{
    enchantUV = uv0 * 5.0 + enchantScroll * time;
    enchantPulse = 0.82 + 0.18 * sin(time * 3.1);
    gl_Position = worldViewProj * vertex;
}
