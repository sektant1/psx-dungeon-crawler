#include "TooltipBuilder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

namespace game {
namespace {

// Rarity reads as a colour before it reads as a word, so the accent rail does
// the work and the label stays plain text.
std::string metres(float distance) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.1f m", double(distance));
    return buffer;
}

int displayInteger(float value) {
    if (!std::isfinite(value))
        return 0;
    const double clamped = std::clamp(
        double(value), 0.0, double(std::numeric_limits<int>::max()));
    return int(std::lround(clamped));
}

} // namespace

eng::ui::UiTone rarityAccent(std::string_view rarity) {
    if (rarity == "uncommon")
        return eng::ui::UiTone::Positive;
    if (rarity == "rare")
        return eng::ui::UiTone::Edge;
    if (rarity == "arcane")
        return eng::ui::UiTone::Mystic;
    return eng::ui::UiTone::Muted;
}

eng::ui::TooltipContent buildTooltip(const InteractionFocus& focus,
                                     const PropInfo* prop,
                                     const ActorLook& actor,
                                     std::string_view interactKey,
                                     const PickupLook& pickup,
                                     const NpcLook& npc) {
    eng::ui::TooltipContent content;
    if (!focus.available)
        return content;

    switch (focus.kind) {
    case TargetKind::Torch:
        content.id = "torch/" + std::to_string(focus.id);
        content.title = "Wall Torch";
        content.subtitle = focus.active ? "Fixture - burning" : "Fixture - cold";
        content.accent = eng::ui::UiTone::Warning;
        content.lines.push_back(
            {focus.active ? "Pitch-soaked and steady. Dousing it darkens the "
                            "room but hides you in it."
                          : "Cold pitch, still dry enough to catch."});
        content.action = focus.active ? "DOUSE" : "KINDLE";
        content.actionKey = std::string(interactKey);
        break;

    case TargetKind::PortalDown:
        content.id = "portal/down";
        content.title = "Descending Gate";
        content.subtitle = "Threshold";
        content.accent = eng::ui::UiTone::Mystic;
        content.lines.push_back({"The membrane pulls inward. Beyond it the "
                                 "dungeon starts again, deeper."});
        content.action = "DESCEND";
        content.actionKey = std::string(interactKey);
        break;

    case TargetKind::PortalUp:
        content.id = "portal/up";
        content.title = "Ascending Gate";
        content.subtitle = "Threshold";
        content.accent = eng::ui::UiTone::Warning;
        content.lines.push_back({"The way you came. It remembers the floor "
                                 "above."});
        content.action = "ASCEND";
        content.actionKey = std::string(interactKey);
        break;

    case TargetKind::Prop:
        if (!prop)
            return {};
        content.id = "prop/" + prop->id + "/" + std::to_string(focus.id);
        content.title = prop->displayName;
        content.subtitle = prop->category;
        content.accent = rarityAccent(prop->rarity);
        if (!prop->description.empty())
            content.lines.push_back({prop->description});
        if (!prop->interact.empty()) {
            content.action = prop->interact;
            content.actionKey = std::string(interactKey);
        }
        break;

    case TargetKind::Actor:
        if (!actor.valid)
            return {};
        content.id = "actor/" + std::to_string(focus.id);
        content.title = actor.name;
        content.subtitle = actor.category;
        content.accent = actor.hostile ? eng::ui::UiTone::Danger
                                       : eng::ui::UiTone::Positive;
        // A combatant is the thing you are engaging with, so it goes to the
        // banner: a panel under the crosshair would cover the enemy you are
        // aiming at, which is the one place it must not be.
        content.emphasis = eng::ui::TooltipEmphasis::Focus;
        if (std::isfinite(actor.healthMax) && actor.healthMax > 0.0f) {
            const float health = std::isfinite(actor.health)
                                     ? std::clamp(actor.health, 0.0f,
                                                  actor.healthMax)
                                     : 0.0f;
            char readout[32];
            std::snprintf(readout, sizeof(readout), "%d/%d",
                          displayInteger(health),
                          displayInteger(actor.healthMax));
            content.bars.push_back(
                {"VIT", health / actor.healthMax,
                 actor.hostile ? eng::ui::UiTone::Danger
                               : eng::ui::UiTone::Positive,
                 readout});
        }
        break;

    case TargetKind::Item:
        if (!pickup.valid)
            return {};
        content.id = "item/" + std::to_string(focus.id);
        content.title = pickup.count > 1
                            ? pickup.name + " x" + std::to_string(pickup.count)
                            : pickup.name;
        content.subtitle = pickup.category;
        content.accent = rarityAccent(pickup.rarity);
        if (!pickup.description.empty())
            content.lines.push_back({pickup.description});
        if (pickup.takeable) {
            content.action = "TAKE";
            content.actionKey = std::string(interactKey);
        } else {
            // No verb, because pressing it would do nothing. Saying why is the
            // difference between a full pack and a broken game.
            content.lines.push_back({"Too heavy to carry any more of."});
            content.accent = eng::ui::UiTone::Warning;
        }
        break;

    case TargetKind::Npc:
        if (!npc.valid)
            return {};
        content.id = "npc/" + std::to_string(focus.id);
        content.title = npc.name;
        content.subtitle = npc.role;
        // Someone with something to hand in reads as positive; someone with a
        // request reads as mystic; idle small talk stays neutral. §20 wants
        // minimal quest markers, and a tooltip the player has to aim at is
        // about as minimal as a marker gets.
        content.accent = npc.questReady  ? eng::ui::UiTone::Positive
                         : npc.hasQuest ? eng::ui::UiTone::Mystic
                                                       : eng::ui::UiTone::Text;
        if (!npc.line.empty())
            content.lines.push_back({npc.line});
        content.action = "SPEAK";
        content.actionKey = std::string(interactKey);
        break;
    }

    // Distance is the one value every target carries: it is what tells the
    // player whether the prompt will arm if they step forward. It goes in the
    // header rather than the body -- as a body line it was indistinguishable
    // from flavour text, which is exactly what it is not.
    content.meta = metres(focus.distance);
    return content;
}

} // namespace game
