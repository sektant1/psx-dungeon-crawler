#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// The content root: one place every app, tool and test looks for shipped data.
//
// Before this, four asset trees were found through ~20 compile-time macros
// (ENG_ASSET_DIR, APP_ASSET_DIR, DEMO_SCENE_TOML, ...) baking absolute source
// paths into the binaries, and every call site concatenated its own strings on
// top -- including one that climbed back *out* of an asset root to reach the
// project directory. That made the build unrelocatable and left "where does
// this file live" answered differently in each caller.
//
// Here the root is discovered at runtime (D5), a manifest declares the packs
// (D6), and a logical path is resolved against an ordered mount list (D7).
// Callers ask `resolve("config/enemies.toml")` and stop knowing about roots.
//
// This header is a leaf in eng_core: <filesystem> and the TOML reader, nothing
// above. Do not pull it into eng/Engine.h's public includes -- a widely
// included header here costs a full rebuild.
namespace eng::assets {

// One mounted content pack. `dir` is absolute; `resources` are the subdirs of
// it that are registered with Ogre's flat resource group -- anything not
// listed is data read by path, which is what keeps authoring debris out of the
// group.
struct Pack {
    std::string id;
    std::filesystem::path dir;
    std::vector<std::string> resources;
};

// Discovers the content root and parses <root>/assets.toml. Called once by
// Engine::init; tools and tests call it directly. Re-callable: a second call
// replaces the manifest and drops any mounts.
//
// Discovery order, first candidate *containing assets.toml* wins:
//   1. `rootOverride`, when non-empty -- and only that one, no fallback
//   2. $PSX_ASSET_ROOT
//   3. <exe>/../share/psx-dungeon/assets   (an installed build)
//   4. <exe>/assets                        (a portable build)
//   5. PSX_ASSET_ROOT_DEV                  (the source tree, set by CMake)
//
// Returns false and logs on a missing or malformed manifest. Never throws.
bool init(const std::string& rootOverride = {});

// True once init() has succeeded. Everything below is safe to call either way;
// this exists so a caller can tell "nothing mounted" from "never initialised".
bool ready();

// .../assets -- the directory holding assets.toml.
const std::filesystem::path& root();

// The repo root in a dev build, the install prefix otherwise. Exists so that
// writing next to the project (playtest.log, editor scratch files) is a stated
// operation rather than a "/../.." climb out of an asset path.
const std::filesystem::path& project();

// Mount a named set from the manifest's [mounts], e.g. "editor". The set's own
// order is the priority, highest first. Replaces any previous mount; it does
// not accumulate. Fails, and mounts nothing at all, if the set is unknown or
// names a pack the manifest does not declare.
bool mount(const std::string& mountSet);
const std::vector<Pack>& mounted();

// Every pack the manifest declares, whether or not it is mounted.
const std::vector<Pack>& packs();

// "config/enemies.toml" -> the first mounted pack that has it.
//
// A pack-qualified path -- one whose first segment is a mounted pack id, as in
// "game/config/enemies.toml" -- pins that pack and does not fall through, for
// the cases that must reach past a higher-priority override.
//
// Returns an empty path when unresolved, and for any path that would escape
// its pack. Never throws, never asserts: an unresolved asset is a content bug
// to report, not a crash.
std::filesystem::path resolve(std::string_view logical);
bool exists(std::string_view logical);

// The absolute dir of one mounted pack, empty when that pack is not mounted.
//
// This is deliberately NOT resolve()'s job. resolve() answers "where is this
// file", by looking for one that exists; a caller that means to *create* a
// file has nothing to look for yet, and got an empty path for its trouble.
// That is why ENG_ASSET_DIR outlived P1 in ImGuiLayout, which writes the
// docked layout back into the engine pack. Naming the pack is the honest form
// of that question -- and it stays honest after the trees move, which a macro
// baking an absolute source path does not.
//
// Prefer resolve() for anything that only reads.
std::filesystem::path packDir(std::string_view id);

// Materials the manifest's [materials] `internal` list hides from
// Renderer::materialNames() -- the editor's material picker and the debug
// swatch list.
//
// This replaces a naming convention that had quietly become a mechanism: a
// name beginning "__" was dropped from the list, and so was anything under
// "Sprite/". Both are real materials -- warmed up, shader-checked, bindable by
// exact name -- they are simply not things a user should be able to assign to
// world geometry (a drag-preview ghost, the material-preview rig's staging
// sprite, the four sprite templates the renderer clones per clip). Encoding
// that in the *name* meant the derived naming rule (D14) could not be applied
// without changing behaviour, and it meant a prefix could not be renamed at
// all. It is a property of the material, so it is declared with the material.
//
// This is a whole-list query, not a prefix test: the list is short and the
// membership is exact, so a rename cannot silently change visibility.
//
// Both are safe before init() -- nothing is internal until a manifest says so.
const std::vector<std::string>& internalMaterials();
bool materialInternal(std::string_view name);

// Every `resources` dir of every mounted pack, in mount priority order, then
// in the pack's declared order. Dirs that do not exist are dropped with a
// warning. This is exactly what the renderer registers -- and nothing else,
// which is the point: the old code walked both roots recursively and picked up
// whatever authoring folder happened to be sitting there.
std::vector<std::filesystem::path> resourceDirs();

} // namespace eng::assets
