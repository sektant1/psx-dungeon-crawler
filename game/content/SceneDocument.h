#pragma once
#include <glm/glm.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace game::content {

// A name the author gives an entity, unique within the document and stable for
// the life of the scene.
//
// This is the reason a SceneDocument exists instead of the editor working
// straight on an entt::registry: entity handles are recycled, so a delete
// followed by an undo hands back a *different* handle and every history entry
// that referenced the old one silently rots. Undo commands and cross-entity
// references address entities by AuthorId and resolve them at apply time, so
// the history stays valid across any sequence of edits.
using AuthorId = std::string;

// Authored transform, kept in the units the .scn file uses: degrees, not a
// quaternion. Round-tripping euler->quat->euler is lossy and would make a save
// after a no-op edit produce a diff.
struct XformAuthor {
    glm::vec3 position{0.0f};
    glm::vec3 rotationDegrees{0.0f};
    glm::vec3 scale{1.0f};
};

// Where a grid-constrained piece sits. Derived quantities (the world transform)
// come from GridMath; this is the authored intent, and it is what makes a wall
// still be "the north wall of cell (3,4)" after somebody nudges the geometry.
struct CellPlacement {
    enum class Edge { None, North, East, South, West };

    int col = 0;
    int row = 0;
    Edge edge = Edge::None; // Wall/Opening sit on an edge; Floor/Fill do not
    int span = 1;
    int yawQuarters = 0; // 0..3, rotation in 90-degree steps
    float level = 0.0f;  // work-plane height in metres
};

struct ColliderAuthor {
    glm::vec3 halfExtents{0.5f};
    glm::vec3 offset{0.0f};
};

struct LightAuthor {
    enum class Type { Directional, Point };
    Type type = Type::Point;
    glm::vec3 colour{1.0f};
    float range = 8.0f;
    bool castShadows = false;
};

struct TriggerAuthor {
    glm::vec3 size{1.0f}; // half-extents of the box volume
    std::string event;
};

struct Entity {
    AuthorId id;
    std::string name;   // display name; defaults to id
    std::string prefab; // "kit.wall", or empty for a marker/light/trigger
    XformAuthor transform;
    bool castShadows = true;

    std::optional<CellPlacement> cell;
    std::optional<ColliderAuthor> collider;
    std::optional<LightAuthor> light;
    std::optional<float> exitYawDegrees;
    std::optional<std::string> marker;
    std::optional<std::string> enemySpawn; // enemy type id
    std::optional<std::string> pickup;     // pickup type id
    std::optional<TriggerAuthor> trigger;
    bool playerSpawn = false;
};

// The authored scene: what a .scn file says, in memory. Renderer-free and
// EnTT-free on purpose, so it loads in a headless test in milliseconds.
class SceneDocument
{
public:
    std::string id; // "scene.test.ritual_boss_showroom"
    std::vector<Entity> entities;

    Entity* find(std::string_view authorId);
    const Entity* find(std::string_view authorId) const;
    bool contains(std::string_view authorId) const;

    Entity& add(Entity entity); // asserts the id is free
    bool remove(std::string_view authorId);

    // Deterministic id allocation: "<stem>_0001", lowest free index. Never a
    // timestamp or a session counter -- two authors editing in parallel must
    // not be guaranteed a merge conflict.
    AuthorId allocateId(std::string_view stem) const;

    // Bumped on every mutation the editor makes, so the preview bridge can skip
    // its diff when nothing changed. Not serialized.
    uint64_t revision = 0;
    void touch() { ++revision; }

private:
    // Rebuilt on demand rather than maintained: entities is small and the
    // editor mutates it in bursts.
    void rebuildIndex() const;
    mutable std::unordered_map<std::string, std::size_t> mIndex;
    mutable uint64_t mIndexRevision = ~uint64_t(0);
};

} // namespace game::content
