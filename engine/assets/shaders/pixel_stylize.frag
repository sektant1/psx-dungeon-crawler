#version 330 core
// Pixel-art edge pass. Kernel and edge classification follow David Holland's
// "3D Pixel Art Rendering" (davidhol.land/articles/3d-pixel-art-rendering):
//   * a 4-tap cross (up/down/left/right only) offset by whole texels, so every
//     line comes out exactly one render pixel thick. A 3x3 sobel widens
//     diagonals to two pixels, which reads as anti-aliased mush at 320x240.
//   * the depth buffer drives object silhouettes, the normal buffer drives
//     interior edges.
//   * interior edges are split convex/concave by the sign of the normal
//     derivative along each tap's own screen axis -- the cross-product
//     convexity test, written as the dot product it reduces to for
//     front-facing normals. Edge *highlights* then land only on convex folds
//     (box corners, ridges catching the light) and concave folds get the dark
//     crease instead, instead of both effects firing on every discontinuity.
//     edgeConvexity 0 restores the old undivided behaviour.
in vec2 uv;
uniform sampler2D sceneTex, normalDepthTex;
uniform float nearClip, farClip, stylizeEnabled, shadowsEnabled, highlightsEnabled;
uniform float shadowStrength, highlightStrength, outlineThickness, shadowThreshold;
uniform float highlightThreshold, highlightDarkFade, outlineEnabled, outlineOpacity;
uniform float highlightColorOverride;
uniform float outlineDepthSens, outlineNormalSens, outlineSharpness, outlineDistFade;
uniform float outlineDarkFade;
uniform float edgeConvexity;  // 1 = convex-only highlights + concave-only creases
uniform float edgeConvexBias; // deadzone half-width of the convex/concave test
uniform vec3 shadowColor, highlightColor, outlineColor;
out vec4 fragColour;
float depthAt(vec2 p) {
    vec4 metadata=texture(normalDepthTex,p);
    return length(metadata.rgb)<0.1 ? farClip : abs(metadata.a)*farClip;
}
vec3 normalAt(vec2 p) { return texture(normalDepthTex,p).rgb*2.0-1.0; }
void main() {
    vec4 scene=texture(sceneTex,uv); vec3 original=scene.rgb;
    // Dungeon materials write zero into the encoded normal metadata. Test
    // that raw encoding (not normalAt(), which remaps zero to -1) before any
    // outline/highlight samples are calculated.
    if (stylizeEnabled<0.5 || length(texture(normalDepthTex, uv).rgb) < 0.1) {
        fragColour=vec4(original,1);
        return;
    }
    // Whole-texel taps only. A fractional offset lets one arm of the cross
    // land back in the centre texel while the opposite arm reaches two texels
    // out, which is what thickens lines unevenly along diagonals.
    vec2 e=max(floor(outlineThickness+0.5),1.0)/vec2(textureSize(sceneTex,0));
    float d=depthAt(uv), du=depthAt(uv+vec2(0,-1)*e), dd=depthAt(uv+vec2(0,1)*e), dl=depthAt(uv+vec2(-1,0)*e), dr=depthAt(uv+vec2(1,0)*e);
    float depthEdge=clamp(max(max(du-d,dd-d),max(dl-d,dr-d)),0.,1.);
    float shadow=smoothstep(shadowThreshold-.05,shadowThreshold+.05,depthEdge)*shadowsEnabled;
    // uv.y grows downward (Ogre's compositor quad puts uv (0,0) at the top-left
    // corner), so the -v tap is screen up = view-space +y, and the +u tap is
    // screen right = view-space +x. A fold is convex where the normal's
    // component along the step direction grows across it, concave where it
    // shrinks; `conv` is that signed derivative per tap, each measured along
    // its own outward direction.
    vec3 n=normalAt(uv);
    vec3 nu=normalAt(uv+vec2(0,-1)*e), nd=normalAt(uv+vec2(0,1)*e);
    vec3 nl=normalAt(uv+vec2(-1,0)*e), nr=normalAt(uv+vec2(1,0)*e);
    vec4 mag=vec4(1.-dot(n,nu),1.-dot(n,nd),1.-dot(n,nl),1.-dot(n,nr));
    vec4 conv=vec4(nu.y-n.y, n.y-nd.y, n.x-nl.x, nr.x-n.x);
    // Soft classifier: near-flat gradients sit in the deadzone and contribute
    // to neither term, so shallow curvature cannot flicker between a highlight
    // and a crease as the camera moves.
    vec4 cw=smoothstep(-max(edgeConvexBias,1e-4),max(edgeConvexBias,1e-4),conv);
    vec4 cmag=mag*cw, kmag=mag*(1.-cw);
    float normalEdge=max(max(mag.x,mag.y),max(mag.z,mag.w));
    float convexEdge=max(max(cmag.x,cmag.y),max(cmag.z,cmag.w));
    float concaveEdge=max(max(kmag.x,kmag.y),max(kmag.z,kmag.w));
    float hiEdge=mix(normalEdge,convexEdge,edgeConvexity);
    float creaseEdge=mix(normalEdge,concaveEdge,edgeConvexity);
    float luminance=dot(original,vec3(.2126,.7152,.0722));
    float nearFade=smoothstep(nearClip*1.25,nearClip*4.0,d);
    // Dungeon materials encode negative depth: retain their normal/depth for
    // outlines and ink shadows, but never lay the highlight wash over stone.
    float acceptsHighlight=step(0.0,texture(normalDepthTex,uv).a);
    float hi=smoothstep(highlightThreshold-.3,highlightThreshold+.3,hiEdge)*highlightsEnabled*acceptsHighlight*smoothstep(.02,max(highlightDarkFade,.05),luminance)*nearFade;
    float rel=max(max(du+dd-2.*d,dl+dr-2.*d),0.)/max(d,.001);
    float ink=smoothstep(.5-max((1.-outlineSharpness)*.5,.01),.5+max((1.-outlineSharpness)*.5,.01),clamp(rel*outlineDepthSens+creaseEdge*outlineNormalSens,0.,1.))*outlineEnabled*outlineOpacity*exp(-d*outlineDistFade)*smoothstep(.02,max(outlineDarkFade,.03),luminance)*nearFade;
    // The default highlight stays in the material's own hue by lifting its
    // brightest channel to one. An authored colour remains available for
    // palettes that want a single environmental tint (torchlight, moonlight).
    float peak=max(max(original.r,original.g),original.b);
    vec3 matchedHighlight=original/max(peak,1e-4);
    vec3 highlightTarget=mix(matchedHighlight,highlightColor,highlightColorOverride);
    vec3 outc=mix(original,mix(original,highlightTarget,highlightStrength),hi);
    outc=mix(outc,mix(original,shadowColor,shadowStrength),shadow*nearFade);
    fragColour=vec4(mix(outc,outlineColor,ink),1);
}
