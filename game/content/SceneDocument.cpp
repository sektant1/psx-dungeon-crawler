#include "SceneDocument.h"

#include <cstdio>

namespace game::content {

void SceneDocument::rebuildIndex() const
{
    if (mIndexRevision == revision && mIndex.size() == entities.size())
        return;
    mIndex.clear();
    mIndex.reserve(entities.size());
    for (std::size_t i = 0; i < entities.size(); ++i)
        mIndex.emplace(entities[i].id, i);
    mIndexRevision = revision;
}

Entity* SceneDocument::find(std::string_view authorId)
{
    const SceneDocument* self = this;
    return const_cast<Entity*>(self->find(authorId));
}

const Entity* SceneDocument::find(std::string_view authorId) const
{
    rebuildIndex();
    const auto found = mIndex.find(std::string(authorId));
    return found == mIndex.end() ? nullptr : &entities[found->second];
}

bool SceneDocument::contains(std::string_view authorId) const
{
    return find(authorId) != nullptr;
}

Entity& SceneDocument::add(Entity entity)
{
    touch();
    entities.push_back(std::move(entity));
    return entities.back();
}

bool SceneDocument::remove(std::string_view authorId)
{
    for (std::size_t i = 0; i < entities.size(); ++i) {
        if (entities[i].id == authorId) {
            entities.erase(entities.begin() + std::ptrdiff_t(i));
            touch();
            return true;
        }
    }
    return false;
}

AuthorId SceneDocument::allocateId(std::string_view stem) const
{
    std::string prefix(stem.empty() ? std::string_view("entity") : stem);
    // "kit.wall" -> "wall": the prefab namespace is noise in an author id.
    if (const std::size_t dot = prefix.rfind('.'); dot != std::string::npos)
        prefix = prefix.substr(dot + 1);

    for (int index = 1; index < 100000; ++index) {
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "_%04d", index);
        AuthorId candidate = prefix + buffer;
        if (!contains(candidate))
            return candidate;
    }
    return prefix + "_overflow";
}

} // namespace game::content
