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

// An item on the floor, or a person worth talking to, flattened the same way.
// Filled by whoever owns the RPG state; the builder stays a pure function of
// values and never sees an ItemLibrary.
struct PickupLook {
    bool valid = false;
    std::string name;
    std::string category;    // "Reagent - 0.3 kg" and so on
    std::string description;
    std::string rarity;      // one of the rarity words; drives the accent
    int count = 1;
    // False when the player's pack cannot take it. The tooltip says so instead
    // of offering a verb that will not work.
    bool takeable = true;
};

struct NpcLook {
    bool valid = false;
    std::string name;
    std::string role;        // "Blacksmith", "Physician"
    std::string line;        // one line of flavour, or the pending request
    bool hasQuest = false;
    bool questReady = false; // something to hand in
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
                                     std::string_view interactKey,
                                     const PickupLook& pickup = {},
                                     const NpcLook& npc = {});

} // namespace game
