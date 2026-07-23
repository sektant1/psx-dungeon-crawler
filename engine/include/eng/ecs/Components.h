#pragma once
#include <eng/Handles.h>
#include <eng/LightDesc.h> // eng::LightDesc

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

// The renderer scene node backing this entity. Populated by SceneSync; callers
// never set this directly.
struct NodeRef {
    NodeHandle handle;
};

} // namespace eng::ecs
