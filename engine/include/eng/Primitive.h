#pragma once

#include <eng/Handles.h>
#include <eng/Physics.h>
#include <eng/render/Enchantment.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <optional>
#include <string>

namespace eng {

class Renderer;
struct NodeTransform;

enum class PrimitiveKind {
    Box,
    BeveledBox,
    Sphere,
    Capsule,
    Cylinder,
    Cone,
    Plane,
    Disc,
};

struct PrimitiveMeshDesc {
    PrimitiveKind kind = PrimitiveKind::Box;
    glm::vec3 size{1.0f};
    float radius = 0.5f;
    float height = 1.0f;
    float bevel = 0.12f;
    float thickness = 0.05f;
    int rings = 12;
    int segments = 16;
    bool inwardFacing = false;
    int subdivisions = 0;
};

struct PrimitiveDesc {
    PrimitiveMeshDesc mesh;
    NodeHandle parent = kRootNode;
    glm::vec3 position{0.0f};
    glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
    std::string material;
    bool castShadows = false;
    std::optional<EnchantmentDesc> enchantment;
    bool collision = false;
    CollisionLayer bodyLayer = 0;
    bool dynamic = false;
    bool sensor = false;
    float mass = 1.0f;
    float friction = 0.5f;
    float restitution = 0.0f;
};

struct PrimitiveInstance {
    NodeHandle node;
    MeshHandle mesh;
    BodyHandle body;

    bool valid() const { return node.valid() && mesh.valid(); }
};

struct ResolvedPrimitiveCollider {
    bool collision = false;
    BodyDesc body;

    bool createsBody() const { return collision; }
};

namespace detail {

// Unit-testable dispatch seam used directly by Renderer. Keeping this separate
// from the mesh-building generators makes it impossible for a newly added kind
// to silently fall through to an unrelated mesh.
enum class PrimitiveMeshGenerator {
    Box,
    BeveledBox,
    Sphere,
    Capsule,
    Cylinder,
    Cone,
    Plane,
    Disc,
};

std::optional<PrimitiveMeshGenerator>
primitiveMeshGenerator(PrimitiveKind);

template <typename RemoveBody, typename DestroyNode, typename ReleaseMesh>
void releasePrimitiveOwnership(PrimitiveInstance& instance,
                               RemoveBody&& removeBody,
                               DestroyNode&& destroyNode,
                               ReleaseMesh&& releaseMesh)
{
    if (instance.body.valid())
        removeBody(instance.body);
    if (instance.node.valid())
        destroyNode(instance.node);
    if (instance.mesh.valid())
        releaseMesh(instance.mesh);
    instance = {};
}

} // namespace detail

bool validPrimitiveMeshDesc(const PrimitiveMeshDesc&);
bool validPrimitiveDesc(const PrimitiveDesc&);
std::optional<ResolvedPrimitiveCollider>
resolvePrimitiveCollider(const PrimitiveDesc&);
std::optional<ResolvedPrimitiveCollider>
resolvePrimitiveCollider(const PrimitiveDesc&, const NodeTransform& parent);

PrimitiveInstance spawnPrimitive(Renderer&, Physics&, const PrimitiveDesc&);
void destroyPrimitive(Renderer&, Physics&, PrimitiveInstance&);

} // namespace eng
