# Pixelation + Outline Post-Process Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the Godot/three.js "3D pixel art" stylizer (depth shadow outlines + normal highlight edges + live pixel-size slider) on top of the existing PSX post stack, per `docs/superpowers/specs/2026-07-14-pixelation-outline-design.md`.

**Architecture:** The PSX fragment shaders gain an MRT output (view-space normal + normalized linear depth). A new compositor `PSX/Stylized` replaces `PSX/Dither`: scene renders into a low-res MRT (`window / pixelSize`, relative-scaled textures), a new stylize quad pass applies outlines/highlights, then the existing dither pass upscales to the window. Pixel size changes patch the compositor texture scale factors and rebuild the viewport chain.

**Tech Stack:** C++17, Ogre 14 (GL3Plus, no RTSS, hand-written GLSL 330), SDL2, Dear ImGui via Ogre overlay.

**Verification model:** No unit-test infra exists (rendering engine). Each task verifies via `make build` (must compile clean) and the final task does a runtime + screenshot check with `ogre.log` inspection. Reference shader being ported: `~/gamedev/shaders/Godot-3d-pixelart-demo-master/pixelart_stylizer.gdshader`.

**Key conventions the implementer must know:**

- Build: `make build` from repo root (CMake wrapper). Run demo: `make run-demo`.
- All Ogre types stay inside `engine/src`; the public API (`engine/include/eng/Renderer.h`) is Ogre-free.
- `psx.frag` outputs **sRGB-encoded** colour; the stylizer mixes in that space (fine for a stylized look — do not add linearization).
- `EnvState` (Renderer.h) caches last-set values so DebugUi can read them back.
- Depth convention for the MRT: `alpha = view-space depth / farClip` (0..1). The compositor clear writes alpha = 1.0 (Ogre `ColourValue` default alpha), so background pixels decode to `farClip` — exactly what the outline math wants. Do not change this.
- Commit spec: `docs/superpowers` is gitignored; use `git add -f` for docs there.

---

### Task 1: MRT output from the PSX fragment shaders

**Files:**
- Modify: `engine/assets/shaders/psx.frag`
- Modify: `engine/assets/programs/psx.program`

- [ ] **Step 1: Add the second render-target output to `psx.frag`**

In `engine/assets/shaders/psx.frag`, change the output declaration block (currently just `out vec4 fragColour;` at line 24) to:

```glsl
layout(location = 0) out vec4 fragColour;
// MRT surface 1 (PSX/Stylized compositor): view-space normal encoded
// *0.5+0.5, alpha = linear view depth / far clip. When the scene renders
// straight to the window (no compositor) GL discards this output.
layout(location = 1) out vec4 fragNormalDepth;
uniform float farClip;
```

At the very end of `main()` (after the existing `fragColour = vec4(toSrgb(rgb), alpha);` line) append:

```glsl
#ifdef BLEND_ADD
    // Additive light volumes must not disturb edge data: adding zero is a no-op.
    fragNormalDepth = vec4(0.0);
#else
    fragNormalDepth = vec4(normalize(vNormalVS) * 0.5 + 0.5,
                           vViewDepth / farClip);
#endif
```

`vNormalVS` and `vViewDepth` are already inputs (lines 9–10); no vertex-shader change needed.

- [ ] **Step 2: Feed `farClip` to every fragment program**

In `engine/assets/programs/psx.program`, add this line to the `default_params` block of **all nine** PSX fragment programs (`PSX_FS_Lit`, `PSX_FS_Unlit`, `PSX_FS_LitMetal`, `PSX_FS_UnlitMetal`, `PSX_FS_LitTransparent`, `PSX_FS_UnlitTransparent`, `PSX_FS_LitAlphaScissor`, `PSX_FS_UnlitAlphaScissor`, `PSX_FS_LitNoTex`) — but **not** `PSX_FS_LightVolume` (its BLEND_ADD path writes `vec4(0)` and never reads `farClip`, so the compiler eliminates the uniform and Ogre would error on the named constant):

