# Generic Enchantment and Model Setup Design

## Goal

Allow any loaded 3D model to receive a reusable enchantment effect regardless
of UV quality, including every renderable in a child-node hierarchy. Provide a
small engine API that loads, renders, enchants, and gives a model a sensible
collider without repeating setup code in each game.

## Enchantment API

`EnchantmentDesc` contains style, strength, rune scale, scroll direction and
speed, pulse speed/depth, edge intensity, and recursive application. Existing
`setNodeEnchantment(node, style, strength)` remains as a compatibility wrapper.

The shader uses object-space position and normal-based triplanar projection.
It therefore works on authored models, procedural meshes, and models with
missing or stretched UVs. A fresnel edge component improves silhouette
readability without replacing the base material. The enchantment remains an
additive cloned material pass so each submesh preserves its original material.

Applying an enchantment recursively walks the target node and all descendants,
cloning only materials actually used by attached entities. Clearing follows
the same recursion and restores the recorded original materials. Reapplying
updates/rebuilds the node's enchant materials without stacking duplicate
passes.

## Easy Model and Collider Setup

Add `eng::ModelDesc` and `eng::ModelInstance` plus:

```cpp
ModelInstance spawnModel(Renderer&, Physics&, const ModelDesc&);
void destroyModel(Renderer&, Physics&, ModelInstance&);
```

`ModelDesc` specifies mesh path, material, transform, shadow behavior,
optional enchantment, body layer/dynamic properties, and collider mode:
`None`, `AutoBox`, `AutoSphere`, or `StaticMesh`.

Renderer exposes local mesh bounds and collision geometry through engine-owned
plain data, never Ogre types. `AutoBox` and `AutoSphere` derive dimensions from
scaled mesh bounds. `StaticMesh` uses loaded OBJ vertices/indices and is
rejected for dynamic bodies with a clear log message. Explicit collider size
overrides remain available for gameplay tuning.

The returned `ModelInstance` owns a node, mesh, and optional body handle.
Dynamic callers continue using the existing physics/render synchronization;
the helper does not create a second scene or hidden update loop.

## Standardized Model Import

Engine model assets use metres, +Y as up, and -Z as forward. OBJ source data
is normalized through `ModelImportOptions` rather than one-off bake matrices.
The default pivot is bottom-centre of the imported bounds so world props share
a predictable floor contact point.

Pivot modes are `Source`, `BoundsCenter`, `BottomCenter`, and `Custom`.
Per-model options may also apply a source scale, source orientation, and custom
pivot. The importer bakes this canonical transform into positions and normals,
then recomputes final bounds and collision geometry from the same transformed
data. Rendering and collision therefore cannot disagree about pivot/scale.

Model cache identity includes canonical path plus all import options. A model
loaded with a custom weapon pivot cannot alias the bottom-centred prop variant.
Material setup supports a default fallback and indexed submesh remapping;
missing mappings use the fallback prototype material and log once.

Validation rejects non-finite transforms, zero source scale, invalid custom
pivots, empty material names, and render/collision geometry mismatches.
In-repository callers that require an authored pivot explicitly select
`Source`; obsolete import overloads and inconsistent legacy asset assumptions
do not constrain the new API.

## Failure Handling

Missing mesh/material data returns an invalid instance and logs the path.
Non-finite transforms, enchant parameters, and collider dimensions are
sanitized. Collider creation failure destroys the just-created render node so
callers never receive a half-valid object.

## Testing

Tests cover descriptor defaults, bounds-to-collider conversion under
nonuniform scale, static-mesh/dynamic rejection, no-collider setup, cleanup,
recursive enchant state, repeated application without pass stacking, clear
restoration, shader source/program contracts for triplanar coordinates,
canonical pivot transforms, final bounds/collision agreement, import-cache
identity, and fallback material selection.

## Non-goals

- Animated/skinned mesh collision generation.
- Convex decomposition.
- Per-bone enchant parameters.
- Automatic dynamic render synchronization beyond the existing engine path.
