#pragma once
#include <string>

namespace mapio {
// The asset path a MeshRenderer was built from. The editor sets this when it
// spawns a mesh entity; the serializer persists it; the loader (Plan 3) uses
// it to resolve a runtime MeshHandle. Kept separate so eng::ecs::MeshRenderer
// stays renderer-facing (handle only).
struct MeshSource {
    std::string path;
};
} // namespace mapio
