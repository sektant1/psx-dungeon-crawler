#version 450
// Port of assets/shaders/pixel_stylize.frag. Pixel-art edge pass following
// David Holland's "3D Pixel Art Rendering":
//   * a 4-tap cross (up/down/left/right) offset by whole texels, so every line
//     comes out exactly one render pixel thick. A 3x3 sobel widens diagonals to
//     two pixels, which reads as anti-aliased mush at 320x240.
//   * depth drives object silhouettes, normals drive interior edges.
//   * interior edges split convex/concave by the sign of the normal derivative
//     along each tap's own screen axis, so highlights land only on convex folds
//     and creases only on concave ones.

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColour;

layout(set = 1, binding = 0) uniform sampler2D sceneTexture;
layout(set = 1, binding = 1) uniform sampler2D normalDepthTexture;

layout(set = 0, binding = 0, std140) uniform StylizeUniforms {
    vec4 shadowColour;
    vec4 highlightColour;
    vec4 outlineColour;
    vec4 clip;       // x near, y far
    vec4 toggles;    // x stylize, y shadows, z highlights, w outline
    vec4 shadow;     // x strength, y threshold
    vec4 highlight;  // x strength, y threshold, z darkFade, w colourOverride
    vec4 outlineA;   // x opacity, y thickness, z depthSens, w normalSens
    vec4 outlineB;   // x sharpness, y distFade, z darkFade
    vec4 convex;     // x convexity, y bias
} params;

float depthAt(vec2 p) {
    vec4 metadata = texture(normalDepthTexture, p);
    return length(metadata.rgb) < 0.1 ? params.clip.y
                                      : abs(metadata.a) * params.clip.y;
}
vec3 normalAt(vec2 p) {
    return texture(normalDepthTexture, p).rgb * 2.0 - 1.0;
}

void main() {
    vec3 original = texture(sceneTexture, uv).rgb;
    // Surfaces that wrote no metadata (additive light volumes, cleared sky)
    // encode zero. Test the raw encoding before any taps are calculated --
    // normalAt() would remap that zero to -1 and invent an edge.
    if (params.toggles.x < 0.5 ||
        length(texture(normalDepthTexture, uv).rgb) < 0.1) {
        outColour = vec4(original, 1.0);
        return;
    }

    // Whole-texel taps only. A fractional offset lets one arm of the cross land
    // back in the centre texel while the opposite arm reaches two texels out,
    // which is what thickens lines unevenly along diagonals.
    vec2 e = max(floor(params.outlineA.y + 0.5), 1.0) /
             vec2(textureSize(sceneTexture, 0));
    float d = depthAt(uv);
    float du = depthAt(uv + vec2(0, -1) * e), dd = depthAt(uv + vec2(0, 1) * e);
    float dl = depthAt(uv + vec2(-1, 0) * e), dr = depthAt(uv + vec2(1, 0) * e);
    float depthEdge = clamp(max(max(du - d, dd - d), max(dl - d, dr - d)),
                            0.0, 1.0);
    float shadow = smoothstep(params.shadow.y - 0.05, params.shadow.y + 0.05,
                              depthEdge) * params.toggles.y;

    // uv.y grows downward, so the -v tap is screen up = view-space +y and the
    // +u tap is screen right = view-space +x. A fold is convex where the
    // normal's component along the step direction grows across it, concave
    // where it shrinks.
    vec3 n = normalAt(uv);
    vec3 nu = normalAt(uv + vec2(0, -1) * e), nd = normalAt(uv + vec2(0, 1) * e);
    vec3 nl = normalAt(uv + vec2(-1, 0) * e), nr = normalAt(uv + vec2(1, 0) * e);
    vec4 mag = vec4(1.0 - dot(n, nu), 1.0 - dot(n, nd),
                    1.0 - dot(n, nl), 1.0 - dot(n, nr));
    vec4 conv = vec4(nu.y - n.y, n.y - nd.y, n.x - nl.x, nr.x - n.x);
    // Soft classifier: near-flat gradients sit in the deadzone and contribute
    // to neither term, so shallow curvature cannot flicker between a highlight
    // and a crease as the camera moves.
    float bias = max(params.convex.y, 1e-4);
    vec4 cw = smoothstep(vec4(-bias), vec4(bias), conv);
    vec4 cmag = mag * cw, kmag = mag * (1.0 - cw);
    float normalEdge = max(max(mag.x, mag.y), max(mag.z, mag.w));
    float convexEdge = max(max(cmag.x, cmag.y), max(cmag.z, cmag.w));
    float concaveEdge = max(max(kmag.x, kmag.y), max(kmag.z, kmag.w));
    float hiEdge = mix(normalEdge, convexEdge, params.convex.x);
    float creaseEdge = mix(normalEdge, concaveEdge, params.convex.x);

    float luminance = dot(original, vec3(0.2126, 0.7152, 0.0722));
    float nearFade = smoothstep(params.clip.x * 1.25, params.clip.x * 4.0, d);
    // Negative encoded depth marks stone: keep its outlines and creases, but
    // never lay the highlight wash over it.
    float acceptsHighlight = step(0.0, texture(normalDepthTexture, uv).a);
    float hi = smoothstep(params.highlight.y - 0.3, params.highlight.y + 0.3,
                          hiEdge) *
               params.toggles.z * acceptsHighlight *
               smoothstep(0.02, max(params.highlight.z, 0.05), luminance) *
               nearFade;
    float rel = max(max(du + dd - 2.0 * d, dl + dr - 2.0 * d), 0.0) /
                max(d, 0.001);
    float sharp = max((1.0 - params.outlineB.x) * 0.5, 0.01);
    float ink = smoothstep(0.5 - sharp, 0.5 + sharp,
                           clamp(rel * params.outlineA.z +
                                 creaseEdge * params.outlineA.w, 0.0, 1.0)) *
                params.toggles.w * params.outlineA.x *
                exp(-d * params.outlineB.y) *
                smoothstep(0.02, max(params.outlineB.z, 0.03), luminance) *
                nearFade;

    // The default highlight stays in the material's own hue by lifting its
    // brightest channel to one. An authored colour remains available for
    // palettes that want a single environmental tint (torchlight, moonlight).
    float peak = max(max(original.r, original.g), original.b);
    vec3 matchedHighlight = original / max(peak, 1e-4);
    vec3 highlightTarget = mix(matchedHighlight, params.highlightColour.rgb,
                               params.highlight.w);
    vec3 outc = mix(original,
                    mix(original, highlightTarget, params.highlight.x), hi);
    outc = mix(outc, mix(original, params.shadowColour.rgb, params.shadow.x),
               shadow * nearFade);
    outColour = vec4(mix(outc, params.outlineColour.rgb, ink), 1.0);
}
