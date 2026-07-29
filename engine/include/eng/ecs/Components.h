#pragma once
#include <eng/Handles.h>
#include <eng/LightDesc.h> // eng::LightDesc
#include <eng/Physics.h>   // eng::ShapeKind, eng::CollisionLayer (by value below)

#include <entt/entt.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>
#include <vector>

namespace eng::ecs {

// Human-readable label (editor lists, debugging). Optional per entity.
struct Name {
    std::string value;
};

// Local transform relative to the parent (or world, if no parent).
struct Transform {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; // identity (w,x,y,z)
    glm::vec3 scale{1.0f};
};

// Cached world-space matrix, recomputed by Scene::updateWorldTransforms().
struct WorldTransform {
    glm::mat4 matrix{1.0f};
};

// Tag: this entity's world transform (and its subtree's) needs recomputing.
struct Dirty {};

// Parent link. Absent (or entt::null) means the entity is a scene root.
struct Parent {
    entt::entity value{entt::null};
};

// Child links, maintained by Scene::setParent so subtree walks are O(children).
struct Children {
    std::vector<entt::entity> value;
};

// A renderable mesh to attach when SceneSync first sees this entity.
struct MeshRenderer {
    MeshHandle mesh;
    std::string material;
    bool castShadows = false;
};

// A light to attach when SceneSync first sees this entity. `handle` is filled
// in by SceneSync; callers set only `desc`.
struct LightRef {
    LightDesc desc;
    LightHandle handle;
};

// Per-frame light colour override (linear, energy pre-multiplied). Present on an
// entity that also has a LightRef, SceneSync pushes this to the light every
// frame -- for animated lights (torch flicker, glow pulse) driven by gameplay.
struct LightColour {
    glm::vec3 value{1.0f};
};

// Static collision volume. Materialised as a physics body by PhysicsSync.
// `layer` indexes the application's layer table -- the engine has no taxonomy
// of its own -- and `sensor` makes the body a non-blocking overlap volume, the
// shape an event region (a door trigger, a damage zone) is built out of.
struct Collider {
    ShapeKind shape = ShapeKind::Box;
    glm::vec3 size{0.5f}; // half-extents (box) / radius in x (sphere)
    CollisionLayer layer = 0;
    bool sensor = false;
};

// The physics body backing this entity. Populated by PhysicsSync; callers
// never set this directly.
struct BodyRef {
    BodyHandle handle;
};

// The renderer scene node backing this entity. Populated by SceneSync; callers
// never set this directly.
struct NodeRef {
    NodeHandle handle;
};

} // namespace eng::ecs
