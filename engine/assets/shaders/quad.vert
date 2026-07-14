#version 330 core
// Fullscreen quad VS for the compositor render_quad pass.
in vec4 vertex;
in vec2 uv0;
uniform mat4 worldViewProj;
out vec2 uv;
void main()
{
    gl_Position = worldViewProj * vertex;
    uv = uv0;
}
