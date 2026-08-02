#include "DocumentRaycast.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace ed {

using namespace game::content;

void transformedBounds(const WorldTransform& transform,
                       const glm::vec3& localMin, const glm::vec3& localMax,
                       glm::vec3& worldMin, glm::vec3& worldMax)
{
    const glm::mat4 matrix =
        glm::translate(glm::mat4(1.0f), transform.position) *
        glm::mat4_cast(transform.orientation) *
        glm::scale(glm::mat4(1.0f), transform.scale);
    worldMin = glm::vec3(1e9f);
    worldMax = glm::vec3(-1e9f);
    for (int corner = 0; corner < 8; ++corner) {
        const glm::vec3 local{
            (corner & 1) ? localMax.x : localMin.x,
            (corner & 2) ? localMax.y : localMin.y,
            (corner & 4) ? localMax.z : localMin.z,
        };
        const glm::vec3 world = glm::vec3(matrix * glm::vec4(local, 1.0f));
        worldMin = glm::min(worldMin, world);
        worldMax = glm::max(worldMax, world);
    }
}

DocumentHit raycastDocument(const SceneDocument& doc, const KitCatalog& catalog,
                            const Ray& ray, const EntityFilter& accepts)
{
    DocumentHit best;
    float bestT = 1e30f;
    float bestVolume = 1e30f;

    for (const Entity& entity : doc.entities) {
        if (accepts && !accepts(entity.id))
            continue;

        // A metre box is the fallback for an entity with no kit piece -- a
        // marker, a spawn, a group. It is a poor target and deliberately so:
        // those are picked by their screen-space mark, not by a ray.
        glm::vec3 localMin(-0.5f), localMax(0.5f);
        if (const KitPiece* piece = catalog.find(entity.prefab))
            piece->localBoundsMeters(catalog.scale(), localMin, localMax);

        glm::vec3 min, max;
        transformedBounds(doc.worldTransform(entity.id), localMin, localMax,
                          min, max);

        float t = 0.0f;
        glm::vec3 normal;
        if (!rayAabb(ray, min, max, t, normal))
            continue;

        const glm::vec3 size = max - min;
        const float volume = size.x * size.y * size.z;
        if (t < bestT - 1e-3f ||
            (std::fabs(t - bestT) <= 1e-3f && volume < bestVolume)) {
            bestT = t;
            bestVolume = volume;
            best.valid = true;
            best.id = entity.id;
            best.t = t;
            best.point = ray.origin + ray.dir * t;
            best.normal = normal;
            best.boundsMin = min;
            best.boundsMax = max;
        }
    }
    return best;
}

} // namespace ed
