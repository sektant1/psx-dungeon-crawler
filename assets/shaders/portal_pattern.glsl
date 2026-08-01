// The portal look: one profile of the surface kernel (surface_common.glsl).
//
// Everything generic -- stepped time, the metre pixel grid, dither, the palette,
// emission, the rim modes -- lives in the kernel. What is here is only what
// makes a portal a portal: a log-polar swirl with depth layers, view parallax,
// an event horizon and a containment ring.
//
// Included by portal.frag (samples the authored flow texture) and by
// prototype_portal.frag (synthesises the same field procedurally). Those two
// files are three lines each; the only thing they do differently is answer
// `surfaceField`, so the look cannot drift between them.
//
// Writing another surface type means copying this file, not the kernel: keep
// main() and the uniforms that are yours, and let the kernel do the rest.

// --- motion ----------------------------------------------------------------
uniform float portalFlowSpeed; // inward drift; negative flows outward
uniform float portalSwirlSpeed;// rotation of the whole field
uniform float portalTwist;     // spiral tightness (shear near the centre)
uniform float portalArms;      // arm count; whole numbers keep it seamless
uniform float portalArmWidth;  // arm vs gap of each turn; 0.5 = the raw sine

// --- depth -----------------------------------------------------------------
uniform float portalDepthScale;// log-polar depth compression: tunnel length
uniform float portalParallax;  // metres of depth per layer, for the eye shift

// --- shape -----------------------------------------------------------------
uniform float portalFieldWeight; // flow field vs. the analytic spiral, 0..1
uniform float portalCoreRadius;  // event horizon, in fractions of the height
uniform float portalCoreBoost;   // how hard the core burns through the swirl
uniform float portalRimRadius;   // containment ring radius
uniform float portalRimWidth;    // ring softness
uniform float portalRimIntensity;// ring brightness, 0 removes it
uniform float portalEdgeFade;    // fades the quad's square corners

