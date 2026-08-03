#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec4 inColour;
layout(location = 4) in uvec4 inJoints;
layout(location = 5) in vec4 inWeights;

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

layout(set = 0, binding = 2, std140) uniform SkinningPalette {
    mat4 joints[256];
} skinning;

layout(push_constant) uniform DrawConstants {
    mat4 model;
    vec4 tintOpacity;
    vec4 rimColourStrength;
    vec4 surfaceParams;
    vec4 uvTransform;
} drawData;

void main() {
    mat4 skin = skinning.joints[inJoints.x] * inWeights.x +
                skinning.joints[inJoints.y] * inWeights.y +
                skinning.joints[inJoints.z] * inWeights.z +
                skinning.joints[inJoints.w] * inWeights.w;
    gl_Position = scene.lightViewProjection * drawData.model * skin *
                  vec4(inPosition, 1.0);
}
