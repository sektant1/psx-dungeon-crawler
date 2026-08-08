#pragma once

#include "Targeting.h"
#include "PlayerWeapons.h"
#include "combat/DamageTypes.h"
#include "combat/FeelComponents.h"

#include <entt/entt.hpp>

#include <array>
#include <string>

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

struct HudWeapon {
    std::string name = "EMPTY HAND";
    std::string discipline = "NO DISCIPLINE";
    // Ammunition, for the shooter loadout. `magazineMax == 0` means this weapon
    // has no magazine and the HUD should draw no ammo readout at all -- which
    // is what every fantasy weapon reports, so one HUD serves both loadouts
    // without a mode flag.
    int magazine = 0;
    int magazineMax = 0;
    int reserve = 0;         // -1 is infinite: draw an infinity glyph, not "-1"
    bool reloading = false;
    float reloadProgress = 0.0f; // 0..1, for a ring or bar under the readout

    bool usesMagazine() const { return magazineMax > 0; }
    bool operator==(const HudWeapon&) const = default;
};

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
    HudWeapon weapon;
    InteractionFocus interaction;
    std::array<HudStatus, kMaxStatuses> statuses{};
    int statusCount = 0;
};

float hudResourceRatio(const HudResource& resource);
const char* hudInteractionAction(const InteractionFocus& focus);
const char* hudStatusName(CrowdControl status);

HudSnapshot buildHudSnapshot(const entt::registry& registry,
                              entt::entity player,
                              const PlayerWeaponDef* weapon,
                              const InteractionFocus& interaction,
                              // Null for a loadout with no magazines, which
                              // leaves the ammo fields at zero and tells the
                              // HUD to draw no readout.
                              const WeaponAmmoState* ammo = nullptr);

} // namespace game
