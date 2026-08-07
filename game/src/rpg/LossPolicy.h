#pragma once
#include "Inventory.h"
#include "Items.h"
// For market::conditionFactor: what a death costs has to be priced the same way
// a trader prices it, or the after-action report and the shop disagree about
// what the player just lost.
#include "Trading.h"

#include <string>
#include <vector>

// What a death takes, and what an extraction banks.
//
// WHY THIS IS ITS OWN MODULE
// The risk loop is the game's central promise (GDD §7) and until now it was two
// lines at the edges of the raid state machine: `markAllOwned()` on the way out
// and `loseFindings()` on the way down. Those two lines encoded three policy
// decisions -- that brought-in gear is never at risk, that worn gear is never at
// risk, and that extraction leaves everything in the pack -- without ever
// stating them, and none of them was the design. A player who walks in with a
// full kit and loses only what they picked up has not made a commitment; they
// have made a withdrawal.
//
// So the policy is one pure function over (container, equipment, rules), it
// reports *why* each stack survived, and the rules come out of TOML. That makes
// the hardest question in the design -- "how punishing is death" -- a number a
// designer moves rather than a rewrite, which is exactly what GDD §10 lists as
// still open.
//
// It owns no world, no channels and no save. RpgRuntime calls it on the two
// edges and publishes what it returns.
namespace game::rpg {

// Why a stack came home. Reported per stack because the after-action report is
// the only place the player learns the rules, and "your seal held" and "the
// dungeon has no use for letters" teach different lessons. Lost is the only
// value that means the stack is gone.
enum class KeepReason {
    Lost,
    QuestItem,   // ItemDef::questItem -- a quest cannot be made unfinishable
    Protected,   // ItemDef::dropOnDeath = false
    Secured,     // the player spent a seal on it
    Equipped,    // worn, and the rules spare worn gear
    Carried,     // brought in rather than found, and the rules spare that
    Count
};
const char* nameOf(KeepReason);

// The dials. Every one of these is a design question GDD §10 leaves open, which
// is the reason they are data: the answer is found by playing, not by arguing.
struct LossRules {
    // The three things a death can take. Findings alone is the forgiving
    // setting the game shipped with by accident; findings + carried is the
    // Tarkov reading of GDD §7 and the default here.
    bool losesFindings = true;
    bool losesCarried = true;
    bool losesEquipped = false;

    // How many stacks the player may seal against a death. This is the
    // "secured slots" GDD §10 asks for, and it is a *decision* rather than a
    // softening: sealing the relic means not sealing the medicine.
    int securedSlots = 2;

    // Coin paid back for what was taken, as a fraction of market value. The
    // insurance dial. 0 is pure loss.
    float insuranceRate = 0.0f;

    // Findings move to the stash on a successful extraction rather than staying
    // in the pack (GDD §7: "reaching an extract secures the run's loot into
    // your stash"). What was brought in stays on the player, so a loadout
    // survives the trip and only the haul is banked.
    bool banksFindingsOnExtract = true;

    // Parse `[raid]` out of an already-parsed progression table. Absent keys
    // keep their defaults, so the block is optional and a partial one is
    // meaningful. `tomlTable` is a `const toml::table*`, type-erased for the
    // same reason every other library in this module type-erases it: the
    // headers stay free of tomlplusplus.
    void parse(const void* tomlTable);
};

// One stack and its fate.
struct LossEntry {
    ItemStack stack;
    KeepReason reason = KeepReason::Lost;
    // Market value of the whole stack at the moment it was resolved, so the
    // report can total what a run cost without re-deriving prices later, when
    // the definitions may have moved.
    int value = 0;
    // Which store it came out of. Recorded rather than re-derived, because
    // "is this item also worn" is not answerable after the fact: the player can
    // be carrying a spare of the thing they are wearing, and applying the loss
    // by looking the item up again would take the wrong one.
    bool worn = false;
    EquipSlot slot = EquipSlot::None;
};

struct LossReport {
    std::vector<LossEntry> taken;
    std::vector<LossEntry> kept;
    int insurancePaid = 0;

    int lostUnits() const;
    int lostValue() const;
    bool anythingLost() const { return !taken.empty(); }
};

// What an extraction settled.
struct ExtractReport {
    int bankedStacks = 0;
    int bankedUnits = 0;
    int bankedValue = 0;
    // Stacks the stash refused. The stash is unlimited today, so this is always
    // empty -- it exists because a limited stash is a normal design change and
    // silently vaporising a haul on the way in would be the worst possible way
    // to discover it.
    std::vector<ItemStack> refused;
};

namespace loss {

// What a death would take, without taking it. The inventory screen shows this
// live, which is the whole point of the module being pure: the player can see
// the cost of the descent *before* committing to it, which is what makes the
// commitment a decision (GDD §9: "players hesitate before descending").
LossReport preview(const Container& backpack, const Equipment&,
                   const ItemLibrary&, const LossRules&);

// The same resolution, applied. Stacks that are taken leave the container,
// equipment slots that are taken are cleared, and `currency` gains the
// insurance payout. Returns what happened.
LossReport applyDeath(Container& backpack, Equipment&, const ItemLibrary&,
                      const LossRules&, int& currency);

// Everything carried stops being provisional, and (per the rules) the findings
// move to the stash. Seals are spent: a seal protects one descent, so it is
// released whether or not it was ever tested.
ExtractReport applyExtraction(Container& backpack, Container& stash,
                              const ItemLibrary&, const LossRules&);

// How many seals the player has spent. The screen needs it to know whether the
// next one is allowed.
int securedCount(const Container&);
// Seal or release one stack of `item`. Refuses to seal past `rules.securedSlots`
// and refuses to seal a stack that needs no seal (a quest item is already
// safe), which is what stops a player wasting the scarce thing on nothing.
bool setSecured(Container&, const ItemLibrary&, const LossRules&,
                const std::string& item, bool secured);

} // namespace loss

} // namespace game::rpg
