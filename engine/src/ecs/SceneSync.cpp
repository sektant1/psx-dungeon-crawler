#include <eng/ecs/SceneSync.h>

#include <eng/ecs/Scene.h>
#include <eng/ecs/SceneBackend.h>

#include <algorithm>
#include <utility>

namespace eng::ecs {

SceneSync::SceneSync(Scene& scene, SceneBackend& backend)
    : mScene(scene), mBackend(backend) {}

SceneSync::~SceneSync()
{
    clear();
}

void SceneSync::clear()
{
    auto& reg = mScene.registry();
    for (const auto& [entity, node] : mTracked) {
        mBackend.destroyNode(node);
        if (reg.valid(entity)) {
            if (const auto* ref = reg.try_get<NodeRef>(entity);
                ref && ref->handle.id == node.id)
                reg.remove<NodeRef>(entity);
        }
    }
    mTracked.clear();
    mPushThisFrame.clear();
}

void SceneSync::sync()
{
    auto& reg = mScene.registry();

    // Capture entities changed this frame BEFORE updateWorldTransforms() clears
    // their Dirty tags. New entities are Dirty from create(), so they are
    // included and get their first transform push below.
    mPushThisFrame.clear();
    for (auto e : reg.view<Dirty>())
        mPushThisFrame.push_back(e);

    // 1) New entities with a Transform but no NodeRef: allocate a backing node
    //    and attach any mesh/light once.
    for (auto e : reg.view<Transform>(entt::exclude<NodeRef>)) {
        const NodeHandle node = mBackend.createNode(
            NodeHandle{}, glm::vec3(0.0f),
            reg.all_of<Name>(e) ? reg.get<Name>(e).value : std::string{});
        reg.emplace<NodeRef>(e, node);
        mTracked.emplace_back(e, node);
        // The node was just created at the origin, so its transform has to be
        // pushed at least once regardless of who filled the registry. Only
        // Scene::create() tags Dirty; a registry populated by a deserializer
        // (readMap) or a content cooker never is, and every entity in it would
        // otherwise sit at 0,0,0 forever -- silently, because the scene still
        // renders.
        if (!reg.all_of<Dirty>(e))
            reg.emplace<Dirty>(e);
        mPushThisFrame.push_back(e);
        if (auto* mr = reg.try_get<MeshRenderer>(e))
            mBackend.attachMesh(node, mr->mesh, mr->material, mr->castShadows);
        if (auto* lr = reg.try_get<LightRef>(e))
            lr->handle = mBackend.attachLight(node, lr->desc);
    }

    // 2) Recompute world transforms (clears Dirty).
    mScene.updateWorldTransforms();

    // 3) Push transforms for entities that were dirty this frame. Nodes are
    //    flat under the renderer root, so we push the WORLD position plus the
    //    LOCAL rotation/scale. NOTE: rotated/scaled parents are not fully
    //    composed into children here; a later phase adds world decomposition
    //    (or renderer-side parenting) when nested transforms are needed.
    for (entt::entity e : mPushThisFrame) {
        if (!reg.valid(e) || !reg.all_of<NodeRef>(e))
            continue;
        const NodeHandle node = reg.get<NodeRef>(e).handle;
        const Transform& t = reg.get<Transform>(e);
        const glm::mat4& w = reg.get<WorldTransform>(e).matrix;
        mBackend.setPosition(node, glm::vec3(w[3]));
        mBackend.setOrientation(node, t.rotation);
        mBackend.setScale(node, t.scale);
    }

    // 3b) Push animated light colours. Cheap (few lights); runs every frame so
    //     gameplay can drive flicker/pulse by writing the LightColour component.
    for (auto e : reg.view<LightRef, LightColour>()) {
        const LightHandle h = reg.get<LightRef>(e).handle;
        if (h.valid())
            mBackend.setLightColour(h, reg.get<LightColour>(e).value);
    }

    // 4) Destroyed entities: free their nodes.
    mTracked.erase(std::remove_if(mTracked.begin(), mTracked.end(),
        [&](auto& pair) {
            if (reg.valid(pair.first))
                return false;
            mBackend.destroyNode(pair.second);
            return true;
        }), mTracked.end());
}

} // namespace eng::ecs
