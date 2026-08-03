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

struct Material {
    std::string name;
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
    // The fragment_program_ref this pass named. Ogre compiles one program per
    // look; the RHI backend needs the name to pick the equivalent runtime mode
    // (particle variants especially, which are otherwise indistinguishable).
    std::string fragmentProgram;
    std::unordered_map<std::string, MaterialValue> params;

    glm::vec4 modulate() const;
    glm::vec2 uvScale() const;
    glm::vec2 uvOffset() const;
};

class MaterialLibrary {
public:
    bool loadAll(RenderCore& core);
    bool loadScript(RenderCore& core, const std::filesystem::path& path);
    void refreshTextures(RenderCore& core);

    const Material* find(const std::string& name) const;
    Material* find(const std::string& name);
    const Material& resolve(const std::string& requested,
                            const std::string& fallback) const;
    std::vector<std::string> names() const;
    bool set(const std::string& material, const std::string& parameter,
             MaterialValue value);

private:
    bool parse(RenderCore& core, const std::filesystem::path& path,
               const std::string& source);
    void uploadTexture(RenderCore& core, Material& material);
    std::filesystem::path texturePath(const std::string& name) const;

    std::unordered_map<std::string, Material> mMaterials;
    std::vector<std::filesystem::path> mResourceDirs;
    RenderCore::TextureBinding mWhite;
    mutable Material mEmergency;
};

} // namespace eng::rhi_renderer
