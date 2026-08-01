#version 330 core
// Instanced camera-facing quad for the authored particle materials.
//
// This shader used to read only `vertex`, `colour` and `uv0` and transform the
// base geometry directly. That was correct when particles were Ogre
// ParticleFX billboards carrying their own per-particle vertices; it became
// silently wrong when eng::ParticleBatch replaced them with instancing.
//
// Source 0 is now a *single shared* unit quad and every per-particle value --
// world position, size, colour, rotation, flipbook frame -- arrives through the
// uv1/uv2/uv3 instance stream (ParticleBatch.cpp). Reading only the base quad
// therefore drew every particle of every effect as the same unlit quad at the
// batch's origin, with `colour` unbound entirely: no game or demo effect was
// visible anywhere. This consumes the same stream Particles/SpriteVS does.
in vec4 vertex;   // quad corner, XY in [-0.5, 0.5]
in vec2 uv0;      // quad corner UV, before atlas remap
in vec4 uv1;      // instance: xyz world position, w world size
in vec4 uv2;      // instance: linear RGBA
in vec2 uv3;      // instance: x rotation in radians, y flipbook frame

uniform mat4 worldViewProj;
uniform mat4 view;
uniform float time;
uniform vec2 atlasGrid;
uniform float atlasRow;
uniform float atlasFrames;
uniform float atlasFps;

out vec2 particleUV;
out vec4 particleColour;

void main()
{
    // The view matrix is the inverse of the camera's world transform, so its
    // columns read out as the camera's world-space axes. Taking right/up from
    // there billboards the quad without a per-instance matrix.
    vec3 right = vec3(view[0][0], view[1][0], view[2][0]);
    vec3 up    = vec3(view[0][1], view[1][1], view[2][1]);

    float s = sin(uv3.x);
    float c = cos(uv3.x);
    vec2 corner = vec2(vertex.x * c - vertex.y * s,
                       vertex.x * s + vertex.y * c);
    vec3 world = uv1.xyz + (right * corner.x + up * corner.y) * uv1.w;

    // Atlas selection is unchanged: these materials animate a row of the
    // shared atlas on a clock, which is what their authored atlasRow/atlasFps
    // pairs mean. An effect whose simulation drives the frame instead leaves
    // atlasFps at 0 and the per-instance frame in uv3.y is used.
    vec2 grid = max(atlasGrid, vec2(1.0));
    float count = clamp(atlasFrames, 1.0, grid.x);
    float frame = atlasFps > 0.0 ? mod(floor(time * atlasFps), count)
                                 : mod(floor(max(uv3.y, 0.0)), count);
    particleUV = (uv0 + vec2(frame, clamp(atlasRow, 0.0, grid.y - 1.0))) / grid;
    particleColour = uv2;

    // The batch never moves its scene node, so instance positions are already
    // world-space and worldViewProj degenerates to viewProj.
    gl_Position = worldViewProj * vec4(world, 1.0);
}