```
        param_named_auto farClip far_clip_distance
```

- [ ] **Step 3: Build**

Run: `make build`
Expected: compiles clean. (Shaders compile at runtime, verified in Task 7.)

- [ ] **Step 4: Commit**

```bash
git add engine/assets/shaders/psx.frag engine/assets/programs/psx.program
git commit -m "feat(shaders): MRT normal+depth output from PSX fragment shaders"
```

---

### Task 2: Stylize shader — port of pixelart_stylizer.gdshader

**Files:**
- Create: `engine/assets/shaders/pixel_stylize.frag`
- Modify: `engine/assets/programs/psx.program` (append program defs)
- Modify: `engine/assets/materials/psx.material` (append material)

- [ ] **Step 1: Write `engine/assets/shaders/pixel_stylize.frag`**

```glsl
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
    return texture(normalDepthTex, suv).a * farClip;
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
    float depthDiff = 0.0;
    float negDepthDiff = 0.5;
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
        negDepthDiff += (d - du) + (d - dd) + (d - dr) + (d - dl);
        negDepthDiff = clamp(negDepthDiff, 0.0, 1.0);
        negDepthDiff = clamp(smoothstep(0.5, 0.5, negDepthDiff) * 10.0, 0.0, 1.0);
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
```

- [ ] **Step 2: Declare the programs**

Append to `engine/assets/programs/psx.program` (after the existing `Dither_FS` block):

```
vertex_program Stylize_VS glsl
{
    source quad.vert
    default_params
    {
        param_named_auto worldViewProj worldviewproj_matrix
    }
}

fragment_program Stylize_FS glsl
{
    source pixel_stylize.frag
    default_params
    {
        param_named sceneTex int 0
        param_named normalDepthTex int 1
        param_named_auto farClip far_clip_distance
        param_named stylizeEnabled float 1.0
        param_named shadowsEnabled float 1.0
        param_named highlightsEnabled float 1.0
        param_named shadowStrength float 0.4
        param_named highlightStrength float 0.1
        param_named shadowColor float3 0.0 0.0 0.0
        param_named highlightColor float3 1.0 1.0 1.0
    }
}
```

- [ ] **Step 3: Declare the material**

Append to `engine/assets/materials/psx.material`:

```
// pixelart_stylizer.gdshader port: depth shadow outlines + normal highlight
// edges, applied at the low-res pixelated resolution. Units 0/1 injected by
// the PSX/Stylized compositor (MRT surfaces 0 and 1).
material PSX/PixelStylize
{
    technique { pass {
        depth_check off
        depth_write off
        cull_hardware none
        vertex_program_ref Stylize_VS { }
        fragment_program_ref Stylize_FS { }
        texture_unit    // scene colour (MRT 0)
        {
            filtering none
            tex_address_mode clamp
        }
        texture_unit    // normal + depth (MRT 1)
        {
            filtering none
            tex_address_mode clamp
        }
    } }
}
```

- [ ] **Step 4: Build + commit**

Run: `make build` — expected clean.

```bash
git add engine/assets/shaders/pixel_stylize.frag engine/assets/programs/psx.program engine/assets/materials/psx.material
git commit -m "feat(shaders): pixel stylize pass (outline + highlight) port"
```

---

### Task 3: Compositor — PSX/Stylized replaces PSX/Dither

**Files:**
- Modify: `engine/assets/compositors/psx.compositor` (full rewrite)

- [ ] **Step 1: Rewrite `engine/assets/compositors/psx.compositor`**

Replace the whole file with:

