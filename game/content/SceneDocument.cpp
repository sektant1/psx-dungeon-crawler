#include "SceneDocument.h"

#include <glm/gtc/matrix_transform.hpp>

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
