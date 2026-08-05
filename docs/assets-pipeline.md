# The asset pipeline

How a file an artist made becomes something the game loads.

This implements figure 1.33 of *Game Engine Architecture* (Gregory, 4th ed.) —
"Tools and the asset pipeline" — as three things this repo did not have: a
**resource database** describing every asset, an **exporter** per row of the
diagram, and an **Asset Conditioning Pipeline** that runs them incrementally and
publishes one pack the game reads.

For what art still has to be *made*, see
[art-asset-checklist.md](art-asset-checklist.md). For the content root and how a
logical path resolves, see `ARCHITECTURE.md`.

## The shape of it

```
  assets/source/**            assets/**                    build/cooked/**
  (DCC files: .blend,         (what an artist commits)     (what the game loads)
   vendor .fbx, .psd)
        │                            │                            ▲
        │  exported by hand          │                            │
        └───────────────────────────►│                            │
                                     │                            │
                              ┌──────┴───────┐                    │
                              │ raven_acp    │────────────────────┘
                              │  build       │
                              └──────┬───────┘
                                     │ reads
                              ┌──────┴───────┐
                              │ assets/**/*.meta  ← the resource database
                              └──────────────┘
```

Every file under `assets/` is classified into one **row** of the diagram, and
each row names the tool that produces its intermediate form:

| Diagram row | Exporter | Source | Intermediate |
|---|---|---|---|
| Mesh | Mesh Exporter (Assimp) | `.glb .gltf .fbx .obj .dae .stl .ply .3ds` | `.rmesh` |
| Skel. Hierarchy | `gltf2ozz` | a skinned `.glb` + `*.ozz.json` | `.skeleton.ozz` |
| Animation Clips | `gltf2ozz` | same | `clip_*.ozz` |
| Animation Tree | Animation Tree Exporter | `.animtree.toml` | `.rtree` |
| Material | *(no processor box — straight off the DCC arrow)* | `.mat` | `.mat` |
| DXT Texture | Compression | `.png .tga .jpg .bmp` | `.rtex`, or the source |
| Particle System | Particle Exporter | `particles/*.toml` | `.rpfx` |
| Sound Bank | Audio Management Tool | `config/audio.toml` | `.rbank` |
| WAV sound | *(Sound Forge / REAPER)* | `.wav .ogg .mp3 .flac` | itself |
| Game Obj. Templates | Object Model Editor | `config/kit.toml` | `.rtpl` |
| Game World | World Editor | `.scn` | `.map` |
| Resource DB | Resource Database Management Tool | — | `*.meta` |

`raven_acp formats` prints this table from the code, so it cannot drift from
what the pipeline actually does. The table itself lives in
`engine/include/eng/content/AssetType.h`.

## Using it

```sh
make acp                    # condition everything (warm run: ~0.2 s)
make acp TYPE=mesh          # one row
make acp FILTER=viewmodel   # one subtree
make acp FORCE=1            # ignore build keys
make acp-check              # fail if anything is stale — what CI runs
make assetdb                # what the resource database knows
make assetdb STAMP=1        # write a .meta for every asset that lacks one
make assetformats           # the table above
```

The game picks the pack up from `build/cooked` automatically. To run without it
— which is how you prove the conditioned and source paths agree — set
`RAVEN_COOKED_DIR=/dev/null`.

In the editor the same thing lives in **Asset Browser → Resource DB**: every
asset, its build state, its import settings, and a rebuild button per asset.

## The resource database

Every asset has metadata the file itself cannot express: a stable id that
survives a rename, and the import decisions someone made. That lives in a text
sidecar beside the asset — `lamp.obj` gets `lamp.obj.meta` — checked into git.

```toml
schema = 1
guid = "4e9fcfe877ef4fa0"
type = "mesh"
name = "First-person arms rig"
tags = ["viewmodel", "skinned"]

[import]
skip = true
skip_reason = "skinned rig: gltf2ozz owns it"
```

Sidecars rather than one central database file: a rename moves the metadata with
the file in one commit, and two people adding assets never touch the same line.

An asset with no sidecar still works. It gets a guid derived from its path and
the defaults for its type, so a tree can be conditioned before anyone has
stamped it. `make assetdb STAMP=1` writes them all out for review.

