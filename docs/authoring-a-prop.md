# Authoring a prop

Blender to a spinning object in a playable scene, with no C++. The worked
example is the raccoon head in `assets/scenes/spin_portal.scn`, and every
command below is the one that actually produced it.

For the shot half — cameras, spins, recording — see
[authoring-shots.md](authoring-shots.md). For the components, [ecs.md](ecs.md).

## The whole thing

```sh
make asset BLEND=assets/source/models/Raccoon_Head.blend LIST=1
make asset BLEND=assets/source/models/Raccoon_Head.blend \
           OBJECT=Mapache NAME=prop_raccoon_head
# paste the printed size= line into assets/config/kit.toml, add a material
make editor SCENE=assets/scenes/spin_portal.scn     # place it, parent it, save
make scene SCENE=assets/scenes/spin_portal.scn      # cook and play
```

Five commands and two text edits. What follows is why each one is there.

## Or: import it in the editor

For a model that already exists as a file -- something bought, downloaded, or
exported from someone else's tool -- **Scene > Import model...** does the whole
of steps 1 to 3 in one dialog.

It accepts **every format this build of Assimp reads** (`.glb`, `.gltf`, `.fbx`,
`.obj`, `.dae`, `.blend`, ...), asked of the engine rather than hardcoded, so
the list cannot drift from what the loader actually supports. There is no path
to type: the dialog scans `assets/source` recursively and lists what it finds by
path, with a filter, and a folder browser for reaching a download outside the
tree.

What the import writes, into the game pack:

```
meshes/props/import_<slug>[_pN].obj    one per submesh
textures/import_<slug>_<name>.png      textures copied from beside the model
materials/import_<slug>.material       one PSX material per texture
config/kit.toml                        a marked block of [[piece]] entries
```

The pieces are then in the Catalog, and the model itself is dropped into the
scene at the camera -- multi-part models under a group so they move as one.

Three things worth knowing:

- **Reimporting replaces.** The kit block is delimited with
  `# BEGIN editor import: <slug>`, so fixing a model in Blender and importing it
  again replaces its pieces rather than leaving a second dead copy.
- **Textures must be beside the model.** An `.mtl` naming `386.png` is resolved
  against the *model's* directory. Textures **embedded** in the file are not
  extracted -- the engine's loader treats them as metadata only -- and the
  import says so in the Status panel rather than silently producing an
  untextured prop. Assign a material in the Material panel for those.
- **Geometry only.** Skins and animation are not imported.

The conversion is `ed::importModelToKit`
(`game/editor/ModelImportPipeline.h`), running in process on the engine's own
Assimp importer. It replaced a Python script invoked through `std::system` that
carried a second glTF parser, read one format, and reported failure as "see
terminal output".

The Blender path below is still the one to use when you own the source file:
`make asset` gives you control over the object, the scale and the name, and
leaves nothing to guess at.

## 1. Blender → the engine

```sh
make asset BLEND=assets/source/models/Raccoon_Head.blend LIST=1
```
```
Mapache      2.330 x 2.070 x 1.753  (131 verts)
Fondo       18.128 x 18.128 x 0.000 (4 verts)
Fondo 2     18.128 x 18.128 x 0.000 (4 verts)
```

Start with `LIST=1`, always. A real `.blend` is the model **and the studio** —
backdrops, a light rig, a camera — and a downloaded one rarely names its subject
in English. Exporting the file wholesale gives you a mesh with an 18-metre wall
welded to it, which reads as a broken importer.

```sh
make asset BLEND=... OBJECT=Mapache NAME=prop_raccoon_head
```
```
3 materials -> vertex colours
size = [2.3300, 1.7527, 2.0703]   # metres, for kit.toml
131 vertices
wrote assets/meshes/props/prop_raccoon_head.obj
```

`tools/blend_to_obj.py` settles seven things that are each silently wrong by
default:

| | what goes wrong |
|---|---|
| selection | the studio comes with the model |
| axis | Blender is Z-up, the engine Y-up — the model lands on its face |
| modifiers | an unapplied Subsurf exports the cage: half a raccoon |
| triangles | the loader wants tris; an n-gon arrives as a hole |
| units | a model built at 100× "because it looked right" is a wall |
| UVs | v flipped to match what `gltf_to_obj` emits, so materials agree |
| **colour** | see below — the one Blender has no setting for |

It prints the `size = [...]` line in kit.toml's own syntax, in metres, in the
axis order the engine sees. Measuring a converted mesh by hand afterwards is how
a piece ends up with a footprint that disagrees with its art.

### Colour lives in the materials, not in a texture

This is the one that costs an afternoon. A low-poly model is usually flat
material colours per face and **no texture at all** — the raccoon's `.blend`
references no image. Export it plainly and you get UVs pointing into an atlas
that does not exist, and the engine draws whatever the material happens to name.
The first attempt here sampled an unrelated 8×8 palette from the same asset
pack and produced a raccoon with *green ears*.

So `make asset` bakes each face's material base colour onto its vertices, in the
extended OBJ form the loader already reads (`v x y z r g b a`, sRGB→linear), and
the PSX shader already multiplies. What you saw in Blender is what the engine
draws.

