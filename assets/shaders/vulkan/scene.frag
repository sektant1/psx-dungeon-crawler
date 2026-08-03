#version 450

layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 worldNormal;
layout(location = 2) in vec2 uv;
layout(location = 3) in vec4 colour;
layout(location = 4) in vec3 viewNormal;
layout(location = 5) in float viewDepth;
layout(location = 0) out vec4 outColour;
// MRT surface 1, consumed by the stylize pass: view-space normal encoded
// *0.5+0.5, alpha = linear view depth / farClip. The depth sign is the
// DUNGEON_NO_HIGHLIGHT marker the legacy shader carried as a compile-time
// variant: negative means "keep this surface's outlines and creases, but never
// lay the highlight wash over it" (stone).
layout(location = 1) out vec4 outNormalDepth;

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
layout(set = 1, binding = 0) uniform sampler2D albedoTexture;
layout(set = 1, binding = 1) uniform sampler2D shadowMap;

layout(push_constant) uniform DrawConstants {
    mat4 model;
    vec4 tintOpacity;
    vec4 rimColourStrength;
    vec4 surfaceParams;
    vec4 uvTransform;
} drawData;

// The legacy PSX shader shades in linear space and encodes to sRGB for
// display (Godot's model). Reproduce it here: without the transfer pair the
// whole scene reads ~2.2 gamma too dark. Light/ambient/fog colours already
// arrive linear from the CPU side, so only texture/tint are linearised.
vec3 toLinear(vec3 c) { return pow(max(c, vec3(0.0)), vec3(2.2)); }
vec3 toSrgb(vec3 c) { return pow(max(c, vec3(0.0)), vec3(1.0 / 2.2)); }

// Flat wire tint, matching Wire_FS's authored default. A diagnostic view wants
// one legible colour, not the material's own.
const vec3 kWireColour = vec3(0.55, 0.8, 1.0);

void main() {
    if (drawData.surfaceParams.w > 0.5) {
        // Debug wireframe: the rasterizer is already drawing lines, so this
        // only has to colour them. No lighting, no texture, no fog -- the view
        // exists to show topology.
        outColour = vec4(kWireColour, 1.0);
        outNormalDepth = vec4(0.0);
        return;
    }
    vec4 albedo = texture(albedoTexture, uv) * colour * drawData.tintOpacity;
    if (albedo.a < drawData.surfaceParams.x)
        discard;
    albedo.rgb = toLinear(albedo.rgb);

    vec3 normal = normalize(worldNormal);
    vec3 lighting = scene.ambient.rgb;
    int lightCount = clamp(int(scene.cameraPositionAndLightCount.w + 0.5), 0, 16);
    for (int i = 0; i < lightCount; ++i) {
        vec3 lightVector;
        float attenuation = 1.0;
        if (scene.lightColourType[i].w > 0.5) {
            vec3 delta = scene.lightPositionRange[i].xyz - worldPosition;
            float distanceToLight = length(delta);
            float range = max(scene.lightPositionRange[i].w, 0.001);
            lightVector = delta / max(distanceToLight, 0.0001);
            attenuation = clamp(1.0 - distanceToLight / range, 0.0, 1.0);
        } else {
            lightVector = normalize(-scene.lightPositionRange[i].xyz);
        }
        lighting += scene.lightColourType[i].rgb *
                    max(dot(normal, lightVector), 0.0) * attenuation;
    }

    // Directional shadow. Ogre used stencil volumes modulatively -- one
    // darkening pass over shadowed area -- so this reproduces the RESULT
    // rather than the technique: a hard-edged darkening toward the same
    // shadow tone, which is what the look actually depends on. A depth map is
    // the tractable way to get that here, and at this render resolution its
    // stair-step is under the pixel grid anyway.
    if (scene.shadowParams.x > 0.5) {
        vec4 lightClip = scene.lightViewProjection * vec4(worldPosition, 1.0);
        vec3 lightNdc = lightClip.xyz / max(lightClip.w, 1e-6);
        // V is flipped: VulkanCommandList::setViewport submits a
        // negative-height viewport, so the shadow pass rasterised into the map
        // upside down relative to its own clip space. Sampling without this
        // reads the map mirrored and shadows land nowhere near their casters.
        vec2 shadowUv = lightNdc.xy * 0.5 + 0.5;
        shadowUv.y = 1.0 - shadowUv.y;
        // Outside the map is lit, not shadowed: the map covers a short range
        // around the camera, and clamping would smear its border across the
        // whole level.
        if (all(greaterThanEqual(shadowUv, vec2(0.0))) &&
            all(lessThanEqual(shadowUv, vec2(1.0))) &&
            lightNdc.z <= 1.0) {
            // Slope-scaled bias: a surface edge-on to the sun crosses many
            // depth texels per pixel, and a flat bias large enough for it
            // would peel the shadow off everything else (peter-panning).
            float ndl = clamp(dot(normal, normalize(
                -scene.lightPositionRange[0].xyz)), 0.0, 1.0);
            float bias = scene.shadowParams.y * (2.0 - ndl);
            float texel = scene.shadowParams.w;
            float lit = 0.0;
            // Four taps: enough to break the staircase along a silhouette
            // without softening the shadow into something un-PSX.
            for (int x = 0; x < 2; ++x)
                for (int y = 0; y < 2; ++y) {
                    vec2 offset = (vec2(x, y) - 0.5) * texel;
                    float depth = texture(shadowMap, shadowUv + offset).r;
                    lit += lightNdc.z - bias <= depth ? 1.0 : 0.0;
                }
            lit *= 0.25;
            lighting *= mix(scene.shadowParams.z, 1.0, lit);
        }
    }

    vec3 viewDirection = normalize(scene.cameraPositionAndLightCount.xyz -
                                   worldPosition);
    float rim = pow(1.0 - clamp(dot(normal, viewDirection), 0.0, 1.0),
                    max(drawData.surfaceParams.y, 0.01));
    // Gate by the local light level: the sheen is reflected light, so it has to
    // die on shadowed/unlit faces instead of glowing through them (psx.frag).
    float rimLit = clamp(dot(lighting, vec3(0.299, 0.587, 0.114)), 0.0, 1.0);
    vec3 rgb = albedo.rgb * lighting +
               toLinear(drawData.rimColourStrength.rgb) *
               (rim * drawData.rimColourStrength.a * rimLit);
    float distanceToEye = length(scene.cameraPositionAndLightCount.xyz -
                                 worldPosition);
    float fog = clamp(1.0 - exp(-max(scene.fogColourDensity.a, 0.0) *
                                distanceToEye), 0.0, 1.0);
    // Verdigris murk (psx.frag): desaturate + darken with distance before the
    // fog mix, so geometry sinks into the fog instead of flat-lerping toward
    // its colour. ambient.w carries fogDesatBoost; 0 leaves this a no-op.
    float sink = clamp(fog * scene.ambient.w, 0.0, 1.0);
    rgb = mix(rgb, vec3(dot(rgb, vec3(0.2126, 0.7152, 0.0722))) * 0.6, sink);
    rgb = mix(rgb, scene.fogColourDensity.rgb, fog);
    outColour = vec4(toSrgb(rgb), albedo.a);
    float encodedDepth = viewDepth / max(scene.clipParams.y, 1e-4);
    outNormalDepth = vec4(normalize(viewNormal) * 0.5 + 0.5,
                          drawData.surfaceParams.z > 0.5 ? -encodedDepth
                                                         : encodedDepth);
}
