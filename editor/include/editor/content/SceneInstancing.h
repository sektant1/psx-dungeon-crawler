#pragma once
#include <editor/content/SceneDocument.h>

#include <string>
#include <vector>

namespace game::content {

// Replaces every `instance` entity with the contents of the scene it names.
//
// This is the whole of scene instancing. A torch is authored once as its own
// .scn; placing it is one entity carrying an `instance`; and by the time
// anything downstream sees the document that entity has become the torch's
// entities, parented under it and moved with it. The cooker, the validator and
// the editor's preview therefore work on a flat scene and none of them knows
// instancing exists -- which is what keeps a feature this central from being
// threaded through every consumer.
//
// The instancing entity SURVIVES the expansion, stripped of its `instance` and
// keeping its transform, name, layer and scripts. It is the node the contents
// hang from, so moving the placement moves the torch, and a script on the
// placement is the torch's script. What it does not keep is any mesh of its
// own: an entity that both instances a scene and draws something was never a
// meaningful thing to author, and the validator rejects it.
//
// Ids are namespaced with the placement's own id and a slash --
// "torch_a/flame" -- so two placements of one scene cannot collide, and so a
// diagnostic names something an author can find. Nothing downstream parses that
// separator; it is a naming convention, not a second identifier scheme.
//
// Fails, and leaves `document` untouched, on: a scene that does not resolve, a
// scene that will not parse, an instance cycle, or nesting deeper than
// kMaxInstanceDepth. Each of those is reported by naming the placement and the
// file, because "cycle detected" with no path in it is a message somebody has
// to reproduce before they can act on it.
//
// `assetRoot` is the directory scene paths resolve against -- the open
// project's, or the content pack's.
inline constexpr int kMaxInstanceDepth = 8;

bool expandInstances(SceneDocument& document, const std::string& assetRoot,
                     std::string& error);

// The scenes this document instances, directly and transitively, as logical
// paths. Empty when it instances none.
//
// Exists so the editor can tell an author what a scene depends on, and so a
// future watcher can know which open documents a saved .scn invalidates.
// Reports what it can and stops at a cycle rather than diagnosing one: this is
// a query, and the diagnosis belongs to the expansion.
std::vector<std::string> instancedScenes(const SceneDocument& document,
                                         const std::string& assetRoot);

} // namespace game::content
