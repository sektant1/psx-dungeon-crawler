#version 330 core
// Debug wireframe view: flat-colour mesh lines, drawn by PSX/DebugWireframe
// with polygon_mode wireframe. Reuses PSX_VS_Unlit so the lines keep the
// vertex snap/jitter of the real geometry. Still writes the MRT normal/depth
// surface so the stylize pass sees sane edge data instead of garbage.

noperspective in vec3 vNormalVS;
noperspective in float vViewDepth;

uniform vec4 wireColor;
uniform float farClip;

layout(location = 0) out vec4 fragColour;
layout(location = 1) out vec4 fragNormalDepth;

void main()
{
    fragColour = wireColor;
    fragNormalDepth = vec4(normalize(vNormalVS) * 0.5 + 0.5,
                           vViewDepth / farClip);
}
