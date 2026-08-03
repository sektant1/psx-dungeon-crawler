#version 450
// Particle billboards. Unlike the Ogre path -- which instances a shared unit
// quad and billboards it in the vertex stage from the view matrix columns --
// the corners here are already expanded to world space on the CPU, with the
// flipbook window folded into the UVs. Same result; it keeps this backend off
// the instanced-attribute-divisor path for a stream that is rebuilt every frame
// anyway, and the expansion cost is trivial next to the sort it already does.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inColour;

layout(set = 0, binding = 0, std140) uniform SceneUniforms {
    mat4 viewProjection;
    mat4 view;
    vec4 cameraPositionAndLightCount;
    vec4 ambient;
    vec4 fogColourDensity;
    vec4 clipParams;
    mat4 lightViewProjection;
    vec4 shadowParams;        // enabled, bias, strength, texel
    vec4 lightPositionRange[16];
    vec4 lightColourType[16];
} scene;

layout(location = 0) out vec2 uv;
layout(location = 1) out vec4 colour;

void main() {
    uv = inUv;
    colour = inColour;
    gl_Position = scene.viewProjection * vec4(inPosition, 1.0);
}