### Import settings by type

| Type | Key | Default | Meaning |
|---|---|---|---|
| mesh | `pivot` | `"source"` | `source`, `bounds_center`, `bottom_center`, `custom` |
| mesh | `metres_per_source_unit` | `1.0` | authored units → metres |
| mesh | `texcoord_v` | `"format_default"` | `preserve`, `flip` |
| mesh | `generate_collision` | `true` | keep the collision triangle soup |
| texture | `compression` | `"none"` | `none`, `auto`, `bc1`, `bc3`, `rgba8` |
| texture | `generate_mips` | `false` | the RHI uploads one level today |
| texture | `max_size` | `2048` | checked, never applied |
| texture | `srgb` | `true` | recorded for the renderer |
| world | `cook` | `true` | |
| *any* | `skip` | `false` | this row does not own this file |
| *any* | `skip_reason` | | why, so the next person does not undo it |

## How it decides what to rebuild

Each asset gets a **build key**:

```
hash(source bytes) + hash(import settings) + exporter version + dependency hashes
```

Timestamps are deliberately not in it. A git checkout rewrites every mtime in
the tree, and a pipeline that rebuilds everything after a branch switch is one
nobody runs — which means content ships unconditioned.

`build/cooked/pack.manifest` records each asset's key from the last run, so the
manifest *is* the build cache. A separate cache file would be a second source of
truth about what is current, and the two would disagree the first time someone
deleted one of them.

Dependencies are discovered by the exporters and written into the manifest: a
model's textures, a scene's kit and every mesh that kit names, a sound bank's
clips. Editing `meshes/kit/Wall_01.obj` therefore rebuilds the five `.map` files
that place it, without anyone having to know that.

Outputs whose source has been deleted are removed. Left behind, a deleted asset
would keep working for anyone running off the pack, and nobody would find out
until a clean build.

**Only a full run does that.** `--filter` and `--type` make a *partial* run: it
starts from the previous manifest, replaces only what it rebuilt, and removes
nothing. A partial run has no idea whether an output it never considered still
has a source, and answering "no" deletes the pack.

**A checked-in build artifact is not published.** `assets/scenes/start_hall.map`
is committed beside the `.scn` the World row cooks it from, so two records claim
one output. The source wins, and the committed copy is reported:

```
skipped scenes/start_hall.map   not published: the pipeline builds this from
                                'scenes/start_hall.scn'. The checked-in copy is
                                a build artifact -- delete it.
```

Those five `.map` files are still load-bearing for a run *without* a pack —
`assets::resolve()` finds them and the pack is optional — so they stay for now.
Once the pipeline is mandatory they should go. Two *sources* claiming one output
has no safe answer and fails the build.

## What the runtime actually reads

The pack does **not** change how `assets::resolve()` answers. Asking for
`meshes/props/lamp.obj` still gets you the `.obj`, because that is what the
string says and half the engine does arithmetic on the answer. Instead a loader
that can read the conditioned form asks for it by name:

```cpp
if (const fs::path rmesh = assets::conditioned(path); !rmesh.empty())
    ... read the .rmesh ...
else
    ... run Assimp ...
```

Two loaders do this today, and both are one function:

- `eng::detail::loadStaticModel()` (`engine/src/render/ConditionedModel.cpp`) —
  every `Renderer::loadMesh` call site goes through it.
- `rhi_renderer::loadImage()` — for a texture that opted into compression.

**A conditioned mesh is only used when its recorded import settings match what
the call site asked for.** Geometry is baked, so handing back a mesh built with
a different pivot is the whole level moving, silently. On a mismatch the loader
warns once, naming the asset and both pivots, and falls back to the source
importer. If you see that warning, fix the `.meta`.

## Verifying a change

The image is frozen (`CLAUDE.md`), so any pipeline change has to be proven not
to move a pixel:

```sh
make acp
RAVEN_COOKED_DIR=/dev/null RAVEN_SCREENSHOT=/tmp/source.png RAVEN_SCREENSHOT_FRAME=200 ./build/game
RAVEN_SCREENSHOT=/tmp/packed.png RAVEN_SCREENSHOT_FRAME=200 ./build/game
md5sum /tmp/source.png /tmp/packed.png      # must match
```

