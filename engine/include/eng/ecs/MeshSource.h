#pragma once
#include <string>

namespace eng::ecs {

// The asset path a MeshRenderer was built from. Editors set this when they
// spawn a mesh entity; scene serialisers persist it; loaders use it to resolve
// a runtime MeshHandle. Kept separate so MeshRenderer stays renderer-facing
// (handle only) and never carries a path into the hot path.
struct MeshSource {
    std::string path;
};

} // namespace eng::ecs
