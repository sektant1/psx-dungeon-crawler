#version 330 core

// Portal membrane, vertex stage. Shared by the authored (portal.frag) and the
// art-free (prototype_portal.frag) fragment shaders.
//
// It forwards more than UVs because the membrane is a *slab* now, not a sheet:
//
//   surfaceLocal   object-space position, in metres. With surfaceUV it tells the
//                 fragment stage how big the mesh actually is (see
//                 surface_common.glsl), which is what keeps the pixel grid a
//                 fixed size in centimetres when the membrane is resized.
//   surfaceNormal  object-space normal, used to tell the two big faces from the
//                 four rims that close the slab's thickness.
//   surfaceView    object-space vector to the eye, for the parallax between the
//                 swirl's depth layers.
in vec4 vertex;
in vec3 normal;
in vec2 uv0;
uniform mat4 worldViewProj;
uniform vec3 cameraPositionObject;
out vec2 surfaceUV;
out vec3 surfaceLocal;
out vec3 surfaceNormal;
out vec3 surfaceView;

void main()
{
    surfaceUV = uv0;
    surfaceLocal = vertex.xyz;
    surfaceNormal = normal;
    surfaceView = cameraPositionObject - vertex.xyz;
    gl_Position = worldViewProj * vertex;
}
