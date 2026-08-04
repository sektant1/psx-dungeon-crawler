#pragma once

#include <eng/ecs/components/PrimitiveMesh.h>

#include <string>
#include <string_view>
#include <vector>

namespace ed {

// Every mesh file in the project, as a browsable list.
//
// The kit catalogue (kit.toml) answers "what pieces does this level's
// vocabulary have"; this answers "what geometry exists at all". They are
// different questions with different answers: the kit is forty authored pieces
// with sockets and spans, and the tree under assets/meshes is two hundred files
// -- props, tiles, imported models, the debris of every import ever run. A
// mesh that is not in kit.toml was previously unplaceable, which meant the only
// way to put an imported model in a level was to write a kit entry describing
// it as architecture.
//
// Nothing here parses geometry. Reading two hundred OBJ files to fill a list
// would cost seconds at startup and answer a question the browser does not ask;
// the renderer is asked for a mesh's triangle count and bounds when one is
// actually previewed.
struct MeshAsset {
    // Pack-relative, which is what MeshSource carries and what the resolver
    // takes: "meshes/kit/Barrel.obj".
    std::string path;
    // The file's stem, for the row: "Barrel".
    std::string name;
    // The directory below `meshes/`, for the grouping header: "kit", "props",
    // "" for a file sitting directly in meshes/.
    std::string group;
    std::string extension; // ".obj", lowercased
    // The kit piece that uses this file, when one does. A mesh already in the
    // kit is nearly always better placed as its prefab -- it comes with the
    // right material, socket and grid snapping -- so the browser says so rather
    // than silently offering the worse of two routes.
    std::string kitPrefab;
    // What kit.toml says this piece wears, or empty. The browser's preview has
    // to dress the mesh in something, and a wall previewed in the default
    // material tells you about the material.
    std::string material;
    unsigned long long sizeBytes = 0;
};

// The mesh tree, sorted by group then name.
//
// `meshDir` is a resolved absolute directory (eng::assets::resolve("meshes"));
// an empty or missing one yields an empty catalogue rather than an error,
// because an editor with no meshes is still an editor.
class MeshCatalog
{
public:
    // `extensions` is what the renderer says it can load
    // (eng::Renderer::supportedModelExtensions), lowercased with the dot.
    // Passed in rather than hardcoded so a format the importer gains appears
    // here without a second list to update.
    void load(const std::string& meshDir,
              const std::vector<std::string>& extensions);
    // Cross-references the kit, so a row can say "this is kit.wall". Safe to
    // call before or after load(); the catalogue re-applies it either way.
    void annotate(const std::vector<std::pair<std::string, std::string>>&
                      pathToPrefab,
                  const std::vector<std::pair<std::string, std::string>>&
                      pathToMaterial);

    const std::vector<MeshAsset>& all() const { return mAssets; }
    const MeshAsset* find(std::string_view path) const;
    // Group names in list order, first-seen.
    std::vector<std::string> groups() const;
    bool loaded() const { return mLoaded; }

private:
    std::vector<MeshAsset> mAssets;
    bool mLoaded = false;
};

// The primitives, as a browsable list beside the files.
//
// A generated box is a mesh an author picks exactly as they pick a file, so it
// belongs in the same panel; what differs is that there is no file to scan, so
// the list is this table. Each entry carries the parameters that make that kind
// read as itself at a useful default size -- a plane is a metre square, a
// capsule is roughly person-shaped -- because a browser whose every entry is
// the unit box teaches nothing about what the kinds are.
struct PrimitivePreset {
    const char* id;    // "sphere", matching eng::ecs::primitiveKindName
    const char* label; // "Sphere"
    const char* hint;  // one line, what it is for
    eng::ecs::PrimitiveMesh mesh;
};

const std::vector<PrimitivePreset>& primitivePresets();

} // namespace ed
