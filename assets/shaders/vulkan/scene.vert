#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec4 inColour;

layout(set = 0, binding = 0, std140) uniform SceneUniforms {
    mat4 viewProjection;
    mat4 view;
    vec4 cameraPositionAndLightCount;
    vec4 ambient;
    vec4 fogColourDensity;
    vec4 clipParams;
    mat4 lightViewProjection;
    vec4 shadowParams;        // enabled, bias, strength, texel          // x = near, y = far
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

layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec3 worldNormal;
layout(location = 2) out vec2 uv;
layout(location = 3) out vec4 colour;
// View-space metadata for the stylize MRT surface. The edge pass classifies
// convex vs concave folds by the sign of the normal derivative along each
// screen axis, so the normal has to be in view space, not world space.
layout(location = 4) out vec3 viewNormal;
layout(location = 5) out float viewDepth;

void main() {
    vec4 world = drawData.model * vec4(inPosition, 1.0);
    worldPosition = world.xyz;
    worldNormal = normalize(transpose(inverse(mat3(drawData.model))) * inNormal);
    uv = inUv * drawData.uvTransform.xy + drawData.uvTransform.zw;
    colour = inColour;
    vec4 viewPosition = scene.view * world;
    viewNormal = normalize(mat3(scene.view) * worldNormal);
    viewDepth = -viewPosition.z;   // positive in front of the camera
    gl_Position = scene.viewProjection * world;
}
