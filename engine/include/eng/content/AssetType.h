#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// What kind of thing an asset is -- the top row of Game Engine Architecture's
// figure 1.33, named once so the resource database, the conditioning pipeline
// and the editor's browser all agree.
//
// The engine already knew these categories, but only implicitly and only in
// scattered extension lists: assets.toml's [formats] knows mesh and texture,
// assetlint.py knows materials, the editor's MeshCatalog knows geometry, the
// cooker knows scenes. A record in the resource database has to name its type
// before any of that runs, so the classification moves here and the others
// become consumers.
//
// Classification is by extension first and directory second, because in this
// tree the extension is nearly always decisive (.obj is geometry, .mat is a
// material) and the two ambiguous cases are both TOML: a particle description
// under particles/ and a game-object template under config/. Nothing here
// opens the file -- a classifier that reads content cannot answer for a path
// that does not exist yet, which is what the editor asks when it invents an
// output name.
namespace eng::content {

enum class AssetType : uint8_t {
    Unknown = 0,
    Mesh,           // .obj/.glb/.gltf/.fbx/... -- geometry, DCC-authored
    Skeleton,       // joint hierarchy, exported beside a skinned mesh
    Animation,      // one clip
    Material,       // .mat scripts
    Texture,        // .png
    ParticleSystem, // particles/*.toml
    Sound,          // one clip
    SoundBank,      // audio.toml: the bank that groups clips
    ObjectTemplate, // config/kit.toml prefabs -- "Game Obj. Templates"
    World,          // .scn -> .map
    Shader,         // .glsl/.vert/.frag
    Font,
    Script,  // .lua
    Config,  // any other TOML the game reads by name
    Ui,      // ui/*.toml
    Count
};

// Stable, lowercase, safe in a filename and in a TOML key. These strings are
// written into .meta sidecars and the runtime manifest, so they are part of the
// on-disk format: rename one and every checked-in sidecar becomes wrong.
std::string_view assetTypeName(AssetType type);
AssetType assetTypeFromName(std::string_view name);

// The type a path holds, by extension then by the directory it sits under.
// `logical` is a content-root-relative path ("meshes/props/lamp.obj"); an
// absolute path works too, but only the parts under the root are consulted.
AssetType classifyAsset(std::string_view logical);

// Extensions this build recognises for a type, leading dot, lowercase. Empty
// for types that have no file form of their own (Skeleton and Animation arrive
// inside a mesh file, and the conditioner splits them out).
const std::vector<std::string>& assetTypeExtensions(AssetType type);

// Directories under the content root that are pipeline INPUT or authoring
// debris, never assets in their own right: source/ (archives, .blend), and
// templates/. The scanner skips them wholesale rather than producing a database
// full of records nothing will ever cook.
bool ignoredContentDir(std::string_view firstSegment);

} // namespace eng::content
