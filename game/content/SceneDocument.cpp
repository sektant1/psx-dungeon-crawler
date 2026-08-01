#include "SceneDocument.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace game::content {

glm::quat authorOrientation(const glm::vec3& rotationDegrees)
{
    return glm::angleAxis(glm::radians(rotationDegrees.y), glm::vec3(0, 1, 0)) *
           glm::angleAxis(glm::radians(rotationDegrees.x), glm::vec3(1, 0, 0)) *
           glm::angleAxis(glm::radians(rotationDegrees.z), glm::vec3(0, 0, 1));
}

glm::vec3 authorRotationDegrees(const glm::quat& orientation)
{
    const glm::mat3 matrix = glm::mat3_cast(glm::normalize(orientation));
    const float pitch = std::asin(glm::clamp(-matrix[2][1], -1.0f, 1.0f));
    float yaw = 0.0f;
    float roll = 0.0f;
    if (std::abs(std::cos(pitch)) > 1e-5f) {
        yaw = std::atan2(matrix[2][0], matrix[2][2]);
        roll = std::atan2(matrix[0][1], matrix[1][1]);
    } else {
        // At +/-90 degrees yaw and roll share one axis. Keep roll canonical at
        // zero and put the equivalent combined angle into yaw.
        yaw = pitch > 0.0f
                  ? std::atan2(matrix[1][0], matrix[0][0])
                  : std::atan2(-matrix[1][0], matrix[0][0]);
    }
    return glm::degrees(glm::vec3(pitch, yaw, roll));
}

glm::mat4 authorTransformMatrix(const XformAuthor& transform)
{
    return glm::translate(glm::mat4(1.0f), transform.position) *
           glm::mat4_cast(authorOrientation(transform.rotationDegrees)) *
           glm::scale(glm::mat4(1.0f), transform.scale);
}

WorldTransform composeTransform(const WorldTransform& parent,
                                const XformAuthor& local)
{
    WorldTransform out;
    out.orientation = parent.orientation * authorOrientation(local.rotationDegrees);
    out.scale = parent.scale * local.scale;
    out.position =
        parent.position + parent.orientation * (parent.scale * local.position);
    return out;
}

XformAuthor localFromWorld(const WorldTransform& parent,
                           const WorldTransform& world)
{
    // A zero on any axis would divide the child's offset away; a level can
    // carry one (validate reports scale.zero) and this must not produce a NaN
    // that spreads into the file.
    glm::vec3 divisor = parent.scale;
    for (int axis = 0; axis < 3; ++axis)
        if (std::abs(divisor[axis]) < 1e-6f)
            divisor[axis] = divisor[axis] < 0.0f ? -1e-6f : 1e-6f;

    const glm::quat inverse = glm::inverse(parent.orientation);
    XformAuthor local;
    local.position = (inverse * (world.position - parent.position)) / divisor;
    local.rotationDegrees = authorRotationDegrees(inverse * world.orientation);
    local.scale = world.scale / divisor;
    return local;
}

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

WorldTransform SceneDocument::worldTransform(std::string_view authorId) const
{
    const Entity* entity = find(authorId);
    if (!entity)
        return {};

    // Walked to the root first, then composed downward. Recursing on the parent
    // would be the same result, but a cycle would recurse until the stack ran
    // out -- and a document with a cycle is one an author has to be able to
    // open in order to fix.
    std::vector<const Entity*> chain;
    std::vector<std::string> seen;
    for (const Entity* at = entity; at != nullptr;) {
        if (std::find(seen.begin(), seen.end(), at->id) != seen.end())
            break; // the loop closes here: treat this link as the world
        seen.push_back(at->id);
        chain.push_back(at);
        at = at->parent.empty() ? nullptr : find(at->parent);
    }

    WorldTransform world;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        world = composeTransform(world, (*it)->transform);
    return world;
}

std::vector<const Entity*>
SceneDocument::childrenOf(std::string_view authorId) const
{
    std::vector<const Entity*> children;
    for (const Entity& entity : entities)
        if (entity.parent == authorId)
            children.push_back(&entity);
    return children;
}

std::vector<AuthorId> SceneDocument::descendantsOf(std::string_view authorId) const
{
    std::vector<AuthorId> out;
    std::vector<AuthorId> pending{AuthorId(authorId)};
    std::vector<AuthorId> seen{AuthorId(authorId)};
    while (!pending.empty()) {
        const AuthorId at = pending.back();
        pending.pop_back();
        for (const Entity* child : childrenOf(at)) {
            if (std::find(seen.begin(), seen.end(), child->id) != seen.end())
                continue;
            seen.push_back(child->id);
            out.push_back(child->id);
            pending.push_back(child->id);
        }
    }
    return out;
}

bool SceneDocument::wouldCycle(std::string_view child,
                               std::string_view parent) const
{
    if (parent.empty())
        return false; // unparenting always terminates
    if (child == parent)
        return true;
    // Walking up from the proposed parent is cheaper than listing every
    // descendant of the child, and it is bounded by the entity count even when
    // the document already contains a loop.
    std::vector<std::string> seen;
    for (const Entity* at = find(parent); at != nullptr;) {
        if (at->id == child)
            return true;
        if (std::find(seen.begin(), seen.end(), at->id) != seen.end())
            return true; // already looping: refuse to add to it
        seen.push_back(at->id);
        at = at->parent.empty() ? nullptr : find(at->parent);
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
