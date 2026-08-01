#version 330 core
// Debug line overlay (physics colliders). Colour to MRT target 0; ZERO to the
// normal/depth metadata (target 1). The PSX stylize pass passes any pixel with
// near-zero metadata straight through (see pixel_stylize.frag), so the collider
// lines keep their exact bright colour instead of being darkened by the outline/
// shadow ink. Depth state is left DEFAULT in the material, so the lines occlude
// against nearer geometry like the wireframe view -- not clipped to objects.

noperspective in vec4 vColour;
noperspective in vec3 vNormalVS;
noperspective in float vViewDepth;

layout(location = 0) out vec4 fragColour;
layout(location = 1) out vec4 fragNormalDepth;

void main()
{
    fragColour = vec4(vColour.rgb, 1.0);
    fragNormalDepth = vec4(0.0); // pass-through: stylizer leaves these pixels alone
}
