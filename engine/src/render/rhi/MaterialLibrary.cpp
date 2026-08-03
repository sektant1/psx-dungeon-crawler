#include "MaterialLibrary.h"

#include <eng/Log.h>
#include <eng/assets/AssetRoot.h>
#include <eng/render/PrototypeAssets.h>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <algorithm>
#include <array>
#include <string>

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

// Material files are TOML like the rest of the project's data. Values are read
// defensively: a missing or wrongly typed key takes the default rather than
// failing the load, because one bad material must not cost a level all of them.
std::string stringOr(const toml::table& table, const char* key,
                     std::string fallback)
{
    return table[key].value_or(std::move(fallback));
}

rhi::FilterMode filterFromName(const std::string& id)
{
    return id == "linear" ? rhi::FilterMode::Linear : rhi::FilterMode::Nearest;
}

rhi::AddressMode addressFromName(const std::string& id)
{
    if (id == "clamp") return rhi::AddressMode::ClampToEdge;
    if (id == "mirror") return rhi::AddressMode::MirrorRepeat;
    return rhi::AddressMode::Repeat;
}

rhi::CullMode cullFromName(const std::string& id)
{
    if (id == "none") return rhi::CullMode::None;
    if (id == "front") return rhi::CullMode::Front;
    return rhi::CullMode::Back;
}

rhi::BlendMode blendFromName(const std::string& id)
{
    if (id == "alpha") return rhi::BlendMode::AlphaBlend;
    if (id == "additive") return rhi::BlendMode::Additive;
    return rhi::BlendMode::Opaque;
}

// A parameter is one number or a short vector of them, which covers everything
// the shaders take. Anything else is skipped with a warning.
bool readParam(const toml::node& node, MaterialValue& out)
{
    if (const auto scalar = node.value<double>()) {
        out = float(*scalar);
        return true;
    }
    const toml::array* array = node.as_array();
    if (!array)
        return false;
    float values[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const size_t count = array->size();
    if (count < 2 || count > 4)
        return false;
    for (size_t i = 0; i < count; ++i) {
        const auto scalar = (*array)[i].value<double>();
        if (!scalar)
            return false;
        values[i] = float(*scalar);
    }
    if (count == 2) out = glm::vec2(values[0], values[1]);
    else if (count == 3) out = glm::vec3(values[0], values[1], values[2]);
    else out = glm::vec4(values[0], values[1], values[2], values[3]);
    return true;
}

} // namespace

// Three parameters the renderer reads on every draw, so they get accessors
// rather than being looked up by name at each call site. Absent means neutral.
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

MaterialShader materialShaderFromName(const std::string& id, bool& known)
{
    struct Entry { const char* id; MaterialShader shader; };
    static const Entry kShaders[] = {
        {"lit", MaterialShader::Lit},
        {"lit.untextured", MaterialShader::LitUntextured},
        {"lit.metal", MaterialShader::LitMetal},
        {"unlit", MaterialShader::Unlit},
        {"unlit.metal", MaterialShader::UnlitMetal},
        {"unlit.light_volume", MaterialShader::UnlitLightVolume},
        {"surface.liquid", MaterialShader::SurfaceLiquid},
        {"surface.lava", MaterialShader::SurfaceLava},
        {"surface.portal", MaterialShader::SurfacePortal},
        {"particle.textured", MaterialShader::ParticleTextured},
        {"particle.atlas", MaterialShader::ParticleAtlas},
        {"particle.flame", MaterialShader::ParticleFlame},
        {"particle.smoke", MaterialShader::ParticleSmoke},
        {"particle.rain", MaterialShader::ParticleRain},
        {"particle.block", MaterialShader::ParticleBlock},
        {"particle.mote", MaterialShader::ParticleMote},
        {"particle.shard", MaterialShader::ParticleShard},
        {"particle.bubble", MaterialShader::ParticleBubble},
        {"particle.wisp", MaterialShader::ParticleWisp},
        {"particle.voxel", MaterialShader::ParticleVoxel},
        {"sprite", MaterialShader::Other},
        {"wire", MaterialShader::Other},
        {"decal", MaterialShader::Other},
        {"debug_lines", MaterialShader::Other},
        {"editor.checkerboard", MaterialShader::Other},
        {"editor.icon", MaterialShader::Other},
        {"editor.ghost", MaterialShader::Other},
    };
    known = true;
    for (const Entry& entry : kShaders)
        if (id == entry.id)
            return entry.shader;
    if (id.rfind("post.", 0) == 0)
        return MaterialShader::Post;
    known = false;
    return MaterialShader::Lit;
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
            if (it->is_regular_file() && it->path().extension() == ".mat")
                scripts.push_back(it->path());
        }
        error.clear();
    }
    std::sort(scripts.begin(), scripts.end());
    bool okay = true;
    for (const std::filesystem::path& script : scripts)
        okay = loadFile(core, script) && okay;

    if (!find(prototype::kSurfaceMaterial)) {
        Material fallback;
        fallback.name = prototype::kSurfaceMaterial;
        fallback.texture = core.fallbackTexture();
        mMaterials.emplace(fallback.name, std::move(fallback));
    }
    log::info("RHI renderer: %zu materials", mMaterials.size());
    return okay;
}

