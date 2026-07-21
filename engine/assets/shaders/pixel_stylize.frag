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
uniform float outlineThickness;   // edge sample offset in low-res pixels (1..4)
uniform float shadowThreshold;    // depth-edge smoothstep centre, default 0.25
uniform float highlightThreshold; // normal-edge smoothstep centre, default 0.5
uniform float highlightDarkFade;  // luma where highlights reach full, default 0.25

// Ink outline (Boltgun-style): a hard contour drawn over the frame, separate
// from the soft shadow/highlight tinting above. Depth silhouettes ink the
// *near* object only (one-sided, so lines stay 1 tap wide); normal creases
// add interior feature lines.
uniform float outlineEnabled;     // 1.0/0.0
uniform vec3 outlineColor;        // default black
uniform float outlineOpacity;     // 0..1 ink coverage, default 0.85
uniform float outlineDepthSens;   // relative depth-step gain, default 15
uniform float outlineNormalSens;  // crease line gain (0 = silhouette only), 0.6
uniform float outlineSharpness;   // 1 = hard pixel edge, 0 = soft, default 0.85
uniform float outlineDistFade;    // exp(-depth*fade): ink dies into the fog, 0.08
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

    // Thickness scales the cross-sample offsets: 1 = the reference's
    // single-pixel outline, higher pushes the taps out so edges fatten
    // (in low-res pixels, so the result stays chunky after upscale).
    vec2 e = outlineThickness / vec2(textureSize(sceneTex, 0));

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
        // Centre exposed as shadowThreshold (reference: 0.25); lower = more
        // depth steps count as edges, higher = only strong silhouettes.
        depthDiff = smoothstep(shadowThreshold - 0.05, shadowThreshold + 0.05,
                               depthDiff);
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
        // Centre exposed as highlightThreshold (reference: 0.5); lower =
        // shallower creases highlight, higher = only sharp convex corners.
        normalDiff = smoothstep(highlightThreshold - 0.3,
                                highlightThreshold + 0.3, normalDiff);
        normalDiff = clamp(normalDiff - negDepthDiff, 0.0, 1.0);
        // Highlights are painted light; on a surface sitting in darkness
        // they read as a glowing wire. Fade them with scene luminance so
        // only lit geometry gets edge highlights.
        float lum = dot(original, vec3(0.2126, 0.7152, 0.0722));
        normalDiff *= smoothstep(0.04, max(highlightDarkFade, 0.05), lum);
    }

    // Ink outline: relative depth steps (dn - d) / d so a 10 cm ledge inks
    // the same at 2 m and 10 m; normal disagreement adds interior creases.
    float ink = 0.0;
    if (outlineEnabled > 0.5) {
        float d  = getDepth(uv);
        float du = getDepth(uv + vec2( 0.0, -1.0) * e);
        float dr = getDepth(uv + vec2( 1.0,  0.0) * e);
        float dd = getDepth(uv + vec2( 0.0,  1.0) * e);
        float dl = getDepth(uv + vec2(-1.0,  0.0) * e);
        // Second difference (depth curvature), not first difference: a flat
        // surface at ANY viewing angle has du+dd-2d ~ 0 (its per-pixel depth
        // step is constant), so grazing corridor floors/walls never self-ink.
        // A silhouette jumps on exactly one side, leaving a large positive
        // curvature. Normalise by d so a ledge inks the same at 2 m and 10 m.
        float rel = max(max(du + dd - 2.0 * d, dl + dr - 2.0 * d), 0.0) /
                    max(d, 1e-3);
        float edge = clamp(rel * outlineDepthSens, 0.0, 1.0);

        vec3 n  = getNormal(uv);

        vec3 nu = getNormal(uv + vec2( 0.0, -1.0) * e);
        vec3 nr = getNormal(uv + vec2( 1.0,  0.0) * e);
        vec3 nd = getNormal(uv + vec2( 0.0,  1.0) * e);
        vec3 nl = getNormal(uv + vec2(-1.0,  0.0) * e);
        float nEdge = max(max(1.0 - dot(n, nu), 1.0 - dot(n, nd)),
                          max(1.0 - dot(n, nr), 1.0 - dot(n, nl)));
        edge = max(edge, clamp(nEdge * outlineNormalSens, 0.0, 1.0));

        // Sharpness narrows the smoothstep band around 0.5: at 1.0 the line
        // is a hard step (crisp pixel ink), at 0 it fades in over the full
        // range. Distance fade sinks far ink into the fog instead of
        // leaving black wires floating in the murk.
        float band = max((1.0 - outlineSharpness) * 0.5, 0.01);
        edge = smoothstep(0.5 - band, 0.5 + band, edge);
        ink = edge * outlineOpacity * exp(-d * outlineDistFade);
    }

    vec3 final = original;
    final = mix(final, mix(original, highlightColor, highlightStrength),
                normalDiff * highlightsEnabled);
    final = mix(final, mix(original, shadowColor, shadowStrength),
                depthDiff * shadowsEnabled);
    final = mix(final, outlineColor, ink * 0.0); // TEMP: ink neutered
    fragColour = vec4(final, 1.0);
}
