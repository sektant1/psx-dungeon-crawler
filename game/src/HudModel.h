#pragma once

#include "Targeting.h"
#include "combat/DamageTypes.h"
#include "combat/FeelComponents.h"

#include <entt/entt.hpp>

#include <array>

namespace game {

struct HudResource {
    float current = 0.0f;
    float maximum = 0.0f;
    bool available = false;
};

struct HudStatus {
    CrowdControl kind = CrowdControl::Stun;
    float remaining = 0.0f;
};

enum class HudWeapon { Blade, Staff, Torch, Unknown };

// Immutable render-frame data. Simulation components are copied after the
// fixed tick, so the HUD never owns or mutates combat state while drawing.
struct HudSnapshot {
    static constexpr int kMaxStatuses = 4;

    bool valid = false;
    HudResource health;
    HudResource stamina;
    HudResource mana;
    HudResource poise;
    ActionPhase action = ActionPhase::Idle;
    HudWeapon weapon = HudWeapon::Unknown;
    InteractionFocus interaction;
    std::array<HudStatus, kMaxStatuses> statuses{};
    int statusCount = 0;
};

float hudResourceRatio(const HudResource& resource);
HudWeapon hudWeaponFromIndex(int weaponIndex);
const char* hudWeaponName(HudWeapon weapon);
const char* hudWeaponDiscipline(HudWeapon weapon);
const char* hudInteractionAction(const InteractionFocus& focus);
const char* hudStatusName(CrowdControl status);

HudSnapshot buildHudSnapshot(const entt::registry& registry,
                             entt::entity player, int weaponIndex,
                             const InteractionFocus& interaction);

} // namespace game
