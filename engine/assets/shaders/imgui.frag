#version 330 core
// Fragment shader for the Dear ImGui overlay (see imgui.vert).
in vec2 uv;
in vec4 col;
uniform sampler2D fontTex;
out vec4 fragColour;
void main()
{
    fragColour = col * texture(fontTex, uv);
}
