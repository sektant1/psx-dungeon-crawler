#pragma once
#include "Npcs.h"
#include "Targeting.h"
#include "actor/ActorVisual.h"

#include <eng/Handles.h>

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace game {
struct GameContext;
struct ScenePlacement; // scene/MapRuntime.h
}

// People standing in the world.
//
// The same shape as PickupSystem and for the same reason: the editor can place
// an NPC, SceneCook writes it into the `.map` and MapRuntime reports it, and
// before this nothing read that call -- so an authored villager was an entity
// in the file that nobody could ever meet.
//
// An NPC is a render node and a look target, not a physics body. A villager
// does not need to be pushed, shot or stood on for the vertical slice, and
// giving each one a Jolt body would put a permanent island next to the player
// in the one level they spend the most time standing still in. When somebody
// needs to be walked around rather than through, that is a Collider on the
// authored entity, which the cooker already understands -- not a change here.
namespace game::rpg {

class NpcSystem {
public:
    struct Entry {
        int id = 0;                // stable while the NPC exists
        NpcLibrary::Ref def;       // never null for an entry that exists
        glm::vec3 position{0.0f};
        float yaw = 0.0f;
        eng::NodeHandle node;
        // The animated body. Invalid when this NPC named its own mesh, or when
        // the shared rig failed to load -- `node` is then the old static
        // capsule, and everything else about an NPC is unchanged.
        actor::ActorVisual body;
    };

    // Stand one person at a spot. Returns the new entry's id, or -1 when the
    // renderer refused (no node budget left).
    int spawn(game::GameContext&, const NpcLibrary::Ref& def,
              glm::vec3 position, float yaw);

    // The authored half: `npc.<id>` placements out of the cooked map. Unknown
    // ids are logged and skipped, so a level naming a deleted person says so
    // rather than quietly standing nobody there.
    int spawnAuthored(game::GameContext&, const NpcLibrary&,
                      const std::vector<game::ScenePlacement>& placements);

    // Publish everyone within reach as a look target.
    void appendTargets(std::vector<GameplayTarget>& out) const;

    // Advance everyone's idle. `dt` is the stepped creature channel, the same
    // one enemies animate on, so a villager and a goblin standing in the same
    // room move in the same beats. `listener` is who the talkers turn toward.
    void update(game::GameContext&, float dt, const glm::vec3* listener);
    // Who this NPC is talking to, which is what puts them in the talk idle.
    // Cleared by passing -1; the dialogue layer owns when that happens.
    void setSpeaking(int id);

    const Entry* find(int id) const;
    // Who this is, by npc id -- for the quest and dialogue code, which knows
    // people by name and not by placement.
    const Entry* findByNpcId(const std::string& npc) const;

    // Level transition: destroy every node. Who a person *is* lives in the
    // library and the save; what stands in this level does not survive it.
    void clear(game::GameContext&);

    const std::vector<Entry>& entries() const { return mEntries; }
    int count() const { return int(mEntries.size()); }

private:
    std::vector<Entry> mEntries;
    int mNextId = 1;
    int mSpeaking = -1;
};

} // namespace game::rpg