```
// PSX post stack: scene -> low-res MRT (colour + normal/depth) -> pixel
// stylizer (outlines/highlights, pixelart_stylizer.gdshader port) -> dither/
// colour-depth pass (pp_band-dither.gdshader port) -> window, nearest-upscaled.
//
// Texture sizes are target_width_scaled/target_height_scaled with factor
// 1/pixelSize; RenderCore::setPixelSize patches the factors on the definition
// and rebuilds the chain. Default factor 0.333333 = pixel size 3 (matches the
// original Godot pp_stack stretch_shrink 3).
//
// mrt is FLOAT16 on both surfaces: surface 1 stores view depth / farClip in
// alpha and 8-bit alpha would quantize edge detection to mush.
compositor PSX/Stylized
{
    technique
    {
        texture mrt target_width_scaled 0.333333 target_height_scaled 0.333333 PF_FLOAT16_RGBA PF_FLOAT16_RGBA
        texture rt_post target_width_scaled 0.333333 target_height_scaled 0.333333 PF_BYTE_RGBA

        target mrt
        {
            input previous      // scene renders into the low-res MRT
        }

        target rt_post
        {
            input none
            pass render_quad
            {
                material PSX/PixelStylize
                input 0 mrt 0
                input 1 mrt 1
            }
        }

        target_output
        {
            input none
            pass render_quad
            {
                material PSX/DitherPost
                input 0 rt_post
            }
        }
    }
}
```

- [ ] **Step 2: Build + commit**

Run: `make build` — expected clean (script parses at runtime; Task 7 verifies).

```bash
git add engine/assets/compositors/psx.compositor
git commit -m "feat(post): PSX/Stylized compositor chain (MRT -> stylize -> dither)"
```

---

### Task 4: RenderCore — chain management + pixel size

**Files:**
- Modify: `engine/src/RenderCore.h`
- Modify: `engine/src/RenderCore.cpp`

- [ ] **Step 1: Extend the header**

In `engine/src/RenderCore.h`, replace

```cpp
    void setDitherEnabled(bool enabled);
```

with

```cpp
    // Toggles the whole PSX/Stylized post chain (scene downscale + stylize +
    // dither). Name kept from the dither-only era; the debug UI checkbox and
    // Renderer::setDitherEnabled route here.
    void setDitherEnabled(bool enabled);
    // Rebuilds the chain with RT sizes = window / pixelSize. Clamped 1..16.
    void setPixelSize(int pixelSize);
```

and replace

```cpp
    bool mDitherAdded = false;
```

with

```cpp
    bool mChainAdded = false;
    bool mChainEnabled = false;
    int mPixelSize = 3;
```

- [ ] **Step 2: Implement in `engine/src/RenderCore.cpp`**

Add the include near the other Ogre includes:

```cpp
#include <OgreCompositor.h>
```

Replace the `setDitherEnabled` implementation with:

```cpp
void RenderCore::setDitherEnabled(bool enabled)
{
    auto& cm = Ogre::CompositorManager::getSingleton();
    if (enabled && !mChainAdded) {
        cm.addCompositor(mViewport, "PSX/Stylized");
        mChainAdded = true;
    }
    if (mChainAdded)
        cm.setCompositorEnabled(mViewport, "PSX/Stylized", enabled);
    mChainEnabled = enabled;
}

void RenderCore::setPixelSize(int pixelSize)
{
    mPixelSize = std::clamp(pixelSize, 1, 16);
    Ogre::CompositorPtr comp =
        Ogre::CompositorManager::getSingleton().getByName("PSX/Stylized");
    if (!comp)
        return;
    // Patch the definition; instances are rebuilt from it on re-add. Scaled
    // (widthFactor/heightFactor) textures also track window resizes for free.
    const float f = 1.0f / float(mPixelSize);
    Ogre::CompositionTechnique* tech = comp->getTechnique(0);
    for (const char* name : {"mrt", "rt_post"}) {
        auto* def = tech->getTextureDefinition(name);
        def->widthFactor = f;
        def->heightFactor = f;
    }
    if (mChainAdded) {
        Ogre::CompositorManager::getSingleton().removeCompositor(mViewport,
                                                                 "PSX/Stylized");
        mChainAdded = false;
        setDitherEnabled(mChainEnabled); // re-add + restore enable state
    }
}
```

Add `#include <algorithm>` with the other standard includes for `std::clamp`.

