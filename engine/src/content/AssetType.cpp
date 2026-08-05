#include <eng/content/AssetType.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>

namespace eng::content {
namespace {

constexpr size_t kTypeCount = static_cast<size_t>(AssetType::Count);

const std::array<std::string_view, kTypeCount> kNames = {
    "unknown",  "mesh",       "skeleton", "animation", "animation_tree",
    "material", "texture",    "particle", "sound",     "sound_bank",
    "object_template",        "world",    "shader",    "font",
    "script",   "config",     "ui",
};
static_assert(kNames.size() == kTypeCount, "name table must cover every type");

// The diagram, top to bottom. Mesh's source list mirrors assets.toml's
// [formats] mesh entry, which is the set eng::detail::supportedAssimpModel-
// Extensions() reports; the acp_formats test asserts they still agree rather
// than leaving it to a comment.
const std::vector<AssetFormat>& formats()
{
    static const std::vector<AssetFormat> table = {
        {AssetType::Mesh, "mesh", "Mesh Exporter",
         {".glb", ".gltf", ".fbx", ".obj", ".dae", ".stl", ".ply", ".3ds"},
         ".rmesh", false},
        // ozz owns these two rows: gltf2ozz, driven by a *.ozz.json config, is
        // this engine's skeletal hierarchy and animation exporter, and it has
        // been producing .skeleton.ozz and clip_*.ozz since before there was a
        // pipeline to put them in. A second exporter writing a second skeleton
        // format would be a competing animation runtime, not a pipeline stage.
        {AssetType::Skeleton, "skeleton", "gltf2ozz", {}, ".skeleton.ozz", true},
        {AssetType::Animation, "animation", "gltf2ozz", {}, ".ozz", true},
        {AssetType::AnimationTree, "animation_tree", "Animation Tree Exporter",
         {".animtree.toml"}, ".rtree", false},
        // No processor box in the diagram: the DCC arrow reaches Material
        // directly. Exported by copy, conditioned by validation.
        {AssetType::Material, "material", {}, {".mat", ".material"}, ".mat", true},
        {AssetType::Texture, "texture", "Compression",
         {".png", ".tga", ".jpg", ".jpeg", ".bmp"}, ".rtex", false},
        {AssetType::ParticleSystem, "particle", "Particle Exporter",
         {".particles.toml"}, ".rpfx", false},
        {AssetType::Sound, "sound", {}, {".wav", ".ogg", ".mp3", ".flac"},
         {}, true},
        {AssetType::SoundBank, "sound_bank", "Audio Management Tool",
         {".bank.toml"}, ".rbank", false},
        {AssetType::ObjectTemplate, "object_template", "Object Model Editor",
         {".kit.toml"}, ".rtpl", false},
        {AssetType::World, "world", "World Editor", {".scn"}, ".map", false},
        {AssetType::Shader, "shader", {},
         {".glsl", ".vert", ".frag", ".comp", ".spv", ".compositor"}, {}, true},
        {AssetType::Font, "font", {}, {".ttf", ".otf"}, {}, true},
        {AssetType::Script, "script", {}, {".lua"}, {}, true},
        // .ini is the docked ImGui layout the engine writes back into the pack.
        {AssetType::Ui, "ui", {}, {".ini"}, {}, true},
        // .json covers the gltf2ozz configs beside the rigs they export -- the
        // Skel. Hierarchy and Animation Clips rows are driven by them, so a
        // pipeline that did not track one could not tell that a rig's export
        // settings had changed.
        {AssetType::Config, "config", {}, {".toml", ".json"}, {}, true},
    };
    return table;
}

std::string lower(std::string_view text)
{
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(), [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    });
    return result;
}

bool endsWith(std::string_view text, std::string_view suffix)
{
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool underDir(std::string_view logical, std::string_view dir)
{
    return logical.size() > dir.size() &&
           logical.compare(0, dir.size(), dir) == 0 && logical[dir.size()] == '/';
}

// A compound suffix (".particles.toml") beats a plain one (".toml"), and the
// longest match wins, so ".skeleton.ozz" is a Skeleton and a bare ".ozz" is an
// Animation clip. Sorting by suffix length is what removes the need for the
// table to be in a particular order.
struct SuffixMatch {
    AssetType type = AssetType::Unknown;
    size_t length = 0;
    bool intermediate = false;
};

SuffixMatch matchSuffix(std::string_view logical)
{
    SuffixMatch best;
    const auto consider = [&](AssetType type, std::string_view suffix,
                              bool isIntermediate) {
        if (suffix.empty() || !endsWith(logical, suffix))
            return;
        if (suffix.size() < best.length)
            return;
        // A tie means source and intermediate spell the same thing, which is
        // exactly the authoredDirectly rows; keep the source reading so a .mat
        // is not reported as something an exporter produced.
        if (suffix.size() == best.length && isIntermediate)
            return;
        best = {type, suffix.size(), isIntermediate};
    };
    for (const AssetFormat& format : formats()) {
        for (const std::string& extension : format.source)
            consider(format.type, extension, false);
        consider(format.type, format.intermediate, true);
    }
    return best;
}

} // namespace

const std::vector<AssetFormat>& assetFormats() { return formats(); }

const AssetFormat* assetFormat(AssetType type)
{
    for (const AssetFormat& format : formats())
        if (format.type == type)
            return &format;
    return nullptr;
}

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

bool ignoredContentDir(std::string_view firstSegment)
{
    return firstSegment == "source" || firstSegment == "templates" ||
           firstSegment == "schemas" || firstSegment == "baselines";
}

bool ignoredContentFile(std::string_view logicalView)
{
    const std::string logical = lower(logicalView);
    static const std::vector<std::string> kDebris = {
        ".md", ".txt", ".sh", ".py", ".mp4", ".webm", ".gif", ".log",
    };
    for (const std::string& extension : kDebris)
        if (endsWith(logical, extension))
            return true;
    if (logical.find(".autosave.") != std::string::npos)
        return true;
    // A licence file next to the font it covers: no extension at all, and no
    // amount of extension matching will catch it.
    const std::filesystem::path path(logical);
    if (path.extension().empty())
        return true;
    return false;
}

AssetType classifyAsset(std::string_view logicalView)
{
    const std::string logical = lower(logicalView);
    const SuffixMatch match = matchSuffix(logical);

    // The TOML ambiguities, resolved by directory. These are the only places
    // where a plain ".toml" means something more specific than Config, and
    // encoding them here rather than renaming 40 shipped files is the
    // difference between a pipeline that can be adopted and one that cannot.
    if (match.type == AssetType::Config) {
        if (underDir(logical, "particles"))
            return AssetType::ParticleSystem;
        if (underDir(logical, "ui"))
            return AssetType::Ui;
        if (logical == "config/kit.toml")
            return AssetType::ObjectTemplate;
        if (logical == "config/audio.toml")
            return AssetType::SoundBank;
        return AssetType::Config;
    }

    return match.type;
}

AssetStage assetStage(std::string_view logicalView)
{
    const std::string logical = lower(logicalView);
    const SuffixMatch match = matchSuffix(logical);
    return match.intermediate ? AssetStage::Intermediate : AssetStage::Source;
}

std::string intermediatePathFor(std::string_view logicalView)
{
    const std::string logical(logicalView);
    const AssetType type = classifyAsset(logical);
    const AssetFormat* format = assetFormat(type);
    if (!format || format->intermediate.empty() || format->authoredDirectly)
        return {};
    if (assetStage(logical) == AssetStage::Intermediate)
        return {};

    // Strip the matched source suffix rather than std::filesystem's extension:
    // "hall.particles.toml" must become "hall.rpfx", not "hall.particles.rpfx".
    const std::string lowered = lower(logical);
    for (const std::string& extension : format->source) {
        if (endsWith(lowered, extension))
            return logical.substr(0, logical.size() - extension.size()) +
                   std::string(format->intermediate);
    }
    // A directory-classified TOML (config/kit.toml, particles/*.toml) has no
    // matching entry in the source list; fall back to the plain extension.
    const std::filesystem::path path(logical);
    return (path.parent_path() / path.stem()).generic_string() +
           std::string(format->intermediate);
}

} // namespace eng::content
