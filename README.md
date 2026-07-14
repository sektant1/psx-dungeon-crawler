# ogre-psx-demo

1:1-as-practical port of [godot-psx-style](https://github.com/expressobits/godot-psx-style)
(MenacingMecha's Godot PSX Style Demo — `world/world.tscn` + `shaders/`) to
**OGRE 14.5 + SDL2**. SDL2 owns the window and the main loop; Ogre owns the
GPU. Manual `Ogre::Root` init, no `ApplicationContext`, **no RTSS** — every
material is hand-written GLSL (`#version 330`) ported from the Godot shaders.

## Build & run

```sh
make run        # wraps: cmake -B build && cmake --build build && ./build/psx_demo
```

Requires system OGRE 14.x (`pacman -S ogre`: plugins `RenderSystem_GL3Plus`,
`Plugin_ParticleFX`, `Codec_STBI`), SDL2, CMake >= 3.16. On Wayland the
Makefile forces `SDL_VIDEODRIVER=x11` (XWayland); native Wayland would need
Ogre's `externalWlDisplay`/`externalWlSurface` params instead. The SDL window
is created **without** `SDL_WINDOW_OPENGL`; Ogre GL3Plus builds its own GL
context against the raw X11 handle passed via `externalWindowHandle`. Asset
paths are compiled in (`ASSET_DIR`), so the binary runs from anywhere.

Controls (port of `world/scene_controls.gd`): **Space/Enter** pause animation,
**R** restart, **Esc** quit. `PSX_SCREENSHOT=/path.png ./psx_demo` renders 90
frames, saves a screenshot, and exits (verification hook).

## Layout

```
src/            Renderer.{h,cpp} (Ogre lifetime), ObjLoader, ProceduralMeshes,
                Animation.h (OrbitCamera / SinPan / ShadowScale), main.cpp (scene)
assets/shaders/     psx.vert, psx.frag, quad.vert, dither.frag (GLSL 330)
assets/programs/    psx.program (all GPU program variant declarations)
assets/materials/   psx.material (9 wrapper materials + scene materials)
assets/compositors/ psx.compositor (320x240 scene target + dither render_quad)
assets/particles/   sparkle.particle
assets/meshes/      box.obj, bevel-box.obj (copied), crystal_*.obj,
                    light_shaft.obj (converted, see tools/)
assets/textures/    copied from the Godot repo (incl. the 4x4 psxdither.png)
tools/gltf_to_obj.py  one-shot glTF/GLB -> OBJ-with-vertex-colours converter
```

## Shader mapping (Godot -> Ogre)

`shaders/psx_base.gdshaderinc` becomes one `psx.vert` + `psx.frag` pair,
compiled into variants with `preprocessor_defines` — the same define scheme the
Godot wrapper shaders use:

| Godot shader | Ogre programs (defines) | Ogre wrapper material |
|---|---|---|
| psx_lit.gdshader | PSX_VS_Lit + PSX_FS_Lit (`LIT`) | `PSX/Lit` |
| psx_unlit.gdshader | PSX_VS_Unlit + PSX_FS_Unlit | `PSX/Unlit` |
| psx_lit_metal.gdshader | PSX_FS_LitMetal (`LIT,METAL`) | `PSX/LitMetal` |
| psx_unlit_metal.gdshader | PSX_FS_UnlitMetal (`METAL`) | `PSX/UnlitMetal` |
| psx_lit_transparent.gdshader | PSX_FS_LitTransparent (`LIT,ALPHA_BLEND`) | `PSX/LitTransparent` |
| psx_unlit_transparent.gdshader | PSX_FS_UnlitTransparent (`ALPHA_BLEND`) | `PSX/UnlitTransparent` |
| psx_lit_alpha-scissor.gdshader | PSX_FS_LitAlphaScissor (`LIT,ALPHA_SCISSOR`) | `PSX/LitAlphaScissor` |
| psx_unlit_alpha-scissor.gdshader | PSX_FS_UnlitAlphaScissor (`ALPHA_SCISSOR`) | `PSX/UnlitAlphaScissor` |
| psx_light-volume.gdshader | PSX_FS_LightVolume (`LIT,NO_TEXTURE,LIGHT_VOLUME,BLEND_ADD`) | `PSX/LightVolume` |
| (psx_lit with no texture bound) | PSX_FS_LitNoTex (`LIT,NO_TEXTURE`) | crystal materials |
| pp_band-dither.gdshader | Dither_VS + Dither_FS | `PSX/DitherPost` + `PSX/Dither` compositor |

Feature-by-feature:

| psx_base feature | Port |
|---|---|
| `get_snapped_pos` (160x120 NDC grid * `precision_multiplier`) | identical math on `gl_Position` in `psx.vert`; `precisionMultiplier` is a `param_named` (default 1.0) on every vertex program |
| `POSITION /= abs(POSITION.w)` affine mapping | `noperspective` on all varyings (deviation 1 below) |
| `vertex_lighting, diffuse_lambert, specular_disabled` | lambert computed **per-vertex** in `psx.vert` from auto params `light_position_view_space_array` / `light_diffuse_colour_array` / `light_attenuation_array` / `light_count` / `ambient_light_colour`; `w == 0` marks the directional light; omni falloff uses Godot's curve `pow(1 - d/range, omni_attenuation)` (0.0915055). Godot pre-multiplies light energy by pi, cancelling its 1/pi lambert BRDF, so plain NdotL matches 1:1 |
| UV `scale/offset + pan * TIME` | `uvScale`/`uvOffset`/`uvPanVelocity` params + `param_named_auto time time` |
| `modulate_color`, `albedoTex` (`filter_nearest, repeat_enable`) | `modulateColor` param; `texture_unit` with `filtering none`, `tex_address_mode wrap` |
| METAL fake matcap (`uv = (N.x/2+0.5, -N.y/2+0.5)`, view-space normal) | verbatim, normal via `inverse_transpose_worldview_matrix` |
| ALPHA_SCISSOR (`ALPHA_SCISSOR_THRESHOLD`) | `discard` below `alphaScissor` in `psx.frag` |
| LIGHT_VOLUME (`ALPHA = 1.0 - UV.y`) | identical |
| render_mode cull/depth/blend | pass state per material (mapping table in the `psx.material` header comment); Godot `blend_add` = `scene_blend src_alpha one` |
| Godot linear-space shading + sRGB display output | albedo texture / `source_color` uniforms linearised in `psx.frag`; light, ambient, and fog colours linearised CPU-side (energy multiplies the *linear* colour, as Godot does); final `pow(1/2.2)` encode |
| world_env.tres (ambient colour * 0.15, exp fog density 0.05, background colour) | `setAmbientLight` + `setFog(FOG_EXP, ...)` + viewport background colour; fog applied per-pixel in `psx.frag` via `fog_colour`/`fog_params` auto params using Godot's `1 - exp(-dist * density)` on `length(view-space vertex)` |
| pp_band-dither + 320x240 SubViewport (`pp_stack.tscn` stretch_shrink 3) | `PSX/Dither` compositor: scene -> 320x240 RT -> `render_quad` with `dither.frag` (identical `round(c * 15 + dith - 0.5)/15` math, 4x4 `psxdither.png`, nearest+repeat) -> stretched to the window with nearest filtering |

## Scene mapping (world.tscn)

| Godot node | Ogre |
|---|---|
| OrbitPoint + orbit_camera.gd (yaw += 1.0 rad/s) | scene node + `OrbitCamera` (`src/Animation.h`), base yaw from the tscn basis |
| Camera3D (fov 68.1243, slight down-pitch, origin 0/2.147/4.48151) | camera on child node, `setFOVy(Degree(68.1243))` (vertical, = Godot KEEP_HEIGHT), near 0.05 / far 4000 (Godot defaults) |
| DirectionalLight (child of OrbitPoint -> rotates with camera, energy 1.5) | directional light on child node (direction = node -Z in both engines), diffuse 1.5 white, specular black |
| 2x OmniLight3D (colour 0.909804/0.803922/0.666667, energy 4.75, range 3, attenuation 0.0915055) | point lights at (+-4, 0.784, 0); Ogre supplies range, Godot's falloff curve lives in the shader |
| Background: BoxMesh 40^3, subdivide 25, flip_faces, floor.tres, y = 20 | `ProceduralMeshes::createInteriorBox` (inward normals + reversed winding, Godot 3x2 per-face UV atlas), material `PSX/Floor` (uv_scale 24x24, modulate 0.66/0.7/0.8) |
| LightShaft (light-shaft_mesh.res + light-shaft_mat.tres) | `light_shaft.obj` (converted from `light-shaft.glb`) + `PSX/LightShaft` |
| BoxMetal (bevel-box.obj, scale 0.613118, -1/2.236/0) + spatial_sin_pan.gd | base node (default transform) + animated child node (`SinPan`: y = sin(t), Euler YXZ (t,t,t) — parent*child == Godot `default * offset`), material `PSX/BoxMetal` (modulate 0.95/0.6/0) |
| BoxLit (box.obj, 1/2.236/0, `_reverse_direction = true`) | same with reversed pan, material `PSX/BoxLit` (texture swapped from icon.png to `Prototype_orange_32x32px.png` from ~/gamedev prototyping textures, per request) |
| GPUParticles3D sparkles (amount 8, lifetime 1, initial velocity 2, spread 180, no gravity, 0.3 QuadMesh, sparkle_mat) | `PSX/Sparkles` ParticleFX system (point emitter, rate 8, ttl 1, vel 2, angle 180) attached to the animated BoxMetal node, material `PSX/Sparkle` |
| shadow.tscn instances (PlaneMesh + shadow_mat modulate a=0.78; shadow.gd scale 0.775 -+ 0.125 sin(t)) | `ProceduralMeshes::createPlane` + `PSX/Shadow`, `ShadowScale` animation, mesh child at y = 0.05 (shadow.gd overwrites the tscn root scale of 3 in `_ready`, so 0.775 is the effective base) |
| 5x crystal.gltf instances (crystal_mesh.tscn) | per instance: root node (tscn transform) -> "Ground" node (gltf translation + uniform scale 0.310583) holding crystal_ground + 4 spire entities; `PSX/CrystalGround` / `PSX/CrystalSpire` (`NO_TEXTURE`: albedo = vertex COLOR_0 * modulate, matching Godot's white default sampler) |
| scene_controls.gd | Space/Enter pause, R restart in the SDL loop |

`.tscn Transform3D(a..i, o)` stores the basis **rows**, which is exactly
`Ogre::Matrix3`'s row-major constructor order, so all values are copied
verbatim.

## Deliberate deviations (and why)

1. **Affine mapping via `noperspective`** instead of `POSITION /= abs(w)`:
   identical screen-linear interpolation, but clip-space w stays intact so
   near-plane clipping and the depth buffer keep working (Godot's w-trick is a
   known source of near-camera glitches). The vertex snap still runs BEFORE
   interpolation, as in the source.
2. **No Assimp codec** in the system Ogre install, so `crystal.gltf` and
   `light-shaft.glb` were converted offline by `tools/gltf_to_obj.py` into OBJ
   with the vertex-colour extension (`v x y z r g b a`), loaded by
   `src/ObjLoader.cpp` (also used for the two Blender OBJs; fan-triangulates
   polygons and flips v like Godot's OBJ importer). The LightShaft uses the
   `.glb` source rather than Godot's imported `.res` binary.
3. **Spire transforms are baked into the meshes**: the `crystal_mesh.tscn`
   overrides for Spire_2/3 (and the gltf TRS of Spire_1/4) contain
   rotation * non-uniform-scale bases that Ogre TRS scene nodes cannot
   represent. Positions get the matrix; normals its inverse-transpose.
4. **BoxMesh UV atlas**: faces map to a 3x2 UV grid like Godot's BoxMesh, but
   the face-to-cell assignment may differ — invisible with the tiling floor
   texture (24 tiles per UV wrap; 8x12 per cell, both integral).
5. **Post stack**: only the dither-banding stage of `pp_stack.tscn` is ported
   (320x240 + dither + nearest upscale). The outer blur and LCD-overlay
   SubViewport stages were out of scope.
6. **Sparkles**: Ogre's BillboardSet does the billboarding, so the shader's
   `billboard` matrix branch is unused; emission randomness follows Ogre's
   point emitter, and GPUParticles3D's `fixed_fps 15` stepping is not
   emulated. Restart (R) does not clear live particles.
7. **`depth_prepass_alpha`** approximated as in-shader `discard` +
   `depth_write on` (no separate depth prepass).
8. **Linear-space shading** uses the gamma-2.2 approximation instead of the
   exact piecewise sRGB curve (differences are far below the 15-level
   colour-depth quantisation of the dither pass).
9. **Additive fog**: Godot's fog handling for `blend_add` materials is
   approximated by fading to black with the fog amount (never brightening).
10. `reflected_light_source = 1` in world_env.tres has no effect here
    (specular is disabled in every material anyway).
