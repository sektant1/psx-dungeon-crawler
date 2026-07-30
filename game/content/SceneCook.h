#pragma once
#include "KitCatalog.h"
#include "SceneDocument.h"

#include <entt/entt.hpp>

#include <string>
#include <unordered_map>

namespace game::content {

// Expands the authored IR into the runtime component model: prefab ids become
// MeshSource + MeshRenderer, colliders become child entities, markers become
// components. This is where "kit.wall" stops being a name and becomes a mesh.
//
// Entities are created in author-id order so the resulting registry -- and
// therefore the .map bytes -- are identical for identical input, which is what
// makes the CLI and the editor interchangeable.
// `authorToEntity`, when given, receives the id -> entity mapping the editor
// needs to highlight a selection. The cooker passes null: a .map has no author
// ids in it, by design.
bool buildRegistry(const SceneDocument& document, const KitCatalog& catalog,
                   entt::registry& out, std::string& error,
                   std::unordered_map<AuthorId, entt::entity>* authorToEntity =
                       nullptr);

// The whole cook: IR -> registry -> binary .map, written atomically by
// mapio::writeMap. The one function both scene_cook and the editor call; there
// is deliberately no second path from a scene to a map.
bool cookToMap(const SceneDocument& document, const KitCatalog& catalog,
               const std::string& mapPath, std::string& error);

} // namespace game::content
