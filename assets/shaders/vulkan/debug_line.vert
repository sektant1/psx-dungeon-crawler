#version 450
// Debug line overlay: the editor's grid, physics collider outlines, the
// wireframe diagnostics. World-space endpoints straight from the CPU, one
// colour per vertex, no lighting and no model matrix -- eng::Renderer::DebugLine
// carries world positions already.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColour;

layout(set = 0, binding = 0, std140) uniform SceneUniforms {
    mat4 viewProjection;
    mat4 view;
    vec4 cameraPositionAndLightCount;
    vec4 ambient;
    vec4 fogColourDensity;
    vec4 clipParams;
    mat4 lightViewProjection;
    vec4 shadowParams;
    vec4 lightPositionRange[16];
    vec4 lightColourType[16];
} scene;

layout(location = 0) out vec4 colour;

void main() {
    colour = inColour;
    gl_Position = scene.viewProjection * vec4(inPosition, 1.0);
}