void main()
{
    float steppedTime = surfaceSteppedTime();

    // Derivatives first: taken before any branching, for the reason documented
    // on surfaceMeshSize().
    vec2 meshSize = surfaceMeshSize();
    float height = max(meshSize.y, 1e-3);
    // The rims run along X or along Z depending on which of the four it is, so
    // their length is the distance travelled in the XZ plane per unit u rather
    // than one axis of the mapping.
    float edgeMetres =
        length(vec2(fwidth(surfaceLocal.x), fwidth(surfaceLocal.z))) /
        max(fwidth(surfaceUV.x), 1e-6);

    // What the slab's four thickness faces do. 0 continues the front pattern
    // (below), 1 gives them a band of their own, 2 removes them.
    bool rim = surfaceIsRim();
    if (rim && surfaceEdgeMode >= 1.5) {
        // discard, not an early return with alpha 0: the pass writes depth, so
        // an invisible-but-written rim would still punch a hole in whatever is
        // behind it.
        discard;
    }
    if (rim && surfaceEdgeMode >= 0.5) {
        fragColour = vec4(
            surfaceRimShade(steppedTime, edgeMetres) * surfaceBrightness, 1.0);
        return;
    }

    // Field position from OBJECT space rather than from the UVs, which is what
    // lets the rims simply continue the front pattern. The four rim faces share
    // the face's XZ footprint -- only their Y differs -- so taking the pattern
    // from XZ alone extrudes it through the thickness and the sides become the
    // edge of the same swirl. surfaceUV cannot do this: on a rim those UVs run
    // along the edge and across the thickness, a different mapping entirely.
    //
    // For the face itself this is the same number as the UV route: the plane
    // spans +-half in X and Z with UVs linear across it.
    vec2 p = surfaceLocal.xz / height;
    float cells = surfaceCells(height);

    // Parallax: metres of sideways shift per metre of depth, converted into
    // field units. The clamp stops a grazing view from shearing the layers off
    // the mesh entirely.
    vec3 viewDir = normalize(surfaceView);
    vec2 parallax = -vec2(viewDir.x, viewDir.z) /
                    max(abs(viewDir.y), 0.25) * (portalParallax / height);

    vec2 q = surfaceQuantize(p, cells);
    float radius = max(length(q), 0.004);
    float angle = atan(q.y, q.x);
    float arms = max(floor(portalArms + 0.5), 1.0);

    // Three depth layers, each further in, slower to turn and further sheared
    // by the eye. Weighted 1, 1/2, 1/3 so the nearest one carries the read and
    // the deeper ones only give it somewhere to fall to.
    //
    // Log-polar: equal steps along the sampling axis are equal *ratios* of
    // radius, so a layer scrolling at a constant rate reads as an endless fall
    // inward instead of a texture sliding across a quad.
    float layers = 0.0;
    float weights = 0.0;
    for (int i = 0; i < 3; ++i) {
        float li = float(i);
        vec2 lp = surfaceQuantize(p + parallax * li, cells);
        float lr = max(length(lp), 0.004);
        float la = atan(lp.y, lp.x);
        // Angular coordinate is scaled by a whole number of arms, so the wrap
        // at +-pi lands on a texture repeat instead of a seam.
        vec2 coord = vec2(
            la * (arms + li) / kTau + portalTwist / max(lr, 0.05) +
                steppedTime * portalSwirlSpeed * (1.0 + li * 0.25),
            log(lr) * portalDepthScale * (1.0 + li * 0.35) +
                steppedTime * portalFlowSpeed * (1.0 + li * 0.40));
        float w = 1.0 / (1.0 + li);
        layers += surfaceField(coord) * w;
        weights += w;
    }
    float field = layers / max(weights, 1e-4);

    // The analytic spiral the portal has always had. Kept as a blend target so
    // the shape stays readable at low resolution even when the flow art is
    // noisy: portalFieldWeight picks how much of the look the art carries.
    float spiral = 0.5 + 0.5 * sin(angle * arms -
                                   log(radius) * portalDepthScale * kTau +
                                   steppedTime * portalSwirlSpeed * kTau);
    float value = mix(spiral, field, clamp(portalFieldWeight, 0.0, 1.0));

    // How much of each turn is lit arm rather than dark gap. The raw sine is an
    // even 50/50 split and the palette thresholds are fixed, so without this the
    // arms have exactly one thickness and the only way to fatten them is to
    // recolour the palette -- which changes the portal's colour to fix its
    // shape. A power curve skews the duty cycle instead, leaving both alone.
    //
    // 0.5 is the raw sine, so the default is a no-op and existing profiles are
    // untouched. Below 0.5 thins the arms toward threads, above fattens them
    // until the gaps are what read as the pattern.
    float armWidth = clamp(portalArmWidth, 0.02, 0.98);
    value = pow(clamp(value, 0.0, 1.0), (1.0 - armWidth) / armWidth);

    // Event horizon: the middle burns out into the core tone.
    float core = 1.0 - smoothstep(0.0, max(portalCoreRadius, 0.001), radius);
    value = mix(value, 1.0, core * clamp(portalCoreBoost, 0.0, 1.0));

    // The light comes from deep inside, so the field dims as it climbs out of
    // the swirl and is cut off at the containment ring. That cut is what makes
    // a rectangular slab read as a round maw: without it the membrane is a
    // fully lit square sitting inside the arch, every corner as bright as the
    // centre.
    float fade = max(portalEdgeFade, 0.001);
    float inside = smoothstep(portalRimRadius + fade, portalRimRadius - fade,
                              radius);
    // Gently: the palette's first band is the void between the arms, so a steep
    // gradient drops the whole outer disc into it and the portal reads as an
    // empty ring instead of a swirl.
    float depthGain = mix(0.78, 1.10,
                          smoothstep(portalRimRadius, 0.0, radius));
    // Not to zero: outside the ring the swirl keeps a faint spill, which is
    // what stops the rest of the slab reading as a flat painted panel.
    float mask = mix(0.55, 1.0, inside);
    // The slab is wider than it is tall, so the round cut alone can still leave
    // the two side corners lit; the quad's own border closes them. From object
    // space for the same reason `p` is: on a rim the UVs mean something else,
    // and this has to agree with the pattern it is masking.
    vec2 border = abs(surfaceLocal.xz) / max(meshSize * 0.5, vec2(1e-4));
    mask *= 1.0 - smoothstep(1.0 - fade, 1.0, max(border.x, border.y));
    value *= depthGain * mask;

    // Containment ring, the classic hard edge of a sprite portal. Added after
    // the cut, since it *is* the cut's edge.
    float ring = 1.0 - smoothstep(0.0, max(portalRimWidth, 0.001),
                                  abs(radius - portalRimRadius));
    value = max(value, ring * portalRimIntensity);

    fragColour = surfaceResolve(value, floor(q * cells));
}
