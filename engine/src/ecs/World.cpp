#include <eng/ecs/World.h>

#include <algorithm>
#include <vector>

namespace eng::ecs {

World::World() = default;
World::~World() = default;

void World::detachAll()
{
    // Destructors release the nodes and bodies; the entities keep their
    // components, so re-attaching later re-materialises them.
    mRender.reset();
    mPhysics.reset();
    mAudio.reset();
}

entt::entity World::create(std::string name)
{
    const entt::entity e = mReg.create();
    mReg.emplace<Transform>(e);
    mReg.emplace<Dirty>(e);
    if (mActiveGroup != 0)
        mReg.emplace<EntityGroup>(e, EntityGroup{mActiveGroup});
    if (!name.empty())
        mReg.emplace<Name>(e, std::move(name));
    return e;
}

std::size_t World::destroyGroup(uint32_t group)
{
    if (group == 0)
        return 0;
    // Collected first: destroy() edits the Parent/Children of neighbours, and
    // that can touch the pool being iterated.
    std::vector<entt::entity> doomed;
    for (auto e : mReg.view<EntityGroup>())
        if (mReg.get<EntityGroup>(e).value == group)
            doomed.push_back(e);
    for (entt::entity e : doomed)
        destroy(e);
    return doomed.size();
}

void World::destroy(entt::entity e)
{
    if (!mReg.valid(e))
        return;
    if (auto* parent = mReg.try_get<Parent>(e); parent && parent->value != entt::null) {
        if (auto* pc = mReg.try_get<Children>(parent->value)) {
            auto& v = pc->value;
            v.erase(std::remove(v.begin(), v.end(), e), v.end());
        }
    }
    if (auto* ch = mReg.try_get<Children>(e)) {
        for (entt::entity c : ch->value) {
            if (auto* cp = mReg.try_get<Parent>(c))
                cp->value = entt::null;
            // An orphan's world transform still has its dead parent's baked
            // into it. Nothing would recompute it -- the entity did not move --
            // so it would draw at the old composed pose until something else
            // touched it, which is a stale position with no visible cause.
            markSubtreeDirty(c);
        }
    }
    mReg.destroy(e);
}

void World::destroyHierarchy(entt::entity e)
{
    if (!mReg.valid(e))
        return;
    // Depth-first into a flat list, then destroy: destroy() edits the Children
    // of neighbours, so walking the live graph while unlinking it is not safe.
    std::vector<entt::entity> doomed{e};
    for (std::size_t i = 0; i < doomed.size(); ++i) {
        if (const auto* ch = mReg.try_get<Children>(doomed[i]))
            for (entt::entity c : ch->value)
                if (mReg.valid(c))
                    doomed.push_back(c);
    }
    // Leaves first, so each destroy() sees a subtree that is already empty and
    // never marks a doomed child dirty on the way out.
    for (auto it = doomed.rbegin(); it != doomed.rend(); ++it)
        destroy(*it);
}

void World::setLocalTransform(entt::entity e, const Transform& t)
{
    if (!mReg.valid(e))
        return;
    mReg.get_or_emplace<Transform>(e) = t;
    markSubtreeDirty(e);
}

void World::markSubtreeDirty(entt::entity e)
{
    if (!mReg.valid(e))
        return;
    if (!mReg.all_of<Dirty>(e))
        mReg.emplace<Dirty>(e);
    if (auto* ch = mReg.try_get<Children>(e))
        for (entt::entity c : ch->value)
            markSubtreeDirty(c);
}

void World::resolveWorld(entt::entity e)
{
    // Clean *and* already resolved: nothing to do. The second half of that
    // condition is what makes a parent safe to recurse into. An entity that has
    // never been resolved has no WorldTransform, and it is never Dirty either
    // when it came out of a file rather than out of create() -- so an early-out
    // on Dirty alone would fall through to reading a component that does not
    // exist, for the single case (an authored group node with a mesh under it)
    // that hierarchy is mostly used for.
    if (!mReg.all_of<Dirty>(e) && mReg.all_of<WorldTransform>(e))
        return;

    // A Transform is not guaranteed: the add/remove menu can take one off, and
    // a registry filled by something other than create() may never have had one.
    // Identity keeps the subtree resolvable instead of throwing mid-frame.
    const Transform local =
        mReg.all_of<Transform>(e) ? mReg.get<Transform>(e) : Transform{};
    glm::mat4 world = matrixOf(local);

    if (auto* p = mReg.try_get<Parent>(e); p && p->value != entt::null &&
                                           mReg.valid(p->value)) {
        resolveWorld(p->value); // ensure parent world is up to date first
        world = mReg.get<WorldTransform>(p->value).matrix * world;
    }
    mReg.get_or_emplace<WorldTransform>(e).matrix = world;
    mReg.remove<Dirty>(e);
}

void World::updateWorldTransforms()
{
    // Snapshot the dirty set first: resolveWorld removes Dirty as it goes.
    std::vector<entt::entity> dirty;
    for (auto e : mReg.view<Dirty>())
        dirty.push_back(e);
    for (entt::entity e : dirty)
        if (mReg.valid(e))
            resolveWorld(e);
}

void World::setParent(entt::entity e, entt::entity parent)
{
    if (!mReg.valid(e))
        return;
    if (e == parent)
        return; // self-parenting would infinitely recurse in resolveWorld
    // Reject cycles: walking up from `parent` must not reach `e`.
    for (entt::entity a = parent; a != entt::null && mReg.valid(a);) {
        if (a == e)
            return;
        auto* ap = mReg.try_get<Parent>(a);
        a = ap ? ap->value : entt::null;
    }
    auto& link = mReg.get_or_emplace<Parent>(e);
    // Remove from the previous parent's Children.
    if (link.value != entt::null && mReg.valid(link.value)) {
        if (auto* oc = mReg.try_get<Children>(link.value)) {
            auto& v = oc->value;
            v.erase(std::remove(v.begin(), v.end(), e), v.end());
        }
    }
    link.value = parent;
    if (parent != entt::null && mReg.valid(parent))
        mReg.get_or_emplace<Children>(parent).value.push_back(e);
    markSubtreeDirty(e);
}

void World::sync()
{
    // Simulated poses land on their Transforms first, so everything below --
    // the resolve, the renderer, gameplay reading a position after sync --
    // sees where the bodies actually are this frame, not last frame.
    if (mPhysics)
        mPhysics->readback();
    // SceneSync recomputes world transforms itself (it has to capture the dirty
    // set before they are cleared); without a renderer attached this World still
    // owes its consumers up-to-date WorldTransforms.
    if (mRender)
        mRender->sync();
    else
        updateWorldTransforms();
    if (mPhysics)
        mPhysics->sync();
    if (mAudio)
        mAudio->sync();
}

void World::clear()
{
    if (mRender)
        mRender->clear();
    if (mPhysics)
        mPhysics->clear();
    if (mAudio)
        mAudio->clear();
    mReg.clear();
}

entt::entity spawnMesh(World& world, MeshHandle mesh, const std::string& material,
                       glm::vec3 position, glm::quat orientation,
                       glm::vec3 scale, bool castShadows, const std::string& name)
{
    const entt::entity e = world.create(name);
    Transform t;
    t.position = position;
    t.rotation = orientation;
    t.scale = scale;
    world.setLocalTransform(e, t);
    world.registry().emplace<MeshRenderer>(e, mesh, material, castShadows);
    return e;
}

entt::entity spawnLight(World& world, const LightDesc& desc, glm::vec3 position,
                        const std::string& name)
{
    const entt::entity e = world.create(name);
    Transform t;
    t.position = position;
    world.setLocalTransform(e, t);
    auto& reg = world.registry();
    reg.emplace<LightRef>(e).desc = desc;
    reg.emplace<LightColour>(e, desc.colour);
    return e;
}

} // namespace eng::ecs
