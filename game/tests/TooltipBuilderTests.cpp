#include "ui/TooltipBuilder.h"

#include <cstdlib>
#include <limits>
#include <string>

namespace {

bool has(const eng::ui::TooltipContent& content, const std::string& needle) {
    if (content.title.find(needle) != std::string::npos)
        return true;
    for (const auto& line : content.lines)
        if (line.text.find(needle) != std::string::npos)
            return true;
    return false;
}

} // namespace

int main()
{
    const game::ActorLook noActor;

    // No focus, no tooltip: the HUD must be able to ask every frame.
    if (!game::buildTooltip({}, nullptr, noActor, "E").empty())
        return EXIT_FAILURE;

    // A torch describes itself and offers the right verb for its state.
    game::InteractionFocus torch;
    torch.available = true;
    torch.kind = TargetKind::Torch;
    torch.id = 3;
    torch.active = true;
    torch.distance = 1.5f;
    const eng::ui::TooltipContent lit =
        game::buildTooltip(torch, nullptr, noActor, "F");
    if (lit.action != "DOUSE" || lit.actionKey != "F")
        return EXIT_FAILURE;
    torch.active = false;
    if (game::buildTooltip(torch, nullptr, noActor, "F").action != "KINDLE")
        return EXIT_FAILURE;
    // Distance is metadata, not prose: it belongs in the header field, where
    // the presentation layer can right-align it, and must NOT be mixed into
    // the body lines where it reads as flavour text.
    if (lit.meta != "1.5 m")
        return EXIT_FAILURE;
    if (has(lit, "1.5 m"))
        return EXIT_FAILURE;

    // A prop is pure data: name, category, body and accent all come from the
    // catalog, and a prop with no catalog entry produces nothing at all.
    game::InteractionFocus propFocus;
    propFocus.available = true;
    propFocus.kind = TargetKind::Prop;
    propFocus.id = 0;
    propFocus.catalogIndex = 2;
    if (!game::buildTooltip(propFocus, nullptr, noActor, "E").empty())
        return EXIT_FAILURE;

    game::PropInfo chest;
    chest.id = "loot_chest";
    chest.displayName = "Iron-Bound Chest";
    chest.category = "Container";
    chest.description = "Banded oak.";
    chest.rarity = "rare";
    chest.interact = "PRISE OPEN";
    const eng::ui::TooltipContent propTip =
        game::buildTooltip(propFocus, &chest, noActor, "E");
    if (propTip.title != "Iron-Bound Chest" ||
        propTip.subtitle != "Container" || propTip.action != "PRISE OPEN")
        return EXIT_FAILURE;
    if (propTip.accent != game::rarityAccent("rare"))
        return EXIT_FAILURE;
    if (game::rarityAccent("rare") == game::rarityAccent("common"))
        return EXIT_FAILURE;
    // An unknown rarity must degrade to common, not to an undefined colour.
    if (game::rarityAccent("gilded") != game::rarityAccent("common"))
        return EXIT_FAILURE;

    // An inert prop offers no key prompt at all.
    chest.interact.clear();
    if (!game::buildTooltip(propFocus, &chest, noActor, "E").action.empty())
        return EXIT_FAILURE;

    // An actor contributes a health bar, and only when it has one.
    game::InteractionFocus actorFocus;
    actorFocus.available = true;
    actorFocus.kind = TargetKind::Actor;
    game::ActorLook actor;
    actor.valid = true;
    actor.name = "Training Effigy";
    actor.health = 25.0f;
    actor.healthMax = 100.0f;
    const eng::ui::TooltipContent actorTip =
        game::buildTooltip(actorFocus, nullptr, actor, "E");
    if (actorTip.bars.size() != 1 || actorTip.bars[0].ratio > 0.2501f ||
        actorTip.bars[0].ratio < 0.2499f)
        return EXIT_FAILURE;
    // A combatant asks to be shown as the focus, which is what routes it to
    // the top banner instead of under the crosshair. Everything else stays
    // inline. This is the one bit of presentation intent gameplay owns, so it
    // is worth pinning here rather than in the HUD.
    if (actorTip.emphasis != eng::ui::TooltipEmphasis::Focus)
        return EXIT_FAILURE;
    if (lit.emphasis != eng::ui::TooltipEmphasis::Inline)
        return EXIT_FAILURE;
    if (game::buildTooltip(propFocus, &chest, noActor, "E").emphasis !=
        eng::ui::TooltipEmphasis::Inline)
        return EXIT_FAILURE;
    // The bar carries its own readout, so the banner can print it without
    // knowing what the numbers mean.
    if (actorTip.bars[0].value != "25/100")
        return EXIT_FAILURE;

    actor.healthMax = 0.0f;
    if (!game::buildTooltip(actorFocus, nullptr, actor, "E").bars.empty())
        return EXIT_FAILURE;

    actor.health = std::numeric_limits<float>::quiet_NaN();
    actor.healthMax = 100.0f;
    const auto invalidHealth =
        game::buildTooltip(actorFocus, nullptr, actor, "E");
    if (invalidHealth.bars.size() != 1 || invalidHealth.bars[0].ratio != 0.0f ||
        invalidHealth.bars[0].value != "0/100")
        return EXIT_FAILURE;
    actor.healthMax = std::numeric_limits<float>::infinity();
    if (!game::buildTooltip(actorFocus, nullptr, actor, "E").bars.empty())
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}
