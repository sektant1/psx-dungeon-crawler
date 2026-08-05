#pragma once
#include "Channel.h"
#include "RpgTypes.h"

#include <eng/StringId.h>

// The channels the game raises, and the one the quest system raises back.
//
// Each gameplay system owns its channel and is the only thing that raises on
// it; everything else subscribes. That is the whole coupling story between
// combat, inventory, the world and the quest log: the enemy system does not
// know quests exist, and a quest does not know what an enemy system is.
//
// One struct holds them all so a quest's `onActivate` receives one reference
// and picks the channel it needs, instead of every Quest constructor taking a
// different set of arguments.
namespace game::rpg {

// Something died. `enemy` is the enemy *definition* id, which is what a quest
// names ("kill six cinder thralls"), not the entity.
using CombatChannel = Channel<eng::StringId /*enemy*/, int /*count*/>;

// The player's holdings changed. `held` is the new total across the backpack,
// which is what a collect objective tests -- a quest asking for six reagents
// must not be satisfied by picking up one six times and spending five.
// `delta` is signed, for the objectives that care about the act rather than
// the holding (delivering, selling, using).
using InventoryChannel =
    Channel<eng::StringId /*item*/, int /*held*/, int /*delta*/>;

// Everything else the world reports. Kept as three narrow channels rather than
// one wide "world event" so a subscriber's signature still says what it is
// listening for.
using FlagChannel = Channel<eng::StringId /*flag*/>;
using NpcChannel = Channel<eng::StringId /*npc*/>;
using DepthChannel = Channel<int /*depth*/, bool /*extracted*/>;

class Quest;

// The quest system's own pipeline, per the reference architecture: assignment
// and completion are announced, and whoever cares about rewards, UI or audio
// subscribes. QuestBook raises these; it never grants a reward itself.
struct QuestChannel {
    Channel<Quest&> assigned;
    Channel<Quest&> completed;  // objectives satisfied
    Channel<Quest&> turnedIn;   // rewards collected
    Channel<Quest&> failed;
};

// Everything a quest may listen to. Held by reference: the systems that own
// these outlive the quest log.
struct GameChannels {
    CombatChannel combat;
    InventoryChannel inventory;
    FlagChannel flags;
    NpcChannel npcs;
    DepthChannel depth;
    QuestChannel quests;

    void clear()
    {
        combat.clear();
        inventory.clear();
        flags.clear();
        npcs.clear();
        depth.clear();
        quests.assigned.clear();
        quests.completed.clear();
        quests.turnedIn.clear();
        quests.failed.clear();
    }
};

} // namespace game::rpg
