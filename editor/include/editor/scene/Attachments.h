#pragma once
#include <editor/content/KitCatalog.h>
#include <editor/content/SceneDocument.h>

#include <vector>

namespace ed {

// Turning a compound kit piece's declared parts into real child entities.
//
// A compound piece -- kit.prop_boss_placeholder and its sword, an imported
// model and its twenty-four submeshes -- states its parts in kit.toml, and the
// cooker emits them as ECS children at build time. That draws correctly and
// keeps a .scn from carrying an entity per torch bracket, but it means the
// parts are not IN the document: they cannot be selected, moved, re-materialled
// or given a component, and the hierarchy shows a boss with no sword.
//
// This is the one expansion both routes out of that share: placing a compound
// piece (which unpacks immediately, so what you placed is what the tree shows)
// and the Unpack attachments command on a piece placed before it did.
//
// `ids` is a document the caller owns and which already holds `root`; ids are
// allocated against it as they are handed out, so two attachments of the same
// prefab cannot collide. The returned children are appended to it as well, and
// are NOT recorded as commands here -- the caller decides whether they are one
// undo entry with the placement or one with the unpack.
//
// Recursive: a part may be compound itself, and each such part is marked
// unpacked so the cooker does not generate a second copy underneath it.
std::vector<game::content::Entity>
buildAttachmentEntities(const game::content::KitCatalog& catalog,
                        game::content::SceneDocument& ids,
                        const game::content::Entity& root);

// Whether `entity` has parts the document is not holding yet. False for a piece
// with no attachments, for one already unpacked, and for anything that is not a
// kit piece at all.
bool hasPackedAttachments(const game::content::KitCatalog& catalog,
                          const game::content::Entity& entity);

} // namespace ed
