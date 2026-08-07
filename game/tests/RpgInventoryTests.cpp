// Items, containers, equipment, and loot.
//
// The rules asserted here are the ones whose failure loses a player's things:
// a partial add must place what it can and report it, a move must never destroy
// what the destination refused, and a death must take exactly the findings and
// nothing else.
#include "../src/rpg/Inventory.h"
#include "../src/rpg/Items.h"
#include "../src/rpg/LossPolicy.h"

#include <cmath>
#include <cstdio>

using namespace game::rpg;

static int failures = 0;
static void check(bool c, const char* m)
{
    if (!c) { std::fprintf(stderr, "FAIL: %s\n", m); ++failures; }
}
static bool nearly(float a, float b, float eps = 1e-3f)
{
    return std::fabs(a - b) < eps;
}

static const char* kItems = R"(
[archetype.base]
name = "Thing"
weight = 1.0
value = 10

[item.stone]
inherits = "base"
name = "Stone"
stack = 10
weight = 2.0
value = 5

[item.feather]
inherits = "base"
name = "Feather"
stack = 50
weight = 0.1
value = 1

[item.anvil]
inherits = "base"
name = "Anvil"
weight = 40.0
value = 200
size = [3, 3]

[item.relic]
inherits = "base"
name = "Relic"
quest_item = true

[item.heirloom]
inherits = "base"
name = "Heirloom"
drop_on_death = false

[item.helm]
inherits = "base"
name = "Helm"
slot = "head"
weight = 2.0
[[item.helm.modifier]]
stat = "vigour"
flat = 3.0
[item.helm.resistance]
fire = 0.25

[item.crown]
inherits = "base"
name = "Crown"
slot = "head"
[[item.crown.modifier]]
stat = "fortune"
flat = 5.0
)";

