#version 330 core

smooth in vec3 vNormalVS;
smooth in float vViewDepth;

uniform vec4 ghostColour;
uniform float farClip;

layout(location = 0) out vec4 fragColour;
layout(location = 1) out vec4 fragNormalDepth;

void main()
{
    vec3 normalVS = normalize(vNormalVS);
    float facing = 0.65 + 0.35 * abs(normalVS.z);
    fragColour = vec4(ghostColour.rgb * facing, ghostColour.a);
    fragNormalDepth = vec4(normalVS * 0.5 + 0.5,
                           vViewDepth / max(farClip, 1e-3));
}
