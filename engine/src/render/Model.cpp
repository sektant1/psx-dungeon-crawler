#include <eng/Model.h>

#include <eng/Log.h>

#include <utility>
#include <vector>

namespace eng {

ModelInstance spawnModel(Renderer& renderer, Physics& physics,
                         const ModelDesc& desc)
{
    if (!validModelDesc(desc)) {
        log::error("Model: invalid descriptor for mesh '%s'",
                   desc.meshPath.c_str());
        return {};
    }
    if (desc.collider == ColliderMode::StaticMesh && desc.dynamic) {
        log::error("Model: dynamic StaticMesh collider is unsupported for '%s'",
                   desc.meshPath.c_str());
        return {};
    }

    NodeTransform parentTransform;
    if (!renderer.nodeWorldTransform(desc.parent, parentTransform)) {
        log::error("Model: invalid parent node for '%s'",
                   desc.meshPath.c_str());
        return {};
    }
    const ModelDesc worldDesc =
        composeModelDesc(parentTransform, desc);
    const NodeTransform worldTransform{
        worldDesc.position, worldDesc.orientation, worldDesc.scale};
    if (!validModelDesc(worldDesc)) {
        log::error("Model: parent produced an invalid world transform for '%s'",
                   desc.meshPath.c_str());
        return {};
    }

    const MeshHandle mesh = renderer.loadObj(desc.meshPath, desc.import);
    if (!mesh.valid())
        return {};

    MeshBounds bounds;
    if ((desc.collider == ColliderMode::AutoBox ||
         desc.collider == ColliderMode::AutoSphere) &&
        !renderer.meshBounds(mesh, bounds)) {
        log::error("Model: no usable mesh bounds for '%s'",
                   desc.meshPath.c_str());
        renderer.releaseMesh(mesh);
        return {};
    }

    const auto resolved =
        resolveModelCollider(desc, bounds, parentTransform);
    if (!resolved) {
        log::error("Model: collider resolution failed for '%s'",
                   desc.meshPath.c_str());
        renderer.releaseMesh(mesh);
        return {};
    }

    std::vector<glm::vec3> collisionVertices;
    std::vector<uint32_t> collisionIndices;
    if (desc.collider == ColliderMode::StaticMesh &&
        !renderer.meshCollisionGeometry(mesh, collisionVertices,
                                        collisionIndices)) {
        log::error(
            "Model: StaticMesh collider unsupported for '%s': "
            "cached OBJ collision geometry is unavailable",
            desc.meshPath.c_str());
        renderer.releaseMesh(mesh);
        return {};
    }

    const NodeHandle node =
        renderer.createNode(desc.parent, desc.position);
    renderer.setOrientation(node, glm::normalize(desc.orientation));
    renderer.setScale(node, desc.scale);
    const ResolvedModelMaterial material =
        resolveModelMaterialForSubmesh(
            desc, 0, [&](const std::string& requested) {
                return renderer.materialAvailable(requested);
            });
    renderer.attachMesh(node, mesh, material, desc.castShadows,
                        desc.renderOnTop);
    if (desc.enchantment)
        renderer.setNodeEnchantment(node, *desc.enchantment);

    BodyHandle body;
    if (desc.collider == ColliderMode::AutoBox ||
        desc.collider == ColliderMode::AutoSphere) {
        body = physics.createBody(resolved->body);
    } else if (desc.collider == ColliderMode::StaticMesh) {
        for (glm::vec3& vertex : collisionVertices)
            vertex *= worldTransform.scale;
        if (worldTransform.scale.x * worldTransform.scale.y *
                worldTransform.scale.z <
            0.0f)
            for (size_t i = 0; i + 2 < collisionIndices.size(); i += 3)
                std::swap(collisionIndices[i + 1],
                          collisionIndices[i + 2]);
        body = physics.createMeshBody(
            collisionVertices, collisionIndices, worldTransform.position,
            resolved->body.orientation, desc.bodyLayer);
    }

    if (resolved->createsBody() && !body.valid()) {
        log::error("Model: body creation failed for '%s'",
                   desc.meshPath.c_str());
        renderer.destroyNode(node);
        renderer.releaseMesh(mesh);
        return {};
    }

    return {node, mesh, body};
}

void destroyModel(Renderer& renderer, Physics& physics,
                  ModelInstance& instance)
{
    if (instance.body.valid())
        physics.removeBody(instance.body);
    if (instance.node.valid())
        renderer.destroyNode(instance.node);
    if (instance.mesh.valid())
        renderer.releaseMesh(instance.mesh);
    instance = {};
}

} // namespace eng
