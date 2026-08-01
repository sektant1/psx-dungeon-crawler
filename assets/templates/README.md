# Effect templates

Copy these files into the relevant game asset folders; do not edit the templates
in place. They are inert until registered in a game VFX program file and
referenced by a game material/effect record.

| Template | Copy to | Purpose |
| --- | --- | --- |
| `membrane.vert.glsl.in`, `membrane.frag.glsl.in` | `assets/game/shaders/` | Portal membrane, flesh wall, shield, magical film |
| `membrane.program.in` | append to `assets/game/programs/vfx.program` | OGRE GLSL program declarations and defaults |
| `membrane.material.in` | `assets/game/materials/` | Ogre material binding for the membrane programs |
| `effect.template.toml` | a `[[effect]]` entry in `assets/game/particles.toml` | Burst, trail, aura, smoke, or wisp effect |

Replace every `@UPPER_ID@`, `@MATERIAL_ID@`, and `@ALBEDO_TEXTURE@` token.

The render/compositor shaders under `assets/engine/` are engine-owned and are
not effect templates. See the [pipeline guide](../../../docs/design/2026-07-27-prototype-content-reset-and-pipeline.md#shader-portal-liquid-and-particle-recipes).
