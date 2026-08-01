#pragma once
#include "Commands.h"

#include <vector>

namespace ed {

// Copying entities within a scene, and between two runs of the editor.
//
// Duplicate already existed, and it dropped the copy exactly on top of the
// original: the only feedback that anything happened was the selection changing
// and the entity count going up, and the usual next move -- drag it aside --
// picked the copy or the original at random. Anything that lands a new entity
// in the document therefore goes through here, and here always offsets.
//
// The offset is one grid cell rather than a nudge, because a kit piece's cell
// placement is what the cooker reads: a copy that shares a cell with its
// original is two walls in one slot, which is a scene that cooks and then looks
// wrong in the game rather than in the editor.

// Fresh copies of `source`, with ids free in `document` and shifted one cell
// along +X. Entities are not added to the document -- the caller wraps them in
// commands so the paste is one undo entry.
std::vector<Entity> offsetCopies(const Doc& document,
                                 const std::vector<Entity>& source,
                                 float metres, int cells);

// The entities named by `ids`, in the order given, skipping anything the
// document no longer has.
std::vector<Entity> collectEntities(const Doc& document,
                                    const std::vector<AuthorId>& ids);

// `ids` plus everything parented beneath them, each once, roots before their
// children.
//
// Copying and deleting both need this: a composed object is one thing to the
// author, and half of one is a scatter of entities pointing at a parent that is
// somewhere else -- or nowhere.
std::vector<AuthorId> withDescendants(const Doc& document,
                                      const std::vector<AuthorId>& ids);

} // namespace ed
