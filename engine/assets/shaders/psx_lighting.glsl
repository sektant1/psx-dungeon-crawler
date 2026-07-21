// Shared PSX light evaluation: ambient + lambert + Godot omni attenuation.
// Included by psx.vert (vertex-lit path) and psx.frag (per-pixel path) so the
// loop exists exactly once. Uniform bindings live in psx.program on BOTH the
// vertex and the LIT fragment programs.
uniform vec4 ambientLight; // ambient_light_colour (env ambient * energy)
uniform vec4 lightPos[16];  // view space; w == 0 -> directional (dir TO light)
uniform vec4 lightDiffuse[16]; // light colour * energy
uniform vec4 lightAtten[16];   // y = range (x is a huge dummy so Ogre's
                              // frustum light-culling never drops the light)
uniform float lightCount;
uniform float omniAttenuation; // Godot OmniLight3D.omni_attenuation exponent
uniform float lightSteps;      // > 0.5: posterize diffuse into N hard bands
                               // (ambient stays continuous); 0 = smooth

// (Godot pre-multiplies light energy by PI to cancel the 1/PI in its lambert
//  BRDF, so plain NdotL * colour*energy matches 1:1.)
vec3 psxComputeLight(vec3 vsPos, vec3 vsNormal)
{
    vec3 diffuse = vec3(0.0);
    int count = int(min(lightCount, 16.0) + 0.5);
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
            att = pow(clamp(1.0 - dist / lightAtten[i].y, 0.0, 1.0),
                      omniAttenuation);
        }
        diffuse += lightDiffuse[i].rgb * max(dot(vsNormal, L), 0.0) * att;
    }
    // Stepped torch-pool rings: quantize the diffuse term only, so ambient
    // never bands the whole scene toward black.
    if (lightSteps > 0.5)
        diffuse = floor(diffuse * lightSteps) / lightSteps;
    return ambientLight.rgb + diffuse;
}
