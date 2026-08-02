#pragma once
#include <editor/content/KitCatalog.h>
#include <editor/scene/Picker.h>
#include <editor/content/SceneDocument.h>

#include <functional>

namespace ed {

// The world box of a local one, under a resolved transform.
//
// It takes a WorldTransform rather than the authored one because a parented
// entity's authored transform is local: framing, picking and placement all have
// to agree with what the viewport draws, and the viewport draws the cooked
// chain.
void transformedBounds(const game::content::WorldTransform& transform,
                       const glm::vec3& localMin, const glm::vec3& localMax,
                       glm::vec3& worldMin, glm::vec3& worldMax);

// What a ray found in the document.
//
// The bounds come back with the hit because the two things that want a surface
// want different heights from it: dressing goes exactly where the cursor
// touched, architecture goes on top of whatever it was pointed at. Returning
// the box means BrushPlacement can make that choice without a second lookup.
struct DocumentHit {
    bool valid = false;
    game::content::AuthorId id;
    float t = 0.0f;
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f}; // outward, of the face entered
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
};

// Which entities a ray may hit. A predicate rather than the editor's hidden and
// locked lists directly, so this file stays testable without an EditorState --
// and so placement can exclude the entities of the stroke that is painting them.
using EntityFilter = std::function<bool(const game::content::AuthorId&)>;

// Nearest entity along the ray, tested against catalogue bounds rather than the
// loaded meshes: an entity has to be hittable even before its mesh is on disk,
// which is exactly when the author most needs to click it and see why.
//
// Ties go to the smaller box, so clicking a barrel inside a room does not
// select the room.
DocumentHit raycastDocument(const game::content::SceneDocument& doc,
                            const game::content::KitCatalog& catalog,
                            const Ray& ray, const EntityFilter& accepts);

} // namespace ed
