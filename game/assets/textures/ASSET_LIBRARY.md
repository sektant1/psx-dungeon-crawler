# Texture library

Curated imports from the local `~/gamedev/` asset library. These folders are
registered as OGRE resource locations, but no production material references
them automatically.

## `prototype/`

Seventy-eight Kenney-style grid/prototype textures: thirteen patterns in each
of dark, light, orange, green, purple, and red. Names follow
`proto_<colour>_<number>.png`. Intended for blockouts, encounter readability,
collision/debug surfaces, and editor previews.

## `vfx/`

- `flame_01..04.png` — flipbook candidates for stylized fire.
- `smoke_soft.png`, `smoke_wisp.png`, `smoke_puff.png` — particle sprites.
- `glow_radial.png` — additive light/glow sprite.

## `surfaces/`

Small fantasy tiling references for brick, cobble, tile, and wood. Intended
for material experiments and room-theme prototypes.

Imports are intentionally isolated from active materials. Promote an asset by
adding a named material and documenting its gameplay/art-direction role.
