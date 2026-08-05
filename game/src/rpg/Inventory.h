#pragma once
#include "Items.h"
#include "RpgTypes.h"

#include <array>
#include <string>
#include <vector>

// Carrying things.
//
// AGENTS.md §14 wants three storage contexts -- village stash, expedition
// loadout, and the findings of the current run -- and says inventory design
// should "emphasise decisions rather than grid-management busywork". So there
// is one Container type with different limits, and "carried findings" is a flag
// on the stack rather than a third class: it is the only thing that
// distinguishes them mechanically (what a death takes), and modelling it as
// storage would mean three code paths for one predicate.
//
// Weight is the only carry dimension turned on. §14 explicitly warns against
// activating bulk, noise, fragility and contamination at once without a reason.
namespace game::rpg {

// One occupancy of a container. `count` is always >= 1 for a live stack; a
// stack that reaches zero is erased rather than kept as an empty slot.
struct ItemStack {
    std::string item;
    int count = 1;
    // Acquired during the current expedition and therefore not truly owned
    // (§14.3). Cleared for everything on a successful extraction.
    bool foundThisRun = false;
    // Wear, as a fraction of the definition's durabilityMax. 1 is pristine.
    // Only meaningful for items that wear, and only ever for a stack of one --
    // two swords at different conditions are two stacks, which is why anything
    // with durability has stackMax = 1.
    float condition = 1.0f;

    bool operator==(const ItemStack&) const = default;
};

// Why an add failed. Callers show these to the player, so they are outcomes,
// not error codes: "you cannot carry any more" is a gameplay message.
enum class AddResult { Added, PartiallyAdded, NoRoom, TooHeavy, UnknownItem };

// A place things are. `slotLimit <= 0` means unlimited slots (the stash);
// `weightLimit <= 0` means unlimited weight (also the stash).
class Container {
public:
    Container() = default;
    Container(std::string name, int slotLimit, float weightLimit)
        : mName(std::move(name)), mSlotLimit(slotLimit),
          mWeightLimit(weightLimit)
    {
    }

    const std::string& name() const { return mName; }
    int slotLimit() const { return mSlotLimit; }
    void setSlotLimit(int v) { mSlotLimit = v; }
    float weightLimit() const { return mWeightLimit; }
    void setWeightLimit(float v) { mWeightLimit = v; }

    // --- grid mode ----------------------------------------------------------
    //
    // A container in grid mode is limited by *cells* rather than by slots: a
    // stack costs `gridWidth * gridHeight` of them, so a rifle costs four
    // times what a bandage does and "what do I leave behind" becomes a shape
    // question as well as a weight one.
    //
    // WHAT THIS IS NOT, deliberately: a 2D packer. There are no coordinates and
    // no rotation -- a stack occupies area, not a rectangle at (x, y). Real
    // placement is a bigger change (positions in the save format, a drag-drop
    // UI, a first-fit scan on every add) and AGENTS.md §14 explicitly warns
    // against grid-management busywork, so the budget is the part that changes
    // decisions and the coordinates are the part that changes chores. This is
    // the seam if that call is revisited: `capacityCells` and `usedCells` are
    // what a real packer would replace.
    void setGrid(int width, int height);
    int capacityCells() const { return mGridWidth * mGridHeight; }
    int usedCells(const ItemLibrary&) const;
    bool gridMode() const { return mGridWidth > 0 && mGridHeight > 0; }
    int gridWidth() const { return mGridWidth; }
    int gridHeight() const { return mGridHeight; }

    const std::vector<ItemStack>& stacks() const { return mStacks; }
    std::vector<ItemStack>& stacks() { return mStacks; }
    bool empty() const { return mStacks.empty(); }

    // Total mass, computed from the library. Not cached: the container does not
    // own the library, definitions hot-reload, and a stale weight is the kind
    // of bug that shows up as "I cannot pick this up" with no visible cause.
    float weight(const ItemLibrary& library) const;

    // How many of `item` are held, across every stack.
    int count(const std::string& item) const;

    // Add up to `count`, respecting stack size, slot count and weight.
    // `added` reports what actually went in -- a partial add is a real outcome
    // (you took four of the six torches you found), not a failure.
    AddResult add(const ItemLibrary& library, const std::string& item,
                  int count, bool foundThisRun, int* added = nullptr);

    // Remove up to `count`. Returns how many were actually removed.
    int remove(const std::string& item, int count);

    // Move up to `count` of `item` into `dest`, respecting dest's limits.
    // Returns how many moved. Nothing is destroyed on a partial move: what
    // dest refused stays where it was.
    int moveTo(const ItemLibrary& library, Container& dest,
               const std::string& item, int count);

    void clear() { mStacks.clear(); }
    // Everything acquired this run stops being provisional.
    void markAllOwned();
    // Drop every stack a death takes: found-this-run, not a quest item, and not
    // flagged drop_on_death = false. Returns what was lost, for the report the
    // player deserves to see.
    std::vector<ItemStack> loseFindings(const ItemLibrary& library);

private:
    // remove(), restricted to stacks with a given provenance. moveTo needs it
    // so a partial transfer takes from exactly the stacks the destination
    // accepted; remove()'s own found-first preference is the right default
    // everywhere else.
    int removeMatching(const std::string& item, int count, bool foundThisRun);

    std::string mName = "container";
    std::vector<ItemStack> mStacks;
    int mSlotLimit = 0;
    float mWeightLimit = 0.0f;
    int mGridWidth = 0;
    int mGridHeight = 0;
};

// What the player is wearing. One stack per slot, never stacked, because a
// slot holds exactly one thing.
class Equipment {
public:
    static constexpr int kSlotCount = int(EquipSlot::Count);

    // The item in a slot, or empty. `EquipSlot::None` always reads empty.
    const std::string& at(EquipSlot slot) const;
    bool equipped(const std::string& item) const;

    // Put `item` in its own slot, returning what came out (empty when the slot
    // was free). The caller is responsible for putting the returned item back
    // in a container -- Equipment owns no storage and will not silently
    // destroy the helmet you swapped off.
    std::string equip(const ItemDef& def);
    std::string unequip(EquipSlot slot);
    void clear();

    // The modifier groups every equipped piece contributes, ready to be handed
    // to CharacterSheet::setModifiers. One group per slot, named
    // "equip:<slot>", so unequipping clears exactly one source.
    struct SlotModifiers {
        EquipSlot slot = EquipSlot::None;
        std::string source;
        std::vector<StatModifier> modifiers;
    };
    std::vector<SlotModifiers> modifiers(const ItemLibrary& library) const;

    // Summed resistance grants, by damage-type id. Indexed like the combat
    // Resistances component so applying them is a copy.
    std::array<float, kMaxDamageTypes> resistances(const ItemLibrary&) const;

    // The weapon row (weapons.toml) of whatever weapon item is equipped, or
    // empty. The loadout reads this; nothing else needs to know weapons are
    // items at all.
    std::string weaponRow(const ItemLibrary& library) const;

private:
    std::array<std::string, std::size_t(kSlotCount)> mSlots{};
};

// The player's whole carry state: what is on them, what is at home, and what
// they are wearing.
struct Inventory {
    Container backpack{"backpack", 24, 45.0f};
    Container stash{"stash", 0, 0.0f}; // village storage: no limits
    Equipment equipment;
    int currency = 0;

    // The name a modifier source gets for a slot, so equip/unequip and the
    // debug panel all spell it the same way.
    static std::string sourceFor(EquipSlot slot);
};

} // namespace game::rpg
