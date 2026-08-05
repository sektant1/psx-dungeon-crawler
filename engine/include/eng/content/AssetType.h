#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Figure 1.33 of Game Engine Architecture (4th ed.), as a table this engine can
// execute.
//
// The diagram has three columns and this header is the middle one. On the left
// are DCC tools and the files they write; in the middle, one intermediate
// asset per row -- Mesh, Skel. Hierarchy, Animation Clips, Animation Tree,
// Material, DXT Texture, Particle System, Sound Bank, Game Obj. Templates, Game
// World -- each produced by a named exporter; on the right, the Asset
// Conditioning Pipeline consumes every intermediate plus the Resource DB and
// hands one package to the game.
//
// The engine already had most of the left column and a third of the middle one,
// but nowhere that said so. `.obj` was "a mesh" in five places with five
// different extension lists; `.scn -> .map` was a pipeline of one; the ozz
// skeleton and clips beside it were not modelled as pipeline output at all.
// Naming the whole table in one place is what lets the exporters, the ACP, the
// resource database and the editor's browser agree without consulting each
// other.
//
//   AssetStage::Source        what a DCC writes. Never loaded by the game.
//   AssetStage::Intermediate  what an exporter writes. What the ACP consumes,
//                             what the resource database has a record for, and
//                             -- for the formats that need no further work --
//                             what the game ends up loading.
//
// Classification is by extension first and directory second: in this tree the
// extension is nearly always decisive, and the ambiguous cases are all TOML
// (a particle description under particles/, a template under config/). Nothing
// here opens a file, so a path that does not exist yet still classifies, which
// is what the editor asks when it invents an output name.
namespace eng::content {

enum class AssetType : uint8_t {
    Unknown = 0,
    Mesh,             // Mesh Exporter        -> .rmesh
    Skeleton,         // Skel. Hier. Exporter -> .skeleton.ozz
    Animation,        // Animation Exporter   -> .ozz
    AnimationTree,    // Animation Tree Editor-> .rtree
    Material,         // authored             -> .mat
    Texture,          // Compression          -> .rtex
    ParticleSystem,   // Particle Exporter    -> .rpfx
    Sound,            // Sound Forge etc.     -> .wav  (a bank member)
    SoundBank,        // Audio Mgmt Tool      -> .rbank
    ObjectTemplate,   // Object Model Editor  -> .rtpl
    World,            // World Editor         -> .map
    Shader,           // no exporter row: GLSL is compiled by the RHI
    Font,
    Script,
    Config,
    Ui,
    Count
};

// Which column of the diagram a file sits in.
enum class AssetStage : uint8_t {
    Source,       // left column: DCC output, an exporter's input
    Intermediate, // middle column: an exporter's output, the ACP's input
};

// One row of the diagram.
struct AssetFormat {
    AssetType type = AssetType::Unknown;
    std::string_view name;          // stable id: on disk in .meta and manifests
    std::string_view exporter;      // the diagram's processor box, "" if none
    std::vector<std::string> source;       // DCC-side extensions
    std::string_view intermediate;  // the exporter's output extension
    // True when the source form IS the intermediate form -- the diagram's
    // Material row, which comes straight off the DCC arrow with no processor
    // box between it and the pipeline. Such an asset is exported by copying,
    // and the ACP's job for it is validation and dependency tracking.
    bool authoredDirectly = false;
};

const std::vector<AssetFormat>& assetFormats();
const AssetFormat* assetFormat(AssetType type);

// Stable, lowercase, safe in a filename and in a TOML key. Written into .meta
// sidecars and the manifest, so these strings are part of the on-disk format.
std::string_view assetTypeName(AssetType type);
AssetType assetTypeFromName(std::string_view name);

// The type a path holds, and which column it is in. `logical` is a
// content-root-relative path with forward slashes.
AssetType classifyAsset(std::string_view logical);
AssetStage assetStage(std::string_view logical);

// The intermediate file an exported source lands at, content-root-relative:
//   "meshes/props/lamp.obj"  -> "meshes/props/lamp.rmesh"
//   "textures/wall.png"      -> "textures/wall.rtex"
//   "scenes/start_hall.scn"  -> "scenes/start_hall.map"
// Empty when the type has no exporter, or when the path is already an
// intermediate -- an exporter that consumed its own output would loop.
std::string intermediatePathFor(std::string_view logical);

// Directories under the content root that are pipeline INPUT or authoring
// debris, never assets in their own right. Skipped wholesale by the scanner
// rather than filling the database with records nothing will ever build.
bool ignoredContentDir(std::string_view firstSegment);

// Files that live in the content tree without being content: documentation,
// licence texts, authoring scripts, reference footage, and the editor's
// `.autosave.scn` scratch. Skipped by the scanner rather than recorded as
// Unknown -- a pipeline that reports eleven assets it cannot handle on every
// run is one whose output people stop reading.
bool ignoredContentFile(std::string_view logical);

} // namespace eng::content
