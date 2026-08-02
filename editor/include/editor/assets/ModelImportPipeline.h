#pragma once

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace ed {

// Turning a source model into kit pieces the editor can place.
//
// This used to be tools/editor_import_glb.py, driven through std::system: a
// second GLTF parser, in a second language, that accepted exactly one format.
// The engine already reads every format its Assimp build supports and bakes
// them to one canonical orientation and pivot (eng::detail::importStaticModel),
// so the parser was not only duplicated, it was the narrower of the two.
//
// What remains here is the part Assimp does not do: writing the pack's own OBJ
// parts, a material script, and the kit.toml entries that make a mesh
// placeable. Those are this project's conventions, not an interchange concern.
//
// Textures are deliberately NOT extracted. The engine's loader treats embedded
// textures as metadata only, and a converter that silently invents a material
// from a texture it half-understood is how a model arrives looking wrong with
// no indication why. Referenced texture names come back in `textures` instead,
// for the author to bind in the Material panel.

struct ImportedPart {
    std::string prefab;   // "kit.import_foo" or "kit.import_foo_p3"
    std::string name;     // the submesh's own name, for the outliner
    glm::vec3 offset{0.0f}; // from the model's pivot, so parts reassemble
};

struct ModelImportResult {
    bool ok = false;
    std::string error;
    // Written only when a part needed a material of its own. Empty means every
    // part used a stock engine material and nothing needs loading.
    std::string materialScript;
    std::vector<ImportedPart> parts;
    // Source material and texture names the model referenced. Surfaced so the
    // author knows what still has to be bound rather than discovering it by
    // looking at an untextured prop.
    std::vector<std::string> textures;
    std::vector<std::string> warnings;
};

// Reads `sourcePath` through the engine's importer and writes into `assetRoot`:
//
//   meshes/props/<id>.obj      one per submesh
//   materials/<prefix>.material  only if a part needed its own material
//   config/kit.toml            a marked block, replaced wholesale on reimport
//
// The kit block is delimited by "# BEGIN editor import: <slug>" so importing
// the same model twice replaces its pieces instead of accumulating them.
ModelImportResult importModelToKit(const std::string& sourcePath,
                                   const std::string& assetRoot);

// The id stem a source file maps to: lowercase, non-alphanumerics collapsed to
// underscores. Exposed for the tests, and because the caller shows it.
std::string modelSlug(const std::string& sourcePath);

// Image formats this build can actually load: OGRE's STBI codec plus its
// built-in DDS one (FreeImage is off -- see cmake/Dependencies.cmake).
//
// Copying a texture the renderer cannot read is worse than not finding one: the
// prop renders untextured either way, but the material claims a file that
// exists, so nothing points at the cause.
const std::vector<std::string>& textureExtensions();

} // namespace ed
