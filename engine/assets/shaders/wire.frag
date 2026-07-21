#version 330 core
// Debug wireframe view: flat-colour mesh lines, drawn by PSX/DebugWireframe
// with polygon_mode wireframe. Reuses PSX_VS_Unlit so the lines keep the
// vertex snap/jitter of the real geometry. Still writes the MRT normal/depth
// surface so the stylize pass sees sane edge data instead of garbage.

noperspective in vec3 vNormalVS;
noperspective in float vViewDepth;

uniform vec4 wireColor;
uniform float wireDepthFade; // exp(-depth*fade) sinks far lines; 0 = flat
uniform float farClip;

layout(location = 0) out vec4 fragColour;
layout(location = 1) out vec4 fragNormalDepth;

void main()
{
    // Depth cue: nearby lines keep full brightness, far ones dim toward
    // black so dense distant geometry stops reading as solid noise.
    fragColour = vec4(wireColor.rgb * exp(-vViewDepth * wireDepthFade),
                      wireColor.a);
    fragNormalDepth = vec4(normalize(vNormalVS) * 0.5 + 0.5,
                           vViewDepth / farClip);
}
