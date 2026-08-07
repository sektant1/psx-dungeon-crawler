# Draw submission: what it cost, and what fixed it {#doc-render-batching}

*Companion to `docs/memory-profiling.md`. Same method: measure, fix the one
thing the measurement points at, measure again — and here, prove the image did
not move.*

## The constraint

`CLAUDE.md` says the PSX look is a shipped result and no refactor may change the
rendered image. So the goal was never "fewer milliseconds" on its own; it was
**fewer milliseconds at byte-identical output**. Every change below was verified
against a deterministic capture:

```sh
cd build && RAVEN_GEN_SEED=1 RAVEN_FIXED_DT=0.016666667 \
  RAVEN_SCREENSHOT=/tmp/f200.png RAVEN_SCREENSHOT_FRAME=200 ./game
```

With a fixed timestep and a fixed seed this is bit-reproducible — the same run
twice produces the same MD5 — which makes it a real oracle rather than a
smell test. Frames 90, 200 and 400 were captured before any change and compared
after every one.

## What the frame actually looked like

A probe over the main scene pass, on a populated dungeon:

```
787 draws -> 22 unique (mesh, submesh, material) keys, 4 unique pipelines
760 of the 787 draws are repeats of a key already drawn
   x299  mesh 1 : Game/Kit/Dungeon
   x299  mesh 1 : Game/Kit/DungeonTwoSided
   x99   mesh 2 : Game/Kit/Dungeon
```

And where the time went — 3.02 ms of a 3.87 ms frame was the CPU submit loop,
about 3.8 µs per draw, which is far too slow to be Vulkan command recording:

| part of the submit loop | before | after |
| --- | --- | --- |
| material + pipeline resolve | 0.66 ms | 0.09 ms |
| building `DrawConstants` | 0.26 ms | 0.15 ms |
| RHI command calls | 0.86 ms | 0.69 ms |
| loop overhead / temporaries | 0.83 ms | ~0.31 ms |
| **total submit** | **2.61 ms** | **1.24 ms** |

The bottleneck was never the GPU and never the driver. Dropping the framebuffer
to a ninth of the pixels moved the frame by 7%, so it was not fill-bound; the
cost was renderer-side work being redone per draw.

## The four fixes

All four are order-preserving and touch no shader, so the image cannot move.

1. **The prototype fallback was an eager argument.**
   `materials.resolve(requested, prototypes.materialFor(requested))` —
   `materialFor()` lowercases the name into a fresh `std::string` and
   substring-searches every material rule, and it ran on *every* draw even
   though `resolve()` only uses the fallback when the direct lookup misses,
   which is the rare case. Now: `find()` first, fallback only on a miss.
   Resolve went 0.66 ms → 0.11 ms.

2. **The material name was copied per draw.** `const std::string requested = …`
   became a `const std::string&`.

3. **The static-batch loop allocated per record.** It built a fresh
   `MeshAttachment` — a `std::vector<std::string>` heap allocation and a string
   copy — for each of several hundred props, every frame, purely to hand
   `drawMesh` the shape it expects. One reused attachment now keeps its
   capacity across the loop.

4. **`DrawConstants` recomputed per-material values per draw.** Tint, rim
   colour, rim power and alpha scissor are pure functions of the material, and
   each was three string-keyed map lookups on every draw. They are memoised per
   material now, keyed by address, and evicted precisely on
   `setMaterialParam` — precisely, because the post and stylize passes drive
   that every frame and clearing the whole table there would discard all 22
   entries to invalidate one.

Plus one in the RHI, which every app on it inherits:

5. **Redundant pipeline and buffer binds.** `bindTexture`/`bindUniformBuffer`
   already skipped redundant binds; `bindPipeline`, `bindVertexBuffer` and
   `bindIndexBuffer` did not, so a scene issued 787 `vkCmdBindPipeline` calls
   through 4 pipelines. They now compare against what is already bound. These
   are command-buffer state in Vulkan and survive render-pass boundaries, so
   the cache is cleared only in `reset()`, where the command buffer changes.

## Result

Interleaved A/B, three rounds, same machine, same scene, "after" ahead every
round:

```
before   p50 3.42  3.52  3.82 ms
after    p50 2.86  2.92  2.98 ms      ~17% off the frame, ~52% off the submit loop
```

Frames 90 / 200 / 400: **MD5-identical to the pre-change capture.**

