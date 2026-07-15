#version 330 core
// Port of Godot-3d-pixelart-demo pixelart_stylizer.gdshader (MIT, Leo Peltola),
// itself based on https://threejs.org/examples/webgl_postprocessing_pixel.html.
// Runs as a compositor quad pass at the low-res pixelated resolution, between
// the MRT scene render and the dither pass.
//
// Inputs: sceneTex = scene colour (sRGB-encoded, psx.frag output),
//         normalDepthTex = view-space normal *0.5+0.5, a = view depth/farClip.
// Background pixels have a = 1.0 (compositor clear alpha) -> depth = farClip,
// so silhouettes against the sky/void get shadow outlines like the reference.
in vec2 uv;
uniform sampler2D sceneTex;
uniform sampler2D normalDepthTex;
uniform float farClip;            // far_clip_distance auto param
uniform float stylizeEnabled;     // 1.0/0.0: 0 = pass-through (pixel-identical)
uniform float shadowsEnabled;     // 1.0/0.0
uniform float highlightsEnabled;  // 1.0/0.0
uniform float shadowStrength;     // 0..1, default 0.4
uniform float highlightStrength;  // 0..1, default 0.1
uniform vec3 shadowColor;         // default black
uniform vec3 highlightColor;      // default white
out vec4 fragColour;

float getDepth(vec2 suv)
{
    // Background pixels carry the clear alpha: 1.0 when the clear uses the
    // viewport background (opaque), 0.0 when Ogre clears the intermediate RT
    // to transparent black. Geometry always writes a > 0 (view depth >= near
    // clip), so map the a == 0 case to the far plane as well.
    float a = texture(normalDepthTex, suv).a;
    return a <= 0.0 ? farClip : a * farClip;
}

vec3 getNormal(vec2 suv)
{
    return texture(normalDepthTex, suv).rgb * 2.0 - 1.0;
}

// Credit: three.js webgl_postprocessing_pixel example.
float normalIndicator(vec3 normalEdgeBias, vec3 baseNormal, vec3 newNormal,
                      float depthDiff)
{
    float normalDiff = dot(baseNormal - newNormal, normalEdgeBias);
    float indicator = clamp(smoothstep(-0.01, 0.01, normalDiff), 0.0, 1.0);
    float depthIndicator = clamp(sign(depthDiff * 0.25 + 0.0025), 0.0, 1.0);
    return (1.0 - dot(baseNormal, newNormal)) * depthIndicator * indicator;
}

void main()
{
    vec3 original = texture(sceneTex, uv).rgb;
    if (stylizeEnabled < 0.5) {
        fragColour = vec4(original, 1.0);
        return;
    }

    vec2 e = 1.0 / vec2(textureSize(sceneTex, 0));

    // Shadow outlines: centre nearer than a neighbour = exterior edge.
    // negDepthDiff starts at 0 (not the Godot original's 0.5 seed) so a
    // disabled shadow pass doesn't leak a flat 0.5 suppression into the
    // highlight branch below; with shadows on, the 0.5 seed is applied
    // inside the block and behaviour matches the reference exactly.
    float depthDiff = 0.0;
    float negDepthDiff = 0.0;
    if (shadowsEnabled > 0.5) {
        float d  = getDepth(uv);
        float du = getDepth(uv + vec2( 0.0, -1.0) * e);
        float dr = getDepth(uv + vec2( 1.0,  0.0) * e);
        float dd = getDepth(uv + vec2( 0.0,  1.0) * e);
        float dl = getDepth(uv + vec2(-1.0,  0.0) * e);
        depthDiff += clamp(du - d, 0.0, 1.0);
        depthDiff += clamp(dd - d, 0.0, 1.0);
        depthDiff += clamp(dr - d, 0.0, 1.0);
        depthDiff += clamp(dl - d, 0.0, 1.0);
        negDepthDiff = 0.5 + (d - du) + (d - dd) + (d - dr) + (d - dl);
        negDepthDiff = clamp(negDepthDiff, 0.0, 1.0);
        // Godot original: smoothstep(0.5, 0.5, x) -- undefined per GLSL spec
        // when edge0 == edge1; every driver degenerates it to step(). Written
        // explicitly here, identical result.
        negDepthDiff = step(0.5, negDepthDiff);
        depthDiff = smoothstep(0.2, 0.3, depthDiff);
    }

    // Highlight edges: convex creases via normal divergence, depth-gated so
    // concave corners (negDepthDiff) don't glow.
    float normalDiff = 0.0;
    if (highlightsEnabled > 0.5) {
        vec3 n  = getNormal(uv);
        vec3 nu = getNormal(uv + vec2( 0.0, -1.0) * e);
        vec3 nr = getNormal(uv + vec2( 1.0,  0.0) * e);
        vec3 nd = getNormal(uv + vec2( 0.0,  1.0) * e);
        vec3 nl = getNormal(uv + vec2(-1.0,  0.0) * e);
        vec3 bias = vec3(1.0);
        normalDiff += normalIndicator(bias, n, nu, depthDiff);
        normalDiff += normalIndicator(bias, n, nr, depthDiff);
        normalDiff += normalIndicator(bias, n, nd, depthDiff);
        normalDiff += normalIndicator(bias, n, nl, depthDiff);
        normalDiff = smoothstep(0.2, 0.8, normalDiff);
        normalDiff = clamp(normalDiff - negDepthDiff, 0.0, 1.0);
    }

    vec3 final = original;
    final = mix(final, mix(original, highlightColor, highlightStrength),
                normalDiff * highlightsEnabled);
    final = mix(final, mix(original, shadowColor, shadowStrength),
                depthDiff * shadowsEnabled);
    fragColour = vec4(final, 1.0);
}
