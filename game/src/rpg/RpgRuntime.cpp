#include "RpgRuntime.h"

#include "GameAssets.h"
#include "GameContext.h"
#include "combat/CombatComponents.h"
#include "combat/FeelComponents.h"
#include "combat/StatusEffectSystem.h"

#include <eng/Log.h>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <algorithm>
#include <cstdio>

namespace game::rpg {

namespace {

// Preserve the fraction rather than the absolute value when a maximum moves. A
// player who equips a +40 health breastplate at half health should be at half
// health, not at the same 50 points out of a larger pool -- and one who takes
// it off must not die because their current exceeded a max that shrank.
void retune(float& current, float& max, float newMax)
{
    const float ratio = max > 0.0f ? current / max : 1.0f;
    max = newMax;
    current = std::clamp(ratio, 0.0f, 1.0f) * newMax;
}

float num(const toml::table& t, const char* key, float fallback)
{
    return float(t[key].value_or(double(fallback)));
}

void readCurve(const toml::table& t, ProgressionCurve& c)
{
    c.healthBase = num(t, "health_base", c.healthBase);
    c.healthPerVigour = num(t, "health_per_vigour", c.healthPerVigour);
    c.staminaBase = num(t, "stamina_base", c.staminaBase);
    c.staminaPerAgility = num(t, "stamina_per_agility", c.staminaPerAgility);
    c.manaBase = num(t, "mana_base", c.manaBase);
    c.manaPerAttunement = num(t, "mana_per_attunement", c.manaPerAttunement);
    c.poiseBase = num(t, "poise_base", c.poiseBase);
    c.poisePerVigour = num(t, "poise_per_vigour", c.poisePerVigour);
    c.poisePerMight = num(t, "poise_per_might", c.poisePerMight);
    c.carryBase = num(t, "carry_base", c.carryBase);
    c.carryPerMight = num(t, "carry_per_might", c.carryPerMight);
    c.meleePowerPerMight = num(t, "melee_power_per_might", c.meleePowerPerMight);
    c.castPowerPerAttunement =
        num(t, "cast_power_per_attunement", c.castPowerPerAttunement);
    c.critBase = num(t, "crit_base", c.critBase);
    c.critPerFortune = num(t, "crit_per_fortune", c.critPerFortune);
    c.moveSpeedPerAgility = num(t, "move_speed_per_agility", c.moveSpeedPerAgility);
    c.staminaRegenBase = num(t, "stamina_regen_base", c.staminaRegenBase);
    c.staminaRegenPerAgility =
        num(t, "stamina_regen_per_agility", c.staminaRegenPerAgility);
    c.manaRegenBase = num(t, "mana_regen_base", c.manaRegenBase);
    c.manaRegenPerAttunement =
        num(t, "mana_regen_per_attunement", c.manaRegenPerAttunement);
}

} // namespace

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

bool RpgRuntime::load(const Paths& paths, const CombatVocabulary& vocabulary)
{
    mPaths = paths;
    mQuests.bind(mChannels);

    if (!mItems.load(assetPath(paths.items))) {
        eng::log::error("rpg: items.toml failed to load; the RPG layer is off");
        return false;
    }
    mItems.resolve(vocabulary);

    // Each of these is survivable on its own. A dungeon with no quests is a
    // dungeon; a dungeon with no items is a bug, which is why only the item
    // table is fatal.
    if (!mLoot.load(assetPath(paths.loot)))
        eng::log::error("rpg: loot.toml failed to load; nothing will drop");
    if (!mQuestLibrary.load(assetPath(paths.quests)))
        eng::log::error("rpg: quests.toml failed to load; no quests available");
    if (!mDialogue.load(assetPath(paths.dialogue)))
        eng::log::error("rpg: dialogue.toml failed to load; nobody will talk");
    if (!mTraders.load(assetPath(paths.traders)))
        eng::log::error("rpg: traders.toml failed to load; nobody will deal");
    mMarket.sync(mTraders);
    if (!mStations.load(assetPath(paths.stations)))
        eng::log::error("rpg: stations.toml failed to load; the safehouse "
                        "cannot be upgraded");

    // Cross-validation. A dangling reference here is content that will fail
    // silently at the worst possible moment -- a drop table that drops nothing,
    // a reward that grants nothing -- so it is reported at load, where the file
    // is in front of whoever wrote it. Reported, not fatal: a half-written
    // content tree should still boot so the rest can be worked on.
    const auto checkItems = [&](const std::vector<std::string>& ids,
                                const char* who) {
        for (const std::string& id : ids)
            if (!mItems.find(id))
                eng::log::error("rpg: %s references item '%s', which "
                                "items.toml does not define",
                                who, id.c_str());
    };
    checkItems(mLoot.referencedItems(), "loot.toml");
    checkItems(mQuestLibrary.referencedItems(), "quests.toml");
    checkItems(mTraders.referencedItems(), "traders.toml");
    checkItems(mStations.referencedItems(), "stations.toml");
    for (const std::string& id : mQuestLibrary.ids()) {
        const QuestDef* q = mQuestLibrary.find(id);
        if (q && !q->giver.empty() && !mDialogue.find(q->giver))
            eng::log::error("rpg: quest '%s' is given by '%s', who has no "
                            "dialogue tree", id.c_str(), q->giver.c_str());
    }

    // Progression: the curve and the starting kit.
    ProgressionCurve curve;
    mStartingSkills.clear();
    mStartingItems.clear();
    mStartingEquipment.clear();
    mStartingCurrency = 0;
    Attributes startAttributes;
    const std::string progressionPath = assetPath(paths.progression);
    if (toml::parse_result parsed = toml::parse_file(progressionPath)) {
        const toml::table& root = parsed.table();
        if (const toml::table* c = root["curve"].as_table())
            readCurve(*c, curve);
        mSkillTable.parse(&root);
        if (const toml::table* s = root["start"].as_table()) {
            if (const toml::table* attrs = (*s)["attributes"].as_table()) {
                for (auto&& [key, node] : *attrs) {
                    StatField field{};
                    if (!parseStatField(key.str(), field) || !isAttribute(field)) {
                        eng::log::error("progression.toml: '%s' is not a base "
                                        "attribute",
                                        std::string(key.str()).c_str());
                        continue;
                    }
                    startAttributes[field] = float(node.value_or(10.0));
                }
            }
            mStartingCurrency = (*s)["currency"].value_or(0);
            // Starting *experience*, not starting levels: there is exactly one
            // representation of progression and it is experience. A designer
            // who wants the player to open at Attack 10 writes the xp for it,
            // and the level follows.
            if (const toml::table* trained = (*s)["skill"].as_table())
                for (auto&& [key, node] : *trained)
                    mStartingSkills.emplace_back(std::string(key.str()),
                                                 node.value_or(int64_t(0)));
            if (const toml::array* items = (*s)["item"].as_array()) {
                for (const toml::node& i : *items) {
                    const toml::table* it = i.as_table();
                    if (!it)
                        continue;
                    const std::string id = (*it)["id"].value_or(std::string());
                    if (id.empty())
                        continue;
                    mStartingItems.emplace_back(
                        id, std::max(1, (*it)["count"].value_or(1)));
                    if ((*it)["equipped"].value_or(false))
                        mStartingEquipment.push_back(id);
                }
            }
        }
    } else {
        eng::log::error("rpg: %s: %s; using built-in progression defaults",
                        progressionPath.c_str(),
                        std::string(parsed.error().description()).c_str());
    }
    mSheet.setCurve(curve);
    mSheet.base() = startAttributes;
    mSkills.setTable(&mSkillTable);
    syncSkillModifiers();
    syncHideout();

    wireRewards();
    mLoaded = true;
    eng::log::info("rpg: %d items, %d drop tables, %d quests, %d conversations, "
                   "%d traders, %d skills",
                   mItems.size(), mLoot.size(), mQuestLibrary.size(),
                   mDialogue.size(), mTraders.size(), mSkillTable.size());
    return true;
}

bool RpgRuntime::reload(const CombatVocabulary& vocabulary)
{
    // Everything the *player* has is preserved across a content reload; only
    // the content is replaced. That is the whole point of a hot reload: edit a
    // reward, see it on the quest you are already carrying.
    const bool ok = mItems.load(assetPath(mPaths.items));
    if (ok)
        mItems.resolve(vocabulary);
    mLoot.load(assetPath(mPaths.loot));
    mQuestLibrary.load(assetPath(mPaths.quests));
    mDialogue.load(assetPath(mPaths.dialogue));
    mTraders.load(assetPath(mPaths.traders));
    mMarket.sync(mTraders);
    mStations.load(assetPath(mPaths.stations));
    const std::vector<std::string> dropped = mQuests.rebind(mQuestLibrary);
    for (const std::string& id : dropped)
        eng::log::error("rpg: quest '%s' was in the log and is no longer in "
                        "quests.toml; dropped", id.c_str());
    syncSkillModifiers();
    syncHideout();
    rpgsave::syncEquipmentModifiers(mItems, mInventory.equipment, mSheet);
    endConversation();
    return ok;
}

// ---------------------------------------------------------------------------
// Progression
// ---------------------------------------------------------------------------

void RpgRuntime::syncSkillModifiers()
{
    // One group, replaced wholesale. Skills reach the derived block through
    // exactly the layer equipment uses, which is what makes it impossible for
    // a level-up and a breastplate to double-count against each other.
    mSheet.setModifiers(stats::kSkillModifierSource, mSkills.modifiers());
}

void RpgRuntime::syncHideout()
{
    mSheet.setModifiers(kHideoutModifierSource, mHideout.modifiers(mStations));
    // Capacities are not modifiers: they are limits on containers, and the
    // container is the thing that owns them.
    constexpr int kBaseStashSlots = 40;
    mInventory.stash.setSlotLimit(kBaseStashSlots +
                                  mHideout.stashSlots(mStations));
    // A carry bonus from the base rides on the derived capacity rather than
    // replacing it, so a bigger pack and a stronger back add up.
    mInventory.backpack.setWeightLimit(mSheet.derived().carryCapacity +
                                       mHideout.carryBonus(mStations));
}

RpgRuntime::BuildCheck RpgRuntime::canBuild(const std::string& station) const
{
    BuildCheck check;
    const StationDef* def = mStations.find(station);
    if (!def) {
        check.blocker = "No such project.";
        return check;
    }
    check.tier = mHideout.next(mStations, station);
    if (!check.tier) {
        check.blocker = def->name + " is already as good as it gets.";
        return check;
    }
    if (mRaid.phase() != RaidPhase::Safehouse) {
        check.blocker = "Not from out here.";
        return check;
    }
    if (mInventory.currency < check.tier->currency) {
        check.blocker = "Not enough coin.";
        return check;
    }
    for (const auto& [item, need] : check.tier->materials) {
        // The stash, not the pack: this is work done at home with what is
        // stored there, and making the player carry it around the village
        // would be busywork.
        if (mInventory.stash.count(item) + mInventory.backpack.count(item) < need) {
            const ItemLibrary::Ref want = mItems.find(item);
            check.blocker = "Short on " + (want ? want->name : item) + ".";
            return check;
        }
    }
    for (const Condition& c : check.tier->requirements) {
        if (!evaluate(c)) {
            check.blocker = "Not yet.";
            return check;
        }
    }
    check.ok = true;
    return check;
}

bool RpgRuntime::build(const std::string& station)
{
    const BuildCheck check = canBuild(station);
    if (!check.ok) {
        note(check.blocker);
        return false;
    }
    // Everything was verified above, so paying cannot half-fail.
    for (const auto& [item, need] : check.tier->materials) {
        const int fromStash = std::min(need, mInventory.stash.count(item));
        mInventory.stash.remove(item, fromStash);
        if (fromStash < need)
            takeAway(item, need - fromStash);
    }
    mInventory.currency -= check.tier->currency;
    mHideout.setLevel(station, check.tier->level);
    for (const std::string& flag : check.tier->grantsFlags)
        apply({EffectKind::SetFlag, flag, 0});
    syncHideout();
    train("construction", int64_t(50 + 50 * check.tier->level));
    note(check.tier->name + " finished.");
    saveProfile();
    return true;
}

void RpgRuntime::train(const std::string& skill, int64_t amount)
{
    if (amount <= 0)
        return;
    const int before = mSkills.characterLevel();
    const int gained = mSkills.award(skill, amount);
    if (gained > 0) {
        const SkillDef* def = mSkillTable.find(skill);
        const std::string name = def ? def->name : skill;
        note(name + " level " + std::to_string(mSkills.level(skill)));
        // A skill level can change a derived maximum, so the sheet has to be
        // told. Only on a level, not on every point of experience: the
        // modifiers are a function of the level, not of the xp.
        syncSkillModifiers();
    }
    if (mSkills.characterLevel() > before)
        note("Character level " + std::to_string(mSkills.characterLevel()));
}

void RpgRuntime::awardCharacterXp(int64_t amount)
{
    if (amount <= 0)
        return;
    const int gained = mSkills.awardCharacter(amount);
    for (int i = 0; i < gained; ++i)
        note("Character level " + std::to_string(mSkills.characterLevel()));
}

// ---------------------------------------------------------------------------
// The raid loop
// ---------------------------------------------------------------------------

void RpgRuntime::beginRaid(int depth)
{
    // Everything already carried becomes provisional the moment the raid
    // starts: a loadout taken in is at risk, which is the whole loop.
    for (ItemStack& s : mInventory.backpack.stacks())
        s.foundThisRun = true;
    mRaid.beginRaid(depth);
}

void RpgRuntime::enterRaidLevel(int depth)
{
    mRaid.setDepth(depth);
    mRaid.enterActive();
    onDepthReached(depth);
}

void RpgRuntime::beginExtraction()
{
    if (mRaid.phase() != RaidPhase::Active)
        return;
    mRaid.beginExtraction();
    note("Holding the way out...");
}

void RpgRuntime::cancelExtraction()
{
    if (mRaid.phase() != RaidPhase::Extracting)
        return;
    mRaid.cancelExtraction();
    note("You stepped away from the threshold.");
}

bool RpgRuntime::tickRaid(float dt)
{
    if (!mRaid.tick(dt))
        return false;
    // Landed. Apply the extraction and write the profile in that order: the
    // save has to record the state the player earned, not the one before it.
    onExtracted(mRaid.depth());
    // Surviving is worth character experience in its own right, scaled by how
    // deep the run went -- the Tarkov survival bonus.
    awardCharacterXp(int64_t(250 + 250 * std::max(0, mRaid.depth())));
    mMarket.advanceDays(mTraders, 1);
    saveProfile();
    return true;
}

void RpgRuntime::returnToSafehouse()
{
    mRaid.returnToSafehouse();
    saveProfile();
}

// ---------------------------------------------------------------------------
// Trading
// ---------------------------------------------------------------------------

PriceQuote RpgRuntime::sellQuote(const std::string& trader,
                                 const std::string& item) const
{
    PriceQuote q;
    const TraderDef* def = mTraders.find(trader);
    if (!def) {
        q.refusal = "Nobody here trades.";
        return q;
    }
    // Condition of the worst stack the player would part with first; the
    // container spends found-this-run stock before owned stock, so that is the
    // one being quoted.
    float condition = 1.0f;
    for (const ItemStack& s : mInventory.backpack.stacks())
        if (s.item == item)
            condition = std::min(condition, s.condition);
    return mMarket.sellQuote(*def, mItems, item, condition,
                             mWorld.standing(trader));
}

PriceQuote RpgRuntime::buyQuote(const std::string& trader,
                                const std::string& item) const
{
    PriceQuote q;
    const TraderDef* def = mTraders.find(trader);
    if (!def) {
        q.refusal = "Nobody here trades.";
        return q;
    }
    return mMarket.buyQuote(*def, mItems, item, mWorld.standing(trader));
}

RpgRuntime::TradeResult RpgRuntime::sell(const std::string& trader,
                                         const std::string& item, int count)
{
    TradeResult result;
    const TraderDef* def = mTraders.find(trader);
    if (!def) {
        result.message = "Nobody here trades.";
        return result;
    }
    const int have = mInventory.backpack.count(item);
    if (have <= 0) {
        result.message = "You are not carrying that.";
        return result;
    }
    const int units = std::min(count, have);
    const PriceQuote quote = sellQuote(trader, item);
    if (!quote.tradeable) {
        result.message = quote.refusal;
        return result;
    }
    int coin = quote.unitPrice * units;
    TraderState* state = mMarket.state(trader);
    if (state && coin > state->purse) {
        // A trader with an empty purse is a real constraint, not an error: it
        // is what stops one vendor absorbing an entire expedition and is why
        // the player has to know more than one person.
        coin = state->purse;
        if (coin <= 0) {
            result.message = def->name + " has no coin left today.";
            return result;
        }
    }
    takeAway(item, units);
    mMarket.recordSale(*def, item, units, coin);
    mInventory.currency += coin;
    // Trading is a skill like any other, and it is trained by doing it.
    train("bartering", int64_t(std::max(1, coin / 4)));
    result.ok = true;
    result.coin = coin;
    result.message = "Sold " + std::to_string(units) + " for " +
                     std::to_string(coin);
    note(result.message);
    return result;
}

RpgRuntime::TradeResult RpgRuntime::buy(const std::string& trader,
                                        const std::string& item, int count)
{
    TradeResult result;
    const TraderDef* def = mTraders.find(trader);
    if (!def) {
        result.message = "Nobody here trades.";
        return result;
    }
    const TraderState* state = mMarket.state(trader);
    const auto stocked = state ? state->stock.find(item) : decltype(state->stock)::const_iterator{};
    const int available = (state && stocked != state->stock.end()) ? stocked->second : 0;
    if (available <= 0) {
        result.message = def->name + " has none of those.";
        return result;
    }
    const PriceQuote quote = buyQuote(trader, item);
    const int units = std::min(count, available);
    const int coin = quote.unitPrice * units;
    if (coin > mInventory.currency) {
        result.message = "Not enough coin.";
        return result;
    }
    int added = 0;
    give(item, units, /*foundThisRun=*/false, &added);
    if (added <= 0) {
        result.message = "No room for that.";
        return result;
    }
    const int paid = quote.unitPrice * added;
    mInventory.currency -= paid;
    mMarket.recordPurchase(*def, item, added, paid);
    train("bartering", int64_t(std::max(1, paid / 8)));
    result.ok = true;
    result.coin = -paid;
    result.message = "Bought " + std::to_string(added) + " for " +
                     std::to_string(paid);
    note(result.message);
    return result;
}

RpgRuntime::TradeResult RpgRuntime::barter(const std::string& trader,
                                           const std::string& barterId)
{
    TradeResult result;
    const TraderDef* def = mTraders.find(trader);
    if (!def) {
        result.message = "Nobody here trades.";
        return result;
    }
    const auto it = std::find_if(def->barters.begin(), def->barters.end(),
                                 [&](const BarterRecipe& b) {
                                     return b.id == barterId;
                                 });
    if (it == def->barters.end()) {
        result.message = "No such arrangement.";
        return result;
    }
    if (it->unique && mMarket.barterDone(trader, barterId)) {
        result.message = "That was a one-time arrangement.";
        return result;
    }
    if (mWorld.standing(trader) < it->minStanding) {
        result.message = def->name + " does not trust you that far.";
        return result;
    }
    // Check the whole cost before paying any of it: a barter that takes two of
    // three ingredients and then fails is unrecoverable for the player.
    for (const auto& [item, need] : it->wants) {
        if (mInventory.backpack.count(item) < need) {
            const ItemLibrary::Ref want = mItems.find(item);
            result.message = "You are short on " +
                             (want ? want->name : item) + ".";
            return result;
        }
    }
    for (const auto& [item, need] : it->wants)
        takeAway(item, need);
    give(it->gives, it->givesCount, /*foundThisRun=*/false);
    mMarket.noteBarter(*def, barterId);
    mWorld.addStanding(trader, 1);
    train("bartering", 150);
    result.ok = true;
    const ItemLibrary::Ref got = mItems.find(it->gives);
    result.message = "Traded for " + (got ? got->name : it->gives);
    note(result.message);
    return result;
}

void RpgRuntime::wireRewards()
{
    // The reference architecture's decoupling: the quest system announces, and
    // whoever owns experience, coin and the inventory takes what it recognises.
    // That listener is this lambda, and it is the only place a reward is
    // spent -- QuestBook itself never touches the sheet.
    mChannels.quests.completed.subscribe([this](Quest& q) {
        note(q.title() + " - complete");
        if (q.def().autoTurnIn) {
            // Nothing to walk back to: resolve it here so the player is not
            // left holding a quest with no giver.
            if (Quest* turned = mQuests.turnIn(q.id()))
                (void)turned;
        }
    });
    mChannels.quests.turnedIn.subscribe([this](Quest& q) { grantRewards(q); });
    mChannels.quests.assigned.subscribe(
        [this](Quest& q) { note("New task: " + q.title()); });
}

void RpgRuntime::grantRewards(Quest& quest)
{
    const QuestDef::Rewards& r = quest.rewards();
    if (r.xp > 0)
        awardCharacterXp(r.xp);
    if (r.currency != 0) {
        mInventory.currency = std::max(0, mInventory.currency + r.currency);
    }
    for (const auto& [item, count] : r.items)
        give(item, count, /*foundThisRun=*/false);
    for (const Effect& e : r.effects)
        apply(e);
    note(quest.title() + " - reward taken");
}

// ---------------------------------------------------------------------------
// Conditions and effects
// ---------------------------------------------------------------------------

bool RpgRuntime::evaluate(const Condition& c) const
{
    bool result = false;
    switch (c.kind) {
        case ConditionKind::Always:
            result = true;
            break;
        case ConditionKind::FlagSet:
            result = mWorld.flag(c.subject);
            break;
        case ConditionKind::FlagClear:
            result = !mWorld.flag(c.subject);
            break;
        case ConditionKind::HasItem:
            result = mInventory.backpack.count(c.subject) >=
                     std::max(1, c.value);
            break;
        case ConditionKind::QuestActive:
            result = mQuests.isActive(c.subject);
            break;
        case ConditionKind::QuestCompleted:
            result = mQuests.isFinished(c.subject);
            break;
        case ConditionKind::QuestUnstarted:
            result = !mQuests.isStarted(c.subject);
            break;
        case ConditionKind::LevelAtLeast:
            // An empty subject means the character level (the Tarkov number the
            // world gates on); a subject names a skill (the OSRS number a door
            // or a piece of equipment gates on).
            result = c.subject.empty()
                         ? mSkills.characterLevel() >= c.value
                         : mSkills.level(c.subject) >= c.value;
            break;
        case ConditionKind::StandingAtLeast:
            result = mWorld.standing(c.subject) >= c.value;
            break;
        case ConditionKind::CurrencyAtLeast:
            result = mInventory.currency >= c.value;
            break;
        case ConditionKind::Count:
            break;
    }
    return c.negate ? !result : result;
}

void RpgRuntime::apply(const Effect& e)
{
    switch (e.kind) {
        case EffectKind::None:
            break;
        case EffectKind::SetFlag:
            mWorld.setFlag(e.subject);
            // Raised, not just stored: a FlagQuest is waiting on exactly this.
            mChannels.flags.raise(eng::intern(e.subject));
            break;
        case EffectKind::ClearFlag:
            mWorld.clearFlag(e.subject);
            break;
        case EffectKind::GiveItem:
            give(e.subject, std::max(1, e.value), /*foundThisRun=*/false);
            break;
        case EffectKind::TakeItem:
            takeAway(e.subject, std::max(1, e.value));
            break;
        case EffectKind::GiveCurrency:
            mInventory.currency = std::max(0, mInventory.currency + e.value);
            break;
        case EffectKind::GiveXp:
            // `subject` names a skill when the effect wants to train one, and
            // is empty when it is plain character experience.
            if (e.subject.empty())
                awardCharacterXp(e.value);
            else
                train(e.subject, e.value);
            break;
        case EffectKind::StartQuest:
            mQuests.assign(mQuestLibrary, e.subject,
                           [this](const Condition& c) { return evaluate(c); });
            break;
        case EffectKind::CompleteQuest:
            mQuests.forceComplete(mQuestLibrary, e.subject);
            break;
        case EffectKind::TurnInQuest:
            mQuests.turnIn(e.subject);
            break;
        case EffectKind::FailQuest:
            mQuests.fail(e.subject);
            break;
        case EffectKind::AddStanding:
            mWorld.addStanding(e.subject, e.value);
            break;
        case EffectKind::AdvanceDay:
            mWorld.advanceDay(std::max(1, e.value));
            note("A day passes in the village.");
            break;
        case EffectKind::Count:
            break;
    }
}

// ---------------------------------------------------------------------------
// Inventory actions
// ---------------------------------------------------------------------------

void RpgRuntime::publishInventory(const std::string& item, int delta)
{
    mChannels.inventory.raise(eng::intern(item),
                              mInventory.backpack.count(item), delta);
}

AddResult RpgRuntime::give(const std::string& item, int count,
                           bool foundThisRun, int* added)
{
    int placed = 0;
    const AddResult result =
        mInventory.backpack.add(mItems, item, count, foundThisRun, &placed);
    if (added)
        *added = placed;
    if (placed > 0)
        publishInventory(item, placed);
    return result;
}

int RpgRuntime::takeAway(const std::string& item, int count)
{
    const int removed = mInventory.backpack.remove(item, count);
    if (removed > 0)
        publishInventory(item, -removed);
    return removed;
}

bool RpgRuntime::equip(const std::string& item)
{
    const ItemLibrary::Ref def = mItems.find(item);
    if (!def)
        return false;
    if (def->slot == EquipSlot::None && def->weaponId.empty()) {
        note("That is not something you can wear.");
        return false;
    }
    if (mInventory.backpack.count(item) <= 0)
        return false;
    const std::string displaced = mInventory.equipment.equip(*def);
    // Worn items leave the pack and displaced ones come back to it, so weight
    // means what it says and a swap cannot duplicate or destroy anything.
    mInventory.backpack.remove(item, 1);
    if (!displaced.empty())
        mInventory.backpack.add(mItems, displaced, 1, false);
    rpgsave::syncEquipmentModifiers(mItems, mInventory.equipment, mSheet);
    note("Equipped " + def->name);
    return true;
}

bool RpgRuntime::unequip(EquipSlot slot)
{
    const std::string item = mInventory.equipment.at(slot);
    if (item.empty())
        return false;
    int added = 0;
    mInventory.backpack.add(mItems, item, 1, false, &added);
    if (added <= 0) {
        note("No room to stow that.");
        return false;
    }
    mInventory.equipment.unequip(slot);
    rpgsave::syncEquipmentModifiers(mItems, mInventory.equipment, mSheet);
    return true;
}

bool RpgRuntime::useItem(entt::registry& registry, entt::entity player,
                         const std::string& item)
{
    const ItemLibrary::Ref def = mItems.find(item);
    if (!def || mInventory.backpack.count(item) <= 0)
        return false;
    const UseDef& use = def->use;
    if (use.kind == UseKind::None) {
        note(def->name + " does nothing on its own.");
        return false;
    }
    if (!registry.valid(player))
        return false;

    switch (use.kind) {
        case UseKind::RestoreHealth:
            if (Health* h = registry.try_get<Health>(player))
                h->current = std::min(h->max, h->current + use.magnitude);
            break;
        case UseKind::RestoreStamina:
            if (Stamina* s = registry.try_get<Stamina>(player))
                s->current = std::min(s->max, s->current + use.magnitude);
            break;
        case UseKind::RestoreMana:
            if (Mana* m = registry.try_get<Mana>(player))
                m->current = std::min(m->max, m->current + use.magnitude);
            break;
        case UseKind::CureStatus:
            if (StatusEffects* fx = registry.try_get<StatusEffects>(player))
                fx->active.clear();
            break;
        case UseKind::GrantEffect:
            // Timed buffs are a modifier group like any other. They are
            // deliberately not saved (a save is a state boundary), and the
            // duration is not ticked here -- whoever owns the frame does.
            mSheet.setModifiers("effect:" + def->id, use.grants);
            break;
        case UseKind::None:
        case UseKind::Count:
            break;
    }

    if (use.consumesStack)
        takeAway(item, 1);
    if (!def->event.empty())
        apply({EffectKind::SetFlag, def->event, 0});
    applyToPlayer(registry, player);
    note("Used " + def->name);
    return true;
}

bool RpgRuntime::canTake(int pickupId) const
{
    const PickupSystem::Entry* entry = mPickups.find(pickupId);
    if (!entry)
        return false;
    const ItemLibrary::Ref def = mItems.find(entry->item);
    if (!def)
        return false;
    if (mInventory.backpack.weightLimit() <= 0.0f)
        return true;
    const float free =
        mInventory.backpack.weightLimit() - mInventory.backpack.weight(mItems);
    return def->weight <= 0.0f || free >= def->weight;
}

bool RpgRuntime::takePickup(GameContext& ctx, int pickupId)
{
    const PickupSystem::Entry* entry = mPickups.find(pickupId);
    if (!entry)
        return false;
    const std::string item = entry->item;
    const int count = entry->count;

    // Ask first, take second: a pickup that vanishes on a failed add is the
    // bug that loses items, and it is unrecoverable for the player.
    int added = 0;
    const AddResult result = give(item, count, /*foundThisRun=*/true, &added);
    if (added <= 0) {
        note(result == AddResult::TooHeavy ? "Too heavy to carry."
                                           : "No room for that.");
        return false;
    }
    const ItemLibrary::Ref def = mItems.find(item);
    const std::string name = def ? def->name : item;
    if (added < count) {
        // Partial: take what fitted and leave the rest where it was.
        mPickups.take(ctx, pickupId);
        mPickups.spawn(ctx, *def, entry->position, count - added, true);
        note("Took " + std::to_string(added) + " " + name +
             "; left the rest.");
        return true;
    }
    mPickups.take(ctx, pickupId);
    note(count > 1 ? "Took " + std::to_string(count) + " " + name
                   : "Took " + name);
    return true;
}

void RpgRuntime::setPickupsForLevel(GameContext& ctx,
                                    const std::vector<ScenePlacement>& placements)
{
    mPickups.clear(ctx);
    const int spawned = mPickups.spawnAuthored(ctx, mItems, placements);
    if (spawned > 0)
        eng::log::info("rpg: %d authored pickups in this level", spawned);
}

// ---------------------------------------------------------------------------
// What the game tells it
// ---------------------------------------------------------------------------

void RpgRuntime::onEnemyKilled(GameContext& ctx, const std::string& enemyId,
                               int64_t xp, const std::string& lootTable,
                               glm::vec3 at)
{
    if (xp > 0)
        awardCharacterXp(xp);
    // Announced after the experience so a listener that draws a level-up
    // banner sees the new level, and before the loot so a quest that counts
    // kills does not race a quest that counts what dropped.
    mChannels.combat.raise(eng::intern(enemyId), 1);

    if (lootTable.empty())
        return;
    const LootTable* table = mLoot.find(lootTable);
    if (!table) {
        eng::log::error("rpg: enemy '%s' drops table '%s', which loot.toml "
                        "does not define", enemyId.c_str(), lootTable.c_str());
        return;
    }
    const LootResult rolled =
        loot::roll(*table, mLootRng, mSheet.derived().attributes[StatField::Fortune]);
    if (rolled.coin > 0) {
        mInventory.currency += rolled.coin;
        note("+" + std::to_string(rolled.coin) + " coin");
    }
    // Drops land in the world rather than in the pack: picking loot up is the
    // decision §14 wants, and a corpse that silently fills your bag removes it.
    int spread = 0;
    for (const LootDrop& drop : rolled.drops) {
        const ItemLibrary::Ref def = mItems.find(drop.item);
        if (!def)
            continue;
        // Fan them out so two drops are two silhouettes, not one.
        const float angle = float(spread++) * 2.4f;
        const glm::vec3 offset{std::cos(angle) * 0.35f, 0.0f,
                               std::sin(angle) * 0.35f};
        mPickups.spawn(ctx, *def, at + offset, drop.count, /*fromLoot=*/true);
    }
}

void RpgRuntime::onDepthReached(int depth)
{
    mWorld.noteDepth(depth);
    mChannels.depth.raise(depth, /*extracted=*/false);
}

void RpgRuntime::onExtracted(int depth)
{
    mWorld.noteDepth(depth);
    // Everything carried stops being provisional the moment it is out.
    mInventory.backpack.markAllOwned();
    mWorld.advanceDay(1);
    mChannels.depth.raise(depth, /*extracted=*/true);
    note("You are out, and what you carry is yours.");
}

std::vector<ItemStack> RpgRuntime::onPlayerDied(const std::string& killerId)
{
    const std::vector<ItemStack> lost = mInventory.backpack.loseFindings(mItems);
    for (const ItemStack& s : lost)
        publishInventory(s.item, -s.count);
    mWorld.advanceDay(1);
    if (!killerId.empty())
        mWorld.addCounter("killed_by." + killerId, 1);
    if (!lost.empty()) {
        int total = 0;
        for (const ItemStack& s : lost)
            total += s.count;
        note("The dungeon kept " + std::to_string(total) + " of your findings.");
    }
    return lost;
}

// ---------------------------------------------------------------------------
// Conversation
// ---------------------------------------------------------------------------

bool RpgRuntime::talkTo(const std::string& npc)
{
    const auto condition = [this](const Condition& c) { return evaluate(c); };
    const auto effect = [this](const Effect& e) { apply(e); };
    if (!mConversation.begin(mDialogue, npc, condition, effect))
        return false;
    mPartner = npc;
    // Delivery quests need to know a hand-over is possible before the choice
    // that performs it fires.
    mQuests.setConversationPartner(npc);
    mChannels.npcs.raise(eng::intern(npc));
    return true;
}

bool RpgRuntime::chooseReply(int offeredIndex)
{
    const auto condition = [this](const Condition& c) { return evaluate(c); };
    const auto effect = [this](const Effect& e) { apply(e); };
    const bool ok = mConversation.choose(offeredIndex, condition, effect);
    if (!mConversation.active())
        endConversation();
    return ok;
}

void RpgRuntime::endConversation()
{
    mConversation.end();
    mPartner.clear();
    mQuests.setConversationPartner({});
}

// ---------------------------------------------------------------------------
// The seam to combat
// ---------------------------------------------------------------------------

void RpgRuntime::applyToPlayer(entt::registry& registry,
                               entt::entity player) const
{
    if (!registry.valid(player))
        return;
    const DerivedStats& d = mSheet.derived();

    if (Health* h = registry.try_get<Health>(player))
        retune(h->current, h->max, d.healthMax);
    if (Stamina* s = registry.try_get<Stamina>(player)) {
        retune(s->current, s->max, d.staminaMax);
        s->regenRate = d.staminaRegen;
    }
    if (Mana* m = registry.try_get<Mana>(player)) {
        retune(m->current, m->max, d.manaMax);
        m->regenRate = d.manaRegen;
    }
    if (Poise* p = registry.try_get<Poise>(player))
        retune(p->current, p->max, d.poiseMax);

    // Resistances are replaced wholesale rather than added to: the equipment
    // is the whole story, so taking a fire cloak off must take the resistance
    // with it. Anything else that wants to grant resistance does it through a
    // status effect, which the status system owns.
    Resistances& res = registry.get_or_emplace<Resistances>(player);
    const std::array<float, kMaxDamageTypes> worn =
        mInventory.equipment.resistances(mItems);
    for (int i = 0; i < kMaxDamageTypes; ++i)
        res.value[i] = worn[std::size_t(i)];
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

bool RpgRuntime::saveTo(const std::string& path) const
{
    // The one rule the extraction loop rests on. There is no override and no
    // force flag: a mid-raid write would let a player undo a death, and every
    // stake in the game is downstream of not being able to.
    if (!mRaid.mayPersist()) {
        eng::log::error("rpg: refusing to write the profile during a raid "
                        "(phase %s)", nameOf(mRaid.phase()));
        return false;
    }
    return rpgsave::writeFile(
        path, rpgsave::capture(mSkills, mInventory, mQuests, mWorld, mMarket,
                               mHideout));
}

bool RpgRuntime::loadFrom(const std::string& path)
{
    const std::optional<RpgSaveData> data = rpgsave::readFile(path);
    if (!data)
        return false;
    const std::vector<std::string> unknown = rpgsave::restore(
        *data, mItems, mQuestLibrary, mTraders, mSkills, mSheet, mInventory,
        mQuests, mWorld, mMarket, mHideout);
    mSkills.setTable(&mSkillTable);
    syncSkillModifiers();
    syncHideout();
    for (const std::string& id : unknown)
        eng::log::error("rpg: the save references '%s', which the content no "
                        "longer defines; kept but inert", id.c_str());
    endConversation();
    note("Loaded.");
    return true;
}

void RpgRuntime::newGame()
{
    mQuests.clear();
    mWorld.clear();
    mInventory.backpack.clear();
    mInventory.stash.clear();
    mInventory.equipment.clear();
    mSheet.clearAllModifiers();
    mSkills.clear();
    for (const auto& [skill, experience] : mStartingSkills)
        mSkills.award(skill, experience);
    mMarket.clear();
    mMarket.sync(mTraders);
    mHideout.clear();
    mRaid.returnToSafehouse();
    mInventory.currency = mStartingCurrency;
    mLog.clear();

    for (const auto& [id, count] : mStartingItems)
        if (mInventory.backpack.add(mItems, id, count, false) ==
            AddResult::UnknownItem)
            eng::log::error("rpg: starting kit names '%s', which items.toml "
                            "does not define", id.c_str());
    for (const std::string& id : mStartingEquipment)
        equip(id);
    syncSkillModifiers();
    syncHideout();
    rpgsave::syncEquipmentModifiers(mItems, mInventory.equipment, mSheet);
}

void RpgRuntime::note(std::string line)
{
    // A short feed, because it is drawn on a HUD and read at a glance while
    // something is trying to kill you.
    constexpr std::size_t kMax = 8;
    mLog.push_back(std::move(line));
    if (mLog.size() > kMax)
        mLog.erase(mLog.begin(), mLog.begin() + long(mLog.size() - kMax));
}

} // namespace game::rpg
