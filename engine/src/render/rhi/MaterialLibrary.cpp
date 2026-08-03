#include "MaterialLibrary.h"

#include <eng/Log.h>
#include <eng/assets/AssetRoot.h>
#include <eng/render/PrototypeAssets.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <sstream>

namespace eng::rhi_renderer {
namespace {

template <typename T>
T valueOr(const std::unordered_map<std::string, MaterialValue>& values,
          const char* name, T fallback)
{
    const auto found = values.find(name);
    if (found == values.end())
        return fallback;
    if (const T* value = std::get_if<T>(&found->second))
        return *value;
    return fallback;
}

std::vector<std::string> tokenize(const std::string& source)
{
    std::vector<std::string> tokens;
    std::string current;
    bool comment = false;
    bool quoted = false;
    const auto flush = [&] {
        if (!current.empty()) {
            tokens.push_back(current);
            current.clear();
        }
    };
    for (size_t i = 0; i < source.size(); ++i) {
        const char c = source[i];
        if (comment) {
            if (c == '\n')
                comment = false;
            continue;
        }
        if (!quoted && c == '/' && i + 1 < source.size() &&
            source[i + 1] == '/') {
            flush();
            comment = true;
            ++i;
            continue;
        }
        if (c == '"') {
            if (quoted) {
                flush();
                quoted = false;
            } else {
                flush();
                quoted = true;
            }
            continue;
        }
        if (!quoted && (std::isspace(static_cast<unsigned char>(c)) ||
                        c == '{' || c == '}')) {
            flush();
            if (c == '{' || c == '}')
                tokens.emplace_back(1, c);
            continue;
        }
        current.push_back(c);
    }
    flush();
    return tokens;
}

bool parseFloat(const std::string& text, float& value)
{
    try {
        size_t consumed = 0;
        value = std::stof(text, &consumed);
        return consumed == text.size() && std::isfinite(value);
    } catch (...) {
        return false;
    }
}

int componentCount(const std::string& type)
{
    if (type == "float" || type == "int") return 1;
    if (type == "float2") return 2;
    if (type == "float3") return 3;
    if (type == "float4") return 4;
    return 0;
}

MaterialValue makeValue(const std::array<float, 4>& values, int count)
{
    switch (count) {
    case 1: return values[0];
    case 2: return glm::vec2(values[0], values[1]);
    case 3: return glm::vec3(values[0], values[1], values[2]);
    default: return glm::vec4(values[0], values[1], values[2], values[3]);
    }
}

} // namespace

glm::vec4 Material::modulate() const
{
    return valueOr(params, "modulateColor", glm::vec4(1.0f));
}
glm::vec2 Material::uvScale() const
{
    return valueOr(params, "uvScale", glm::vec2(1.0f));
}
glm::vec2 Material::uvOffset() const
{
    return valueOr(params, "uvOffset", glm::vec2(0.0f));
}

bool MaterialLibrary::loadAll(RenderCore& core)
{
    mMaterials.clear();
    mResourceDirs = assets::resourceDirs();
    constexpr std::array<uint8_t, 4> white{255, 255, 255, 255};
    mWhite = core.createTexture("renderer.material-white", 1, 1, white.data(),
                                rhi::FilterMode::Nearest,
                                rhi::AddressMode::Repeat);

    std::vector<std::filesystem::path> scripts;
    std::error_code error;
    for (const std::filesystem::path& directory : mResourceDirs) {
        for (std::filesystem::directory_iterator it(directory, error), end;
             !error && it != end; it.increment(error)) {
            if (it->is_regular_file() && it->path().extension() == ".material")
                scripts.push_back(it->path());
        }
        error.clear();
    }
    std::sort(scripts.begin(), scripts.end());
    bool okay = true;
    for (const std::filesystem::path& script : scripts)
        okay = loadScript(core, script) && okay;

    if (!find(prototype::kSurfaceMaterial)) {
        Material fallback;
        fallback.name = prototype::kSurfaceMaterial;
        fallback.texture = core.fallbackTexture();
        mMaterials.emplace(fallback.name, std::move(fallback));
    }
    log::info("RHI renderer: parsed %zu legacy material definitions",
              mMaterials.size());
    return okay;
}

bool MaterialLibrary::loadScript(RenderCore& core,
                                 const std::filesystem::path& path)
{
    std::ifstream input(path);
    if (!input) {
        log::error("RHI renderer: cannot open material script '%s'",
                   path.string().c_str());
        return false;
    }
    std::ostringstream source;
    source << input.rdbuf();
    return parse(core, path, source.str());
}

bool MaterialLibrary::parse(RenderCore& core, const std::filesystem::path& path,
                            const std::string& source)
{
    const std::vector<std::string> tokens = tokenize(source);
    bool okay = true;
    for (size_t i = 0; i < tokens.size();) {
        if (tokens[i] != "material") {
            ++i;
            continue;
        }
        if (i + 2 >= tokens.size() || tokens[i + 2] != "{") {
            log::error("RHI renderer: malformed material declaration in '%s'",
                       path.string().c_str());
            return false;
        }
        Material material;
        material.name = tokens[i + 1];
        i += 3;
        int depth = 1;
        std::vector<std::string> body;
        while (i < tokens.size() && depth > 0) {
            if (tokens[i] == "{") {
                ++depth;
            } else if (tokens[i] == "}") {
                --depth;
                if (depth == 0) {
                    ++i;
                    break;
                }
            }
            if (depth > 0)
                body.push_back(tokens[i]);
            ++i;
        }
        if (depth != 0) {
            log::error("RHI renderer: material '%s' in '%s' has unbalanced braces",
                       material.name.c_str(), path.string().c_str());
            return false;
        }

        for (size_t p = 0; p < body.size(); ++p) {
            const std::string& token = body[p];
            if (token == "texture" && p + 1 < body.size() &&
                body[p + 1] != "{") {
                material.textureName = body[++p];
            } else if (token == "filtering" && p + 1 < body.size()) {
                const std::string& mode = body[++p];
                material.filter = (mode == "none" || mode == "point")
                                      ? rhi::FilterMode::Nearest
                                      : rhi::FilterMode::Linear;
            } else if (token == "tex_address_mode" && p + 1 < body.size()) {
                const std::string& mode = body[++p];
                material.address = mode == "clamp"
                                       ? rhi::AddressMode::ClampToEdge
                                       : mode == "mirror"
                                             ? rhi::AddressMode::MirrorRepeat
                                             : rhi::AddressMode::Repeat;
            } else if (token == "fragment_program_ref" && p + 1 < body.size()) {
                // Compile-time variant in Ogre; here the program name selects a
                // runtime mode instead (and the dungeon variant becomes a flag
                // the scene shader encodes into the MRT depth sign).
                material.fragmentProgram = body[++p];
                material.noHighlight =
                    material.fragmentProgram.find("Dungeon") != std::string::npos;
            } else if (token == "cull_hardware" && p + 1 < body.size()) {
                const std::string& mode = body[++p];
                material.cull = mode == "none" ? rhi::CullMode::None
                               : mode == "anticlockwise" ? rhi::CullMode::Front
                                                          : rhi::CullMode::Back;
            } else if (token == "scene_blend" && p + 1 < body.size()) {
                const std::string mode = body[++p];
                material.blend = mode == "alpha_blend"
                                     ? rhi::BlendMode::AlphaBlend
                                     : mode == "add" || mode == "src_alpha"
                                           ? rhi::BlendMode::Additive
                                           : rhi::BlendMode::Opaque;
            } else if (token == "depth_check" && p + 1 < body.size()) {
                material.depthTest = body[++p] != "off";
            } else if (token == "depth_write" && p + 1 < body.size()) {
                material.depthWrite = body[++p] != "off";
            } else if (token == "param_named" && p + 2 < body.size()) {
                const std::string name = body[++p];
                const std::string type = body[++p];
                const int count = componentCount(type);
                if (count == 0 || p + size_t(count) >= body.size()) {
                    log::error("RHI renderer: material '%s' has malformed param '%s'",
                               material.name.c_str(), name.c_str());
                    okay = false;
                    continue;
                }
                std::array<float, 4> values{};
                bool valid = true;
                for (int component = 0; component < count; ++component)
                    valid = parseFloat(body[++p], values[component]) && valid;
                if (!valid) {
                    log::error("RHI renderer: material '%s' param '%s' is not finite numeric data",
                               material.name.c_str(), name.c_str());
                    okay = false;
                } else {
                    material.params[name] = makeValue(values, count);
                }
            }
        }
        uploadTexture(core, material);
        if (material.name.empty()) {
            log::error("RHI renderer: blank material name in '%s'",
                       path.string().c_str());
            okay = false;
        } else {
            mMaterials[material.name] = std::move(material);
        }
    }
    return okay;
}

std::filesystem::path MaterialLibrary::texturePath(const std::string& name) const
{
    if (name.empty())
        return {};
    const std::filesystem::path named(name);
    if (named.is_absolute() && std::filesystem::is_regular_file(named))
        return named;
    if (const std::filesystem::path logical = assets::resolve(name);
        !logical.empty())
        return logical;
    for (const std::filesystem::path& directory : mResourceDirs) {
        const std::filesystem::path candidate = directory / named;
        if (std::filesystem::is_regular_file(candidate))
            return candidate;
    }
    return {};
}

void MaterialLibrary::uploadTexture(RenderCore& core, Material& material)
{
    if (material.textureName.empty()) {
        material.texture = mWhite.valid() ? mWhite : core.fallbackTexture();
        return;
    }
    const std::filesystem::path path = texturePath(material.textureName);
    if (!path.empty())
        material.texture = core.loadTexture(path, material.filter,
                                            material.address);
    if (!material.texture.valid()) {
        log::error("RHI renderer: material '%s' texture '%s' is missing or invalid; using prototype checker",
                   material.name.c_str(), material.textureName.c_str());
        material.texture = core.fallbackTexture();
    }
}

void MaterialLibrary::refreshTextures(RenderCore& core)
{
    mResourceDirs = assets::resourceDirs();
    for (auto& [name, material] : mMaterials)
        uploadTexture(core, material);
}

const Material* MaterialLibrary::find(const std::string& name) const
{
    const auto found = mMaterials.find(name);
    return found == mMaterials.end() ? nullptr : &found->second;
}
Material* MaterialLibrary::find(const std::string& name)
{
    return const_cast<Material*>(std::as_const(*this).find(name));
}

const Material& MaterialLibrary::resolve(const std::string& requested,
                                         const std::string& fallback) const
{
    if (const Material* material = find(requested))
        return *material;
    if (const Material* material = find(fallback))
        return *material;
    if (const Material* material = find(prototype::kSurfaceMaterial))
        return *material;
    mEmergency = {};
    mEmergency.name = prototype::kSurfaceMaterial;
    mEmergency.texture = mWhite;
    return mEmergency;
}

std::vector<std::string> MaterialLibrary::names() const
{
    std::vector<std::string> result;
    result.reserve(mMaterials.size());
    for (const auto& [name, material] : mMaterials) {
        if (!assets::materialInternal(name) &&
            name.find("DebugWireframe") == std::string::npos)
            result.push_back(name);
    }
    std::sort(result.begin(), result.end());
    return result;
}

bool MaterialLibrary::set(const std::string& material,
                          const std::string& parameter, MaterialValue value)
{
    Material* found = find(material);
    if (!found)
        return false;
    found->params[parameter] = std::move(value);
    return true;
}

} // namespace eng::rhi_renderer
