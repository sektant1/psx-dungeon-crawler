#include "Inventory.h"

#include <eng/Log.h>

#include <algorithm>

namespace game::rpg {

namespace {

// Definitions an inventory asks for that the library does not have. Rather than
// silently dropping the stack (which loses a player's save) the container keeps
// it and treats it as weightless and unusable, and this logs once per lookup so
// the content bug is visible.
const ItemDef* lookup(const ItemLibrary& library, const std::string& id)
{
    const ItemLibrary::Ref ref = library.find(id);
    return ref ? ref.get() : nullptr;
}

} // namespace

float Container::weight(const ItemLibrary& library) const
{
    float total = 0.0f;
    for (const ItemStack& s : mStacks)
        if (const ItemDef* def = lookup(library, s.item))
            total += def->weight * float(s.count);
    return total;
}

void Container::setGrid(int width, int height)
{
    mGridWidth = std::max(0, width);
    mGridHeight = std::max(0, height);
}

int Container::usedCells(const ItemLibrary& library) const
{
    int cells = 0;
    for (const ItemStack& s : mStacks) {
        if (const ItemDef* def = lookup(library, s.item))
            cells += def->gridWidth * def->gridHeight;
        else
            cells += 1; // an unknown item still occupies something
    }
    return cells;
}

int Container::count(const std::string& item) const
{
    int total = 0;
    for (const ItemStack& s : mStacks)
        if (s.item == item)
            total += s.count;
    return total;
}

AddResult Container::add(const ItemLibrary& library, const std::string& item,
                         int count, bool foundThisRun, int* added)
{
    if (added)
        *added = 0;
    if (count <= 0)
        return AddResult::Added;

    const ItemDef* def = lookup(library, item);
    if (!def) {
        eng::log::error("Inventory: '%s' names no item; nothing added",
                        item.c_str());
        return AddResult::UnknownItem;
    }

    int remaining = count;
    int placed = 0;

    // Weight first, because it is the limit that produces a *partial* add: the
    // player takes what they can carry and leaves the rest, which is the
    // decision §14 wants inventory to be about.
    int weightAllows = remaining;
    if (mWeightLimit > 0.0f && def->weight > 0.0f) {
        const float free = mWeightLimit - weight(library);
        weightAllows = free <= 0.0f ? 0 : int(free / def->weight);
        remaining = std::min(remaining, weightAllows);
    }
    if (remaining <= 0)
        return AddResult::TooHeavy;

    // Top up existing stacks before opening a new slot.
    for (ItemStack& s : mStacks) {
        if (remaining <= 0)
            break;
        if (s.item != item || s.count >= def->stackMax)
            continue;
        // A found-this-run stack and an owned one do not merge: merging would
        // launder loot into safety, or put owned goods at risk, depending on
        // which flag won.
        if (s.foundThisRun != foundThisRun)
            continue;
        const int room = def->stackMax - s.count;
        const int take = std::min(room, remaining);
        s.count += take;
        remaining -= take;
        placed += take;
    }

    const int cellCost = def->gridWidth * def->gridHeight;
    while (remaining > 0) {
        if (mSlotLimit > 0 && int(mStacks.size()) >= mSlotLimit)
            break;
        // In grid mode a new stack has to pay for its footprint. Topping up an
        // existing stack above is free, which is why ammunition is worth
        // consolidating and four half-stacks of it are a real mistake.
        if (gridMode() && usedCells(library) + cellCost > capacityCells())
            break;
        const int take = std::min(def->stackMax, remaining);
        mStacks.push_back({item, take, foundThisRun, 1.0f});
        remaining -= take;
        placed += take;
    }

    if (added)
        *added = placed;
    if (placed == count)
        return AddResult::Added;
    if (placed > 0)
        return AddResult::PartiallyAdded;
    // Nothing fitted. Which limit stopped it decides the message.
    return weightAllows < count ? AddResult::TooHeavy : AddResult::NoRoom;
}

int Container::remove(const std::string& item, int count)
{
    if (count <= 0)
        return 0;
    int removed = 0;
    // Spend the provisional stacks first: if the player uses a bandage they
    // found this run, the safe one in their pack should still be there when
    // they die.
    for (int pass = 0; pass < 2 && removed < count; ++pass) {
        const bool wantFound = pass == 0;
        for (ItemStack& s : mStacks) {
            if (removed >= count)
                break;
            if (s.item != item || s.foundThisRun != wantFound)
                continue;
            const int take = std::min(s.count, count - removed);
            s.count -= take;
            removed += take;
        }
    }
    mStacks.erase(std::remove_if(mStacks.begin(), mStacks.end(),
                                 [](const ItemStack& s) { return s.count <= 0; }),
                  mStacks.end());
    return removed;
}

int Container::moveTo(const ItemLibrary& library, Container& dest,
                      const std::string& item, int count)
{
    if (&dest == this || count <= 0)
        return 0;
    const int available = std::min(count, this->count(item));
    if (available <= 0)
        return 0;

    // Ask the destination first and only then take from the source. The other
    // order loses items whenever the destination refuses -- the single most
    // common inventory bug there is.
    int moved = 0;
    // Preserve provenance stack by stack, so moving a mixed pile into the stash
    // does not relabel the owned half as findings.
    for (int pass = 0; pass < 2 && moved < available; ++pass) {
        const bool wantFound = pass == 0;
        int inThisPass = 0;
        for (const ItemStack& s : mStacks)
            if (s.item == item && s.foundThisRun == wantFound)
                inThisPass += s.count;
        if (inThisPass <= 0)
            continue;
        int accepted = 0;
        dest.add(library, item, std::min(inThisPass, available - moved),
                 wantFound, &accepted);
        if (accepted <= 0)
            continue;
        // remove() prefers found stacks, which matches the pass order.
        const int taken = removeMatching(item, accepted, wantFound);
        moved += taken;
    }
    return moved;
}

int Container::removeMatching(const std::string& item, int count, bool foundThisRun)
{
    if (count <= 0)
        return 0;
    int removed = 0;
    for (ItemStack& s : mStacks) {
        if (removed >= count)
            break;
        if (s.item != item || s.foundThisRun != foundThisRun)
            continue;
        const int take = std::min(s.count, count - removed);
        s.count -= take;
        removed += take;
    }
    mStacks.erase(std::remove_if(mStacks.begin(), mStacks.end(),
                                 [](const ItemStack& s) { return s.count <= 0; }),
                  mStacks.end());
    return removed;
}

void Container::markAllOwned()
{
    for (ItemStack& s : mStacks)
        s.foundThisRun = false;
    // Merge the stacks that were only kept apart by provenance, so a pack does
    // not accumulate two half-stacks of the same herb per expedition.
    for (std::size_t i = 0; i < mStacks.size(); ++i) {
        for (std::size_t j = mStacks.size(); j-- > i + 1;) {
            if (mStacks[j].item == mStacks[i].item) {
                mStacks[i].count += mStacks[j].count;
                mStacks.erase(mStacks.begin() + long(j));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Equipment
// ---------------------------------------------------------------------------

const std::string& Equipment::at(EquipSlot slot) const
{
    static const std::string kEmpty;
    const auto i = std::size_t(slot);
    if (slot == EquipSlot::None || i >= mSlots.size())
        return kEmpty;
    return mSlots[i];
}

bool Equipment::equipped(const std::string& item) const
{
    return std::find(mSlots.begin(), mSlots.end(), item) != mSlots.end();
}

std::string Equipment::equip(const ItemDef& def)
{
    if (def.slot == EquipSlot::None) {
        // A weapon item with no armour slot still equips: the loadout reads
        // weaponRow(). Give it the Hands slot so it has somewhere to live and
        // one weapon is held at a time.
        if (def.weaponId.empty())
            return {};
    }
    const EquipSlot slot =
        def.slot == EquipSlot::None ? EquipSlot::Hands : def.slot;
    const auto i = std::size_t(slot);
    std::string previous = mSlots[i];
    mSlots[i] = def.id;
    return previous;
}

std::string Equipment::unequip(EquipSlot slot)
{
    const auto i = std::size_t(slot);
    if (slot == EquipSlot::None || i >= mSlots.size())
        return {};
    std::string previous = mSlots[i];
    mSlots[i].clear();
    return previous;
}

void Equipment::clear()
{
    for (std::string& s : mSlots)
        s.clear();
}

std::vector<Equipment::SlotModifiers>
Equipment::modifiers(const ItemLibrary& library) const
{
    std::vector<SlotModifiers> out;
    for (int i = 0; i < kSlotCount; ++i) {
        const EquipSlot slot = EquipSlot(i);
        if (slot == EquipSlot::None || mSlots[std::size_t(i)].empty())
            continue;
        const ItemDef* def = lookup(library, mSlots[std::size_t(i)]);
        if (!def)
            continue;
        out.push_back({slot, Inventory::sourceFor(slot), def->modifiers});
    }
    return out;
}

std::array<float, kMaxDamageTypes>
Equipment::resistances(const ItemLibrary& library) const
{
    std::array<float, kMaxDamageTypes> out{};
    for (const std::string& id : mSlots) {
        if (id.empty())
            continue;
        const ItemDef* def = lookup(library, id);
        if (!def)
            continue;
        for (const ItemDef::ResistanceGrant& g : def->resistances)
            if (g.id != kInvalidDamageType && g.id < kMaxDamageTypes)
                out[g.id] += g.amount;
    }
    // The combat model documents value[t] in [-1, 0.9]; three pieces of
    // fire-proof armour must not add up to immunity.
    for (float& v : out)
        v = std::clamp(v, -1.0f, 0.9f);
    return out;
}

std::string Equipment::weaponRow(const ItemLibrary& library) const
{
    for (const std::string& id : mSlots) {
        if (id.empty())
            continue;
        if (const ItemDef* def = lookup(library, id); def && !def->weaponId.empty())
            return def->weaponId;
    }
    return {};
}

std::string Inventory::sourceFor(EquipSlot slot)
{
    return std::string("equip:") + nameOf(slot);
}

} // namespace game::rpg
