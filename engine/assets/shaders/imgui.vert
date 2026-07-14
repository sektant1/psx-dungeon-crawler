#version 330 core
// Vertex shader for the Dear ImGui overlay. OGRE's ImGuiOverlay material is
// fixed-function by default, which GL3Plus cannot run; this hand-written GLSL
// pair keeps the engine RTSS-free (see RenderCore.cpp). Vertex layout matches
// ImGUIRenderable: POSITION float2, TEXCOORD0 float2, DIFFUSE ubyte4_norm.
in vec4 vertex;   // xy = screen position (z,w unused)
in vec2 uv0;
in vec4 colour;   // vertex colour, normalised
uniform mat4 worldViewProj; // ortho projection baked by ImGUIRenderable::_update
out vec2 uv;
out vec4 col;
void main()
{
    gl_Position = worldViewProj * vec4(vertex.xy, 0.0, 1.0);
    uv = uv0;
    col = colour;
}
