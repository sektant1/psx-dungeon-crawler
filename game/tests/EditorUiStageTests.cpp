// The 2D viewport's state -> HUD-input mapping.
//
// The drawing is the game's own GameHud, which has its own tests. What is new
// here is the translation: an author dials ratios and counts, the HUD reads
// current/maximum pairs and a fixed-size status array. The failures live in
// that gap -- a resource that reads full when it is empty, a status count that
// walks off the end of the array -- and none of them are visible in a
// screenshot.

#include "UiStage.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace ed;

static void require(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "EditorUiStageTests: " << message << '\n';
        std::exit(1);
    }
}

static bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

int main()
{
    // --- ratios become current/maximum pairs -------------------------------
    {
        UiStageState state;
        state.health = 0.25f;
        state.stamina = 0.0f;
        state.mana = 1.0f;
        const game::HudSnapshot snapshot = hudSnapshotFrom(state);
        require(snapshot.valid, "the stage always produces a drawable snapshot");
        require(near(game::hudResourceRatio(snapshot.health), 0.25f),
                "a quarter dialled in reads as a quarter");
        require(near(game::hudResourceRatio(snapshot.stamina), 0.0f),
                "empty reads as empty, not as full");
        require(near(game::hudResourceRatio(snapshot.mana), 1.0f),
                "full reads as full");
        require(snapshot.health.available && snapshot.stamina.available,
                "every dialled resource is drawn, including an empty one");
    }

    // --- out-of-range input is clamped, not trusted -------------------------
    {
        UiStageState state;
        state.health = 2.5f;
        state.stamina = -1.0f;
        const game::HudSnapshot snapshot = hudSnapshotFrom(state);
        require(near(game::hudResourceRatio(snapshot.health), 1.0f),
                "over-full clamps to full rather than overdrawing the bar");
        require(near(game::hudResourceRatio(snapshot.stamina), 0.0f),
                "negative clamps to empty");
    }

    // --- the status array cannot be overrun --------------------------------
    // A count past kMaxStatuses would write over whatever follows the array,
    // and the symptom would be a corrupted neighbouring field rather than a
    // missing icon.
    {
        UiStageState state;
        state.statusCount = 99;
        const game::HudSnapshot snapshot = hudSnapshotFrom(state);
        require(snapshot.statusCount == game::HudSnapshot::kMaxStatuses,
                "more statuses than the HUD holds are clamped to what it holds");
        state.statusCount = -3;
        require(hudSnapshotFrom(state).statusCount == 0,
                "a negative count is none, not a huge one");
    }

    // Distinct kinds, so the row shows the variety a real fight produces
    // rather than four copies of one icon.
    {
        UiStageState state;
        state.statusCount = 3;
        const game::HudSnapshot snapshot = hudSnapshotFrom(state);
        require(snapshot.statuses[0].kind != snapshot.statuses[1].kind &&
                    snapshot.statuses[1].kind != snapshot.statuses[2].kind,
                "consecutive statuses differ");
        require(snapshot.statuses[0].remaining > 0.0f,
                "a status the author asked for has time left on it");
    }

    // --- the weapon block is what the author typed --------------------------
    {
        UiStageState state;
        state.weaponName = "A VERY LONG WEAPON NAME INDEED";
        state.weaponDiscipline = "SOME / DISCIPLINE";
        const game::HudSnapshot snapshot = hudSnapshotFrom(state);
        require(snapshot.weapon.name == state.weaponName,
                "the weapon name passes through verbatim -- the point of the "
                "panel is to see a long one overflow");
        require(snapshot.weapon.discipline == state.weaponDiscipline,
                "and so does the discipline");
    }

    // --- the tooltip is opt-in ---------------------------------------------
    {
        UiStageState state;
        require(hudTooltipFrom(state).empty(),
                "no tooltip unless the stage asks for one");
        require(!hudSnapshotFrom(state).interaction.available,
                "and the snapshot agrees, so the HUD does not reserve its band");

        state.showTooltip = true;
        state.tooltipTitle = "Iron Sconce";
        const eng::ui::TooltipContent tooltip = hudTooltipFrom(state);
        require(!tooltip.empty() && tooltip.title == "Iron Sconce",
                "an asked-for tooltip carries the author's title");
        require(hudSnapshotFrom(state).interaction.available,
                "and the snapshot reports the interaction it belongs to");
    }

    // --- fitScale is integer, bounded, and never zero -----------------------
    // Fractional scale is how a bitmap-font HUD gets soft edges and uneven
    // letter spacing, so the canvas only ever magnifies by whole numbers.
    {
        require(fitScale({320, 240}, {640.0f, 480.0f}) == 2, "an exact fit");
        require(fitScale({320, 240}, {700.0f, 480.0f}) == 2,
                "the limiting axis decides");
        require(fitScale({320, 240}, {100.0f, 100.0f}) == 1,
                "too small still draws, at 1:1, rather than vanishing");
        require(fitScale({0, 0}, {640.0f, 480.0f}) >= 1,
                "a degenerate size does not divide by zero");
        require(fitScale({320, 240}, {40000.0f, 40000.0f}) == 8,
                "and magnification is capped");
    }

    std::cout << "EditorUiStageTests: ok\n";
    return 0;
}
