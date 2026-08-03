#include <editor/assets/MaterialCatalog.h>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace ed {
namespace {

std::string trim(const std::string& text)
{
    const std::size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return {};
    const std::size_t end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

// The first whitespace-separated token after `keyword`, or empty.
std::string tokenAfter(const std::string& line, const std::string& keyword)
{
    const std::size_t at = line.find(keyword);
    if (at == std::string::npos)
        return {};
    std::istringstream in(line.substr(at + keyword.size()));
    std::string token;
    in >> token;
    // Materials are written both as `fragment_program_ref X { }` and with the
    // brace on the next line; strip a trailing one either way.
    while (!token.empty() && (token.back() == '{' || token.back() == '}'))
        token.pop_back();
    return token;
}

bool startsWith(const std::string& text, const char* prefix)
{
    return text.rfind(prefix, 0) == 0;
}

// The material states its shader family, so classification is a mapping rather
// than an inference. It used to be read off the Ogre vertex program's name --
// the thing that bound the vertex streams -- which meant renaming a program
// silently reclassified every material using it.
MaterialClass classify(const MaterialInfo& info)
{
    const std::string& shader = info.shader;
    if (shader.empty())
        return MaterialClass::Unknown;
    if (startsWith(shader, "post."))
        return MaterialClass::PostProcess;
    // Instanced: the vertex stage reads a per-instance stream, so a static mesh
    // supplies nothing and the draw produces nothing.
    if (startsWith(shader, "particle."))
        return MaterialClass::Particle;
    if (shader == "sprite" || shader == "decal" || shader == "wire" ||
        shader == "debug_lines")
        return MaterialClass::Sprite;
    // Liquids and portals: the field is computed from 0..1 surface UVs.
    if (startsWith(shader, "surface."))
        return MaterialClass::VfxSurface;
    if (startsWith(shader, "editor."))
        return MaterialClass::EditorOnly;
    if (startsWith(shader, "lit") || startsWith(shader, "unlit")) {
        // An atlas is a clamped sheet: the UVs must land inside a region of it.
        // Wrapping textures tile, so they sit on anything.
        return info.clamped ? MaterialClass::Atlas : MaterialClass::Surface;
    }
    return MaterialClass::Unknown;
}

} // namespace

const char* materialClassName(MaterialClass klass)
{
    switch (klass) {
    case MaterialClass::Surface: return "surface";
    case MaterialClass::Atlas: return "atlas";
    case MaterialClass::VfxSurface: return "vfx";
    case MaterialClass::Particle: return "particle";
    case MaterialClass::Sprite: return "sprite";
    case MaterialClass::PostProcess: return "post";
    case MaterialClass::EditorOnly: return "editor";
    case MaterialClass::Unknown: break;
    }
    return "unknown";
}

std::vector<MaterialInfo> parseMaterialScript(const std::string& path)
{
    std::vector<MaterialInfo> materials;
    const toml::parse_result parsed = toml::parse_file(path);
    if (!parsed)
        return materials;
    const toml::table* table = parsed.table()["material"].as_table();
    if (!table)
        return materials;

    for (const auto& [key, node] : *table) {
        const toml::table* entry = node.as_table();
        if (!entry)
            continue;
        MaterialInfo info;
        info.name = std::string(key.str());
        info.shader = (*entry)["shader"].value_or(std::string{"lit"});
        info.texture = (*entry)["texture"].value_or(std::string{});
        info.clamped = (*entry)["address"].value_or(std::string{"repeat"}) ==
                       "clamp";
        info.twoSided =
            (*entry)["cull"].value_or(std::string{"back"}) == "none";
        info.klass = classify(info);
        materials.push_back(std::move(info));
    }
    // Directory order is filesystem-dependent and the table above is a map, so
    // sort: the editor's list must not reshuffle between runs.
    std::sort(materials.begin(), materials.end(),
              [](const MaterialInfo& a, const MaterialInfo& b) {
                  return a.name < b.name;
              });
    return materials;
}

std::vector<MaterialInfo> loadMaterialCatalog(const std::string& directory)
{
    std::vector<MaterialInfo> all;
    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec))
        return all;
    for (const std::filesystem::directory_entry& file :
         std::filesystem::directory_iterator(directory, ec)) {
        if (ec)
            break;
        if (!file.is_regular_file() || file.path().extension() != ".mat")
            continue;
        std::vector<MaterialInfo> some = parseMaterialScript(file.path().string());
        all.insert(all.end(), std::make_move_iterator(some.begin()),
                   std::make_move_iterator(some.end()));
    }
    std::sort(all.begin(), all.end(),
              [](const MaterialInfo& a, const MaterialInfo& b) {
                  return a.name < b.name;
              });
    return all;
}

MeshKind meshKindForMaterial(MaterialClass klass)
{
    switch (klass) {
    case MaterialClass::Atlas: return MeshKind::AtlasUv;
    case MaterialClass::Surface: return MeshKind::Tiling;
    case MaterialClass::VfxSurface: return MeshKind::Generated;
    default: break;
    }
    return MeshKind::Unknown;
}

bool isEntityMaterial(MaterialClass klass)
{
    switch (klass) {
    case MaterialClass::Surface:
    case MaterialClass::Atlas:
    case MaterialClass::VfxSurface:
    case MaterialClass::Unknown:
        return true;
    case MaterialClass::Particle:
    case MaterialClass::Sprite:
    case MaterialClass::PostProcess:
    case MaterialClass::EditorOnly:
        return false;
    }
    return false;
}

MaterialAdvice materialFits(MaterialClass klass, MeshKind mesh)
{
    switch (klass) {
    case MaterialClass::PostProcess:
        return {Fit::Broken,
                "a full-screen compositor pass -- it samples the framebuffer, "
                "not this mesh"};
    case MaterialClass::Particle:
        return {Fit::Broken,
                "needs the per-instance stream the particle system supplies; "
                "on a static mesh the draw produces nothing"};
    case MaterialClass::Sprite:
        return {Fit::Broken,
                "for billboards and decals the engine generates itself, not "
                "for authored geometry"};
    case MaterialClass::EditorOnly:
        return {Fit::Risky,
                "an editor material -- it is not in the game's content, so the "
                "cooked level would render it as missing"};
    case MaterialClass::Unknown:
        return {Fit::Risky,
                "the material script does not say what geometry it expects"};

    case MaterialClass::Atlas:
        if (mesh == MeshKind::AtlasUv)
            return {};
        return {Fit::Risky,
                "an atlas sampled with clamp: this mesh's UVs run 0..1, so the "
                "whole sheet stretches across each face"};

    case MaterialClass::VfxSurface:
        if (mesh == MeshKind::Generated)
            return {};
        return {Fit::Risky,
                "an animated VFX surface written for a flat quad with 0..1 "
                "UVs; on this mesh the flow follows the atlas layout instead "
                "of the surface"};

    case MaterialClass::Surface:
        // A wrapping texture tiles over whatever UVs it is given. This is the
        // one class that is safe everywhere, which is why it is the answer when
        // an author wants to restyle a piece.
        return {};
    }
    return {};
}

} // namespace ed