bool MaterialLibrary::loadFile(RenderCore& core,
                               const std::filesystem::path& path)
{
    return parse(core, path);
}

bool MaterialLibrary::parse(RenderCore& core, const std::filesystem::path& path)
{
    const toml::parse_result parsed = toml::parse_file(path.string());
    if (!parsed) {
        log::error("RHI renderer: cannot parse material file '%s': %s",
                   path.string().c_str(),
                   std::string(parsed.error().description()).c_str());
        return false;
    }
    const toml::table* materials = parsed.table()["material"].as_table();
    if (!materials) {
        log::error("RHI renderer: '%s' declares no [material] table",
                   path.string().c_str());
        return false;
    }

    bool okay = true;
    for (const auto& [key, node] : *materials) {
        const toml::table* entry = node.as_table();
        if (!entry) {
            log::error("RHI renderer: material '%s' is not a table",
                       std::string(key.str()).c_str());
            okay = false;
            continue;
        }
        Material material;
        material.name = std::string(key.str());

        bool knownShader = true;
        material.shader =
            materialShaderFromName(stringOr(*entry, "shader", "lit"),
                                   knownShader);
        if (!knownShader) {
            log::warn("RHI renderer: material '%s' names an unknown shader "
                      "'%s'; drawing it lit",
                      material.name.c_str(),
                      stringOr(*entry, "shader", "lit").c_str());
            okay = false;
        }

        material.textureName = stringOr(*entry, "texture", "");
        material.filter = filterFromName(stringOr(*entry, "filter", "nearest"));
        material.address =
            addressFromName(stringOr(*entry, "address", "repeat"));
        material.cull = cullFromName(stringOr(*entry, "cull", "back"));
        material.blend = blendFromName(stringOr(*entry, "blend", "opaque"));
        material.depthTest = (*entry)["depth_test"].value_or(true);
        material.depthWrite = (*entry)["depth_write"].value_or(true);
        // `highlight = false` is the stone case: outlines and creases stay,
        // the stylize highlight wash does not.
        material.noHighlight = !(*entry)["highlight"].value_or(true);
        material.wireframe = stringOr(*entry, "polygon", "fill") == "line";

        if (const toml::table* params = (*entry)["params"].as_table()) {
            for (const auto& [name, value] : *params) {
                MaterialValue parsedValue;
                if (readParam(value, parsedValue))
                    material.params.emplace(std::string(name.str()),
                                            parsedValue);
                else
                    log::warn("RHI renderer: material '%s' parameter '%s' is "
                              "not a number or a 2-4 element vector",
                              material.name.c_str(),
                              std::string(name.str()).c_str());
            }
        }

        uploadTexture(core, material);
        mMaterials.insert_or_assign(material.name, std::move(material));
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
