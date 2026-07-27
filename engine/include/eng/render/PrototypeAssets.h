#pragma once

#include <eng/Primitive.h>

#include <string>

namespace eng::prototype {

// Engine-owned diagnostic render assets. They ship as files beside this engine
// (engine/assets/textures, regenerate-prototypes.sh), so every application gets
// reliable fallbacks without shipping app assets.
inline constexpr const char* kSurfaceTexture = "EnginePrototypeSurface.png";
inline constexpr const char* kSpriteTexture = "EnginePrototypeSprite.png";
inline constexpr const char* kParticleTexture = "EnginePrototypeParticle.png";
inline constexpr const char* kSurfaceMaterial = "Engine/PrototypeSurface";
inline constexpr const char* kParticleMaterial = "Engine/PrototypeParticle";
// VFX prototypes, defined in materials/prototype_vfx.material. Unlike the
// surface fallback these are shader-driven and art-free, so a missing effect
// still animates instead of flattening into a checkered box.
inline constexpr const char* kPortalMaterial = "Engine/PrototypePortal";
inline constexpr const char* kPortalUpMaterial = "Engine/PrototypePortalUp";
inline constexpr const char* kLiquidMaterial = "Engine/PrototypeLiquid";
inline constexpr const char* kLavaMaterial = "Engine/PrototypeLava";
inline constexpr const char* kSlimeMaterial = "Engine/PrototypeSlime";

// Picks the prototype that best matches what `requested` was meant to be, by
// name. A caller asking for a portal gets a portal, not a box: the point of the
// fallback is to keep the scene readable, and a static box where a portal
// belongs reads as broken geometry rather than as missing art. Anything
// unrecognised falls to kSurfaceMaterial. Matching is case-insensitive and
// substring-based, deliberately loose -- material names are authored strings,
// and over-matching costs a slightly wrong prototype while under-matching costs
// a box.
std::string fallbackMaterialFor(const std::string& requested);

// One dungeon cell, matching the cell_size the level documents author.
inline constexpr float kCellSize = 4.0f;

// A prototype stand-in for a mesh that would not load, chosen from the asset's
// filename. `role` is a stable key for caching -- every asset resolving to the
// same role shares one mesh.
struct MeshShape {
    const char* role = "default";
    PrimitiveMeshDesc desc;
};

// Picks the primitive that best matches the missing mesh, by filename. The
// naming convention in game/assets/meshes is what carries the intent
// (prop_barrel_p0, prop_torch, ...), so a barrel becomes a barrel-shaped
// cylinder rather than a generic cube. Anything unrecognised -- including the
// structural tiles, see the note in meshShapeFor -- gets a unit box.
MeshShape meshShapeFor(const std::string& assetPath);

} // namespace eng::prototype
