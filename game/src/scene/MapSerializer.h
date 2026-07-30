#pragma once

#include "ComponentRegistry.h"

#include <entt/entt.hpp>

#include <string>

namespace mapio {

// Serialize every entity in `reg` (their registered components + parent links)
// to a deterministic binary runtime .map. Writes through a temporary file and
// atomically replaces the destination only after the full write succeeds.
bool writeMap(const std::string& path, const entt::registry& reg,
              const eng::ecs::ComponentRegistry& types);

// Transactionally load a .map into `out`. Components with an unknown
// stableTypeId are skipped. On open, version, hierarchy, bounds, or corruption
// failure, `out` is unchanged.
bool readMap(const std::string& path, entt::registry& out,
             const eng::ecs::ComponentRegistry& types);

// Human-readable dump of a .map file to stdout. Returns false if unreadable.
bool dumpMap(const std::string& path, const eng::ecs::ComponentRegistry& types);

} // namespace mapio
