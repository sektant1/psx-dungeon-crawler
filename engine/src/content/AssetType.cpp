#include <eng/content/AssetType.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>

namespace eng::content {
namespace {

constexpr size_t kTypeCount = static_cast<size_t>(AssetType::Count);

const std::array<std::string_view, kTypeCount> kNames = {
    "unknown", "mesh",   "skeleton", "animation", "material",
    "texture", "particle", "sound",  "sound_bank", "object_template",
    "world",   "shader", "font",     "script",    "config",
    "ui",
};
static_assert(kNames.size() == kTypeCount, "name table must cover every type");

// The extension tables. Mesh mirrors assets.toml's [formats] mesh list, which
// is the same set eng::detail::supportedAssimpModelExtensions() reports; they
// are kept in step by the acp_types test rather than by a comment.
struct TypeExtensions {
    AssetType type;
    std::vector<std::string> extensions;
};

const std::vector<TypeExtensions>& extensionTable()
{
    static const std::vector<TypeExtensions> table = {
        {AssetType::Mesh,
         {".glb", ".gltf", ".fbx", ".obj", ".dae", ".stl", ".ply", ".3ds"}},
        {AssetType::Texture, {".png", ".jpg", ".jpeg", ".tga", ".bmp", ".dds"}},
        {AssetType::Material, {".mat", ".material"}},
        {AssetType::World, {".scn"}},
        {AssetType::Script, {".lua"}},
        {AssetType::Shader, {".glsl", ".vert", ".frag", ".comp", ".spv"}},
        {AssetType::Font, {".ttf", ".otf"}},
        {AssetType::Sound, {".wav", ".ogg", ".mp3", ".flac"}},
        {AssetType::Config, {".toml"}},
    };
    return table;
}

const std::vector<std::string>& emptyExtensions()
{
    static const std::vector<std::string> empty;
    return empty;
}

std::string lower(std::string_view text)
{
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(), [](char c) {
        return static_cast<char>(
            std::tolower(static_cast<unsigned char>(c)));
    });
    return result;
}

bool startsWithDir(std::string_view logical, std::string_view dir)
{
    return logical.size() > dir.size() && logical.compare(0, dir.size(), dir) == 0 &&
           (logical[dir.size()] == '/' || logical[dir.size()] == '\\');
}

} // namespace

std::string_view assetTypeName(AssetType type)
{
    const auto index = static_cast<size_t>(type);
    return index < kTypeCount ? kNames[index] : kNames[0];
}

AssetType assetTypeFromName(std::string_view name)
{
    for (size_t i = 0; i < kTypeCount; ++i)
        if (kNames[i] == name)
            return static_cast<AssetType>(i);
    return AssetType::Unknown;
}

const std::vector<std::string>& assetTypeExtensions(AssetType type)
{
    for (const TypeExtensions& entry : extensionTable())
        if (entry.type == type)
            return entry.extensions;
    return emptyExtensions();
}

bool ignoredContentDir(std::string_view firstSegment)
{
    return firstSegment == "source" || firstSegment == "templates" ||
           firstSegment == "schemas" || firstSegment == "baselines";
}

AssetType classifyAsset(std::string_view logicalView)
{
    const std::string logical = lower(logicalView);
    const std::filesystem::path path(logical);
    const std::string extension = path.extension().string();

    AssetType byExtension = AssetType::Unknown;
    for (const TypeExtensions& entry : extensionTable()) {
        const auto& list = entry.extensions;
        if (std::find(list.begin(), list.end(), extension) != list.end()) {
            byExtension = entry.type;
            break;
        }
    }

    // The two TOML ambiguities. Everything else under config/ stays Config:
    // enemies.toml and weapons.toml really are "the game reads this by name",
    // and inventing a type per file would put a classification decision in the
    // scanner that belongs in the file's own .meta.
    if (byExtension == AssetType::Config) {
        if (startsWithDir(logical, "particles"))
            return AssetType::ParticleSystem;
        if (startsWithDir(logical, "ui"))
            return AssetType::Ui;
        if (logical == "config/kit.toml")
            return AssetType::ObjectTemplate;
        if (logical == "config/audio.toml")
            return AssetType::SoundBank;
        return AssetType::Config;
    }

    if (byExtension != AssetType::Unknown)
        return byExtension;

    // No extension match. `.compositor` and the handful of other bespoke
    // suffixes are Unknown on purpose: an Unknown record is still scanned,
    // hashed and tracked for dependency purposes, it simply has no conditioner,
    // which is the honest answer for a file whose format nothing here parses.
    return AssetType::Unknown;
}

} // namespace eng::content
