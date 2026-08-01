#pragma once

#include <eng/Primitive.h>

#include <string>
#include <vector>

namespace eng::prototype {

// Engine-owned diagnostic render assets. They ship as files beside this engine
// (assets/engine/textures, regenerate-prototypes.sh), so every application gets
// reliable fallbacks without shipping app assets.
inline constexpr const char* kSurfaceTexture = "EnginePrototypeSurface.png";
inline constexpr const char* kSpriteTexture = "EnginePrototypeSprite.png";
inline constexpr const char* kParticleTexture = "EnginePrototypeParticle.png";
inline constexpr const char* kSurfaceMaterial = "Engine/Psx/PrototypeSurface";
inline constexpr const char* kParticleMaterial = "Engine/Psx/PrototypeParticle";
// VFX prototypes, defined in materials/prototype_vfx.material. Unlike the
// surface fallback these are shader-driven and art-free, so a missing effect
// still animates instead of flattening into a checkered box. The engine ships
// these materials; which authored names should land on them is the
// application's rule to write (see PrototypeCatalog).
inline constexpr const char* kPortalMaterial = "Engine/PrototypeVfx/Portal";
inline constexpr const char* kPortalUpMaterial = "Engine/PrototypeVfx/PortalUp";
inline constexpr const char* kLiquidMaterial = "Engine/PrototypeVfx/Liquid";
inline constexpr const char* kLavaMaterial = "Engine/PrototypeVfx/Lava";
inline constexpr const char* kSlimeMaterial = "Engine/PrototypeVfx/Slime";

// A prototype stand-in for a mesh that would not load. `role` is a stable key
// for caching -- every asset resolving to the same role shares one mesh.
struct MeshShape {
    std::string role = "default";
    PrimitiveMeshDesc desc;
};

// What an application substitutes for an asset that would not load.
//
// The point of a fallback is that the scene stays readable: a missing barrel
// should be barrel-shaped and a missing portal should still swirl, because a
// static box where a portal belongs reads as broken geometry rather than as
// missing art. That requires knowing what the asset was *meant* to be, and the
// only thing carrying that intent is the name the application authored --
// prop_barrel_p0.obj, Game/Vfx/PortalDown. So the engine holds the rule table and
// the application fills it: a renderer that knows what a barrel is would be a
// renderer that knows which game it is running.
//
// Rules match case-insensitively as substrings, in insertion order, so the
// more specific rule is added first ("portalup" before "portal"). Substring
// matching is deliberately loose -- these are authored strings, and
// over-matching costs a slightly wrong prototype while under-matching costs a
// box.
class PrototypeCatalog {
public:
    // `match` is tested against the missing mesh's *filename*, so a directory
    // component like ".../props/" cannot decide the shape of a tile inside it.
    void addMeshRule(std::string match, MeshShape shape);
    // `match` is tested against the requested material name.
    void addMaterialRule(std::string match, std::string material);

    // The unit box from PrimitiveMeshDesc's defaults when nothing matches.
    MeshShape meshFor(const std::string& assetPath) const;
    // kSurfaceMaterial when nothing matches.
    std::string materialFor(const std::string& requested) const;

    bool empty() const { return mMeshRules.empty() && mMaterialRules.empty(); }

private:
    struct MeshRule {
        std::string match;
        MeshShape shape;
    };
    struct MaterialRule {
        std::string match;
        std::string material;
    };
    std::vector<MeshRule> mMeshRules;
    std::vector<MaterialRule> mMaterialRules;
};

} // namespace eng::prototype
