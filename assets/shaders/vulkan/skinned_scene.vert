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

layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec3 worldNormal;
layout(location = 2) out vec2 uv;
layout(location = 3) out vec4 colour;
layout(location = 4) out vec3 viewNormal;
layout(location = 5) out float viewDepth;

void main() {
    mat4 skin = skinning.joints[inJoints.x] * inWeights.x +
                skinning.joints[inJoints.y] * inWeights.y +
                skinning.joints[inJoints.z] * inWeights.z +
                skinning.joints[inJoints.w] * inWeights.w;
    vec4 skinnedPosition = skin * vec4(inPosition, 1.0);
    vec3 skinnedNormal = normalize(mat3(skin) * inNormal);
    vec4 world = drawData.model * skinnedPosition;
    worldPosition = world.xyz;
    worldNormal = normalize(transpose(inverse(mat3(drawData.model))) *
                            skinnedNormal);
    uv = inUv * drawData.uvTransform.xy + drawData.uvTransform.zw;
    colour = inColour;
    vec4 viewPosition = scene.view * world;
    viewNormal = normalize(mat3(scene.view) * worldNormal);
    viewDepth = -viewPosition.z;
    gl_Position = scene.viewProjection * world;
}
