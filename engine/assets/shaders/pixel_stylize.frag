#version 330 core
in vec2 uv;
uniform sampler2D sceneTex, normalDepthTex;
uniform float nearClip, farClip, stylizeEnabled, shadowsEnabled, highlightsEnabled;
uniform float shadowStrength, highlightStrength, outlineThickness, shadowThreshold;
uniform float highlightThreshold, highlightDarkFade, outlineEnabled, outlineOpacity;
uniform float outlineDepthSens, outlineNormalSens, outlineSharpness, outlineDistFade;
uniform float outlineDarkFade;
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
    vec2 e=outlineThickness/vec2(textureSize(sceneTex,0));
    float d=depthAt(uv), du=depthAt(uv+vec2(0,-1)*e), dd=depthAt(uv+vec2(0,1)*e), dl=depthAt(uv+vec2(-1,0)*e), dr=depthAt(uv+vec2(1,0)*e);
    float depthEdge=clamp(max(max(du-d,dd-d),max(dl-d,dr-d)),0.,1.);
    float shadow=smoothstep(shadowThreshold-.05,shadowThreshold+.05,depthEdge)*shadowsEnabled;
    vec3 n=normalAt(uv); float normalEdge=max(max(1.-dot(n,normalAt(uv+vec2(0,-1)*e)),1.-dot(n,normalAt(uv+vec2(0,1)*e))),max(1.-dot(n,normalAt(uv+vec2(-1,0)*e)),1.-dot(n,normalAt(uv+vec2(1,0)*e))));
    float luminance=dot(original,vec3(.2126,.7152,.0722));
    float nearFade=smoothstep(nearClip*1.25,nearClip*4.0,d);
    // Dungeon materials encode negative depth: retain their normal/depth for
    // outlines and ink shadows, but never lay the highlight wash over stone.
    float acceptsHighlight=step(0.0,texture(normalDepthTex,uv).a);
    float hi=smoothstep(highlightThreshold-.3,highlightThreshold+.3,normalEdge)*highlightsEnabled*acceptsHighlight*smoothstep(.02,max(highlightDarkFade,.05),luminance)*nearFade;
    float rel=max(max(du+dd-2.*d,dl+dr-2.*d),0.)/max(d,.001);
    float ink=smoothstep(.5-max((1.-outlineSharpness)*.5,.01),.5+max((1.-outlineSharpness)*.5,.01),clamp(rel*outlineDepthSens+normalEdge*outlineNormalSens,0.,1.))*outlineEnabled*outlineOpacity*exp(-d*outlineDistFade)*smoothstep(.02,max(outlineDarkFade,.03),luminance)*nearFade;
    vec3 outc=mix(original,mix(original,highlightColor,highlightStrength),hi);
    outc=mix(outc,mix(original,shadowColor,shadowStrength),shadow*nearFade);
    fragColour=vec4(mix(outc,outlineColor,ink),1);
}
