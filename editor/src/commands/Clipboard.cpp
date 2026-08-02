#include <editor/commands/Clipboard.h>

#include <algorithm>

namespace ed {

std::vector<Entity> collectEntities(const Doc& document,
                                    const std::vector<AuthorId>& ids)
{
    std::vector<Entity> entities;
    entities.reserve(ids.size());
    for (const AuthorId& id : ids)
        if (const Entity* entity = document.find(id))
            entities.push_back(*entity);
    return entities;
}

std::vector<AuthorId> withDescendants(const Doc& document,
                                      const std::vector<AuthorId>& ids)
{
    std::vector<AuthorId> out;
    const auto push = [&out](const AuthorId& id) {
        if (std::find(out.begin(), out.end(), id) == out.end())
            out.push_back(id);
    };
    for (const AuthorId& id : ids) {
        push(id);
        // descendantsOf walks breadth-first from the root and drops cycles, so
        // a parent always lands in `out` before the children that name it --
        // which is what offsetCopies relies on to re-point the links.
        for (const AuthorId& below : document.descendantsOf(id))
            push(below);
    }
    return out;
}

std::vector<Entity> offsetCopies(const Doc& document,
                                 const std::vector<Entity>& source,
                                 float metres, int cells)
{
    // Ids are allocated against a working copy, so pasting forty pillars cannot
    // hand out the same id twice: the real document has not been touched yet,
    // and allocateId only knows what a document already holds.
    Doc probe = document;
    std::vector<Entity> copies;
    std::vector<std::pair<AuthorId, AuthorId>> renamed; // old -> new
    copies.reserve(source.size());
    for (const Entity& entity : source) {
        Entity copy = entity;
        copy.id = probe.allocateId(entity.prefab.empty() ? entity.id
                                                         : entity.prefab);
        renamed.emplace_back(entity.id, copy.id);
        // Only a root moves. A child's transform is an offset inside its
        // parent, so shifting it too would move it twice -- once with the
        // parent it was copied alongside, and once on its own.
        const bool copiedWithParent =
            !entity.parent.empty() &&
            std::any_of(source.begin(), source.end(),
                        [&entity](const Entity& other) {
                            return other.id == entity.parent;
                        });
        if (!copiedWithParent) {
            copy.transform.position.x += metres;
            // A grid-constrained piece is addressed by its cell, and the cooker
            // reads the cell, not the transform -- shifting only the position
            // would move it in the viewport and leave it where it was in the
            // game.
            if (copy.cell)
                copy.cell->col += cells;
        }
        probe.add(copy);
        copies.push_back(std::move(copy));
    }

    // Re-point the links that stayed inside the copy. Without this a duplicated
    // chandelier's candles hang off the *original* chandelier: moving the copy
    // leaves them behind, and deleting the original takes them with it.
    //
    // A parent outside the copied set is left alone on purpose: copying one
    // candle of a chandelier should give a second candle on the same
    // chandelier, which is what an author means by duplicating it.
    for (Entity& copy : copies) {
        if (copy.parent.empty())
            continue;
        for (const auto& [before, after] : renamed) {
            if (copy.parent == before) {
                copy.parent = after;
                break;
            }
        }
    }
    return copies;
}

} // namespace ed