- [ ] **Step 3: Build + commit**

Run: `make build` — expected clean.

```bash
git add engine/src/RenderCore.h engine/src/RenderCore.cpp
git commit -m "feat(render): PSX/Stylized chain management + runtime pixel size"
```

---

### Task 5: Renderer public API + EnvState

**Files:**
- Modify: `engine/include/eng/Renderer.h`
- Modify: `engine/src/Renderer.cpp`

- [ ] **Step 1: Extend `EnvState` and the API in `engine/include/eng/Renderer.h`**

In `struct EnvState`, after `bool dither = false;` add:

```cpp
    int pixelSize = 3;       // PSX/Stylized RT = window / pixelSize
    bool stylize = true;     // outline/highlight pass active
```

In the `// --- post + verification` section of `class Renderer`, after `void setDitherEnabled(bool enabled);` add:

```cpp
    void setPixelSize(int pixelSize);      // 1..16, rebuilds the post chain
    void setStylizeEnabled(bool enabled);  // off = pass-through (PSX look only)
```

- [ ] **Step 2: Implement in `engine/src/Renderer.cpp`**

After the `Renderer::setDitherEnabled` implementation add:

```cpp
void Renderer::setPixelSize(int pixelSize)
{
    mImpl->env.pixelSize = std::clamp(pixelSize, 1, 16);
    mImpl->core.setPixelSize(mImpl->env.pixelSize);
}

void Renderer::setStylizeEnabled(bool enabled)
{
    mImpl->env.stylize = enabled;
    setMaterialParam("PSX/PixelStylize", "stylizeEnabled", enabled ? 1.0f : 0.0f);
}
```

Add `#include <algorithm>` to the include block (for `std::clamp`).

- [ ] **Step 3: Build + commit**

Run: `make build` — expected clean.

```bash
git add engine/include/eng/Renderer.h engine/src/Renderer.cpp
git commit -m "feat(render): pixel size + stylize toggles on Renderer facade"
```

---

### Task 6: Debug UI — "Pixel Art" section

**Files:**
- Modify: `engine/src/DebugUiImpl.h`
- Modify: `engine/src/DebugUi.cpp`

- [ ] **Step 1: UI-side caches in `engine/src/DebugUiImpl.h`**

After the existing tunables block (`float precisionMultiplier ...; float colDepth ...; bool ditherBanding ...;`) add:

```cpp
    // Pixel-art stylizer tunables (defaults match Stylize_FS in psx.program;
    // colours are raw sRGB, mixed post-encode like the Godot reference).
    bool stylizeShadows = true;
    bool stylizeHighlights = true;
    float shadowStrength = 0.4f;
    float highlightStrength = 0.1f;
    glm::vec3 shadowColor{0.0f};
    glm::vec3 highlightColor{1.0f};
```

Add `#include <glm/glm.hpp>` to the header's includes.

Declare the new widget body next to the others:

```cpp
    void drawPixelArt();
```

- [ ] **Step 2: Section + widgets in `engine/src/DebugUi.cpp`**

In `buildFrame`, after the `"PSX Shaders"` header block add:

```cpp
    if (ImGui::CollapsingHeader("Pixel Art"))
        drawPixelArt();
```

Add the widget body next to `drawShaders()`:

