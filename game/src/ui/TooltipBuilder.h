#pragma once

#include "../PropInfo.h"
#include "../Targeting.h"

#include <eng/ui/Tooltip.h>

#include <string>
#include <string_view>

namespace game {

// A combatant the crosshair is on, flattened for presentation. Filled by
// whoever owns the actor (main.cpp today); the builder never touches the
// registry, so it stays a pure function of values.
struct ActorLook {
    bool valid = false;
    std::string name;
    std::string category;
    float health = 0.0f;
    float healthMax = 0.0f;
    bool hostile = true;
};

// Accent colour for a rarity word. Unknown rarities read as common rather
// than as an error, so a typo in the catalog dims a tooltip instead of
// crashing a level.
eng::ui::UiTone rarityAccent(std::string_view rarity);

// Turns the look-target focus into tooltip content. Torches and portals get
// their text here because they are engine-level fixtures with no catalog
// entry; everything else is data.
eng::ui::TooltipContent buildTooltip(const InteractionFocus& focus,
                                     const PropInfo* prop,
                                     const ActorLook& actor,
                                     std::string_view interactKey);

} // namespace game
