#pragma once
#include <eng/Handles.h>

#include <string>

namespace eng::ecs {

// A mesh to draw, attached by SceneSync when the entity first gets a node.
//
// Holds a handle, not a path: resolving an asset is a load-time job, and the
// path it came from lives in MeshSource so this stays renderer-facing. A
// MaterialOverride on the same entity wins over `material`.
struct MeshRenderer {
    MeshHandle mesh;
    std::string material;
    bool castShadows = false;
};

} // namespace eng::ecs