Three ctests cover the rest: `asset_format` (every on-disk format round-trips
exactly), `acp_pipeline` (the resource database, and "this changed, therefore
exactly this rebuilt"), and `acp_content` (the real tree, conditioned end to
end).

---

# How to add a new asset type

Say you want particle **flipbook atlases** to be their own row rather than
riding along as config.

1. **Add the row to the table.** `engine/src/content/AssetType.cpp`, one entry
   in `formats()`: the type, the diagram-style exporter name, the source
   extensions, the intermediate extension. Add the enum value and its stable
   name string in `AssetType.h`/`kNames`.

2. **Give it defaults.** `defaultSettings()` in
   `engine/src/content/ResourceDb.cpp` — what a freshly stamped `.meta` should
   contain.

3. **Write the exporter.** A new TU in `engine/src/acp/`, one class implementing
   `eng::acp::Exporter`:

   ```cpp
   std::string_view name() const override { return "Flipbook Exporter"; }
   AssetType type() const override { return AssetType::Flipbook; }
   uint32_t version() const override { return 1; }   // bump to force a rebuild
   ExportResult run(const ExportContext&) const override;
   ```

   `run()` must be a pure function of (source bytes, import settings) — the
   pipeline runs it on a thread pool and keys the cache on exactly those two.
   Report every other file you read in `result.dependencies`.

4. **Register it.** One line in `registerBuiltinExporters()`
   (`engine/src/acp/Registry.cpp`). If the converter lives outside the engine —
   as the World row's does, in `game_content` — register it from
   `engine/tools/acp/main.cpp` instead, on top of the built-ins.

5. **Add a format test** to `engine/tests/AssetFormatTests.cpp` if it writes a
   new binary format. Round-trip it exactly.

`make acp` now picks up every file with that extension. No renderer change, no
call-site change, and `raven_acp formats` documents it.

# How to add a new asset

1. Put the file under `assets/` in the directory its type lives in.
2. `make acp` — it is classified, conditioned and published.
3. If it needs non-default import settings, `make assetdb STAMP=1`, edit the
   `.meta`, `make acp` again. Or do both in the editor's Resource DB tab.

If a file is in the tree but should not be conditioned by the row its extension
implies — a skinned `.glb` that belongs to `gltf2ozz`, not to the static mesh
exporter — say so in its `.meta`:

```toml
[import]
skip = true
skip_reason = "skinned rig: gltf2ozz owns it (see config/arms_rig.ozz.json)"
```

# Design notes

**Why the texture row usually does nothing.** The obvious move is to always
write a `.rtex` so the runtime stops decoding PNGs. Measured: 6.8 MB of PSX
pixel art expands to 502 MB as raw RGBA8, because flat indexed colour is exactly
what PNG is best at. Reading 74× the bytes costs more than the decode it saves,
and start-up got *slower*. So `compression = "none"` publishes the source image
unchanged, and the conditioned form is produced only when it earns its place —
a block format, which is smaller than the PNG and is what the GPU wants. BC1 and
BC3 are implemented and tested; they are off by default because they visibly
alter nearest-neighbour art, and per the book that is the artist's call per
asset, not a pipeline-wide switch.

**Why `.rpfx`, `.rbank` and `.rtpl` share one encoding.** All three are authored
as TOML and parsed at start-up; `config/kit.toml` alone is ~1600 lines the
generator walks before the first frame. A binary struct per row would be a
second definition of a file `ParticleLibrary`, `Audio` and `KitCatalog` already
parse, and the two would drift the first time anyone added a key. So the
exporter conditions the *document* — TOML in, an ordered typed tree out, one
binary encoding for all three. A new authoring key needs no pipeline work.

**Why ozz owns two rows.** `gltf2ozz`, driven by `assets/config/*.ozz.json`, has
been this engine's skeleton and animation exporter since before there was a
pipeline to put it in. A second exporter writing a second skeleton format would
be a competing animation runtime, not a pipeline stage. The ACP tracks and
publishes what ozz produces.
