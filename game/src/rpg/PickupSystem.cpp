#include "PickupSystem.h"

#include "GameAssets.h"
#include "GameContext.h"
#include "scene/MapRuntime.h"

#include <eng/Log.h>
#include <eng/Renderer.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace game::rpg {

namespace {

// Rarity decides the stand-in's colour when an item ships no mesh of its own,
// so an unfinished content tree still reads at a glance: junk is dull, a relic
// glows. These are the shipped prototype materials, not new ones -- the image
// is frozen and a pickup is not the place to add a shader.
const char* materialFor(Rarity rarity)
{
    switch (rarity) {
        // Only three projectile materials exist (prototype.mat), so the five
        // rarities share them rather than naming materials that do not exist:
        // assetlint fails the build on a dangling material name, and a
        // placeholder that fails the build is worse than one that repeats.
        case Rarity::Common:   return "Game/Prototype/Floor";
        case Rarity::Uncommon: return "Game/Prototype/ProjectileEidolon";
        case Rarity::Rare:     return "Game/Prototype/ProjectileVesper";
        case Rarity::Arcane:   return "Game/Prototype/ProjectileVesper";
        case Rarity::Relic:    return "Game/Prototype/ProjectileTalon";
        case Rarity::Count:    break;
    }
    return "Game/Prototype/Floor";
}

// Small enough to read as loot rather than furniture (the same rule
// assets/config/kit.toml applies to its item props), and lifted clear of the
// floor so it is visible over a lip or a rug.
constexpr float kBaseSize = 0.22f;
constexpr float kHoverHeight = 0.45f;
constexpr float kBobAmplitude = 0.06f;
constexpr float kBobRate = 1.9f;
constexpr float kSpinRate = 0.9f;
// Generous, because a boomer shooter's player is moving fast and lining a
// crosshair up on a 22 cm cube at a run is not the interaction being tested.
constexpr float kReach = 2.6f;
constexpr float kTargetRadius = 0.55f;

} // namespace

int PickupSystem::spawn(GameContext& ctx, const ItemDef& def,
                        glm::vec3 position, int count, bool fromLoot)
{
    eng::Renderer& r = ctx.renderer;

    Entry entry;
    entry.id = mNextId++;
    entry.item = def.id;
    entry.count = std::max(1, count);
    entry.position = position;
    entry.fromLoot = fromLoot;
    // Phase from the id rather than from a clock, so two pickups that land on
    // the same frame do not bob in lockstep like one object.
    entry.bobPhase = float(entry.id) * 1.37f;

    entry.node = r.createNode(eng::kRootNode,
                              position + glm::vec3(0.0f, kHoverHeight, 0.0f),
                              "pickup_" + def.id);
    if (!entry.node.valid()) {
        eng::log::error("PickupSystem: no node for '%s'", def.id.c_str());
        return -1;
    }

    const std::string material =
        def.material.empty() ? materialFor(def.rarity) : def.material;
    eng::MeshHandle mesh;
    if (!def.mesh.empty()) {
        mesh = r.loadMesh(assetPath(def.mesh));
    } else {
        // A bevelled box, not a plain one: the bevel catches the vertex
        // lighting as it spins, which is what makes a static-lit prototype
        // item look like it is turning at all.
        eng::PrimitiveMeshDesc desc;
        desc.kind = eng::PrimitiveKind::BeveledBox;
        desc.size = glm::vec3(kBaseSize * def.pickupScale);
        desc.bevel = 0.06f;
        mesh = r.createPrimitiveMesh(desc);
    }
    if (mesh.valid())
        r.attachMesh(entry.node, mesh, material, "Game/Prototype/Floor",
                     /*castShadows=*/false);
    r.setScale(entry.node, glm::vec3(def.mesh.empty() ? 1.0f : def.pickupScale));
    if (!def.pickupEffect.empty())
        r.spawnParticles(def.pickupEffect,
                         position + glm::vec3(0.0f, kHoverHeight, 0.0f));

    mEntries.push_back(std::move(entry));
    return mEntries.back().id;
}

int PickupSystem::spawnAuthored(GameContext& ctx, const ItemLibrary& library,
                                const std::vector<ScenePlacement>& placements)
{
    int spawned = 0;
    for (const ScenePlacement& p : placements) {
        // MapRuntime reports these already prefixed ("pickup.<id>"), the same
        // shape enemy spawns arrive in, so the two sources are
        // indistinguishable downstream.
        constexpr std::string_view kPrefix = "pickup.";
        if (p.type.size() <= kPrefix.size() ||
            p.type.compare(0, kPrefix.size(), kPrefix) != 0)
            continue;
        const std::string id = p.type.substr(kPrefix.size());
        const ItemLibrary::Ref def = library.find(id);
        if (!def) {
            eng::log::error("PickupSystem: the level places 'pickup.%s', which "
                            "items.toml does not define",
                            id.c_str());
            continue;
        }
        if (spawn(ctx, *def, p.position, 1, /*fromLoot=*/false) >= 0)
            ++spawned;
    }
    return spawned;
}

void PickupSystem::update(GameContext& ctx, float dt)
{
    mTime += dt;
    eng::Renderer& r = ctx.renderer;
    for (const Entry& e : mEntries) {
        if (!e.node.valid())
            continue;
        const float bob =
            std::sin(mTime * kBobRate + e.bobPhase) * kBobAmplitude;
        r.setPosition(e.node,
                      e.position + glm::vec3(0.0f, kHoverHeight + bob, 0.0f));
        r.setOrientation(e.node,
                         glm::angleAxis(mTime * kSpinRate + e.bobPhase,
                                        glm::vec3(0.0f, 1.0f, 0.0f)));
    }
}

void PickupSystem::appendTargets(std::vector<GameplayTarget>& out) const
{
    for (const Entry& e : mEntries)
        out.push_back({TargetKind::Item, e.id,
                       e.position + glm::vec3(0.0f, kHoverHeight, 0.0f), kReach,
                       kTargetRadius});
}

const PickupSystem::Entry* PickupSystem::find(int id) const
{
    const auto it = std::find_if(mEntries.begin(), mEntries.end(),
                                 [id](const Entry& e) { return e.id == id; });
    return it == mEntries.end() ? nullptr : &*it;
}

std::optional<PickupSystem::Entry> PickupSystem::take(GameContext& ctx, int id)
{
    const auto it = std::find_if(mEntries.begin(), mEntries.end(),
                                 [id](const Entry& e) { return e.id == id; });
    if (it == mEntries.end())
        return std::nullopt;
    Entry taken = *it;
    if (taken.node.valid())
        ctx.renderer.destroyNode(taken.node);
    ctx.renderer.spawnParticles("engine.pickup_burst",
                                taken.position +
                                    glm::vec3(0.0f, kHoverHeight, 0.0f));
    mEntries.erase(it);
    return taken;
}

void PickupSystem::clear(GameContext& ctx)
{
    for (Entry& e : mEntries)
        if (e.node.valid())
            ctx.renderer.destroyNode(e.node);
    mEntries.clear();
}

} // namespace game::rpg
