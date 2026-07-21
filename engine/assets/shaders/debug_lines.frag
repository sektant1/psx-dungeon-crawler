#version 330 core
// Debug line overlay fragment shader: outputs per-vertex colour as flat lit
// geometry. Writes zero to the normal/depth MRT so post-processing doesn't
// pick up noise from the lines.

noperspective in vec4 vColour;
noperspective in vec3 vNormalVS;
noperspective in float vViewDepth;

layout(location = 0) out vec4 fragColour;
layout(location = 1) out vec4 fragNormalDepth;

void main()
{
    fragColour = vec4(vColour.rgb, 1.0);
    fragNormalDepth = vec4(0.0);
}
