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
uniform vec4 ambientLight; // ambient_light_colour (env ambient * energy)
uniform vec4 lightPos[3]; // view space; w == 0 -> directional (dir TO light)
uniform vec4 lightDiffuse[3]; // light colour * energy
uniform vec4 lightAtten[3]; // x = range
uniform float lightCount;
uniform float omniAttenuation; // Godot OmniLight3D.omni_attenuation exponent
#endif

noperspective out vec2 vUV;
noperspective out vec4 vColour;
noperspective out vec3 vLight;
noperspective out vec3 vNormalVS;
noperspective out float vViewDepth;

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

    #ifdef LIT
    // Godot render_mode vertex_lighting + diffuse_lambert + specular_disabled.
    // (Godot pre-multiplies light energy by PI to cancel the 1/PI in its
    //  lambert BRDF, so plain NdotL * colour*energy matches 1:1.)
    vec3 light = ambientLight.rgb;
    int count = int(min(lightCount, 3.0) + 0.5);
    for (int i = 0; i < count; ++i)
    {
        vec3 L;
        float att;
        if (lightPos[i].w == 0.0)
        {
            L = normalize(lightPos[i].xyz);
            att = 1.0;
        }
        else
        {
            vec3 toLight = lightPos[i].xyz - vsPos;
            float dist = length(toLight);
            L = toLight / max(dist, 1e-5);
            // Godot omni attenuation curve: pow(1 - d/range, attenuation)
            att = pow(clamp(1.0 - dist / lightAtten[i].x, 0.0, 1.0), omniAttenuation);
        }
        light += lightDiffuse[i].rgb * max(dot(vsNormal, L), 0.0) * att;
    }
    vLight = light;
    #else
    vLight = vec3(1.0);
    #endif

    gl_Position = get_snapped_pos(worldViewProj * vertex);
}
