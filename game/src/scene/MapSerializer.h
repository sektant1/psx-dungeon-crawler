#pragma once

#include "ComponentRegistry.h"

#include <entt/entt.hpp>

#include <string>

namespace mapio {

// Serialize every entity in `reg` (their registered components + parent links)
// to a binary .map file. Returns false on file-open failure.
bool writeMap(const std::string& path, const entt::registry& reg,
              const ComponentRegistry& types);

// Load a .map file into `out` (which should be empty). Components with an
// unknown stableTypeId are skipped. Returns false on open / bad-magic /
// version-too-new / truncated file.
bool readMap(const std::string& path, entt::registry& out,
             const ComponentRegistry& types);

// Human-readable dump of a .map file to stdout. Returns false if unreadable.
bool dumpMap(const std::string& path, const ComponentRegistry& types);

} // namespace mapio
