#version 450
// Vertex stage for the stylised-surface family. Separate from scene.vert
// because the portal profile reads its field from OBJECT space, not from UVs:
// the four thickness faces of a slab share the face's XZ footprint and differ
// only in Y, so taking the pattern from XZ extrudes it through the thickness
// and the rims become the edge of the same swirl. UVs cannot express that -- on
// a rim they run along the edge and across the thickness, a different mapping.
//
// Keeping it out of scene.vert means the mesh path does not pay for three extra
// varyings and a matrix inverse it never reads.

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
    vec4 shadowParams;        // enabled, bias, strength, texel
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

layout(location = 0) out vec2 surfaceUV;
layout(location = 1) out vec3 surfaceLocal;  // object space, metres
layout(location = 2) out vec3 surfaceNormal; // object space
layout(location = 3) out vec3 surfaceView;   // object space, surface -> eye
layout(location = 4) out vec3 viewNormal;    // view space, for the MRT
layout(location = 5) out float viewDepth;

void main() {
    surfaceUV = inUv * drawData.uvTransform.xy + drawData.uvTransform.zw;
    surfaceLocal = inPosition;
    surfaceNormal = inNormal;

    // The eye, brought into object space, so the parallax shear is expressed in
    // the same frame as the field it shears. One inverse per vertex is paid by
    // a handful of portal quads, never by scene geometry.
    vec4 world = drawData.model * vec4(inPosition, 1.0);
    vec3 localEye =
        (inverse(drawData.model) *
         vec4(scene.cameraPositionAndLightCount.xyz, 1.0)).xyz;
    surfaceView = localEye - inPosition;

    vec4 viewPosition = scene.view * world;
    viewNormal = normalize(mat3(scene.view) *
                           normalize(transpose(inverse(mat3(drawData.model))) *
                                     inNormal));
    viewDepth = -viewPosition.z;
    gl_Position = scene.viewProjection * world;
}
