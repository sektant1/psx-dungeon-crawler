#include <eng/ecs/SceneSync.h>

#include <eng/ecs/SceneBackend.h>
#include <eng/ecs/World.h>

#include <algorithm>
#include <utility>

namespace eng::ecs {

// Defined here rather than in World.cpp so a target that links only World.cpp
// -- a headless test, the combat sim -- never has to pull the renderer in.
void World::attachRenderer(SceneBackend& backend, bool drivesCamera)
{
    auto sync = std::make_unique<SceneSync>(*this, backend);
    sync->setDrivesCamera(drivesCamera);
    mRender = std::move(sync);
}

SceneSync::SceneSync(World& world, SceneBackend& backend)
    : mWorld(world), mBackend(backend) {}

SceneSync::~SceneSync()
{
    clear();
}

void SceneSync::clear()
{
    auto& reg = mWorld.registry();
    for (const Tracked& tracked : mTracked) {
        mBackend.destroyNode(tracked.node);
        if (reg.valid(tracked.entity)) {
            if (const auto* ref = reg.try_get<NodeRef>(tracked.entity);
                ref && ref->handle.id == tracked.node.id)
                reg.remove<NodeRef>(tracked.entity);
        }
    }
    mTracked.clear();
    mPushThisFrame.clear();
    // Node destruction reclaims renderer-side particle instances. The applied
    // handles must be forgotten too or a later renderer attachment sees a
    // numerically valid stale handle and never starts the emitter again.
    reg.clear<ParticlesRef>();
    // The shot this world was driving is gone with its nodes. Forgetting it
    // here is what makes the next sync() re-attach and re-push rather than
    // compare against an entity id from a registry that no longer exists.
    mCameraEntity = entt::null;
    mCameraPushed = false;
}

void SceneSync::sync()
{
    auto& reg = mWorld.registry();

    // Capture entities changed this frame BEFORE updateWorldTransforms() clears
    // their Dirty tags. New entities are Dirty from create(), so they are
    // included and get their first transform push below.
    mPushThisFrame.clear();
    for (auto e : reg.view<Dirty>())
        mPushThisFrame.push_back(e);

    // 1) Entities that want a node and do not have one yet: allocate it and
    //    attach the mesh/light once. "Wants a node" is MeshRenderer, LightRef or
    //    the RenderNode tag -- see the header for why it is not "has Transform".
    const auto materialise = [&](entt::entity e) {
        if (reg.all_of<NodeRef>(e) || !reg.all_of<Transform>(e))
            return;
        const NodeHandle node = mBackend.createNode(
            NodeHandle{}, glm::vec3(0.0f),
            reg.all_of<Name>(e) ? reg.get<Name>(e).value : std::string{});
        reg.emplace<NodeRef>(e, node);
        // A node is born visible; a hidden entity is corrected by the pass in
        // 3e below on this same frame, before anything draws.
        mTracked.push_back(Tracked{e, node, true});
        // The node was just created at the origin, so its transform has to be
        // pushed at least once regardless of who filled the registry. Only
        // World::create() tags Dirty; a registry populated by a deserializer
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
    };
    for (auto e : reg.view<MeshRenderer>(entt::exclude<NodeRef>))
        materialise(e);
    for (auto e : reg.view<LightRef>(entt::exclude<NodeRef>))
        materialise(e);
    for (auto e : reg.view<RenderNode>(entt::exclude<NodeRef>))
        materialise(e);
    // An emitter needs a node to hang off even when the entity draws nothing
    // else, which is the common case: an author drops an entity somewhere and
    // gives it smoke.
    for (auto e : reg.view<ParticleEmitter>(entt::exclude<NodeRef>))
        materialise(e);
    // A camera draws nothing and still needs a node: the node is what carries
    // it through the transform hierarchy, which is the whole reason an orbiting
    // shot is a camera parented to a spinning pivot rather than code.
    for (auto e : reg.view<Camera>(entt::exclude<NodeRef>))
        materialise(e);

    // 2) Recompute world transforms (clears Dirty).
    mWorld.updateWorldTransforms();

    // 3) Push transforms for entities that were dirty this frame. Nodes are
    //    flat under the renderer root, so the entity's whole composed pose has
    //    to be pushed: the world matrix decomposed back into the three the
    //    backend takes. Pushing the world position with the *local* rotation is
    //    the shortcut this used to take, and it is wrong the moment a parent is
    //    rotated -- the child lands in the right place facing the wrong way.
    for (entt::entity e : mPushThisFrame) {
        if (!reg.valid(e) || !reg.all_of<NodeRef>(e))
            continue;
        const NodeHandle node = reg.get<NodeRef>(e).handle;
        const auto* world = reg.try_get<WorldTransform>(e);
        if (!world)
            continue; // never resolved (no Transform); nothing to push yet
        const Transform t = decompose(world->matrix);
        mBackend.setPosition(node, t.position);
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

    // 3c) Material overrides. Pushed when the named material changes rather
    //     than every frame: the backend call rebuilds an Ogre entity's
    //     technique, so doing it per frame would cost more than the draw.
    for (auto e : reg.view<MaterialOverride, NodeRef>()) {
        const MaterialOverride& mo = reg.get<MaterialOverride>(e);
        MaterialApplied& applied = reg.get_or_emplace<MaterialApplied>(e);
        if (applied.material == mo.material)
            continue;
        applied.material = mo.material;
        // An empty override is "no override", not "no material": pushing an
        // empty name would blank the mesh, which is not what removing the
        // component's text should do.
        if (!mo.material.empty())
            mBackend.setNodeMaterial(reg.get<NodeRef>(e).handle, mo.material);
    }

    // 3c2) Per-entity shader uniforms. Pushed on change, because a push means
    //      resolving a named constant per field on a cloned material -- cheap
    //      once, wasteful sixty times a second for a value nobody touched.
    //      A component removed from a live entity puts the shared material
    //      back, which is the difference between "stop glowing" and "keep the
    //      last glow forever".
    for (auto e : reg.view<ShaderParams, NodeRef>()) {
        const ShaderParams& want = reg.get<ShaderParams>(e);
        ShaderParamsApplied& applied = reg.get_or_emplace<ShaderParamsApplied>(e);
        if (applied.valid && applied.value.tint == want.tint &&
            applied.value.opacity == want.opacity &&
            applied.value.rimColour == want.rimColour &&
            applied.value.rimStrength == want.rimStrength &&
            applied.value.rimPower == want.rimPower &&
            applied.value.alphaScissor == want.alphaScissor)
            continue;
        applied.value = want;
        applied.valid = true;
        mBackend.setNodeShaderParams(reg.get<NodeRef>(e).handle, want);
    }
    for (auto e : reg.view<ShaderParamsApplied, NodeRef>(
             entt::exclude<ShaderParams>)) {
        mBackend.clearNodeShaderParams(reg.get<NodeRef>(e).handle);
        reg.remove<ShaderParamsApplied>(e);
    }

    // 3c3) Shader blocks: a component whose fields are named after a shader's
    //      uniforms, pushed wholesale through its own field table. Adding a
    //      shader to the engine is a struct plus a line here -- no renderer
    //      change, because the renderer reads the table rather than the type.
    //
    //      Not compared against a previous value like the block above: these
    //      are tuning components an author drags a slider on, they sit on a
    //      handful of entities, and the comparison would cost more code than
    //      the push it saves.
    for (auto e : reg.view<PortalParams, NodeRef>()) {
        mBackend.setNodeShaderBlock(reg.get<NodeRef>(e).handle,
                                    shaderBlockOf(reg.get<PortalParams>(e)));
    }

    // 3d) Particle emitters. Attached once the effect resolves; an unknown name
    //     leaves the handle invalid and is retried, because the library may
    //     still be loading when the scene is built.
    for (auto e : reg.view<ParticleEmitter, NodeRef>()) {
        const ParticleEmitter& em = reg.get<ParticleEmitter>(e);
        ParticlesRef& ref = reg.get_or_emplace<ParticlesRef>(e);
        const bool wants = em.playing && !em.effect.empty();
        const bool changed = ref.effect != em.effect || ref.offset != em.offset ||
                             ref.scale != em.scale;
        if (ref.handle.valid() && (!wants || changed)) {
            mBackend.detachParticles(ref.handle);
            ref.handle = {};
        }
        if (wants && !ref.handle.valid()) {
            ParticleSpawnOptions options;
            options.sizeScale = em.scale;
            ref.handle = mBackend.attachParticles(reg.get<NodeRef>(e).handle,
                                                   em.effect, em.offset,
                                                   options);
        }
        ref.effect = em.effect;
        ref.offset = em.offset;
        ref.scale = em.scale;
    }
    // An emitter removed from an entity that is still alive: the entity keeps
    // its node, so the destroyed-entity pass below would never see it.
    for (auto e : reg.view<ParticlesRef>(entt::exclude<ParticleEmitter>)) {
        ParticlesRef& ref = reg.get<ParticlesRef>(e);
        if (ref.handle.valid())
            mBackend.detachParticles(ref.handle);
        ref.handle = {};
    }

    // 3e) Visibility. Pushed on change only -- it is a state, and an entity
    //     that is hidden stays hidden without being told again every frame.
    //     Driven off mTracked rather than a view because the previous value
    //     lives there, and because removing the component has to show the node
    //     again: "no Visibility" means visible.
    for (Tracked& tracked : mTracked) {
        if (!reg.valid(tracked.entity))
            continue;
        const auto* vis = reg.try_get<Visibility>(tracked.entity);
        const bool wants = !vis || vis->visible;
        if (wants == tracked.visible)
            continue;
        tracked.visible = wants;
        mBackend.setNodeVisible(tracked.node, wants);
    }

    // 3f) The shot. After the transform push, so the frame the camera is
    //     attached on already has its node where the scene says it is.
    syncCamera();

    // 4) Destroyed entities: free their nodes.
    mTracked.erase(std::remove_if(mTracked.begin(), mTracked.end(),
        [&](const Tracked& tracked) {
            if (reg.valid(tracked.entity))
                return false;
            mBackend.destroyNode(tracked.node);
            return true;
        }), mTracked.end());
}

void SceneSync::syncCamera()
{
    if (!mDrivesCamera)
        return;
    auto& reg = mWorld.registry();

    // The active camera with the highest priority. First-found wins a tie,
    // which is stable for a given scene; a camera that must win says so with a
    // priority rather than by being declared earlier in a file.
    entt::entity chosen = entt::null;
    const Camera* lens = nullptr;
    for (auto e : reg.view<Camera, NodeRef>()) {
        const Camera& candidate = reg.get<Camera>(e);
        if (!candidate.active)
            continue;
        if (!lens || candidate.priority > lens->priority) {
            chosen = e;
            lens = &candidate;
        }
    }
    if (chosen == entt::null) {
        // No authored camera. Deliberately does NOT restore anything: a scene
        // without a Camera is a scene whose camera belongs to the application,
        // and taking it back would fight the player controller every frame.
        mCameraEntity = entt::null;
        mCameraPushed = false;
        return;
    }

    if (chosen != mCameraEntity) {
        mCameraEntity = chosen;
        mCameraPushed = false; // the lens belongs to the new camera now
    }
    // Re-asserted every frame, not only when the shot changes. The renderer has
    // exactly one camera and anything may attach it -- a player controller does
    // so when it spawns, which happens *after* a scene has loaded. Attaching
    // once would mean the scene's camera silently loses to whatever grabbed it
    // last, and "the scene owns the camera" has to be true rather than true on
    // the first frame. It is a detach and an attach of one object; the
    // expensive half is the lens, which is still pushed only on a change.
    mBackend.setCameraNode(reg.get<NodeRef>(chosen).handle);
    // Attaching is cheap but re-projecting is not, and a lens that has not
    // changed is the common case: pushed only when the numbers move, so an
    // author dragging the fov slider sees it live and a running scene does not
    // pay for it.
    if (!mCameraPushed || lens->fovDegrees != mCameraLens.fovDegrees ||
        lens->nearClip != mCameraLens.nearClip ||
        lens->farClip != mCameraLens.farClip) {
        mCameraLens = *lens;
        mCameraPushed = true;
        mBackend.setCameraLens(lens->fovDegrees, lens->nearClip, lens->farClip);
    }
}

} // namespace eng::ecs
