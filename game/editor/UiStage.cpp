#include "UiStage.h"

#include <algorithm>
#include <cstddef>

namespace ed {

game::HudSnapshot hudSnapshotFrom(const UiStageState& state)
{
    game::HudSnapshot snapshot;
    snapshot.valid = true;

    // Ratios in, absolute values out: an author reasons in "half health", and
    // the HUD reads a current/maximum pair. 100 is arbitrary and only ever
    // shown when the numbers are switched on.
    const auto resource = [](float ratio) {
        game::HudResource out;
        out.available = true;
        out.maximum = 100.0f;
        out.current = std::clamp(ratio, 0.0f, 1.0f) * out.maximum;
        return out;
    };
    snapshot.health = resource(state.health);
    snapshot.stamina = resource(state.stamina);
    snapshot.mana = resource(state.mana);
    snapshot.poise = resource(state.poise);

    snapshot.weapon.name = state.weaponName;
    snapshot.weapon.discipline = state.weaponDiscipline;

    // Clamped to the array the snapshot actually has: a stage that asked for
    // more statuses than the HUD can hold would write past it, and the failure
    // would be a corrupted neighbouring field rather than a missing icon.
    snapshot.statusCount =
        std::clamp(state.statusCount, 0, game::HudSnapshot::kMaxStatuses);
    for (int i = 0; i < snapshot.statusCount; ++i) {
        // Different kinds, so the row shows the variety a real fight produces
        // rather than four copies of one icon.
        static constexpr game::CrowdControl kinds[] = {
            game::CrowdControl::Stun, game::CrowdControl::Burn,
            game::CrowdControl::Slow, game::CrowdControl::Root};
        constexpr std::size_t kKindCount = sizeof(kinds) / sizeof(kinds[0]);
        snapshot.statuses[std::size_t(i)].kind =
            kinds[std::size_t(i) % kKindCount];
        snapshot.statuses[std::size_t(i)].remaining = 3.0f - float(i) * 0.5f;
    }

    snapshot.interaction.available = state.showTooltip;
    return snapshot;
}

eng::ui::TooltipContent hudTooltipFrom(const UiStageState& state)
{
    eng::ui::TooltipContent content;
    if (!state.showTooltip)
        return content;
    content.id = "ui_stage";
    content.title = state.tooltipTitle;
    content.subtitle = state.tooltipBody;
    content.action = "USE";
    content.actionKey = "E";
    return content;
}

int fitScale(glm::ivec2 virtualSize, glm::vec2 availablePixels)
{
    const int byWidth = int(availablePixels.x) / std::max(virtualSize.x, 1);
    const int byHeight = int(availablePixels.y) / std::max(virtualSize.y, 1);
    return std::clamp(std::min(byWidth, byHeight), 1, 8);
}

} // namespace ed
