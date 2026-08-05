#pragma once
#include "Items.h"
#include "Targeting.h"

#include <eng/Handles.h>

#include <glm/glm.hpp>

#include <optional>
#include <string>
#include <vector>

namespace game {
struct GameContext;
struct ScenePlacement; // scene/MapRuntime.h
}

// Items lying in the world.
//
// WHY THIS EXISTS
// The editor already offers a **Pickup** entity, SceneCook already writes it
// into the `.map`, and MapRuntime::pickupPlacements() already reports it -- and
// before this, nothing read that call. An author could place an item, cook it,
// load the level, and no item existed. This is the missing consumer, and it is
// also where loot drops land, so an authored pickup and a dropped one are the
// same thing from the moment they exist.
//
// A pickup is deliberately NOT a physics body. It is a render node, a position
// and a radius: it never collides, never falls, and never wakes Jolt. Loot in a
// bullet-hell dungeon arrives in handfuls, and giving each one a rigid body
// buys tumbling nobody watches at the cost of an island the solver has to keep
// awake near the player.
namespace game::rpg {

class PickupSystem {
public:
    // What one pickup is. `id` is stable for as long as the pickup exists and
    // is what the interaction seam carries (GameplayTarget::id).
    struct Entry {
        int id = 0;
        std::string item;
        int count = 1;
        glm::vec3 position{0.0f};
        eng::NodeHandle node;
        float bobPhase = 0.0f;
        bool fromLoot = false; // dropped, rather than authored into the level
    };

    // Place `count` of `def` at `position`. Returns the new pickup's id, or -1
    // when the renderer refused (which only happens with no node budget left).
    int spawn(game::GameContext&, const ItemDef& def, glm::vec3 position,
              int count, bool fromLoot = false);

    // The authored half: `pickup.<item>` placements out of the cooked map.
    // Unknown item ids are logged and skipped -- a level that names a deleted
    // item should say so, not silently hold nothing.
    int spawnAuthored(game::GameContext&, const ItemLibrary&,
                      const std::vector<game::ScenePlacement>& placements);

    // Idle motion: a slow bob and spin, so loot reads as loot at a glance and
    // not as level geometry. Frame-rate driven, presentation only.
    void update(game::GameContext&, float dt);

    // Publish every pickup within reach as a look target.
    void appendTargets(std::vector<GameplayTarget>& out) const;

    const Entry* find(int id) const;
    // Take a pickup out of the world. The caller decides whether it fits in the
    // pack *before* calling; this removes it unconditionally, because a pickup
    // that vanishes on a failed add is the bug that loses items.
    std::optional<Entry> take(game::GameContext&, int id);

    // Level transition: destroy every node. Pickups are not persistent -- what
    // the player picked up is in their inventory, and what they did not is
    // regenerated with the level.
    void clear(game::GameContext&);

    const std::vector<Entry>& entries() const { return mEntries; }
    int count() const { return int(mEntries.size()); }

private:
    std::vector<Entry> mEntries;
    int mNextId = 1;
    float mTime = 0.0f;
};

} // namespace game::rpg
