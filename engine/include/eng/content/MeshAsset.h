#pragma once

#include <eng/content/MeshData.h>

#include <cstdint>
#include <filesystem>
#include <string>

// `.rmesh` -- a static mesh the game can load without Assimp.
//
// The Mesh row of figure 1.33: "Maya/3ds Max/Blender -> Mesh Exporter -> Mesh".
// The DCC format is read once, at build time, by the tool that understands it,
// and the runtime gets a file whose bytes are already the shape it wants.
// Before this the game ran the full Assimp import -- parse, triangulate, weld,
// tangent-generate, drop degenerates -- on every launch, for every prop.
//
// Deliberately NOT compressed and NOT re-ordered for the GPU. The one job here
// is removing the source parser from the runtime; a vertex-cache optimiser or a
// quantised vertex format would change the rendered image, and the image is
// frozen. Both are honest later versions of this format, which is why the
// version is in the header.
namespace eng::content {

inline constexpr char kMeshAssetMagic[8] = {'R', 'A', 'V', 'E',
                                             'N', 'M', 'S', 'H'};
inline constexpr uint16_t kMeshAssetVersion = 1;
inline constexpr const char* kMeshAssetExtension = ".rmesh";

// What the importer knew and the geometry alone does not say. Carried so the
// editor's mesh inspector and `model_validate` report the same numbers for a
// cooked mesh as for a freshly imported one, rather than going quiet.
struct MeshAssetInfo {
    std::string sourcePath;  // the logical path this was conditioned from
    std::string format;      // "obj", "glb", ...
    uint64_t sourceBytes = 0;
    uint32_t sourceMeshes = 0;
    uint32_t materials = 0;
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
    // The import settings that produced this file, as the exporter resolved
    // them. Every field of eng::ModelImportOptions that changes the vertices,
    // and all of them for a reason: geometry is baked, so a caller that wanted
    // different settings must NOT be handed this file. loadStaticModel()
    // compares these against what the call site asked for and falls back to the
    // source importer when they disagree -- a conditioned mesh with the wrong
    // pivot is the whole level moving, silently.
    float metresPerSourceUnit = 1.0f;
    glm::vec4 sourceOrientation{1.0f, 0.0f, 0.0f, 0.0f}; // w, x, y, z
    glm::vec3 customPivot{0.0f};
    uint8_t pivot = 0;
    uint8_t texcoordV = 0;
    bool canonicalPivotStandard = false;
};

bool writeMeshAsset(const std::filesystem::path& path, const MeshData& mesh,
                     const MeshAssetInfo& info, std::string& error);

bool readMeshAsset(const std::filesystem::path& path, MeshData& mesh,
                    MeshAssetInfo& info, std::string& error);

// Cheap probe: does this path hold an exported mesh? The runtime asks before
// paying for a read, and the answer must not be "the extension says so" --
// a stale .rmesh from an older, incompatible version is a file that exists.
bool isMeshAsset(const std::filesystem::path& path);

} // namespace eng::content
