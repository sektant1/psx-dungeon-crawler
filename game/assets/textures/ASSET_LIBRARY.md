# Prototype texture library

This directory intentionally contains only temporary blockout art. The engine
also creates in-memory prototype surface, sprite, and particle textures, so a
missing authored texture never prevents the demo from running.

## `prototype/`

Seventy-eight grid/prototype textures: thirteen patterns in each of dark,
light, orange, green, purple, and red. Names follow
`proto_<colour>_<number>.png`. Use them for blockouts, encounter readability,
collision/debug surfaces, and editor previews.

Do not put downloaded or commissioned art directly next to these files. Put
source art under `assets-src/` and promote cooked runtime files through the
content pipeline described in
[`docs/design/2026-07-27-prototype-content-reset-and-pipeline.md`](../../../docs/design/2026-07-27-prototype-content-reset-and-pipeline.md).
