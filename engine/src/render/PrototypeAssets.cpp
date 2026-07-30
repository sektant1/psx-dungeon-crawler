#include <eng/render/PrototypeAssets.h>

#include <algorithm>
#include <cctype>
#include <string_view>

namespace eng::prototype {

namespace {

std::string lowered(std::string_view text)
{
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return out;
}

// Filename only: callers pass absolute paths, and a directory component must
// not decide the shape of an asset inside it.
std::string filename(const std::string& path)
{
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

} // namespace

void PrototypeCatalog::addMeshRule(std::string match, MeshShape shape)
{
    mMeshRules.push_back({lowered(match), std::move(shape)});
}

void PrototypeCatalog::addMaterialRule(std::string match, std::string material)
{
    mMaterialRules.push_back({lowered(match), std::move(material)});
}

MeshShape PrototypeCatalog::meshFor(const std::string& assetPath) const
{
    const std::string name = lowered(filename(assetPath));
    for (const MeshRule& rule : mMeshRules)
        if (name.find(rule.match) != std::string::npos)
            return rule.shape;
    return {}; // unit box from PrimitiveMeshDesc's defaults
}

std::string PrototypeCatalog::materialFor(const std::string& requested) const
{
    const std::string name = lowered(requested);
    for (const MaterialRule& rule : mMaterialRules)
        if (name.find(rule.match) != std::string::npos)
            return rule.material;
    return kSurfaceMaterial;
}

} // namespace eng::prototype