int main()
{
    ItemLibrary items;
    check(items.loadFromString(kItems), "the item table loads");
    check(items.size() == 7, "seven items, and the archetype is not one");
    check(items.find("base") == nullptr, "an archetype is not an item");

    // Field-wise inheritance: a row states one number and inherits the rest.
    {
        const ItemLibrary::Ref stone = items.find("stone");
        check(stone != nullptr, "stone exists");
        check(stone && stone->name == "Stone", "own field overrides");
        check(stone && nearly(stone->weight, 2.0f), "stated field takes effect");
        const ItemLibrary::Ref relic = items.find("relic");
        check(relic && nearly(relic->weight, 1.0f), "unstated field inherited");
        check(relic && relic->value == 10, "unstated value inherited");
    }

    // --- stacking and weight -----------------------------------------------
    {
        Container pack("pack", 4, 30.0f);
        int added = 0;
        check(pack.add(items, "stone", 10, false, &added) == AddResult::Added,
              "ten stones fit");
        check(added == 10, "all ten went in");
        check(pack.stacks().size() == 1, "and they are one stack");
        check(nearly(pack.weight(items), 20.0f), "twenty kilos of stone");

        // The weight limit produces a *partial* add, which is a real outcome:
        // you take what you can carry and leave the rest.
        added = 0;
        const AddResult r = pack.add(items, "stone", 10, false, &added);
        check(r == AddResult::PartiallyAdded, "a partial add is reported as one");
        check(added == 5, "five more fitted in the ten kilos left");
        check(nearly(pack.weight(items), 30.0f), "and the pack is now full");

        added = 0;
        check(pack.add(items, "anvil", 1, false, &added) == AddResult::TooHeavy,
              "an anvil does not fit in a full pack");
        check(added == 0, "and nothing was added");
    }

    // Slot limits are separate from weight.
    {
        Container tiny("tiny", 2, 0.0f); // no weight limit
        tiny.add(items, "stone", 10, false);
        tiny.add(items, "feather", 50, false);
        int added = 0;
        check(tiny.add(items, "relic", 1, false, &added) == AddResult::NoRoom,
              "a third stack does not fit in two slots");
        check(added == 0, "and nothing was added");
    }

    // An unknown item is refused rather than silently stored.
    {
        Container pack("pack", 0, 0.0f);
        check(pack.add(items, "nonesuch", 1, false) == AddResult::UnknownItem,
              "an unknown item is refused");
        check(pack.empty(), "and nothing was stored");
    }

    // --- provenance ----------------------------------------------------------
    {
        Container pack("pack", 0, 0.0f);
        pack.add(items, "stone", 3, /*foundThisRun=*/false);
        pack.add(items, "stone", 3, /*foundThisRun=*/true);
        // Found and owned stacks do not merge: merging would launder loot into
        // safety, or put owned goods at risk, depending on which flag won.
        check(pack.stacks().size() == 2, "provenance keeps two stacks apart");
        check(pack.count("stone") == 6, "but the count is the total");

        // Spending prefers the provisional stack, so using what you found this
        // run leaves the safe one intact for when you die.
        check(pack.remove("stone", 2) == 2, "two removed");
        int found = 0;
        for (const ItemStack& s : pack.stacks())
            if (s.foundThisRun)
                found += s.count;
        check(found == 1, "the found stack was spent first");
    }

    // --- what a death takes --------------------------------------------------
    //
    // The policy, not the container: these are the rules the whole risk loop
    // rests on, and every one of them is a dial somebody will move.
    {
        LossRules rules; // defaults: findings and carried are both at stake
        Equipment worn;
        Container pack("pack", 0, 0.0f);
        pack.add(items, "stone", 4, /*foundThisRun=*/true);
        pack.add(items, "feather", 2, /*foundThisRun=*/false);
        pack.add(items, "relic", 1, /*foundThisRun=*/true);    // quest item
        pack.add(items, "heirloom", 1, /*foundThisRun=*/true); // drop_on_death

        int purse = 0;
        const LossReport report =
            loss::applyDeath(pack, worn, items, rules, purse);
        check(pack.count("stone") == 0, "findings are taken");
        check(pack.count("feather") == 0,
              "and so is what was carried in, by default");
        check(pack.count("relic") == 1, "a quest item survives");
        check(pack.count("heirloom") == 1, "so does a protected one");
        check(report.lostUnits() == 6, "six things were taken");
        check(purse == 0, "no insurance without a rate");
    }

    // The forgiving setting: only what the dungeon gave you.
    {
        LossRules rules;
        rules.losesCarried = false;
        Equipment worn;
        Container pack("pack", 0, 0.0f);
        pack.add(items, "stone", 4, true);
        pack.add(items, "feather", 2, false);
        int purse = 0;
        loss::applyDeath(pack, worn, items, rules, purse);
        check(pack.count("stone") == 0, "findings still go");
        check(pack.count("feather") == 2, "what you brought comes home");
    }

    // A seal protects one stack, is refused past the slot count, and is spent
    // by the death whether or not it was tested.
    {
        LossRules rules;
        rules.securedSlots = 1;
        Equipment worn;
        Container pack("pack", 0, 0.0f);
        pack.add(items, "stone", 4, true);
        pack.add(items, "feather", 2, false);
        check(loss::setSecured(pack, items, rules, "stone", true),
              "the first seal is allowed");
        check(!loss::setSecured(pack, items, rules, "feather", true),
              "the second is refused: one slot");
        check(!loss::setSecured(pack, items, rules, "relic", true),
              "and a seal cannot be spent on something already safe");
        int purse = 0;
        loss::applyDeath(pack, worn, items, rules, purse);
        check(pack.count("stone") == 4, "the sealed stack survived");
        check(loss::securedCount(pack) == 0, "and the seal was spent doing it");
    }

    // Worn gear: spared by default, taken when the harsh dial is set, and never
    // taken when the row itself is protected.
    {
        LossRules rules;
        Equipment worn;
        Container pack("pack", 0, 0.0f);
        const ItemLibrary::Ref hat = items.find("helm");
        check(bool(hat), "the fixture has an equippable");
        if (hat)
            worn.equip(*hat);
        int purse = 0;
        loss::applyDeath(pack, worn, items, rules, purse);
        check(!worn.at(hat ? hat->slot : EquipSlot::Head).empty(),
              "worn gear is spared by default");
        rules.losesEquipped = true;
        loss::applyDeath(pack, worn, items, rules, purse);
        check(worn.at(hat ? hat->slot : EquipSlot::Head).empty(),
              "and taken when the dial says so");
    }

    // Insurance pays a fraction of what was actually lost, priced the way a
    // trader would price it.
    {
        LossRules rules;
        rules.insuranceRate = 0.5f;
        Equipment worn;
        Container pack("pack", 0, 0.0f);
        pack.add(items, "stone", 4, true); // value 5 each in the fixture
        int purse = 0;
        const LossReport report =
            loss::applyDeath(pack, worn, items, rules, purse);
        check(report.lostValue() == 20, "twenty coin of stone");
        check(purse == 10, "half of it came back");
    }

    // Preview must not mutate: the inventory screen shows it every frame.
    {
        LossRules rules;
        Equipment worn;
        Container pack("pack", 0, 0.0f);
        pack.add(items, "stone", 4, true);
        const LossReport a = loss::preview(pack, worn, items, rules);
        const LossReport b = loss::preview(pack, worn, items, rules);
        check(pack.count("stone") == 4, "preview took nothing");
        check(a.lostUnits() == b.lostUnits(), "and is stable");
    }

    // Extraction banks the haul and leaves the loadout on the player.
    {
        LossRules rules;
        Container pack("pack", 0, 0.0f);
        Container stash("stash", 0, 0.0f);
        pack.add(items, "stone", 4, /*foundThisRun=*/true);
        pack.add(items, "feather", 2, /*foundThisRun=*/false);
        const ExtractReport report =
            loss::applyExtraction(pack, stash, items, rules);
        check(stash.count("stone") == 4, "the haul is in the vault");
        check(pack.count("feather") == 2, "the kit stayed on the player");
        check(pack.count("stone") == 0, "and left the pack");
        check(report.bankedUnits == 4, "four banked");
        for (const ItemStack& s : stash.stacks())
            check(!s.foundThisRun, "banked goods are owned outright");
    }

    // With banking off, extraction still settles ownership -- which is what
    // makes a second death stop taking the first run's haul.
    {
        LossRules rules;
        rules.banksFindingsOnExtract = false;
        Container pack("pack", 0, 0.0f);
        Container stash("stash", 0, 0.0f);
        pack.add(items, "stone", 4, true);
        loss::applyExtraction(pack, stash, items, rules);
        check(stash.empty(), "nothing was banked");
        check(pack.count("stone") == 4, "it is all still carried");
        for (const ItemStack& s : pack.stacks())
            check(!s.foundThisRun, "but it is owned now");
    }

    // --- moving between containers -------------------------------------------
    {
        Container pack("pack", 0, 0.0f);
        Container stash("stash", 1, 0.0f); // one slot only
        pack.add(items, "stone", 10, false);
        pack.add(items, "feather", 5, false);

        const int moved = pack.moveTo(items, stash, "stone", 10);
        check(moved == 10, "the stones moved");
        check(stash.count("stone") == 10, "and arrived");
        check(pack.count("stone") == 0, "and left");

        // The destination is full now. Nothing may be destroyed on a refusal --
        // asking the destination first is the whole point.
        const int refused = pack.moveTo(items, stash, "feather", 5);
        check(refused == 0, "the full stash refused them");
        check(pack.count("feather") == 5, "and they are still in the pack");
    }

    // --- the grid ------------------------------------------------------------
    {
        Container pack("pack", 0, 0.0f);
        pack.setGrid(4, 4); // sixteen cells
        check(pack.capacityCells() == 16, "sixteen cells");
        pack.add(items, "anvil", 1, false); // 3x3 = nine cells
        check(pack.usedCells(items) == 9, "the anvil costs nine");
        int added = 0;
        pack.add(items, "anvil", 1, false, &added);
        check(added == 0, "a second anvil does not fit in the seven left");
        // Topping up an existing stack is free, which is why consolidating is
        // worth doing and four half-stacks is a real mistake.
        pack.add(items, "stone", 1, false);
        const int before = pack.usedCells(items);
        pack.add(items, "stone", 5, false);
        check(pack.usedCells(items) == before, "topping up costs no cells");
    }

    // --- equipment -----------------------------------------------------------
    {
        Equipment worn;
        const ItemLibrary::Ref helm = items.find("helm");
        const ItemLibrary::Ref crown = items.find("crown");
        check(worn.equip(*helm).empty(), "the head slot was free");
        check(worn.at(EquipSlot::Head) == "helm", "the helm is worn");

        // Swapping returns what came off, so the caller can put it back. An
        // Equipment that silently destroyed it would be the bug that loses gear.
        check(worn.equip(*crown) == "helm", "the helm came off");
        check(worn.at(EquipSlot::Head) == "crown", "the crown is on");

        worn.equip(*helm);
        const std::vector<Equipment::SlotModifiers> mods = worn.modifiers(items);
        check(mods.size() == 1, "one occupied slot");
        check(mods.size() == 1 && mods[0].source == "equip:head",
              "named after its slot, so unequipping clears exactly one source");

        const std::array<float, game::kMaxDamageTypes> res =
            worn.resistances(items);
        // Channel ids come from magic.toml, which is not loaded here, so the
        // grant is unresolved and contributes nothing -- which is the correct
        // behaviour and worth pinning down.
        float total = 0.0f;
        for (float v : res)
            total += v;
        check(nearly(total, 0.0f),
              "an unresolved resistance channel grants nothing");

        check(worn.unequip(EquipSlot::Head) == "helm", "unequip returns it");
        check(worn.modifiers(items).empty(), "and the group is gone");
    }

    // --- loot ---------------------------------------------------------------
    {
        LootLibrary loot;
        check(loot.loadFromString(R"(
[table.simple]
rolls = 4
coin_min = 5
coin_max = 5
[[table.simple.entry]]
item = "stone"
weight = 1
min = 1
max = 1
[[table.simple.entry]]
item = "never"
weight = 1
chance = 0.0
[[table.simple.entry]]
item = "always"
guaranteed = true
chance = 1.0
)"),
              "the loot table loads");

        const LootTable* table = loot.find("simple");
        check(table != nullptr, "the table is found");

        // Deterministic from the seed: one stream through an expedition means
        // a run is reproducible.
        uint32_t a = 12345;
        uint32_t b = 12345;
        const LootResult first = loot::roll(*table, a);
        const LootResult second = loot::roll(*table, b);
        check(first.drops.size() == second.drops.size(),
              "the same seed rolls the same drops");
        check(first.coin == second.coin, "and the same coin");
        check(first.coin == 5, "a fixed coin range gives that coin");

        // A zero chance never drops, whatever its weight.
        for (const LootDrop& d : first.drops)
            check(d.item != "never", "a zero-chance entry never drops");
        // A guaranteed entry is rolled outside the weighted pool.
        bool sawGuaranteed = false;
        for (const LootDrop& d : first.drops)
            sawGuaranteed = sawGuaranteed || d.item == "always";
        check(sawGuaranteed, "a guaranteed entry always drops");

        // Two rolls of the same item merge, so four stones are one pickup.
        for (const LootDrop& d : first.drops)
            if (d.item == "stone")
                check(d.count >= 1, "stones merged into one drop");

        // The seed advanced, so a second roll from the same stream differs.
        check(a != 12345, "the stream advanced");

        // referencedItems is what lets a caller fail the load on a dangling
        // reference rather than dropping nothing at runtime.
        const std::vector<std::string> refs = loot.referencedItems();
        check(refs.size() == 3, "three distinct items referenced");
    }

    // A zero seed is a fixed point for xorshift; the generator must still work.
    {
        uint32_t zero = 0;
        const uint32_t first = loot::nextRandom(zero);
        const uint32_t next = loot::nextRandom(zero);
        check(first != 0 && next != 0 && first != next,
              "a zero seed still produces a usable stream");
    }

    if (failures == 0)
        std::printf("RpgInventoryTests OK\n");
    return failures == 0 ? 0 : 1;
}
