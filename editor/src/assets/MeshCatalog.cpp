#include <editor/assets/MeshCatalog.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

namespace ed {
namespace {

std::string lowered(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return char(std::tolower(c));
    });
    return text;
}

} // namespace

void MeshCatalog::load(const std::string& meshDir,
                       const std::vector<std::string>& extensions)
{
    mAssets.clear();
    mLoaded = true;
    if (meshDir.empty())
        return;

    const std::filesystem::path root(meshDir);
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec))
        return;

    // recursive_directory_iterator with an error_code: a permission-denied
    // subdirectory anywhere under assets/ must not throw out of a panel that
    // is being drawn. A partial list beats a dialog, the same rule the material
    // catalogue follows.
    std::filesystem::recursive_directory_iterator it(
        root, std::filesystem::directory_options::skip_permission_denied, ec);
    const std::filesystem::recursive_directory_iterator end;
    for (; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec))
            continue;
        const std::filesystem::path& file = it->path();
        const std::string extension = lowered(file.extension().string());
        if (std::find(extensions.begin(), extensions.end(), extension) ==
            extensions.end())
            continue;

        MeshAsset asset;
        // Pack-relative and always with '/', because that is what the resolver
        // takes and what a .scn carries -- a backslash here would produce a map
        // that loads on one platform.
        asset.path =
            "meshes/" +
            std::filesystem::relative(file, root, ec).generic_string();
        asset.name = file.stem().string();
        const std::filesystem::path parent =
            std::filesystem::relative(file.parent_path(), root, ec)
                .generic_string();
        asset.group = parent == "." ? std::string() : parent.generic_string();
        asset.extension = extension;
        asset.sizeBytes = std::filesystem::file_size(file, ec);
        if (ec) {
            asset.sizeBytes = 0;
            ec.clear();
        }
        mAssets.push_back(std::move(asset));
    }

    std::sort(mAssets.begin(), mAssets.end(),
              [](const MeshAsset& a, const MeshAsset& b) {
                  // Ungrouped files first: a mesh sitting directly in meshes/
                  // is usually the one somebody just imported.
                  if (a.group != b.group)
                      return a.group < b.group;
                  return a.name < b.name;
              });
}

void MeshCatalog::annotate(
    const std::vector<std::pair<std::string, std::string>>& pathToPrefab,
    const std::vector<std::pair<std::string, std::string>>& pathToMaterial)
{
    for (MeshAsset& asset : mAssets) {
        asset.kitPrefab.clear();
        asset.material.clear();
        for (const auto& [path, prefab] : pathToPrefab) {
            if (path == asset.path) {
                asset.kitPrefab = prefab;
                break;
            }
        }
        for (const auto& [path, material] : pathToMaterial) {
            if (path == asset.path) {
                asset.material = material;
                break;
            }
        }
    }
}

const MeshAsset* MeshCatalog::find(std::string_view path) const
{
    for (const MeshAsset& asset : mAssets)
        if (asset.path == path)
            return &asset;
    return nullptr;
}

std::vector<std::string> MeshCatalog::groups() const
{
    std::vector<std::string> out;
    for (const MeshAsset& asset : mAssets)
        if (std::find(out.begin(), out.end(), asset.group) == out.end())
            out.push_back(asset.group);
    return out;
}

const std::vector<PrimitivePreset>& primitivePresets()
{
    using P = eng::ecs::PrimitiveMesh;
    static const std::vector<PrimitivePreset> kPresets = [] {
        std::vector<PrimitivePreset> presets;

        P box;
        presets.push_back({"box", "Box", "the greybox unit -- walls, blocks, crates", box});

        P beveled;
        beveled.kind = P::BeveledBox;
        beveled.bevel = 0.08f;
        presets.push_back({"beveled_box", "Beveled Box",
                           "a box whose edges catch the light", beveled});

        P sphere;
        sphere.kind = P::Sphere;
        sphere.rings = 16;
        sphere.segments = 24;
        presets.push_back(
            {"sphere", "Sphere", "orbs, projectiles, anything round", sphere});

        P capsule;
        capsule.kind = P::Capsule;
        capsule.radius = 0.35f;
        capsule.height = 1.8f;
        presets.push_back({"capsule", "Capsule",
                           "roughly person-shaped -- blockout actors", capsule});

        P cylinder;
        cylinder.kind = P::Cylinder;
        cylinder.radius = 0.4f;
        cylinder.height = 2.0f;
        presets.push_back(
            {"cylinder", "Cylinder", "columns, barrels, posts", cylinder});

        P cone;
        cone.kind = P::Cone;
        cone.radius = 0.5f;
        cone.height = 1.0f;
        presets.push_back({"cone", "Cone", "spikes, roofs, light cones", cone});

        P plane;
        plane.kind = P::Plane;
        plane.size = {2.0f, 1.0f, 2.0f};
        presets.push_back({"plane", "Plane",
                           "a flat quad -- floors, decals, VFX surfaces", plane});

        P disc;
        disc.kind = P::Disc;
        disc.radius = 0.75f;
        disc.segments = 32;
        presets.push_back(
            {"disc", "Disc", "a flat circle -- pads, seals, portals", disc});

        return presets;
    }();
    return kPresets;
}

} // namespace ed
