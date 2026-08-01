#include "MaterialCatalog.h"

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

// Everything the classifier needs is in the vertex program: it is what binds
// the mesh's vertex streams, so it is what decides which meshes can supply
// them. The fragment program and the sampler only refine the answer.
MaterialClass classify(const MaterialInfo& info)
{
    const std::string& vs = info.vertexProgram;
    if (vs.empty())
        return MaterialClass::Unknown;

    // Full-screen passes: the compositor supplies the quad and the source
    // texture. Nothing about them survives being put on an entity.
    if (startsWith(vs, "Dither_VS"))
        return MaterialClass::PostProcess;
    // Instanced: the vertex shader reads a per-instance stream. A static mesh
    // has none, so the draw produces nothing -- the failure this repo has
    // already chased twice.
    if (startsWith(vs, "Particle") || startsWith(vs, "Particles/"))
        return MaterialClass::Particle;
    if (startsWith(vs, "Sprite/") || startsWith(vs, "Decals/"))
        return MaterialClass::Sprite;
    // Liquids and portals: the flow is computed from 0..1 surface UVs.
    if (startsWith(vs, "PixelVfx/"))
        return MaterialClass::VfxSurface;
    if (startsWith(vs, "Editor_"))
        return MaterialClass::EditorOnly;

    if (startsWith(vs, "PSX_VS_")) {
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
    std::ifstream in(path);
    if (!in)
        return materials;

    MaterialInfo current;
    bool inMaterial = false;
    int depth = 0;
    std::string line;
    while (std::getline(in, line)) {
        // Comments first: several scripts carry a `// psx_lit.gdshader` note
        // after the material name, and one mentions `texture_unit` in prose.
        if (const std::size_t comment = line.find("//");
            comment != std::string::npos)
            line = line.substr(0, comment);
        const std::string text = trim(line);
        if (text.empty())
            continue;

        if (!inMaterial) {
            if (!startsWith(text, "material "))
                continue;
            current = MaterialInfo{};
            current.name = tokenAfter(text, "material ");
            if (current.name.empty())
                continue;
            inMaterial = true;
            depth = 0;
        }

        // Brace depth, so the material ends where its block does rather than at
        // the next `material` keyword -- which would swallow a file whose last
        // material is unterminated.
        depth += int(std::count(text.begin(), text.end(), '{'));
        depth -= int(std::count(text.begin(), text.end(), '}'));

        if (text.find("vertex_program_ref") != std::string::npos)
            current.vertexProgram = tokenAfter(text, "vertex_program_ref");
        if (text.find("fragment_program_ref") != std::string::npos)
            current.fragmentProgram = tokenAfter(text, "fragment_program_ref");
        // `texture <file>`, wherever it sits: the shipped scripts put it on its
        // own line, but the compact `texture_unit { texture x.png ... }` form
        // is equally valid Ogre and appears in hand-written ones. The trailing
        // space is what keeps this from matching `texture_unit` itself.
        if (startsWith(text, "texture ") ||
            text.find(" texture ") != std::string::npos) {
            if (const std::string file = tokenAfter(text, "texture ");
                !file.empty() && current.texture.empty())
                current.texture = file;
        }
        if (text.find("tex_address_mode") != std::string::npos)
            current.clamped = tokenAfter(text, "tex_address_mode") == "clamp";
        if (text.find("cull_hardware") != std::string::npos)
            current.twoSided = tokenAfter(text, "cull_hardware") == "none";

        if (inMaterial && depth <= 0 && text.find('}') != std::string::npos) {
            current.klass = classify(current);
            materials.push_back(std::move(current));
            inMaterial = false;
        }
    }
    // A script that ends mid-material still yields what it had: a partial list
    // is more useful than an empty panel.
    if (inMaterial && !current.name.empty()) {
        current.klass = classify(current);
        materials.push_back(std::move(current));
    }
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
        if (!file.is_regular_file() || file.path().extension() != ".material")
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
