#pragma once
#include <editor/content/KitCatalog.h>
#include <editor/content/SceneDocument.h>

#include <entt/entt.hpp>

#include <string>
#include <vector>
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
// `unresolved`, when given, changes what an entity naming a prefab the kit does
// not have *means*: instead of failing the build, that one entity is skipped,
// its id recorded, and the rest is built.
//
// The two callers genuinely want opposite things. A cook must refuse -- a map
// that ships with a dangling prefab is a hole in the level. The editor's
// preview must not: an author who deletes a kit piece, or opens a scene made
// against a newer kit, still has to be able to see and fix their level. It used
// to get an empty viewport, because a single bad prefab abandoned the registry
// mid-build and every other entity in the scene went with it.
// `assetRoot` is the directory scene-instance paths resolve against -- the open
// project's, or the content pack's. Only read when the document actually
// instances something; a scene that does not is built without touching it, and
// without paying for a copy of itself.
bool buildRegistry(const SceneDocument& document, const KitCatalog& catalog,
                   entt::registry& out, std::string& error,
                   std::unordered_map<AuthorId, entt::entity>* authorToEntity =
                       nullptr,
                   std::vector<AuthorId>* unresolved = nullptr,
                   const std::string& assetRoot = {});

// The whole cook: IR -> registry -> binary .map, written atomically by
// eng::ecs::writeMap. The one function both scene_cook and the editor call; there
// is deliberately no second path from a scene to a map.
bool cookToMap(const SceneDocument& document, const KitCatalog& catalog,
               const std::string& mapPath, std::string& error,
               const std::string& assetRoot = {});

} // namespace game::content
