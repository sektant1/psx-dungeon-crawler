#version 330 core
// Port of shaders/psx_base.gdshaderinc vertex() for Ogre GL3Plus.
// Compiled in variants via preprocessor_defines: LIT
//
// Affine texture mapping: Godot does `POSITION /= abs(POSITION.w)` after the
// snap. Here we instead mark all varyings `noperspective`, which yields the
// same screen-linear (affine) interpolation WITHOUT destroying clip-space w,
// so near-plane clipping and the depth buffer keep working. The vertex snap
// itself is identical math, applied BEFORE interpolation, as in the source.

in vec4 vertex;
in vec3 normal;
in vec2 uv0;
in vec4 colour;

uniform mat4 worldViewProj;
uniform mat4 worldView;
uniform mat4 invTransWorldView;
uniform float time;

// Godot: global uniform float precision_multiplier = 1.0
uniform float precisionMultiplier;
uniform vec2 uvScale;
uniform vec2 uvOffset;
uniform vec2 uvPanVelocity;

#ifdef LIT
// Godot render_mode vertex_lighting + diffuse_lambert + specular_disabled.
#include <psx_lighting.glsl>
#endif

noperspective out vec2 vUV;
noperspective out vec4 vColour;
noperspective out vec3 vLight;
noperspective out vec3 vNormalVS;
noperspective out float vViewDepth;

// Per-pixel lighting path (perPixelLighting uniform in psx.frag): position
// and normal need perspective-correct interpolation -- affine-interpolated
// positions would warp the light falloff we are trying to fix.
smooth out vec3 vVsPos;
smooth out vec3 vNormalSmooth;

// identical to psx_base.gdshaderinc
const vec2 base_snap_res = vec2(512.0, 448.0);
vec4 get_snapped_pos(vec4 base_pos)
{
    vec4 snapped_pos = base_pos;
    snapped_pos.xyz = base_pos.xyz / base_pos.w; // to NDC
    vec2 snap_res = floor(base_snap_res * precisionMultiplier);
    snapped_pos.x = floor(snap_res.x * snapped_pos.x) / snap_res.x;
    snapped_pos.y = floor(snap_res.y * snapped_pos.y) / snap_res.y;
    snapped_pos.xyz *= base_pos.w; // back to clip space
    return snapped_pos;
}

void main()
{
    vUV = uv0 * uvScale + uvOffset + uvPanVelocity * time;
    vColour = colour;

    vec3 vsPos = (worldView * vertex).xyz;
    vec3 vsNormal = normalize(mat3(invTransWorldView) * normal);
    vNormalVS = vsNormal;
    vViewDepth = length(vsPos); // Godot fog uses length(VERTEX)

    vVsPos = vsPos;
    vNormalSmooth = vsNormal;

    #ifdef LIT
    vLight = psxComputeLight(vsPos, vsNormal);
    #else
    vLight = vec3(1.0);
    #endif

    gl_Position = get_snapped_pos(worldViewProj * vertex);
}
