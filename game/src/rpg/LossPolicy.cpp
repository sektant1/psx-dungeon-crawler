#include "LossPolicy.h"

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <algorithm>

namespace game::rpg {
namespace {

const ItemDef* lookup(const ItemLibrary& library, const std::string& id)
{
    const ItemLibrary::Ref ref = library.find(id);
    return ref ? ref.get() : nullptr;
}

// What one stack is worth right now, condition included. Unknown rows are
// worthless rather than free: a save that predates a rename should not pay
// insurance on an item nobody can price.
int stackValue(const ItemLibrary& library, const ItemStack& stack)
{
    const ItemDef* def = lookup(library, stack.item);
    if (!def)
        return 0;
    const float wear =
        def->durabilityMax > 0.0f
            ? market::conditionFactor(stack.condition, def->wornValueFloor)
            : 1.0f;
    return int(float(def->value) * wear * float(stack.count));
}

// The whole policy, in one place. Order matters and encodes the precedence:
// content protection first (a quest cannot be made unfinishable, whatever the
// rules say), then the player's own seal, then the rule dials. Anything that
// falls through every clause is lost.
KeepReason fateOf(const ItemDef* def, const ItemStack& stack,
                  const LossRules& rules)
{
    if (def && def->questItem)
        return KeepReason::QuestItem;
    if (def && !def->dropOnDeath)
        return KeepReason::Protected;
    if (stack.secured)
        return KeepReason::Secured;
    if (stack.foundThisRun)
        return rules.losesFindings ? KeepReason::Lost : KeepReason::Carried;
    return rules.losesCarried ? KeepReason::Lost : KeepReason::Carried;
}

} // namespace

const char* nameOf(KeepReason reason)
{
    switch (reason) {
    case KeepReason::Lost:      return "lost";
    case KeepReason::QuestItem: return "quest item";
    case KeepReason::Protected: return "protected";
    case KeepReason::Secured:   return "sealed";
    case KeepReason::Equipped:  return "worn";
    case KeepReason::Carried:   return "carried in";
    case KeepReason::Count:     break;
    }
    return "?";
}

void LossRules::parse(const void* tomlTable)
{
    const auto* raid = static_cast<const toml::table*>(tomlTable);
    if (!raid)
        return;
    losesFindings = (*raid)["loses_findings"].value_or(losesFindings);
    losesCarried = (*raid)["loses_carried"].value_or(losesCarried);
    losesEquipped = (*raid)["loses_equipped"].value_or(losesEquipped);
    securedSlots = std::max(0, (*raid)["secured_slots"].value_or(securedSlots));
    insuranceRate = std::clamp(
        float((*raid)["insurance_rate"].value_or(double(insuranceRate))), 0.0f,
        1.0f);
    banksFindingsOnExtract =
        (*raid)["banks_findings_on_extract"].value_or(banksFindingsOnExtract);
}

int LossReport::lostUnits() const
{
    int total = 0;
    for (const LossEntry& e : taken)
        total += e.stack.count;
    return total;
}

int LossReport::lostValue() const
{
    int total = 0;
    for (const LossEntry& e : taken)
        total += e.value;
    return total;
}

namespace loss {

LossReport preview(const Container& backpack, const Equipment& equipment,
                   const ItemLibrary& library, const LossRules& rules)
{
    LossReport report;
    for (const ItemStack& stack : backpack.stacks()) {
        LossEntry entry;
        entry.stack = stack;
        entry.value = stackValue(library, stack);
        entry.reason = fateOf(lookup(library, stack.item), stack, rules);
        (entry.reason == KeepReason::Lost ? report.taken : report.kept)
            .push_back(entry);
    }

    // Worn gear is resolved separately because it is not a stack: a slot holds
    // one item at full condition, and it is only ever at risk when the rules
    // say so. It still respects the content protections -- a quest sigil is not
    // taken off a corpse.
    for (int i = 0; i < Equipment::kSlotCount; ++i) {
        const EquipSlot slot = EquipSlot(i);
        const std::string& id = equipment.at(slot);
        if (id.empty())
            continue;
        LossEntry entry;
        entry.stack.item = id;
        entry.stack.count = 1;
        entry.worn = true;
        entry.slot = slot;
        entry.value = stackValue(library, entry.stack);
        const ItemDef* def = lookup(library, id);
        if (!rules.losesEquipped)
            entry.reason = KeepReason::Equipped;
        else if (def && def->questItem)
            entry.reason = KeepReason::QuestItem;
        else if (def && !def->dropOnDeath)
            entry.reason = KeepReason::Protected;
        else
            entry.reason = KeepReason::Lost;
        (entry.reason == KeepReason::Lost ? report.taken : report.kept)
            .push_back(entry);
    }

    report.insurancePaid =
        int(float(report.lostValue()) * std::clamp(rules.insuranceRate, 0.0f, 1.0f));
    return report;
}

LossReport applyDeath(Container& backpack, Equipment& equipment,
                      const ItemLibrary& library, const LossRules& rules,
                      int& currency)
{
    LossReport report = preview(backpack, equipment, library, rules);

    // Resolve first, mutate second. Deciding a stack's fate while erasing the
    // container it lives in is how a loss rule ends up depending on iteration
    // order, and this one is the difference between keeping and losing a run.
    for (const LossEntry& entry : report.taken) {
        if (entry.worn)
            equipment.unequip(entry.slot);
        else
            backpack.remove(entry.stack.item, entry.stack.count);
    }

    // A seal is spent whether or not it was tested. It protects one descent,
    // which is what makes it a decision rather than a permanent upgrade.
    for (ItemStack& stack : backpack.stacks())
        stack.secured = false;

    currency += report.insurancePaid;
    return report;
}

ExtractReport applyExtraction(Container& backpack, Container& stash,
                              const ItemLibrary& library,
                              const LossRules& rules)
{
    ExtractReport report;

    // What to bank is decided while `foundThisRun` still means something.
    // markAllOwned() clears exactly that flag, so settling ownership first would
    // leave nothing identifiable as this run's haul and bank nothing at all.
    //
    // Building the list up front also keeps moveTo -- which erases stacks -- off
    // the container being iterated, the same discipline applyDeath follows.
    struct Pending { std::string item; int count; int value; };
    std::vector<Pending> pending;
    if (rules.banksFindingsOnExtract) {
        for (const ItemStack& stack : backpack.stacks()) {
            if (!stack.foundThisRun)
                continue;
            pending.push_back(
                {stack.item, stack.count, stackValue(library, stack)});
        }
    }

    // Ownership settles unconditionally: you got out, so nothing you hold is
    // provisional any more, whatever happens to it next. Seals are released for
    // the same reason -- a seal protects one descent.
    backpack.markAllOwned();
    for (ItemStack& stack : backpack.stacks())
        stack.secured = false;

    for (const Pending& p : pending) {
        const int moved = backpack.moveTo(library, stash, p.item, p.count);
        if (moved <= 0)
            continue;
        ++report.bankedStacks;
        report.bankedUnits += moved;
        // Pro-rated: a partial move banks a proportional slice of the value,
        // which matters the moment the stash gains a limit.
        report.bankedValue += p.count > 0 ? p.value * moved / p.count : 0;
        if (moved < p.count) {
            ItemStack left;
            left.item = p.item;
            left.count = p.count - moved;
            report.refused.push_back(left);
        }
    }
    return report;
}

int securedCount(const Container& container)
{
    int n = 0;
    for (const ItemStack& stack : container.stacks())
        if (stack.secured)
            ++n;
    return n;
}

bool setSecured(Container& container, const ItemLibrary& library,
                const LossRules& rules, const std::string& item, bool secured)
{
    ItemStack* target = nullptr;
    for (ItemStack& stack : container.stacks()) {
        if (stack.item != item || stack.secured == secured)
            continue;
        target = &stack;
        break;
    }
    if (!target)
        return false;
    if (!secured) {
        target->secured = false;
        return true;
    }
    if (securedCount(container) >= rules.securedSlots)
        return false;
    // Refuse to spend a scarce seal on something a death would not have taken
    // anyway. Letting it through would look like it worked and cost the player
    // the slot that mattered.
    const ItemDef* def = lookup(library, item);
    if (fateOf(def, *target, rules) != KeepReason::Lost)
        return false;
    target->secured = true;
    return true;
}

} // namespace loss

} // namespace game::rpg
