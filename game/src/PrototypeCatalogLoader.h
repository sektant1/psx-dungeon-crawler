#pragma once

#include <eng/render/PrototypeAssets.h>

#include <string>

namespace game {

// Reads assets/prototypes.toml into the catalog the renderer substitutes from.
// Returns false (and logs) when the file is missing or malformed, leaving the
// catalog empty -- which is playable, just unreadable: every missing asset
// becomes a unit box in the checkered prototype material.
bool loadPrototypeCatalog(const std::string& tomlPath,
                          eng::prototype::PrototypeCatalog& out);

} // namespace game
