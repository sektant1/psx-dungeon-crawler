#pragma once

#include <eng/content/MeshData.h>

#include <cstdint>
#include <filesystem>
#include <string>

// `.rmesh` -- a static mesh the game can load without Assimp.
//
// This is the conditioning step figure 1.33 draws as "Mesh Exporter -> Mesh":
// the DCC format is read once, at build time, by the tool that understands it,
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

inline constexpr char kCookedMeshMagic[8] = {'R', 'A', 'V', 'E',
                                             'N', 'M', 'S', 'H'};
inline constexpr uint16_t kCookedMeshVersion = 1;
inline constexpr const char* kCookedMeshExtension = ".rmesh";

// What the importer knew and the geometry alone does not say. Carried so the
// editor's mesh inspector and `model_validate` report the same numbers for a
// cooked mesh as for a freshly imported one, rather than going quiet.
struct CookedMeshInfo {
    std::string sourcePath;  // the logical path this was conditioned from
    std::string format;      // "obj", "glb", ...
    uint64_t sourceBytes = 0;
    uint32_t sourceMeshes = 0;
    uint32_t materials = 0;
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
    // The import settings that produced this file, as the conditioner resolved
    // them. Stored so a mesh loaded from cache can still answer "what scale was
    // this authored at" without re-reading the sidecar.
    float metresPerSourceUnit = 1.0f;
    uint8_t pivot = 0;
    uint8_t texcoordV = 0;
    bool canonicalPivotStandard = false;
};

bool writeCookedMesh(const std::filesystem::path& path, const MeshData& mesh,
                     const CookedMeshInfo& info, std::string& error);

bool readCookedMesh(const std::filesystem::path& path, MeshData& mesh,
                    CookedMeshInfo& info, std::string& error);

// Cheap probe: does this path hold a cooked mesh? The runtime asks before
// paying for a read, and the answer must not be "the extension says so" --
// a stale .rmesh from an older, incompatible version is a file that exists.
bool isCookedMesh(const std::filesystem::path& path);

} // namespace eng::content
