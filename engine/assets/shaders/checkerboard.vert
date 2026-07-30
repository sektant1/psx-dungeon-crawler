#version 330 core
// Editor-only staging floor. Passes world-space XZ through so the fragment
// shader can generate the pattern from position instead of from UVs -- that is
// what makes the floor infinite: there is no texture and no UV range to run out
// of, however far the plane is scaled.

in vec4 vertex;
in vec3 normal;

uniform mat4 worldViewProj;
uniform mat4 world;
uniform mat4 worldView;
uniform mat4 invTransWorldView;

smooth out vec3 vWorldPos;
smooth out vec3 vNormalVS;
smooth out float vViewDepth;

void main()
{
    vWorldPos = (world * vertex).xyz;
    vNormalVS = mat3(invTransWorldView) * normal;
    vViewDepth = -(worldView * vertex).z;
    gl_Position = worldViewProj * vertex;
}
