#include "HudModel.h"

#include "combat/CombatComponents.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "HudModelTests: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main()
{
    using namespace game;

    {
        HudResource value{25.0f, 100.0f, true};
        require(hudResourceRatio(value) == 0.25f,
                "resource ratio was not normalized");
        value.current = 140.0f;
        require(hudResourceRatio(value) == 1.0f,
                "resource ratio did not clamp above one");
        value.current = std::numeric_limits<float>::quiet_NaN();
        require(hudResourceRatio(value) == 0.0f,
                "non-finite resource did not fail closed");
    }

    {
        InteractionFocus focus;
        focus.available = true;
        focus.kind = TargetKind::Torch;
        require(std::string(hudInteractionAction(focus)) == "KINDLE THE FLAME",
                "unlit torch prompt is unclear");
        focus.active = true;
        require(std::string(hudInteractionAction(focus)) == "DOUSE THE FLAME",
                "lit torch prompt is unclear");
        focus.kind = TargetKind::PortalDown;
        require(std::string(hudInteractionAction(focus)) ==
                    "CROSS THE THRESHOLD",
                "descent prompt lost the discovery language");
    }

    {
        entt::registry registry;
        const entt::entity player = registry.create();
        registry.emplace<Health>(player, Health{72.0f, 100.0f});
        registry.emplace<Stamina>(player, Stamina{40.0f, 80.0f});
        registry.emplace<Mana>(player, Mana{30.0f, 60.0f});
        registry.emplace<Poise>(player, Poise{15.0f, 30.0f});
        ActionState action;
        action.phase = ActionPhase::Deflecting;
        registry.emplace<ActionState>(player, action);
        StatusEffects effects;
        effects.active.push_back(
            {CrowdControl::Burn, 3.0f, 2.0f, 0.0f, entt::null});
        effects.active.push_back(
            {CrowdControl::Slow, 0.2f, -1.0f, 0.0f, entt::null});
        registry.emplace<StatusEffects>(player, effects);

        InteractionFocus focus;
        focus.available = true;
        focus.kind = TargetKind::PortalUp;
        const HudSnapshot snapshot = buildHudSnapshot(registry, player, 1, focus);
        require(snapshot.valid, "valid player did not produce a HUD snapshot");
        require(snapshot.health.current == 72.0f &&
                    snapshot.stamina.maximum == 80.0f &&
                    snapshot.mana.current == 30.0f,
                "combat resources were not copied");
        require(snapshot.action == ActionPhase::Deflecting,
                "action phase was not copied");
        require(snapshot.weapon == HudWeapon::Staff,
                "weapon index was not translated");
        require(snapshot.statusCount == 1 &&
                    snapshot.statuses[0].kind == CrowdControl::Burn,
                "expired status was not filtered");
        require(snapshot.interaction.kind == TargetKind::PortalUp,
                "interaction focus was not copied");
    }

    {
        entt::registry registry;
        require(!buildHudSnapshot(registry, entt::null, 0, {}).valid,
                "missing player produced a valid HUD snapshot");
    }

    std::cout << "HudModelTests OK\n";
    return EXIT_SUCCESS;
}
