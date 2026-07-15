// Shared PSX light evaluation: ambient + lambert + Godot omni attenuation.
// Included by psx.vert (vertex-lit path) and psx.frag (per-pixel path) so the
// loop exists exactly once. Uniform bindings live in psx.program on BOTH the
// vertex and the LIT fragment programs.
uniform vec4 ambientLight; // ambient_light_colour (env ambient * energy)
uniform vec4 lightPos[3];  // view space; w == 0 -> directional (dir TO light)
uniform vec4 lightDiffuse[3]; // light colour * energy
uniform vec4 lightAtten[3];   // x = range
uniform float lightCount;
uniform float omniAttenuation; // Godot OmniLight3D.omni_attenuation exponent

// (Godot pre-multiplies light energy by PI to cancel the 1/PI in its lambert
//  BRDF, so plain NdotL * colour*energy matches 1:1.)
vec3 psxComputeLight(vec3 vsPos, vec3 vsNormal)
{
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
            att = pow(clamp(1.0 - dist / lightAtten[i].x, 0.0, 1.0),
                      omniAttenuation);
        }
        light += lightDiffuse[i].rgb * max(dot(vsNormal, L), 0.0) * att;
    }
    return light;
}
