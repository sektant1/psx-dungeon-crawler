#include "GameUiData.h"

#include "rpg/RpgRuntime.h"

#include <algorithm>
#include <cstdio>

namespace game {
namespace {

// The binding vocabulary, in one table per kind.
//
// One place, so "what can a screen bind to" is answerable by reading a list
// rather than by grepping a switch -- and so the editor's picker and the
// documentation test read the same source the resolver does. A key added here
// and not handled below fails the test that walks it.
struct NumberKey {
    const char* key;
    float (*read)(const rpg::RpgRuntime&);
};

struct TextKey {
    const char* key;
    std::string (*read)(const rpg::RpgRuntime&);
};

std::string integer(long long value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%lld", value);
    return buffer;
}

std::string oneDecimal(float value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.1f", double(value));
    return buffer;
}

float weightFraction(const rpg::RpgRuntime& rpg)
{
    const rpg::Container& pack = rpg.inventory().backpack;
    const float limit = pack.weightLimit();
    if (limit <= 0.0f)
        return 0.0f;
    return std::clamp(pack.weight(rpg.items()) / limit, 0.0f, 1.0f);
}

const NumberKey kNumbers[] = {
    {"inventory.weight_fraction", &weightFraction},
    {"inventory.slot_fraction",
     [](const rpg::RpgRuntime& rpg) {
         const rpg::Container& pack = rpg.inventory().backpack;
         const int limit = pack.slotLimit();
         if (limit <= 0)
             return 0.0f;
         return std::clamp(float(pack.stacks().size()) / float(limit), 0.0f,
                           1.0f);
     }},
    // What a death would cost, as a fraction of everything carried. The one
    // number that makes the push-or-extract decision legible, and the reason
    // LossPolicy::preview is a pure function.
    {"raid.at_risk_fraction",
     [](const rpg::RpgRuntime& rpg) {
         const rpg::LossReport report = rpg.previewLoss();
         int total = report.lostValue();
         for (const rpg::LossEntry& kept : report.kept)
             total += kept.value;
         if (total <= 0)
             return 0.0f;
         return std::clamp(float(report.lostValue()) / float(total), 0.0f, 1.0f);
     }},
    {"raid.extraction_progress",
     [](const rpg::RpgRuntime& rpg) {
         const float duration = rpg.raid().extractionDuration();
         if (duration <= 0.0f)
             return 0.0f;
         return std::clamp(
             1.0f - rpg.raid().extractionRemaining() / duration, 0.0f, 1.0f);
     }},
    {"character.level_progress",
     [](const rpg::RpgRuntime& rpg) {
         // Derived from experience rather than stored: the level is a *reading*
         // of xp (see Skills.h), and a progress bar that kept its own number
         // would disagree with it the first time the curve was retuned.
         return std::clamp(
             rpg::xp::levelProgress(rpg.skills().characterExperience()), 0.0f,
             1.0f);
     }},
};

const TextKey kTexts[] = {
    {"inventory.weight",
     [](const rpg::RpgRuntime& rpg) {
         const rpg::Container& pack = rpg.inventory().backpack;
         return oneDecimal(pack.weight(rpg.items())) + " / " +
                oneDecimal(pack.weightLimit());
     }},
    {"inventory.currency",
     [](const rpg::RpgRuntime& rpg) {
         return integer(rpg.inventory().currency);
     }},
    {"raid.phase",
     [](const rpg::RpgRuntime& rpg) {
         return std::string(rpg::nameOf(rpg.raid().phase()));
     }},
    {"raid.depth",
     [](const rpg::RpgRuntime& rpg) { return integer(rpg.raid().depth()); }},
    // Coin, not units: "you would lose 340" is a decision, "you would lose 12
    // things" is trivia.
    {"raid.at_risk",
     [](const rpg::RpgRuntime& rpg) {
         return integer(rpg.previewLoss().lostValue());
     }},
    {"raid.seals",
     [](const rpg::RpgRuntime& rpg) {
         return integer(rpg::loss::securedCount(rpg.inventory().backpack)) +
                " / " + integer(rpg.lossRules().securedSlots);
     }},
    {"character.level",
     [](const rpg::RpgRuntime& rpg) {
         return integer(rpg.skills().characterLevel());
     }},
    {"dialogue.speaker",
     [](const rpg::RpgRuntime& rpg) {
         return rpg.conversation().active() ? rpg.conversation().speaker()
                                            : std::string();
     }},
    {"dialogue.text",
     [](const rpg::RpgRuntime& rpg) {
         return rpg.conversation().active() ? rpg.conversation().text()
                                            : std::string();
     }},
};

// Rows are not a table of function pointers like the two above: each needs the
// row limit and the source's own selection, so they are handled in a switch the
// key list below is checked against.
const char* const kRowKeys[] = {
    "inventory.backpack", "inventory.stash", "trade.stock",
    "dialogue.choices",  "quests.active",   "raid.at_risk_items",
};

// One inventory stack as a row. Shared by the pack and the stash so the two
// cannot drift apart in how they read.
eng::ui::UiDataSource::Row stackRow(const rpg::RpgRuntime& rpg,
                                    const rpg::ItemStack& stack, bool selected)
{
    eng::ui::UiDataSource::Row row;
    const rpg::ItemLibrary::Ref def = rpg.items().find(stack.item);
    row.label = def ? def->name : stack.item;
    if (stack.count > 1)
        row.label += " x" + integer(stack.count);
    // A seal is the scarce decision on this screen, so it is marked in the
    // label rather than only in a tooltip nobody opens.
    if (stack.secured)
        row.label = "* " + row.label;
    row.value = def ? integer(def->value * stack.count) : std::string();
    // Condition as the gauge, and only for things that wear: a full bar under
    // every torch is noise that hides the one cracked breastplate.
    if (def && def->durabilityMax > 0.0f)
        row.ratio = std::clamp(stack.condition, 0.0f, 1.0f);
    row.selected = selected;
    return row;
}

} // namespace

bool GameUiData::number(std::string_view key, float& out) const
{
    if (!mRpg)
        return false;
    for (const NumberKey& entry : kNumbers) {
        if (key != entry.key)
            continue;
        out = entry.read(*mRpg);
        return true;
    }
    return false;
}

bool GameUiData::text(std::string_view key, std::string& out) const
{
    if (!mRpg)
        return false;
    for (const TextKey& entry : kTexts) {
        if (key != entry.key)
            continue;
        out = entry.read(*mRpg);
        return true;
    }
    return false;
}

int GameUiData::rows(std::string_view key, int max, std::vector<Row>& out) const
{
    out.clear();
    if (!mRpg || max <= 0)
        return 0;
    const rpg::RpgRuntime& rpg = *mRpg;
    // An empty focus means "no list owns the cursor", which is the single-list
    // case and every list marks its own selection. Once a screen declares a
    // focus, only the focused list does.
    const int selected =
        (mFocused.empty() || mFocused == key) ? mSelected : -1;

    const auto container = [&](const rpg::Container& from) {
        const int count = std::min(int(from.stacks().size()), max);
        for (int i = 0; i < count; ++i)
            out.push_back(stackRow(rpg, from.stacks()[std::size_t(i)],
                                   i == selected));
    };

    if (key == "inventory.backpack") {
        container(rpg.inventory().backpack);
    } else if (key == "inventory.stash") {
        container(rpg.inventory().stash);
    } else if (key == "raid.at_risk_items") {
        // What a death takes, previewed. Not a filter over the pack: the rules
        // are the policy's and duplicating them here is how a screen starts
        // lying about the risk it is showing.
        const rpg::LossReport report = rpg.previewLoss();
        const int count = std::min(int(report.taken.size()), max);
        for (int i = 0; i < count; ++i) {
            const rpg::LossEntry& entry = report.taken[std::size_t(i)];
            Row row = stackRow(rpg, entry.stack, i == selected);
            row.value = integer(entry.value);
            out.push_back(row);
        }
    } else if (key == "trade.stock") {
        const rpg::TraderDef* trader = rpg.traders().find(mTrader);
        const rpg::TraderState* state = rpg.market().state(mTrader);
        if (!trader || !state)
            return 0;
        int index = 0;
        for (const auto& [item, quantity] : state->stock) {
            if (index >= max)
                break;
            if (quantity <= 0)
                continue;
            const rpg::ItemLibrary::Ref def = rpg.items().find(item);
            Row row;
            row.label = (def ? def->name : item) + " x" + integer(quantity);
            const rpg::PriceQuote quote = rpg.buyQuote(mTrader, item);
            row.value = quote.tradeable ? integer(quote.unitPrice)
                                        : std::string("--");
            // Greyed rather than hidden when unaffordable: knowing what you
            // cannot yet afford is half of what a shop is for.
            row.dim = !quote.tradeable ||
                      quote.unitPrice > rpg.inventory().currency;
            row.selected = index == selected;
            out.push_back(row);
            ++index;
        }
    } else if (key == "dialogue.choices") {
        if (!rpg.conversation().active())
            return 0;
        const auto& offered = rpg.conversation().choices();
        const int count = std::min(int(offered.size()), max);
        for (int i = 0; i < count; ++i) {
            Row row;
            // Numbered, because a conversation is driven by number keys and a
            // reply the player cannot address is a reply they cannot take.
            row.label = integer(i + 1) + ". " +
                        (offered[std::size_t(i)].choice
                             ? offered[std::size_t(i)].choice->text
                             : std::string());
            row.dim = offered[std::size_t(i)].locked;
            row.selected = i == selected;
            out.push_back(row);
        }
    } else if (key == "quests.active") {
        int index = 0;
        for (const auto& quest : rpg.quests().quests()) {
            if (index >= max)
                break;
            if (!quest || quest->state() != rpg::QuestState::Active)
                continue;
            Row row;
            row.label = quest->title();
            // progress()/target() are counts, not a fraction: a "kill 6" quest
            // at 2 reads 0.33, and a quest with no target (a flag quest) gets
            // no gauge rather than a divide by zero.
            const int target = quest->target();
            if (target > 0)
                row.ratio = std::clamp(float(quest->progress()) / float(target),
                                       0.0f, 1.0f);
            row.value = quest->progressText();
            row.selected = index == selected;
            out.push_back(row);
            ++index;
        }
    }
    return int(out.size());
}

std::vector<std::string> GameUiData::numberKeys()
{
    std::vector<std::string> keys;
    for (const NumberKey& entry : kNumbers)
        keys.emplace_back(entry.key);
    return keys;
}

std::vector<std::string> GameUiData::textKeys()
{
    std::vector<std::string> keys;
    for (const TextKey& entry : kTexts)
        keys.emplace_back(entry.key);
    return keys;
}

std::vector<std::string> GameUiData::rowKeys()
{
    std::vector<std::string> keys;
    for (const char* key : kRowKeys)
        keys.emplace_back(key);
    return keys;
}

} // namespace game
