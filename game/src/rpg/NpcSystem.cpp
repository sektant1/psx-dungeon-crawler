#include "NpcSystem.h"

#include "GameAssets.h"
#include "GameContext.h"
#include "scene/MapRuntime.h"

#include <eng/Log.h>
#include <eng/Renderer.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <string_view>

namespace game::rpg {

namespace {

// The stand-in. A capsule of the authored height, which reads as a person from
// across a village square in a way a box does not -- and that is the whole job
// until there are character models. These are the shipped prototype materials,
// not new ones: the image is frozen and a villager is not the place to add a
// shader.
constexpr float kBodyRadius = 0.3f;
// Aimed at the chest rather than the feet or the crown, so looking at somebody
// the way a person looks at somebody arms the prompt.
constexpr float kChestFraction = 0.62f;
// Longer than a pickup's reach: you talk to somebody from conversational
// distance, and having to stand on their toes to get a prompt reads as a bug.
constexpr float kReach = 3.4f;
constexpr float kTargetRadius = 0.8f;

const char* defaultMaterial(const NpcDef& def)
{
    // Someone who keeps a shop is worth picking out of a crowd before you are
    // close enough to read their name.
    return def.trades() ? "Game/Prototype/ProjectileEidolon"
                        : "Game/Prototype/Floor";
}

float yawOf(const glm::quat& rotation)
{
    const glm::vec3 forward = rotation * glm::vec3(0.0f, 0.0f, -1.0f);
    return std::atan2(forward.x, -forward.z);
}

} // namespace

int NpcSystem::spawn(GameContext& ctx, const NpcLibrary::Ref& def,
                     glm::vec3 position, float yaw)
{
    if (!def)
        return -1;
    eng::Renderer& r = ctx.renderer;

    Entry entry;
    entry.id = mNextId++;
    entry.def = def;
    entry.position = position;
    entry.yaw = yaw;

    const float halfHeight = def->height * 0.5f;
    entry.node = r.createNode(eng::kRootNode,
                              position + glm::vec3(0.0f, halfHeight, 0.0f),
                              "npc_" + def->id);
    if (!entry.node.valid()) {
        eng::log::error("NpcSystem: no node for '%s'", def->id.c_str());
        return -1;
    }
    r.setOrientation(entry.node,
                     glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f)));

    const std::string material =
        def->material.empty() ? defaultMaterial(*def) : def->material;

    // The shared humanoid, unless this person named their own mesh. The node
    // above stays and the rig hangs off it, so the yaw and everything keyed to
    // this entry keep working exactly as they did.
    //
    // That node was authored raised to the NPC's mid-height, which is what
    // `Centre` says; the body drops itself back to the floor. `def->scale` goes
    // on the body rather than on the parent for the same reason -- scaling the
    // parent would scale that drop too.
    if (def->mesh.empty() && ctx.humanoid.valid()) {
        actor::ActorVisualDesc visual;
        visual.material = material;
        visual.height = def->height;
        visual.anchor = actor::ActorAnchor::Centre;
        visual.scale = glm::vec3(def->scale);
        if (entry.body.create(r, ctx.humanoid, entry.node, visual)) {
            mEntries.push_back(std::move(entry));
            return mEntries.back().id;
        }
    }

    eng::MeshHandle mesh;
    if (!def->mesh.empty()) {
        mesh = r.loadMesh(assetPath(def->mesh));
    } else {
        eng::PrimitiveMeshDesc desc;
        desc.kind = eng::PrimitiveKind::Capsule;
        desc.radius = kBodyRadius;
        // The capsule's `height` is the cylindrical middle; the caps add a
        // radius at each end. Subtracting them is what makes an NPC authored
        // at 1.8 m actually 1.8 m tall rather than 2.4.
        desc.height = std::max(0.05f, def->height - kBodyRadius * 2.0f);
        mesh = r.createPrimitiveMesh(desc);
    }
    if (mesh.valid())
        r.attachMesh(entry.node, mesh, material, "Game/Prototype/Floor",
                     /*castShadows=*/true);
    r.setScale(entry.node, glm::vec3(def->scale));

    mEntries.push_back(std::move(entry));
    return mEntries.back().id;
}

int NpcSystem::spawnAuthored(GameContext& ctx, const NpcLibrary& library,
                             const std::vector<ScenePlacement>& placements)
{
    int spawned = 0;
    for (const ScenePlacement& p : placements) {
        constexpr std::string_view kPrefix = "npc.";
        if (p.type.size() <= kPrefix.size() ||
            p.type.compare(0, kPrefix.size(), kPrefix) != 0)
            continue;
        const std::string id = p.type.substr(kPrefix.size());
        const NpcLibrary::Ref def = library.find(id);
        if (!def) {
            eng::log::error("NpcSystem: the level places 'npc.%s', which "
                            "npcs.toml does not define",
                            id.c_str());
            continue;
        }
        if (spawn(ctx, def, p.position, yawOf(p.rotation)) >= 0)
            ++spawned;
    }
    return spawned;
}

void NpcSystem::appendTargets(std::vector<GameplayTarget>& out) const
{
    for (const Entry& e : mEntries) {
        const float chest = e.def ? e.def->height * kChestFraction : 1.1f;
        out.push_back({TargetKind::Npc, e.id,
                       e.position + glm::vec3(0.0f, chest, 0.0f), kReach,
                       kTargetRadius});
    }
}

void NpcSystem::update(GameContext& ctx, float dt, const glm::vec3* listener)
{
    for (Entry& e : mEntries) {
        if (!e.body.valid())
            continue;
        actor::ActorAnimationInput input;
        // NPCs do not walk yet, so the whole input is "standing here, facing
        // this way". When one does, this is the line that changes and nothing
        // else: the animator already knows what a walk is.
        input.yawRadians = e.yaw;
        // `position` is the floor the NPC was placed on, so the eye is a height
        // above it -- no anchor conversion, and no second copy of one.
        input.eyePosition =
            e.position + glm::vec3(0.0f, e.def ? e.def->height * 0.9f : 1.6f, 0.0f);
        // Everyone notices you; only the one you are talking to gestures. A
        // room where nobody looks up as you pass reads as a room of statues.
        if (listener)
            input.lookTarget = *listener;
        e.body.animator().setStance(e.id == mSpeaking
                                        ? actor::ActorStance::Talking
                                        : actor::ActorStance::Relaxed);
        e.body.update(ctx.renderer, dt, input);
    }
}

void NpcSystem::setSpeaking(int id) { mSpeaking = id; }

const NpcSystem::Entry* NpcSystem::find(int id) const
{
    const auto it = std::find_if(mEntries.begin(), mEntries.end(),
                                 [id](const Entry& e) { return e.id == id; });
    return it == mEntries.end() ? nullptr : &*it;
}

const NpcSystem::Entry* NpcSystem::findByNpcId(const std::string& npc) const
{
    const auto it =
        std::find_if(mEntries.begin(), mEntries.end(), [&](const Entry& e) {
            return e.def && e.def->id == npc;
        });
    return it == mEntries.end() ? nullptr : &*it;
}

void NpcSystem::clear(GameContext& ctx)
{
    for (Entry& e : mEntries) {
        // Before the parent, not after: destroyNode releases the skin instances
        // on the subtree it takes down, so the far end of that order is
        // ActorVisual releasing a handle the renderer has already forgotten.
        e.body.destroy(ctx.renderer);
        if (e.node.valid())
            ctx.renderer.destroyNode(e.node);
    }
    mEntries.clear();
    mSpeaking = -1;
}

} // namespace game::rpg
