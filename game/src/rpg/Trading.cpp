#include "Trading.h"

#include <eng/Log.h>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <algorithm>
#include <cmath>

namespace game::rpg {

namespace market {

float demandFactor(int flow, float elasticity)
{
    if (elasticity <= 0.0f || flow == 0)
        return 1.0f;
    // A saturating curve: x / (|x| + k) runs from -1 to 1 without ever
    // reaching either, so the factor stays inside (1 - e, 1 + e). k is the
    // number of units at which half the effect has been felt -- eight, which
    // is roughly one expedition's worth of a common reagent.
    constexpr float kHalfLife = 8.0f;
    const float x = float(flow);
    const float saturated = x / (std::fabs(x) + kHalfLife);
    return std::clamp(1.0f - saturated * elasticity, 1.0f - elasticity,
                      1.0f + elasticity);
}

float conditionFactor(float condition, float wornValueFloor)
{
    const float floor = std::clamp(wornValueFloor, 0.0f, 1.0f);
    return floor + (1.0f - floor) * std::clamp(condition, 0.0f, 1.0f);
}

} // namespace market

// ---------------------------------------------------------------------------
// TraderLibrary
// ---------------------------------------------------------------------------

bool TraderLibrary::parse(const void* tomlTable)
{
    const toml::table& root = *static_cast<const toml::table*>(tomlTable);
    const toml::table* group = root["trader"].as_table();
    if (!group) {
        eng::log::error("TraderLibrary: document defines no [trader.*] rows");
        return false;
    }

    mTraders.clear();
    for (auto&& [key, node] : *group) {
        const toml::table* t = node.as_table();
        if (!t)
            continue;
        TraderDef trader;
        trader.id = std::string(key.str());
        trader.name = (*t)["name"].value_or(trader.id);
        trader.role = (*t)["role"].value_or(std::string());
        trader.disinterestFactor =
            float((*t)["disinterest"].value_or(double(trader.disinterestFactor)));
        trader.buyMargin = float((*t)["buy_margin"].value_or(double(trader.buyMargin)));
        trader.sellMargin =
            float((*t)["sell_margin"].value_or(double(trader.sellMargin)));
        trader.purse = (*t)["purse"].value_or(trader.purse);
        trader.purseRestock = (*t)["purse_restock"].value_or(trader.purse);

        if (const toml::array* cats = (*t)["categories"].as_array()) {
            for (const toml::node& c : *cats) {
                const std::string name = c.value_or(std::string());
                ItemCategory category{};
                if (parseItemCategory(name, category))
                    trader.categories.push_back(category);
                else
                    eng::log::error("traders: '%s' is interested in category "
                                    "'%s', which does not exist",
                                    trader.id.c_str(), name.c_str());
            }
        }
        if (const toml::array* tags = (*t)["tags"].as_array())
            for (const toml::node& tag : *tags)
                if (const std::optional<std::string> s = tag.value<std::string>())
                    trader.tags.push_back(*s);

        if (const toml::array* ranks = (*t)["rank"].as_array()) {
            for (const toml::node& rn : *ranks) {
                const toml::table* rt = rn.as_table();
                if (!rt)
                    continue;
                StandingRank rank;
                rank.name = (*rt)["name"].value_or(rank.name);
                rank.atStanding = (*rt)["at"].value_or(rank.atStanding);
                rank.priceModifier =
                    float((*rt)["price"].value_or(double(rank.priceModifier)));
                rank.sellModifier =
                    float((*rt)["sell"].value_or(double(rank.sellModifier)));
                trader.ranks.push_back(std::move(rank));
            }
        }
        // Sorted ascending so rank() can walk it once and keep the last match.
        std::sort(trader.ranks.begin(), trader.ranks.end(),
                  [](const StandingRank& a, const StandingRank& b) {
                      return a.atStanding < b.atStanding;
                  });

        if (const toml::array* stock = (*t)["stock"].as_array()) {
            for (const toml::node& sn : *stock) {
                const toml::table* st = sn.as_table();
                if (!st)
                    continue;
                StockEntry entry;
                entry.item = (*st)["item"].value_or(std::string());
                if (entry.item.empty())
                    continue;
                entry.restockTo = std::max(0, (*st)["quantity"].value_or(1));
                entry.quantity = entry.restockTo;
                entry.restockDays = std::max(1, (*st)["restock_days"].value_or(1));
                entry.minStanding = (*st)["min_standing"].value_or(0);
                trader.stock.push_back(std::move(entry));
            }
        }

        if (const toml::array* barters = (*t)["barter"].as_array()) {
            for (const toml::node& bn : *barters) {
                const toml::table* bt = bn.as_table();
                if (!bt)
                    continue;
                BarterRecipe barter;
                barter.id = (*bt)["id"].value_or(std::string());
                barter.gives = (*bt)["gives"].value_or(std::string());
                barter.givesCount = std::max(1, (*bt)["gives_count"].value_or(1));
                barter.minStanding = (*bt)["min_standing"].value_or(0);
                barter.unique = (*bt)["unique"].value_or(false);
                if (const toml::array* wants = (*bt)["want"].as_array()) {
                    for (const toml::node& wn : *wants) {
                        const toml::table* wt = wn.as_table();
                        if (!wt)
                            continue;
                        const std::string id = (*wt)["item"].value_or(std::string());
                        if (!id.empty())
                            barter.wants.emplace_back(
                                id, std::max(1, (*wt)["count"].value_or(1)));
                    }
                }
                if (barter.id.empty() || barter.gives.empty() ||
                    barter.wants.empty()) {
                    eng::log::error("traders: '%s' has a barter with no id, no "
                                    "reward or nothing wanted; dropped",
                                    trader.id.c_str());
                    continue;
                }
                trader.barters.push_back(std::move(barter));
            }
        }

        mTraders[trader.id] = std::move(trader);
    }

    eng::log::info("TraderLibrary: %d traders", int(mTraders.size()));
    return !mTraders.empty();
}

bool TraderLibrary::load(const std::string& tomlPath)
{
    toml::parse_result parsed = toml::parse_file(tomlPath);
    if (!parsed) {
        eng::log::error("TraderLibrary: %s: %s", tomlPath.c_str(),
                        std::string(parsed.error().description()).c_str());
        mTraders.clear();
        return false;
    }
    return parse(&parsed.table());
}

bool TraderLibrary::loadFromString(const char* tomlSrc)
{
    toml::parse_result parsed = toml::parse(tomlSrc);
    if (!parsed) {
        eng::log::error("TraderLibrary: %s",
                        std::string(parsed.error().description()).c_str());
        mTraders.clear();
        return false;
    }
    return parse(&parsed.table());
}

const TraderDef* TraderLibrary::find(const std::string& id) const
{
    const auto it = mTraders.find(id);
    return it == mTraders.end() ? nullptr : &it->second;
}

std::vector<std::string> TraderLibrary::ids() const
{
    std::vector<std::string> out;
    out.reserve(mTraders.size());
    for (const auto& [id, t] : mTraders)
        out.push_back(id);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> TraderLibrary::referencedItems() const
{
    std::vector<std::string> out;
    for (const auto& [id, t] : mTraders) {
        for (const StockEntry& s : t.stock)
            out.push_back(s.item);
        for (const BarterRecipe& b : t.barters) {
            out.push_back(b.gives);
            for (const auto& [item, count] : b.wants)
                out.push_back(item);
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

// ---------------------------------------------------------------------------
// Market
// ---------------------------------------------------------------------------

void Market::sync(const TraderLibrary& library)
{
    for (const std::string& id : library.ids()) {
        const TraderDef* def = library.find(id);
        if (!def)
            continue;
        TraderState& state = mStates[id];
        if (state.id.empty()) {
            // New to this profile: open the shelves at their authored level.
            state.id = id;
            state.purse = def->purse;
            for (const StockEntry& s : def->stock)
                state.stock[s.item] = s.quantity;
        } else {
            // Already known: a stock line the content just added starts full,
            // everything else keeps whatever the player left it at.
            for (const StockEntry& s : def->stock)
                state.stock.try_emplace(s.item, s.quantity);
        }
    }
}

TraderState* Market::state(const std::string& trader)
{
    const auto it = mStates.find(trader);
    return it == mStates.end() ? nullptr : &it->second;
}

const TraderState* Market::state(const std::string& trader) const
{
    const auto it = mStates.find(trader);
    return it == mStates.end() ? nullptr : &it->second;
}

const StandingRank* Market::rank(const TraderDef& trader, int standing) const
{
    const StandingRank* best = nullptr;
    for (const StandingRank& r : trader.ranks) {
        if (standing < r.atStanding)
            break; // sorted ascending
        best = &r;
    }
    return best;
}

namespace {

// How much this trader cares. A category or tag match is full interest;
// anything else is the "I have no use for that" factor.
float interestIn(const TraderDef& trader, const ItemDef& item)
{
    for (ItemCategory c : trader.categories)
        if (c == item.category)
            return 1.0f;
    for (const std::string& tag : trader.tags)
        if (item.hasTag(tag))
            return 1.0f;
    return std::clamp(trader.disinterestFactor, 0.0f, 1.0f);
}

int flowFor(const TraderState* state, const std::string& item)
{
    if (!state)
        return 0;
    const auto it = state->flow.find(item);
    return it == state->flow.end() ? 0 : it->second;
}

} // namespace

PriceQuote Market::sellQuote(const TraderDef& trader, const ItemLibrary& items,
                             const std::string& item, float condition,
                             int standing) const
{
    PriceQuote q;
    const ItemLibrary::Ref def = items.find(item);
    if (!def) {
        q.refusal = "That is not a thing.";
        return q;
    }
    if (!def->tradeable || def->questItem) {
        q.refusal = def->questItem ? "That is not mine to sell."
                                   : "Nobody deals in that.";
        return q;
    }
    q.baseValue = float(def->value);
    q.conditionFactor = market::conditionFactor(condition, def->wornValueFloor);
    q.demandFactor = market::demandFactor(flowFor(state(trader.id), item),
                                          def->demandElasticity);
    q.interestFactor = interestIn(trader, *def);
    const StandingRank* r = rank(trader, standing);
    q.standingFactor = r ? r->sellModifier : 1.0f;

    const float price = q.baseValue * q.conditionFactor * q.demandFactor *
                        q.interestFactor * q.standingFactor * trader.buyMargin;
    // Floored at 1 for anything with any value at all: an item that is worth
    // "0 coin" reads as a bug, and the player would rather be told it is
    // worthless by the number 1 than by an empty cell.
    q.unitPrice = def->value > 0 ? std::max(1, int(price + 0.5f)) : 0;
    q.tradeable = true;
    return q;
}

PriceQuote Market::buyQuote(const TraderDef& trader, const ItemLibrary& items,
                            const std::string& item, int standing) const
{
    PriceQuote q;
    const ItemLibrary::Ref def = items.find(item);
    if (!def) {
        q.refusal = "That is not a thing.";
        return q;
    }
    q.baseValue = float(def->value);
    q.conditionFactor = 1.0f; // stock is sold pristine
    q.demandFactor = market::demandFactor(flowFor(state(trader.id), item),
                                          def->demandElasticity);
    q.interestFactor = 1.0f; // what they stock, they stock
    const StandingRank* r = rank(trader, standing);
    q.standingFactor = r ? r->priceModifier : 1.0f;

    const float price = q.baseValue * q.demandFactor * q.standingFactor *
                        trader.sellMargin;
    q.unitPrice = std::max(1, int(price + 0.5f));
    q.tradeable = true;
    return q;
}

void Market::recordSale(const TraderDef& trader, const std::string& item,
                        int units, int coin)
{
    TraderState& s = mStates[trader.id];
    if (s.id.empty())
        s.id = trader.id;
    s.flow[item] += units;   // player sold: supply up, price down
    s.stock[item] += units;  // it is on their shelf now
    s.purse = std::max(0, s.purse - coin);
}

void Market::recordPurchase(const TraderDef& trader, const std::string& item,
                            int units, int coin)
{
    TraderState& s = mStates[trader.id];
    if (s.id.empty())
        s.id = trader.id;
    s.flow[item] -= units;
    s.stock[item] = std::max(0, s.stock[item] - units);
    s.purse += coin;
}

void Market::noteBarter(const TraderDef& trader, const std::string& barterId)
{
    TraderState& s = mStates[trader.id];
    if (s.id.empty())
        s.id = trader.id;
    if (!barterDone(trader.id, barterId))
        s.completedBarters.push_back(barterId);
}

bool Market::barterDone(const std::string& trader,
                        const std::string& barterId) const
{
    const TraderState* s = state(trader);
    if (!s)
        return false;
    return std::find(s->completedBarters.begin(), s->completedBarters.end(),
                     barterId) != s->completedBarters.end();
}

void Market::advanceDays(const TraderLibrary& library, int days)
{
    if (days <= 0)
        return;
    for (const std::string& id : library.ids()) {
        const TraderDef* def = library.find(id);
        if (!def)
            continue;
        TraderState& s = mStates[id];
        if (s.id.empty())
            s.id = id;
        s.daysSinceRestock += days;

        for (const StockEntry& entry : def->stock) {
            if (s.daysSinceRestock < entry.restockDays)
                continue;
            int& held = s.stock[entry.item];
            // Toward the target, not to it: a trader who was cleaned out takes
            // several days to recover, which is what makes the restock clock
            // something the player plans around.
            if (held < entry.restockTo)
                held = std::min(entry.restockTo,
                                held + std::max(1, entry.restockTo / 2));
        }

        if (s.daysSinceRestock >= 1) {
            s.purse = std::min(def->purseRestock, s.purse + def->purseRestock / 2);
            // Flow decays toward neutral, so a market the player flooded
            // recovers instead of staying dead for the rest of the run.
            for (auto& [item, flow] : s.flow) {
                const int step = std::max(1, std::abs(flow) / 4);
                flow = flow > 0 ? std::max(0, flow - step)
                                : std::min(0, flow + step);
            }
            s.daysSinceRestock = 0;
        }
    }
}

} // namespace game::rpg
