#version 450
// Depth-only caster pass, from the sun's point of view. No fragment shader is
// bound: the depth buffer is the entire product, which is why this exists
// separately from scene.vert rather than reusing it with the outputs unused.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec4 inColour;

layout(set = 0, binding = 0, std140) uniform SceneUniforms {
    mat4 viewProjection;   // the light's, for this pass
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

layout(push_constant) uniform DrawConstants {
    mat4 model;
    vec4 tintOpacity;
    vec4 rimColourStrength;
    vec4 surfaceParams;
    vec4 uvTransform;
} drawData;

void main() {
    gl_Position = scene.lightViewProjection * drawData.model *
                  vec4(inPosition, 1.0);
}
