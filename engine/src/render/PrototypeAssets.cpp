#include <eng/render/PrototypeAssets.h>

#include <algorithm>
#include <cctype>

namespace eng::prototype {

std::string fallbackMaterialFor(const std::string& requested)
{
    std::string name = requested;
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    const auto has = [&name](const char* needle) {
        return name.find(needle) != std::string::npos;
    };

    // Order matters where names overlap. "portalup"/"portaldown" are checked
    // before the generic portal, and lava/slime before the generic liquid, so
    // the more specific profile wins.
    if (has("portal")) {
        // Ascent portals read blue and flow upward; anything else takes the
        // fel-green descent profile.
        if (has("up") || has("ascend") || has("ascent"))
            return kPortalUpMaterial;
        return kPortalMaterial;
    }
    if (has("lava") || has("magma"))
        return kLavaMaterial;
    if (has("slime") || has("toxic") || has("ooze"))
        return kSlimeMaterial;
    if (has("water") || has("liquid") || has("fluid"))
        return kLiquidMaterial;
    if (has("particle"))
        return kParticleMaterial;
    return kSurfaceMaterial;
}

MeshShape meshShapeFor(const std::string& assetPath)
{
    // Filename only: callers pass absolute paths, and a directory component
    // like ".../props/" must not decide the shape of a tile inside it.
    const size_t slash = assetPath.find_last_of("/\\");
    std::string name =
        slash == std::string::npos ? assetPath : assetPath.substr(slash + 1);
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    const auto has = [&name](const char* needle) {
        return name.find(needle) != std::string::npos;
    };

    MeshShape shape;
    PrimitiveMeshDesc& d = shape.desc;

    // Structural tiles (floor/ceiling/wall/arch/pillar) deliberately fall
    // through to the unit box. Sizing them in cells is what they want visually,
    // but primitives are built centred on their origin while the authored tile
    // meshes were modelled base-at-zero, so a cell-sized slab lands half a cell
    // out and seals the player inside the room. Giving prototypes a pivot
    // convention is the fix; until then a small box in the right place beats a
    // large one in the wrong place.

    // --- props: roughly the silhouette the authored mesh had ---------------
    if (has("barrel") || has("vase") || has("pot")) {
        shape.role = "prop_barrel";
        d.kind = PrimitiveKind::Cylinder;
        d.radius = 0.38f;
        d.height = 0.95f;
        return shape;
    }
    if (has("torch") || has("candle")) {
        shape.role = "prop_torch";
        d.kind = PrimitiveKind::Cylinder;
        d.radius = 0.06f;
        d.height = 0.7f;
        return shape;
    }
    if (has("sword") || has("weapon") || has("blade")) {
        shape.role = "prop_blade";
        d.kind = PrimitiveKind::Box;
        d.size = {0.10f, 1.10f, 0.06f};
        return shape;
    }
    if (has("shield")) {
        shape.role = "prop_shield";
        d.kind = PrimitiveKind::Box;
        d.size = {0.6f, 0.8f, 0.12f};
        return shape;
    }
    if (has("table")) {
        shape.role = "prop_table";
        d.kind = PrimitiveKind::Box;
        d.size = {1.6f, 0.8f, 0.9f};
        return shape;
    }
    if (has("chest") || has("crate")) {
        shape.role = "prop_crate";
        d.kind = PrimitiveKind::BeveledBox;
        d.size = {0.9f, 0.7f, 0.7f};
        return shape;
    }
    if (has("hay") || has("sack") || has("jute")) {
        shape.role = "prop_bale";
        d.kind = PrimitiveKind::BeveledBox;
        d.size = {1.1f, 0.85f, 1.1f};
        d.bevel = 0.2f;
        return shape;
    }
    if (has("bread") || has("pumpkin")) {
        shape.role = "prop_produce";
        d.kind = PrimitiveKind::Sphere;
        d.radius = 0.22f;
        return shape;
    }
    if (has("lamp")) {
        shape.role = "prop_lamp";
        d.kind = PrimitiveKind::Cylinder;
        d.radius = 0.16f;
        d.height = 0.5f;
        return shape;
    }
    if (has("beam") || has("plank") || has("board")) {
        shape.role = "prop_beam";
        d.kind = PrimitiveKind::Box;
        d.size = {0.25f, 0.25f, kCellSize};
        return shape;
    }

    // --- set dressing ------------------------------------------------------
    if (has("spire") || has("crystal")) {
        shape.role = "crystal_spire";
        d.kind = PrimitiveKind::Cone;
        d.radius = 0.5f;
        d.height = 2.4f;
        return shape;
    }
    if (has("shaft")) {
        shape.role = "light_shaft";
        d.kind = PrimitiveKind::Cylinder;
        d.radius = 0.7f;
        d.height = 3.0f;
        return shape;
    }

    return shape; // default: the unit box from PrimitiveMeshDesc's defaults
}

} // namespace eng::prototype
