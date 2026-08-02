#include <editor/scene/PickTarget.h>

namespace ed {

using game::content::AuthorId;
using game::content::Entity;
using game::content::SceneDocument;

AuthorId rootOf(const SceneDocument& document, const AuthorId& id)
{
    AuthorId root = id;
    // Bounded by the entity count rather than by reaching a root: a cycle would
    // otherwise spin forever, and the editor that would fix it is the one that
    // hangs.
    for (std::size_t guard = 0; guard <= document.entities.size(); ++guard) {
        const Entity* entity = document.find(root);
        if (!entity || entity->parent.empty())
            break;
        root = entity->parent;
    }
    return root;
}

AuthorId resolvePickTarget(const SceneDocument& document, const AuthorId& hit,
                           const std::vector<AuthorId>& selection, bool exact)
{
    const Entity* entity = document.find(hit);
    if (!entity || entity->parent.empty())
        return hit; // not part of anything: the hit is already the object
    if (exact)
        return hit; // Alt: "I mean this exact piece"

    const AuthorId root = rootOf(document, hit);
    // Already working inside this object? Then the click is about its parts.
    // "Inside" means the selection holds the root or anything under it, which
    // is what makes the second click drill in and the third stay there.
    for (const AuthorId& id : selection)
        if (id == root || rootOf(document, id) == root)
            return hit;
    return root;
}

} // namespace ed
