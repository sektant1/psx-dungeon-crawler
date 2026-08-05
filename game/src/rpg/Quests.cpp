#include "Quests.h"

#include <eng/Log.h>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <algorithm>
#include <array>

namespace game::rpg {

namespace {

std::vector<Condition> readConditions(const toml::table& t, const char* key,
                                      const std::string& owner)
{
    std::vector<Condition> out;
    const toml::array* arr = t[key].as_array();
    if (!arr)
        return out;
    for (const toml::node& node : *arr) {
        const toml::table* c = node.as_table();
        if (!c)
            continue;
        Condition cond;
        const std::string kind = (*c)["kind"].value_or(std::string("always"));
        if (!parseConditionKind(kind, cond.kind)) {
            eng::log::error("quests: '%s' has condition kind '%s', which does "
                            "not exist", owner.c_str(), kind.c_str());
            continue;
        }
        cond.subject = (*c)["subject"].value_or(std::string());
        cond.value = (*c)["value"].value_or(0);
        cond.negate = (*c)["negate"].value_or(false);
        out.push_back(std::move(cond));
    }
    return out;
}

std::vector<Effect> readEffects(const toml::table& t, const char* key,
                                const std::string& owner)
{
    std::vector<Effect> out;
    const toml::array* arr = t[key].as_array();
    if (!arr)
        return out;
    for (const toml::node& node : *arr) {
        const toml::table* e = node.as_table();
        if (!e)
            continue;
        Effect eff;
        const std::string kind = (*e)["kind"].value_or(std::string("none"));
        if (!parseEffectKind(kind, eff.kind)) {
            eng::log::error("quests: '%s' has effect kind '%s', which does not "
                            "exist", owner.c_str(), kind.c_str());
            continue;
        }
        eff.subject = (*e)["subject"].value_or(std::string());
        eff.value = (*e)["value"].value_or(0);
        out.push_back(std::move(eff));
    }
    return out;
}

constexpr std::array<const char*, std::size_t(QuestState::Count)> kStateNames{
    "pending", "active", "complete", "turned_in", "failed"};

} // namespace

const char* nameOf(QuestState s)
{
    const auto i = std::size_t(s);
    return i < kStateNames.size() ? kStateNames[i] : "?";
}

std::vector<Condition> readConditionArray(const void* table, const char* key,
                                          const std::string& owner)
{
    return readConditions(*static_cast<const toml::table*>(table), key, owner);
}

std::vector<Effect> readEffectArray(const void* table, const char* key,
                                    const std::string& owner)
{
    return readEffects(*static_cast<const toml::table*>(table), key, owner);
}

// ---------------------------------------------------------------------------
// QuestLibrary
// ---------------------------------------------------------------------------

bool QuestLibrary::parse(const void* tomlTable)
{
    const toml::table& root = *static_cast<const toml::table*>(tomlTable);
    const toml::table* group = root["quest"].as_table();
    if (!group) {
        eng::log::error("QuestLibrary: document defines no [quest.*] rows");
        return false;
    }

    const std::vector<std::string> types = questTypes();
    mQuests.clear();
    for (auto&& [key, node] : *group) {
        const toml::table* t = node.as_table();
        if (!t)
            continue;
        QuestDef quest;
        quest.id = std::string(key.str());
        quest.type = (*t)["type"].value_or(quest.type);
        if (std::find(types.begin(), types.end(), quest.type) == types.end()) {
            // A quest with no logic would sit Active forever, which reads to a
            // player as a broken game rather than as a content typo. Refuse it
            // here, where the file is in front of the author.
            std::string known;
            for (const std::string& ty : types)
                known += (known.empty() ? "" : ", ") + ty;
            eng::log::error("QuestLibrary: quest '%s' has type '%s'; known "
                            "types are %s. Row dropped.",
                            quest.id.c_str(), quest.type.c_str(), known.c_str());
            continue;
        }
        quest.title = (*t)["title"].value_or(quest.title);
        quest.summary = (*t)["summary"].value_or(quest.summary);
        quest.giver = (*t)["giver"].value_or(quest.giver);
        quest.objectiveText = (*t)["objective"].value_or(quest.objectiveText);
        quest.autoTurnIn = (*t)["auto_turn_in"].value_or(quest.autoTurnIn);
        quest.repeatable = (*t)["repeatable"].value_or(quest.repeatable);
        quest.target = (*t)["target"].value_or(quest.target);
        quest.amount = std::max(1, (*t)["amount"].value_or(quest.amount));
        quest.extra = (*t)["extra"].value_or(quest.extra);
        quest.requirements = readConditions(*t, "requirement", quest.id);

        if (const toml::table* r = (*t)["reward"].as_table()) {
            quest.rewards.xp = (*r)["xp"].value_or(int64_t(0));
            quest.rewards.currency = (*r)["currency"].value_or(0);
            if (const toml::array* items = (*r)["item"].as_array()) {
                for (const toml::node& i : *items) {
                    const toml::table* it = i.as_table();
                    if (!it)
                        continue;
                    const std::string id = (*it)["id"].value_or(std::string());
                    if (id.empty())
                        continue;
                    quest.rewards.items.emplace_back(
                        id, std::max(1, (*it)["count"].value_or(1)));
                }
            }
            quest.rewards.effects = readEffects(*r, "effect", quest.id);
        }
        mQuests[quest.id] = std::move(quest);
    }

    eng::log::info("QuestLibrary: %d quests", int(mQuests.size()));
    return !mQuests.empty();
}

bool QuestLibrary::load(const std::string& tomlPath)
{
    toml::parse_result parsed = toml::parse_file(tomlPath);
    if (!parsed) {
        eng::log::error("QuestLibrary: %s: %s", tomlPath.c_str(),
                        std::string(parsed.error().description()).c_str());
        mQuests.clear();
        return false;
    }
    return parse(&parsed.table());
}

bool QuestLibrary::loadFromString(const char* tomlSrc)
{
    toml::parse_result parsed = toml::parse(tomlSrc);
    if (!parsed) {
        eng::log::error("QuestLibrary: %s",
                        std::string(parsed.error().description()).c_str());
        mQuests.clear();
        return false;
    }
    return parse(&parsed.table());
}

const QuestDef* QuestLibrary::find(const std::string& id) const
{
    const auto it = mQuests.find(id);
    return it == mQuests.end() ? nullptr : &it->second;
}

std::vector<std::string> QuestLibrary::ids() const
{
    std::vector<std::string> out;
    out.reserve(mQuests.size());
    for (const auto& [id, q] : mQuests)
        out.push_back(id);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> QuestLibrary::referencedItems() const
{
    std::vector<std::string> out;
    for (const auto& [id, q] : mQuests) {
        for (const auto& [item, count] : q.rewards.items)
            out.push_back(item);
        if ((q.type == "collect" || q.type == "deliver") && !q.target.empty())
            out.push_back(q.target);
        for (const Effect& e : q.rewards.effects)
            if (e.kind == EffectKind::GiveItem || e.kind == EffectKind::TakeItem)
                out.push_back(e.subject);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

// ---------------------------------------------------------------------------
// Quest
// ---------------------------------------------------------------------------

void Quest::enable(GameChannels& channels)
{
    if (mState == QuestState::Active)
        return;
    mState = QuestState::Active;
    onActivate(channels);
    // A quest whose condition is already met at assignment completes on the
    // spot rather than waiting for the next event -- "bring me the thing you
    // are already carrying" must be turn-in-able immediately.
    tryComplete();
}

void Quest::disable()
{
    for (Tracked& t : mTracked)
        if (t.cancel && t.id != kNoSubscription)
            t.cancel(t.id);
    mTracked.clear();
}

void Quest::track(SubscriptionId id, std::function<void(SubscriptionId)> cancel)
{
    if (id != kNoSubscription)
        mTracked.push_back({id, std::move(cancel)});
}

void Quest::tryComplete()
{
    if (mState == QuestState::Active && satisfied())
        complete();
}

void Quest::complete()
{
    mState = QuestState::Complete;
    // Unsubscribe before announcing: a handler of `completed` may assign the
    // next quest in a chain, and this one must already be off the channels
    // when that happens.
    disable();
    if (mChannel)
        mChannel->completed.raise(*this);
}

void Quest::forceComplete()
{
    if (mState == QuestState::TurnedIn)
        return;
    mState = QuestState::Active; // so complete() is a legal transition
    complete();
}

void Quest::markTurnedIn()
{
    mState = QuestState::TurnedIn;
    disable();
    if (mChannel)
        mChannel->turnedIn.raise(*this);
}

void Quest::fail()
{
    if (mState == QuestState::TurnedIn)
        return;
    mState = QuestState::Failed;
    disable();
    if (mChannel)
        mChannel->failed.raise(*this);
}

std::string Quest::progressText() const
{
    std::string text =
        mDef->objectiveText.empty() ? mDef->title : mDef->objectiveText;
    if (target() > 1)
        text += " (" + std::to_string(std::min(progress(), target())) + "/" +
                std::to_string(target()) + ")";
    return text;
}

// ---------------------------------------------------------------------------
// The concrete goals
// ---------------------------------------------------------------------------

void CollectItemQuest::onActivate(GameChannels& channels)
{
    const eng::StringId want = eng::intern(mDef->target);
    InventoryChannel& ch = channels.inventory;
    track(ch.subscribe([this, want](eng::StringId item, int held, int) {
              if (item != want)
                  return;
              mHeld = held;
              tryComplete();
          }),
          [&ch](SubscriptionId id) { ch.unsubscribe(id); });
}

void DefeatEnemiesQuest::onActivate(GameChannels& channels)
{
    // An empty target counts anything, which is how "survive ten of them"
    // is written without listing every enemy in the game.
    const eng::StringId want =
        mDef->target.empty() ? eng::StringId{} : eng::intern(mDef->target);
    CombatChannel& ch = channels.combat;
    track(ch.subscribe([this, want](eng::StringId enemy, int count) {
              if (want.valid() && enemy != want)
                  return;
              mDefeated += std::max(1, count);
              tryComplete();
          }),
          [&ch](SubscriptionId id) { ch.unsubscribe(id); });
}

void DeliverItemQuest::onActivate(GameChannels& channels)
{
    const eng::StringId want = eng::intern(mDef->target);
    InventoryChannel& ch = channels.inventory;
    track(ch.subscribe([this, want](eng::StringId item, int, int delta) {
              // A hand-over is a loss that happens while the giver is being
              // spoken to. Losing the thing anywhere else is losing it.
              if (item != want || delta >= 0 || !mNearGiver)
                  return;
              mDelivered += -delta;
              tryComplete();
          }),
          [&ch](SubscriptionId id) { ch.unsubscribe(id); });
}

void ReachDepthQuest::onActivate(GameChannels& channels)
{
    const bool needsExtraction = mDef->extra == "extract";
    DepthChannel& ch = channels.depth;
    track(ch.subscribe([this, needsExtraction](int depth, bool extracted) {
              if (needsExtraction && !extracted)
                  return;
              mBest = std::max(mBest, depth);
              tryComplete();
          }),
          [&ch](SubscriptionId id) { ch.unsubscribe(id); });
}

void TalkToNpcQuest::onActivate(GameChannels& channels)
{
    const eng::StringId want = eng::intern(mDef->target);
    NpcChannel& ch = channels.npcs;
    track(ch.subscribe([this, want](eng::StringId npc) {
              if (npc != want)
                  return;
              ++mTalked;
              tryComplete();
          }),
          [&ch](SubscriptionId id) { ch.unsubscribe(id); });
}

void FlagQuest::onActivate(GameChannels& channels)
{
    const eng::StringId want = eng::intern(mDef->target);
    FlagChannel& ch = channels.flags;
    track(ch.subscribe([this, want](eng::StringId flag) {
              if (flag != want)
                  return;
              mSet = true;
              tryComplete();
          }),
          [&ch](SubscriptionId id) { ch.unsubscribe(id); });
}

std::vector<std::string> questTypes()
{
    return {"collect", "defeat", "deliver", "depth", "talk", "flag"};
}

std::unique_ptr<Quest> makeQuest(const QuestDef& def, QuestChannel& channel)
{
    if (def.type == "collect")
        return std::make_unique<CollectItemQuest>(def, channel);
    if (def.type == "defeat")
        return std::make_unique<DefeatEnemiesQuest>(def, channel);
    if (def.type == "deliver")
        return std::make_unique<DeliverItemQuest>(def, channel);
    if (def.type == "depth")
        return std::make_unique<ReachDepthQuest>(def, channel);
    if (def.type == "talk")
        return std::make_unique<TalkToNpcQuest>(def, channel);
    if (def.type == "flag")
        return std::make_unique<FlagQuest>(def, channel);
    eng::log::error("makeQuest: '%s' has unknown type '%s'", def.id.c_str(),
                    def.type.c_str());
    return nullptr;
}

// ---------------------------------------------------------------------------
// QuestBook
// ---------------------------------------------------------------------------

Quest* QuestBook::find(const std::string& id)
{
    for (const std::unique_ptr<Quest>& q : mQuests)
        if (q && q->id() == id)
            return q.get();
    return nullptr;
}

const Quest* QuestBook::find(const std::string& id) const
{
    return const_cast<QuestBook*>(this)->find(id);
}

// A quest that was never assigned has no object, so there is no state to
// return. Callers ask isStarted() first; reporting Pending for "not in the
// book" is what makes `stateOf(x) == QuestState::Active` safe to write.
QuestState QuestBook::stateOf(const std::string& id) const
{
    const Quest* q = find(id);
    return q ? q->state() : QuestState::Pending;
}

bool QuestBook::isStarted(const std::string& id) const
{
    return find(id) != nullptr;
}
bool QuestBook::isActive(const std::string& id) const
{
    const Quest* q = find(id);
    return q && q->state() == QuestState::Active;
}
bool QuestBook::isFinished(const std::string& id) const
{
    const Quest* q = find(id);
    return q && (q->state() == QuestState::Complete ||
                 q->state() == QuestState::TurnedIn);
}

bool QuestBook::assignUnchecked(const QuestLibrary& library,
                                const std::string& id)
{
    if (!mChannels) {
        eng::log::error("QuestBook: assign('%s') before bind()", id.c_str());
        return false;
    }
    const QuestDef* def = library.find(id);
    if (!def) {
        eng::log::error("QuestBook: cannot assign '%s'; no such quest",
                        id.c_str());
        return false;
    }
    if (Quest* existing = find(id)) {
        // Repeatable quests restart; that is what repeatable means, and the
        // alternative is content that can only ever fire once.
        if (!def->repeatable)
            return false;
        existing->setProgress(0);
        existing->restoreState(QuestState::Pending);
        mChannels->quests.assigned.raise(*existing);
        existing->enable(*mChannels);
        return true;
    }
    std::unique_ptr<Quest> quest = makeQuest(*def, mChannels->quests);
    if (!quest)
        return false;
    Quest& ref = *quest;
    mQuests.push_back(std::move(quest));
    // Announce before enabling: a listener that wants to log the assignment
    // should see it even for a quest that completes the instant it starts.
    mChannels->quests.assigned.raise(ref);
    ref.enable(*mChannels);
    return true;
}

bool QuestBook::assign(const QuestLibrary& library, const std::string& id,
                       const ConditionFn& evaluate)
{
    const QuestDef* def = library.find(id);
    if (!def)
        return false;
    if (evaluate)
        for (const Condition& c : def->requirements)
            if (!evaluate(c))
                return false;
    return assignUnchecked(library, id);
}

Quest* QuestBook::turnIn(const std::string& id)
{
    Quest* q = find(id);
    if (!q || q->state() != QuestState::Complete)
        return nullptr;
    q->markTurnedIn();
    return q;
}

bool QuestBook::fail(const std::string& id)
{
    Quest* q = find(id);
    if (!q || q->state() == QuestState::TurnedIn)
        return false;
    q->fail();
    return true;
}

bool QuestBook::forceComplete(const QuestLibrary& library, const std::string& id)
{
    Quest* q = find(id);
    if (!q) {
        if (!assignUnchecked(library, id))
            return false;
        q = find(id);
    }
    if (!q || q->state() == QuestState::TurnedIn)
        return false;
    if (q->state() == QuestState::Complete)
        return true;
    q->setProgress(q->target());
    q->forceComplete();
    return true;
}

void QuestBook::setConversationPartner(const std::string& npc)
{
    for (const std::unique_ptr<Quest>& q : mQuests) {
        auto* deliver = dynamic_cast<DeliverItemQuest*>(q.get());
        if (!deliver)
            continue;
        // `extra` names the receiver; empty means the quest's giver.
        const std::string& receiver =
            deliver->def().extra.empty() ? deliver->def().giver
                                         : deliver->def().extra;
        deliver->setNearGiver(!receiver.empty() && receiver == npc);
    }
}

std::vector<std::string> QuestBook::rebind(const QuestLibrary& library)
{
    std::vector<std::string> dropped;
    if (!mChannels)
        return dropped;
    std::vector<std::unique_ptr<Quest>> rebuilt;
    for (std::unique_ptr<Quest>& old : mQuests) {
        if (!old)
            continue;
        const QuestDef* def = library.find(old->id());
        if (!def) {
            dropped.push_back(old->id());
            continue;
        }
        std::unique_ptr<Quest> fresh = makeQuest(*def, mChannels->quests);
        if (!fresh) {
            dropped.push_back(old->id());
            continue;
        }
        const QuestState state = old->state();
        fresh->setProgress(old->progress());
        old->disable();
        if (state == QuestState::Active)
            fresh->enable(*mChannels);
        else
            fresh->restoreState(state);
        rebuilt.push_back(std::move(fresh));
    }
    mQuests = std::move(rebuilt);
    return dropped;
}

void QuestBook::clear()
{
    for (const std::unique_ptr<Quest>& q : mQuests)
        if (q)
            q->disable();
    mQuests.clear();
}

} // namespace game::rpg
