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
    // x precision multiplier, y light steps, z step softness, w affine amount.
    vec4 psxParams;
} scene;

layout(push_constant) uniform DrawConstants {
    mat4 model;
    vec4 tintOpacity;
    vec4 rimColourStrength;
    vec4 surfaceParams;
    vec4 uvTransform;
} drawData;


// The GTE had no subpixel precision: it transformed to a fixed-point screen
// grid, so vertices jumped between whole positions and long edges visibly
// crawled. Reproduced against the same 512x448 base grid psx.vert uses, so a
// precision multiplier tuned on the GL path means the same thing here.
const vec2 kSnapResolution = vec2(512.0, 448.0);
vec4 snapToGrid(vec4 clip) {
    // Vertices at or behind the eye have w <= 0, where the NDC divide sends xy
    // to +/-infinity and floor() folds them back across the screen as a stray
    // triangle. Leave those to the clipper.
    if (clip.w < 1e-4 || scene.psxParams.x >= 0.999)
        return clip;
    vec2 grid = floor(kSnapResolution * scene.psxParams.x);
    if (grid.x < 1.0 || grid.y < 1.0)
        return clip;
    vec3 ndc = clip.xyz / clip.w;
    ndc.x = floor(grid.x * ndc.x) / grid.x;
    ndc.y = floor(grid.y * ndc.y) / grid.y;
    return vec4(ndc * clip.w, clip.w);
}

layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec3 worldNormal;
layout(location = 2) out vec2 uv;
layout(location = 3) out vec4 colour;
// View-space metadata for the stylize MRT surface. The edge pass classifies
// convex vs concave folds by the sign of the normal derivative along each
// screen axis, so the normal has to be in view space, not world space.
layout(location = 4) out vec3 viewNormal;
layout(location = 5) out float viewDepth;
// Gouraud term. Written only in vertex-lit mode; the fragment stage decides
// whether to read it, so the varying costs one interpolator either way.
layout(location = 6) out vec3 vertexLighting;
// The same UVs, interpolated without perspective correction. The fragment
// stage blends between this and the smooth pair, which is what makes the warp
// a dial rather than a compile-time variant (psx.frag does the same).
layout(location = 7) noperspective out vec2 uvAffine;

// The diffuse accumulation, identical whichever stage runs it. Vertex-lit mode
// (the PS1 and N64 presets) evaluates this once per vertex and lets the
// rasterizer interpolate; per-pixel mode evaluates it per fragment. Keeping it
// as one function is what stops the two modes drifting into two looks.
vec3 accumulateLighting(vec3 worldPos, vec3 normal) {
    vec3 diffuse = vec3(0.0);
    int lightCount = clamp(int(scene.cameraPositionAndLightCount.w + 0.5), 0, 16);
    for (int i = 0; i < lightCount; ++i) {
        vec3 lightVector;
        float attenuation = 1.0;
        if (scene.lightColourType[i].w > 0.5) {
            vec3 delta = scene.lightPositionRange[i].xyz - worldPos;
            float distanceToLight = length(delta);
            float range = max(scene.lightPositionRange[i].w, 0.001);
            lightVector = delta / max(distanceToLight, 0.0001);
            attenuation = clamp(1.0 - distanceToLight / range, 0.0, 1.0);
        } else {
            lightVector = normalize(-scene.lightPositionRange[i].xyz);
        }
        diffuse += scene.lightColourType[i].rgb *
                   max(dot(normal, lightVector), 0.0) * attenuation;
    }
    // Posterized torch pools: quantize the DIFFUSE only, so ambient never
    // bands the whole scene toward black. Deliberately unclamped -- floor()
    // keeps values above 1, preserving the overbright cores the bloom bright
    // pass thresholds on. Soft-edged so band seams fade instead of snapping.
    if (scene.psxParams.y > 0.5) {
        float edge = clamp(scene.psxParams.z, 0.0, 0.5);
        vec3 x = diffuse * scene.psxParams.y;
        vec3 soft = smoothstep(vec3(0.5 - edge), vec3(0.5 + edge), fract(x));
        diffuse = (floor(x) + soft) / scene.psxParams.y;
    }
    return scene.ambient.rgb + diffuse;
}

void main() {
    vec4 world = drawData.model * vec4(inPosition, 1.0);
    worldPosition = world.xyz;
    worldNormal = normalize(transpose(inverse(mat3(drawData.model))) * inNormal);
    uv = inUv * drawData.uvTransform.xy + drawData.uvTransform.zw;
    uvAffine = uv;
    colour = inColour;
    vec4 viewPosition = scene.view * world;
    viewNormal = normalize(mat3(scene.view) * worldNormal);
    viewDepth = -viewPosition.z;   // positive in front of the camera
    // clipParams.z is the vertex-lighting switch (see Renderer::setPerPixelLightingEnabled).
    vertexLighting = scene.clipParams.z > 0.5
                         ? accumulateLighting(worldPosition, worldNormal)
                         : vec3(0.0);
    gl_Position = snapToGrid(scene.viewProjection * world);
}
