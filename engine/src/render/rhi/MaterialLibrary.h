#pragma once

#include "RenderCore.h"

#include <glm/glm.hpp>

#include <filesystem>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace eng::rhi_renderer {

using MaterialValue =
    std::variant<float, glm::vec2, glm::vec3, glm::vec4>;

// Which shader family draws this material. Named outright in the material file
// rather than recovered by substring-matching a program name, which is what the
// Ogre-era format forced: a renamed program silently changed how a material
// drew, and a new material had to be *named* correctly to be routed at all.
enum class MaterialShader {
    Lit,            // the default surface: textured, lit, fogged
    LitUntextured,  // vertex colour and tint only
    LitMetal,       // view-space matcap
    Unlit,
    UnlitMetal,
    UnlitLightVolume,
    SurfaceLiquid,
    SurfaceLava,
    SurfacePortal,
    ParticleTextured,
    ParticleAtlas,
    ParticleFlame,
    ParticleSmoke,
    ParticleRain,
    ParticleBlock,
    ParticleMote,
    ParticleShard,
    ParticleBubble,
    ParticleWisp,
    ParticleVoxel,
    // Compositor tuning carriers. The passes are fixed in the renderer; these
    // only hold the numbers the palette drives them with.
    Post,
    // Not drawn through the scene pipeline: sprites, wire, decals, editor gizmos.
    Other,
};

// Text id -> family, for the material parser. Unknown ids fall back to Lit and
// are reported, so a typo shows up at load rather than as a wrongly drawn mesh.
MaterialShader materialShaderFromName(const std::string& id, bool& known);
// The family's canonical id, for tools that display which shader draws a
// material. Not an exact inverse: several ids share MaterialShader::Other.
const char* materialShaderId(MaterialShader shader);

struct Material {
    std::string name;
    MaterialShader shader = MaterialShader::Lit;
    std::string textureName;
    RenderCore::TextureBinding texture;
    rhi::FilterMode filter = rhi::FilterMode::Nearest;
    rhi::AddressMode address = rhi::AddressMode::Repeat;
    rhi::CullMode cull = rhi::CullMode::Back;
    rhi::BlendMode blend = rhi::BlendMode::Opaque;
    bool depthTest = true;
    bool depthWrite = true;
    // The legacy PSX_FS_Dungeon variant (DUNGEON_NO_HIGHLIGHT): keeps outlines
    // and creases, but suppresses the stylize highlight wash over stone.
    bool noHighlight = false;
    // Line-filled polygons, for the wireframe debug material.
    bool wireframe = false;
    std::unordered_map<std::string, MaterialValue> params;

    glm::vec4 modulate() const;
    glm::vec2 uvScale() const;
    glm::vec2 uvOffset() const;
};

class MaterialLibrary {
public:
    bool loadAll(RenderCore& core);
    bool loadFile(RenderCore& core, const std::filesystem::path& path);
    void refreshTextures(RenderCore& core);

    const Material* find(const std::string& name) const;
    Material* find(const std::string& name);
    const Material& resolve(const std::string& requested,
                            const std::string& fallback) const;
    std::vector<std::string> names() const;
    bool set(const std::string& material, const std::string& parameter,
             MaterialValue value);

    // Take ownership of a material built in code rather than parsed from a
    // script, resolving its texture on the way in. The one caller is the sprite
    // seam (Renderer::createSpriteMaterial), which turns a SpriteClip into the
    // material an arbitrary mesh can wear -- there is no file to parse for
    // something assembled from a struct at runtime.
    const Material& adopt(RenderCore& core, Material material);

    // Where a texture *name* -- logical id, relative path or absolute file --
    // actually lives. Public because sprites resolve their own texture the same
    // way materials do, and neither should reimplement the search order.
    std::filesystem::path texturePath(const std::string& name) const;

    // Re-resolve and re-upload one material's texture after its `textureName`
    // has been changed in place. Public because retexturing is a supported
    // operation now (Renderer::setMaterialTexture) rather than something only
    // the parser does.
    void refreshTexture(RenderCore& core, Material& material)
    {
        uploadTexture(core, material);
    }

private:
    bool parse(RenderCore& core, const std::filesystem::path& path);
    void uploadTexture(RenderCore& core, Material& material);

    std::unordered_map<std::string, Material> mMaterials;
    std::vector<std::filesystem::path> mResourceDirs;
    RenderCore::TextureBinding mWhite;
    mutable Material mEmergency;
};

} // namespace eng::rhi_renderer