Pass `NO_BAKE=1` for a model that really is textured, where per-vertex colour
would multiply the texture darker.

## 2. A material

For a baked-colour model that is one block, and the texture is load-bearing:

```
material Game/PropVertexColour
{
    technique { pass {
        vertex_program_ref PSX_VS_Lit { }
        fragment_program_ref PSX_FS_Lit { }
        texture_unit { texture white.png  filtering none }
    } }
}
```

`white.png` is a 1×1 white sampler. The shader multiplies texture by vertex
colour, so white is what lets the baked colour through unchanged — naming any
other texture tints the whole model by it. `filtering none` everywhere: this is
a point-sampled engine.

For a *textured* model, copy any `Game/Prop*` block in
`assets/materials/game.material` and swap the texture. One rule bites:

> **v-flip is per-converter, not per-material.** `gltf_to_obj` and `fbx_to_obj`
> flip v inside the file, so materials for their meshes leave `uvScale` alone.
> `make asset` flips too, so the same holds. A mesh exported from Blender *by
> hand* does not, and then the material needs
> `param_named uvScale float2 1.0 -1.0` + `uvOffset float2 0.0 1.0`.

`python3 tools/assetlint.py` catches a material that names a texture nobody
shipped, and a name that collides with another — Ogre aborts on a duplicate
material name during script parsing, so this is worth running before the engine.

## 3. The catalogue

One `[[piece]]` in `assets/config/kit.toml` and the editor can place it:

```toml
[[piece]]
id = "prop_raccoon_head"
role = "prop_decor"
mesh = "meshes/props/prop_raccoon_head.obj"
material = "Game/PropVertexColour"
socket = "prop"
import_scale = 1.0
size = [2.3300, 1.7527, 2.0703]     # straight off `make asset`
```

`import_scale = 1.0` because it was authored in metres; the architectural kit is
on a 20-unit grid and omits it. `socket = "prop"` means free-standing inside a
cell — it does not constrain the grid the way a wall does.

## 4. Into a scene

`make editor SCENE=assets/scenes/spin_portal.scn`, then:

1. **Catalog** panel → the new piece is there, filtered by name.
2. Click it to arm the Place tool, click in the viewport to drop it.
3. **Outliner** → drag its row onto `prop_pivot`. It is now part of that object:
   it turns with the pivot, and clicking it in the viewport selects the whole
   object (Alt-click, or a second click from inside, reaches the head itself).
4. Ctrl+S.

The transform in the row is now **local to the pivot**, and the editor
re-expressed it on the drop so the piece did not move on screen.

## 4b. Make one of them glow

Everything above gives every raccoon the same look, because a material is shared
by name. To change *one* entity, add the **Shader** component in the inspector —
or in the `.scn`:

```json
"shader": {
  "rim_colour": [0.35, 1.0, 0.85],
  "rim_strength": 2.2,
  "rim_power": 2.5
}
```

| field | drives |
|---|---|
| `tint` | multiplied into the albedo before lighting; white is unchanged |
| `opacity` | the same uniform's alpha (needs a blending material to show) |
| `rim_colour` / `rim_strength` | fresnel sheen at glancing angles: "magical", "wet" |
| `rim_power` | falloff — low is a broad wash, high a thin edge line |
| `alpha_scissor` | cutout threshold: below it the fragment is discarded |

Every field is optional and its default is neutral, so adding the component
changes nothing until a value moves. That matters: a component that alters the
image the moment it is added costs an undo on every "what does this do".

**The cost.** This is the one component priced in draw calls. The entity gets a
private copy of its material, which breaks it out of its batch. Right for a few
hero objects, wrong for a hundred and sixty walls — and the inspector says so
under the sliders. Removing the component releases the copy.

**Rim needs a lit material.** It is reflected light, so an unlit pass has none to
reflect and the uniform is not even declared there. Tint, opacity and cutout work
on every material in the PSX family.

## 5. Run it

```sh
make scene SCENE=assets/scenes/spin_portal.scn
```

Because the scene authors a camera, this plays as a *shot*: no HUD, no player,
the view is `camera_main` orbiting on its own pivot. That routing is the scene's
own doing — see [authoring-shots.md](authoring-shots.md#which-loop-runs) for the
two flags that override it.

## When it goes wrong

| symptom | cause |
|---|---|
| the model is on its side | exported by hand without the axis conversion — use `make asset` |
| it is enormous or invisible | authoring units; `SCALE=` on `make asset` |
| grey, or tinted by another prop's texture | material names a texture instead of `white.png`, and the colour is baked |
| texture upside-down | v-flip; see the rule above |
| half the model missing | a modifier that was never applied — `make asset` applies them |
| it does not appear in the Catalog | no `[[piece]]` in kit.toml, or the id is not `kit.<id>` in the scene |
| placed but not drawn | `assetlint` will say: the mesh or material path does not resolve |
| it does not turn | not parented to the spinning pivot — check the Outliner nesting |
| the rim does nothing | the material is unlit; rim is reflected light and needs a lit one |
| the whole level changed colour | a material edit, not a Shader component — that one is per entity |
| the game opens as an FPS instead of the shot | the scene has no Camera, or `--play` was passed |