## Instancing: built, measured, reverted

760 of 787 draws are repeats of one of 22 keys, so the pass *should* collapse to
~22 instanced draws. It was implemented in full and then removed, because it
lost on both of the things that matter here. Both results are worth keeping so
nobody spends the day rediscovering them.

The implementation was the standard one: `DrawConstants` moved out of push
constants into a `std140` uniform array indexed by `gl_InstanceIndex`
(`scene.vert`, `scene.frag`, and `skinned_scene.vert` — which shares
`scene.frag` and so had to feed the same block), an arena so each run owns a
distinct slice, and one upload per pass. It needed no RHI change: slot 3 was
free and the descriptor layout already declares all 8 bindings for both stages.

It worked. **791 draws became 59.** And it was still slower:

```
orig                     p50 2.78  2.85  3.04 ms
non-instanced (shipped)  p50 1.83  2.02  2.00 ms      <- fastest
instanced, 59 draws      p50 2.97  2.90  2.92 ms
```

### Why fewer draws was slower

`scene.frag` reads tint, rim and the surface params for **every pixel**. As a
push constant that is effectively free; as `entries[instanceIndex]` it is a
dependent indexed load out of a uniform buffer, on a fill-heavy renderer, on an
integrated GPU. Paying that per fragment costs more than the ~730 draw calls it
saves — especially once the draw calls had already been made cheap by the fixes
above, at which point the whole submit loop was only ~1.24 ms.

Shrinking the arena from 4096 slots to 1024 changed nothing (2.92 → 2.90 ms), so
the per-pass upload was not the cost either.

### Why it also could not stay

Two capture runs isolate this exactly:

| variant | draws | pixels |
| --- | --- | --- |
| consecutive-only runs (never reorders) | 791 — merges nothing | **identical** |
| bucketed runs (groups by key) | 59 | differ, max channel delta **4/255** |

So the uniform-array indexing is pixel-safe on its own. What moves pixels is the
**reordering**, and reordering is not optional: the dungeon kit draws the same
mesh and submesh alternately through two materials, so consecutive-only merging
finds runs of length one and collapses nothing. Merging here *requires*
grouping, grouping *is* reordering, and reordering perturbs which fragment wins
on depth ties — visible in the MRT metadata the stylize pass reads, which is why
a fifth of the screen shifts by one or two levels.

Four parts in 255 is invisible. It is still a changed image, and this repo's
rule is that the image does not change. So: reverted, and the ~19% that came
from not doing redundant work per draw was kept.

### The vertex-only variant: also built, also reverted

The design this document previously recommended — keep `DrawConstants` in the
push constant for the fragment stage, make **only the model matrix**
per-instance, read in the vertex stage — was then implemented and measured. It
is not the answer either.

`scene.vert` took its model from `modelBlock.models[gl_InstanceIndex]`;
`scene.frag` was **not touched at all**, so the per-fragment indexed load that
was blamed for the first attempt was entirely absent. Runs were bucketed by
(pipeline, mesh, submesh, texture) *and* by the rest of the push constant, since
that still has to be uniform across a run.

```
orig                     p50 6.50  6.14  5.62 ms
non-instanced (shipped)  p50 4.72  4.72  5.28 ms   <- still fastest
vertex-only instanced    p50 7.38  7.29  7.25 ms   (58 draws)
```

790 draws became **58**, and it was still the slowest of the three. So the
hypothesis was wrong: the cost is not "the fragment stage reads a UBO". On this
GPU a dynamically-indexed uniform read is simply more expensive than a push
constant *in either stage*, and at ~1.2 ms of submit there are not enough draw
calls left to buy that back.

Both attempts also still reorder, so both still fail the frozen-image rule for
the same reason. Two independent designs, both measured, both losing on both
counts: the conclusion is that instancing is not what this renderer wants until
either the frame is submitting far more draws than it does today, or a profile
on different hardware says otherwise.

### If it is ever revisited

The one design that could pay: keep `DrawConstants` in push constants for the
fragment stage — they are constant across a run anyway, since a run shares a
material — and put **only the model matrix** per instance, read in the vertex
stage where the cost is per-vertex rather than per-pixel. `scene.frag` would not
change at all. That removes the reason it was slow. It does **not** remove the
reordering problem, so it would still need either a relaxation of the
frozen-image rule or a way to make same-key draws contiguous at build time.
