#include "HudModel.h"

#include "combat/CombatComponents.h"

#include <algorithm>
#include <cmath>

namespace game {

float hudResourceRatio(const HudResource& resource)
{
    if (!resource.available || !std::isfinite(resource.current) ||
        !std::isfinite(resource.maximum) || resource.maximum <= 0.0f)
        return 0.0f;
    return std::clamp(resource.current / resource.maximum, 0.0f, 1.0f);
}

const char* hudInteractionAction(const InteractionFocus& focus)
{
    if (!focus.available)
        return "";
    switch (focus.kind) {
        case TargetKind::Torch:
            return focus.active ? "DOUSE THE FLAME" : "KINDLE THE FLAME";
        case TargetKind::PortalDown:
            return "CROSS THE THRESHOLD";
        case TargetKind::PortalUp:
            return "RETURN BY THE KNOWN WAY";
        case TargetKind::Item:
            return "TAKE IT";
        case TargetKind::Npc:
            return "SPEAK";
        case TargetKind::Prop:
        case TargetKind::Actor:
            break; // described by the tooltip, not by a verb here
    }
    return "INTERACT";
}

const char* hudStatusName(CrowdControl status)
{
    switch (status) {
        case CrowdControl::Stun: return "STUNNED";
        case CrowdControl::Root: return "ROOTED";
        case CrowdControl::Silence: return "SILENCED";
        case CrowdControl::Slow: return "SLOWED";
        case CrowdControl::Chill: return "CHILLED";
        case CrowdControl::Burn: return "BURNING";
        case CrowdControl::Count: break;
    }
    return "AFFLICTED";
}

namespace {

template <typename Component>
HudResource resourceFrom(const entt::registry& registry, entt::entity entity)
{
    if (const Component* value = registry.try_get<Component>(entity))
        return {value->current, value->max, true};
    return {};
}

} // namespace

HudSnapshot buildHudSnapshot(const entt::registry& registry,
                              entt::entity player,
                              const PlayerWeaponDef* weapon,
                              const InteractionFocus& interaction)
{
    HudSnapshot snapshot;
    if (player == entt::null || !registry.valid(player))
        return snapshot;

    snapshot.valid = true;
    snapshot.health = resourceFrom<Health>(registry, player);
    snapshot.stamina = resourceFrom<Stamina>(registry, player);
    snapshot.mana = resourceFrom<Mana>(registry, player);
    snapshot.poise = resourceFrom<Poise>(registry, player);
    if (weapon)
        snapshot.weapon = {weapon->displayName, weapon->discipline};
    snapshot.interaction = interaction;

    if (const ActionState* action = registry.try_get<ActionState>(player))
        snapshot.action = action->phase;
    if (const StatusEffects* effects = registry.try_get<StatusEffects>(player)) {
        for (const ActiveEffect& effect : effects->active) {
            if (effect.remaining <= 0.0f ||
                snapshot.statusCount == HudSnapshot::kMaxStatuses)
                continue;
            snapshot.statuses[size_t(snapshot.statusCount++)] =
                {effect.kind, effect.remaining};
        }
    }
    return snapshot;
}

} // namespace game