```cpp
void DebugUi::Impl::drawPixelArt()
{
    const EnvState& env = renderer->envState();
    int pixelSize = env.pixelSize;
    if (ImGui::SliderInt("pixel size", &pixelSize, 1, 16))
        renderer->setPixelSize(pixelSize);
    bool stylize = env.stylize;
    if (ImGui::Checkbox("stylize (outline+highlight)", &stylize))
        renderer->setStylizeEnabled(stylize);
    if (ImGui::Checkbox("shadows", &stylizeShadows))
        renderer->setMaterialParam("PSX/PixelStylize", "shadowsEnabled",
                                   stylizeShadows ? 1.0f : 0.0f);
    if (ImGui::Checkbox("highlights", &stylizeHighlights))
        renderer->setMaterialParam("PSX/PixelStylize", "highlightsEnabled",
                                   stylizeHighlights ? 1.0f : 0.0f);
    if (ImGui::SliderFloat("shadow strength", &shadowStrength, 0.0f, 1.0f))
        renderer->setMaterialParam("PSX/PixelStylize", "shadowStrength",
                                   shadowStrength);
    if (ImGui::SliderFloat("highlight strength", &highlightStrength, 0.0f, 1.0f))
        renderer->setMaterialParam("PSX/PixelStylize", "highlightStrength",
                                   highlightStrength);
    if (ImGui::ColorEdit3("shadow colour", &shadowColor.x))
        renderer->setMaterialParam("PSX/PixelStylize", "shadowColor", shadowColor);
    if (ImGui::ColorEdit3("highlight colour", &highlightColor.x))
        renderer->setMaterialParam("PSX/PixelStylize", "highlightColor",
                                   highlightColor);
}
```

- [ ] **Step 3: Extend the TOML dump**

In `copyToml()`, extend the format string and argument list. After the `"dither_banding = %s\n"` line add:

```cpp
                  "pixel_size = %d\n"
                  "stylize = %s\n"
                  "stylize_shadows = %s\n"
                  "stylize_highlights = %s\n"
                  "shadow_strength = %.2f\n"
                  "highlight_strength = %.2f\n"
                  "shadow_color_srgb = [%.4f, %.4f, %.4f]\n"
                  "highlight_color_srgb = [%.4f, %.4f, %.4f]\n"
```

and after the `ditherBanding ? "true" : "false",` argument add:

```cpp
                  env.pixelSize, env.stylize ? "true" : "false",
                  stylizeShadows ? "true" : "false",
                  stylizeHighlights ? "true" : "false", shadowStrength,
                  highlightStrength, shadowColor.x, shadowColor.y, shadowColor.z,
                  highlightColor.x, highlightColor.y, highlightColor.z,
```

Grow the buffer: change `char buf[768];` to `char buf[1024];`.

- [ ] **Step 4: Build + commit**

Run: `make build` — expected clean.

```bash
git add engine/src/DebugUiImpl.h engine/src/DebugUi.cpp
git commit -m "feat(debug-ui): pixel art section (pixel size, outline, highlight)"
```

---

### Task 7: Runtime verification

**Files:** none (verification only)

- [ ] **Step 1: Run the demo and capture the log**

Run: `timeout 20 make run-demo; tail -50 ogre.log`

Expected: window opens, scene renders. `ogre.log` must contain **no** errors about: GLSL compile/link failures (`psx.frag`, `pixel_stylize.frag`), missing named constants (`farClip`, `stylizeEnabled`), compositor `PSX/Stylized` texture creation, or `PSX/Dither` (must not be referenced anywhere anymore — `grep -rn "PSX/Dither\"" engine/ game/ samples/` returns only the `PSX/DitherPost` material, not the old compositor name).

- [ ] **Step 2: Visual check via screenshots**

With the demo running (F1 opens the debug panel):
1. Default state — outlines visible on silhouettes (walls/props against farther geometry and background), highlights on convex edges, fog + dither look unchanged.
2. `stylize` off — must be pixel-identical to the pre-feature look (only the pass-through copy differs, which is exact).
3. `pixel size` sweep 1→16 and back — no crash, no `ogre.log` texture errors, chunkiness follows the slider.
4. Resize the window — RTs follow (scaled texture factors), no stretching artifacts.
5. Toggle `shadows` / `highlights` independently; strengths and colours respond live.

If the app can't be run interactively in this environment, use `Renderer::writeScreenshot` from the sample or ask the human to verify — do not claim visual success without evidence.

- [ ] **Step 3: Final commit (if any fixups were needed)**

```bash
git add -A engine/ && git commit -m "fix(post): runtime fixups for stylized post chain"
```

(Skip if the working tree is clean.)
