#include "PrototypeCatalogLoader.h"

#include <eng/Log.h>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

namespace game {

namespace {

bool parseKind(const std::string& name, eng::PrimitiveKind& out)
{
    using K = eng::PrimitiveKind;
    if (name == "box") { out = K::Box; return true; }
    if (name == "beveled_box") { out = K::BeveledBox; return true; }
    if (name == "sphere") { out = K::Sphere; return true; }
    if (name == "capsule") { out = K::Capsule; return true; }
    if (name == "cylinder") { out = K::Cylinder; return true; }
    if (name == "cone") { out = K::Cone; return true; }
    if (name == "plane") { out = K::Plane; return true; }
    if (name == "disc") { out = K::Disc; return true; }
    return false;
}

} // namespace

bool loadPrototypeCatalog(const std::string& tomlPath,
                          eng::prototype::PrototypeCatalog& out)
{
    toml::parse_result parsed = toml::parse_file(tomlPath);
    if (!parsed) {
        eng::log::error("prototypes: %s: %s", tomlPath.c_str(),
                        std::string(parsed.error().description()).c_str());
        return false;
    }
    const toml::table& root = parsed.table();

    int meshes = 0;
    if (const toml::array* rules = root["mesh"].as_array()) {
        for (const toml::node& node : *rules) {
            const toml::table* entry = node.as_table();
            if (!entry) continue;
            const std::string match = (*entry)["match"].value_or(std::string());
            const std::string shape = (*entry)["shape"].value_or(std::string());
            if (match.empty()) {
                eng::log::error("prototypes: mesh rule without a match");
                continue;
            }
            eng::prototype::MeshShape out_shape;
            out_shape.role = (*entry)["role"].value_or(match);
            if (!shape.empty() && !parseKind(shape, out_shape.desc.kind)) {
                eng::log::error("prototypes: '%s' is not a shape kind (rule "
                                "'%s'); using a box",
                                shape.c_str(), match.c_str());
            }
            if (const toml::array* size = (*entry)["size"].as_array();
                size && size->size() == 3) {
                for (int axis = 0; axis < 3; ++axis)
                    out_shape.desc.size[axis] =
                        float((*size)[size_t(axis)].value_or(1.0));
            }
            out_shape.desc.radius =
                float((*entry)["radius"].value_or(double(out_shape.desc.radius)));
            out_shape.desc.height =
                float((*entry)["height"].value_or(double(out_shape.desc.height)));
            out_shape.desc.bevel =
                float((*entry)["bevel"].value_or(double(out_shape.desc.bevel)));
            out.addMeshRule(match, std::move(out_shape));
            ++meshes;
        }
    }

    int materials = 0;
    if (const toml::array* rules = root["material"].as_array()) {
        for (const toml::node& node : *rules) {
            const toml::table* entry = node.as_table();
            if (!entry) continue;
            const std::string match = (*entry)["match"].value_or(std::string());
            const std::string material =
                (*entry)["material"].value_or(std::string());
            if (match.empty() || material.empty()) {
                eng::log::error(
                    "prototypes: material rule needs both match and material");
                continue;
            }
            out.addMaterialRule(match, material);
            ++materials;
        }
    }

    eng::log::info("prototypes: %d mesh rules, %d material rules", meshes,
                   materials);
    return true;
}

} // namespace game
