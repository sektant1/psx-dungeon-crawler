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
//   2. $RAVEN_ASSET_ROOT
//   3. <exe>/../share/raven-engine/assets  (an installed build)
//   4. <exe>/assets                        (a portable build)
//   5. RAVEN_ASSET_ROOT_DEV                (the source tree, set by CMake)
//
// Returns false and logs on a missing or malformed manifest. Never throws.
bool init(const std::string& rootOverride = {});

// The directory holding the running executable.
//
// Public because a shipped build has to be able to find what was shipped
// beside it -- raven_player with no argument looks for a `project/` there, so
// double-clicking an exported game plays it. Everything discovery does is
// relative to this, which is what makes an installed build possible: no
// absolute source path survives into the binary except the dev fallback.
//
// Empty when it cannot be determined, which no supported platform does.
std::filesystem::path exeDirectory();

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

// Overlay a project's own content on top of what is already mounted.
//
// This is the one place the content model admits a second root. Everything
// above assumes exactly one tree, discovered once, because for this game there
// is exactly one -- but a project made in the editor lives wherever its author
// put it, and it cannot render from there alone: it has no shaders, no
// compositors, no fonts and no default materials, and shipping a copy of all
// of them into every new project would be worse than the coupling.
//
// So a project is mounted OVER the engine's own content rather than instead of
// it. `projectDir` must contain an assets.toml declaring [[pack]] entries;
// [mounts] is not read, because a project's packs are all of them, in
// declaration order. They go in at the highest priority, so first-hit-wins --
// already the rule resolve() follows -- means a file a project ships shadows
// the builtin of the same logical path, and overriding a default material is
// therefore a matter of putting a file in the right place.
//
// Accumulates, unlike mount(): calling it does not disturb what is already
// mounted. Fails and mounts nothing on a missing or malformed manifest, or on
// a pack id that collides with one already declared -- a silent shadow there
// would make "which pack did this come from" unanswerable.
bool mountProject(const std::filesystem::path& projectDir);

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

// --- the conditioned pack ---------------------------------------------------
//
// What `raven_acp build` produced: a directory of exported intermediates and a
// pack.manifest indexing them. Mounting one does NOT change resolve() -- a
// caller asking for "meshes/props/lamp.obj" still gets the .obj, because that
// is what the string says and half the engine does arithmetic on the answer.
//
// Instead a loader that can read the conditioned form asks for it BY NAME:
//
//     if (auto rmesh = assets::conditioned(path); !rmesh.empty())
//         ... read the .rmesh ...
//     else
//         ... run Assimp ...
//
// which keeps "is there a faster form of this asset?" an explicit question with
// a visible fallback, rather than a path that silently changes extension under
// a caller that was not written for it.
//
// Discovery, first hit wins: `dirOverride`, then $RAVEN_COOKED_DIR, then
// <project>/build/cooked. Absent or unreadable is not an error -- a source tree
// with no pack is the normal state during development, and everything falls
// back to the source loaders.
bool mountCooked(const std::string& dirOverride = {});
bool cookedMounted();
const std::filesystem::path& cookedDir();

// The conditioned output for a content asset, or an empty path. Takes a
// logical path ("meshes/props/lamp.obj") or an absolute one under the content
// root -- loaders hold the latter and should not have to reverse it, and one
// function rather than two overloads because a std::string argument would be
// ambiguous between them.
std::filesystem::path conditioned(const std::filesystem::path& asset);

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
