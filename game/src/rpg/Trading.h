#pragma once
#include "Items.h"
#include "RpgTypes.h"

#include <string>
#include <unordered_map>
#include <vector>

// The economy: who buys what, at what price, and what they will take instead of
// coin.
//
// THE PRICING MODEL
// A trader's price for an item is
//
//     price = base * condition * demand * standing * margin
//
// where
//   base       is ItemDef::value
//   condition  interpolates to ItemDef::wornValueFloor as durability drops
//   demand     is the supply/demand curve: every unit the player has sold this
//              trader pushes the price down, every unit bought pushes it up,
//              scaled by ItemDef::demandElasticity and decaying back toward 1
//              as the restock clock turns
//   standing   is the faction/reputation rank discount
//   margin     is the trader's own spread: they buy below and sell above
//
// AGENTS.md §15 asks for an economy that creates *conflicting* values and warns
// against "vendors who purchase every object at a universal price". Both fall
// out of this: each trader has an interest list, so the physician pays well for
// medicine and shrugs at ore, and dumping forty of one thing on one trader
// visibly stops paying.
//
// This module is pure. It owns no world, no time and no inventory: the
// restock clock is advanced by whoever owns days, and the transaction is
// executed by RpgRuntime.
namespace game::rpg {

// One thing a trader keeps on the shelf.
struct StockEntry {
    std::string item;
    int quantity = 0;      // current
    int restockTo = 0;     // replenished toward this each restock tick
    int restockDays = 1;   // how often
    int minStanding = 0;   // hidden below this reputation
};

// A barter: give these, get that. §15 wants selling to sometimes be more
// consequential than obtaining, and a barter is where that bites -- the price
// is another object, not a number.
struct BarterRecipe {
    std::string id;
    std::string gives;          // item id the trader hands over
    int givesCount = 1;
    std::vector<std::pair<std::string, int>> wants; // item id, count
    int minStanding = 0;
    // Once only, for the barters that are a story beat rather than a shop line.
    bool unique = false;
};

// A reputation rank. Ranks are authored per trader because §16 insists the
// major NPCs must not be interchangeable reputation bars -- the *numbers* being
// per-character is the cheapest way to keep them from collapsing into one.
struct StandingRank {
    std::string name = "Stranger";
    int atStanding = 0;
    float priceModifier = 1.0f; // multiplies what the player pays
    float sellModifier = 1.0f;  // multiplies what the player receives
};

struct TraderDef {
    std::string id;
    std::string name;
    std::string role;
    // What this trader is interested in. An item whose category or tags match
    // nothing here is bought at `disinterestFactor` -- the "I have no use for
    // that" price -- or refused outright when that is zero.
    std::vector<ItemCategory> categories;
    std::vector<std::string> tags;
    float disinterestFactor = 0.25f;
    // The spread. buyMargin < 1 is what the trader pays you; sellMargin > 1 is
    // what they charge.
    float buyMargin = 0.55f;
    float sellMargin = 1.35f;
    int purse = 500; // coin on hand; refills with the restock clock
    int purseRestock = 500;
    std::vector<StandingRank> ranks;
    std::vector<StockEntry> stock;
    std::vector<BarterRecipe> barters;
};

class TraderLibrary {
public:
    bool load(const std::string& tomlPath);
    bool loadFromString(const char* tomlSrc);

    const TraderDef* find(const std::string& id) const;
    std::vector<std::string> ids() const;
    int size() const { return int(mTraders.size()); }
    std::vector<std::string> referencedItems() const;

private:
    bool parse(const void* tomlTable);
    std::unordered_map<std::string, TraderDef> mTraders;
};

// The mutable half: what has actually been traded, and what is on the shelf
// right now. Separate from the definition for the same reason every other
// library in this tree is: content hot-reloads, and the player's trade history
// must survive it.
struct TraderState {
    std::string id;
    int purse = 0;
    // Net units the player has moved through this trader, per item. Positive =
    // the player has been selling (price falls); negative = buying (price
    // rises). Decays toward zero on each restock tick.
    std::unordered_map<std::string, int> flow;
    std::unordered_map<std::string, int> stock;
    std::vector<std::string> completedBarters;
    int daysSinceRestock = 0;
};

// What a price lookup answered, with the reasons -- so a shop UI can show the
// player *why* the number is what it is instead of an unexplained figure.
struct PriceQuote {
    bool tradeable = false;
    int unitPrice = 0;
    float baseValue = 0.0f;
    float conditionFactor = 1.0f;
    float demandFactor = 1.0f;
    float standingFactor = 1.0f;
    float interestFactor = 1.0f;
    std::string refusal; // set when tradeable is false
};

class Market {
public:
    // Bind the state to the content. Keeps existing state for traders that are
    // still defined, and initialises stock for ones that are new.
    void sync(const TraderLibrary&);

    TraderState* state(const std::string& trader);
    const TraderState* state(const std::string& trader) const;

    // The rank the player currently holds with this trader.
    const StandingRank* rank(const TraderDef&, int standing) const;

    // What the trader will pay the player for one unit.
    PriceQuote sellQuote(const TraderDef&, const ItemLibrary&,
                         const std::string& item, float condition,
                         int standing) const;
    // What the trader charges the player for one unit.
    PriceQuote buyQuote(const TraderDef&, const ItemLibrary&,
                        const std::string& item, int standing) const;

    // Record a completed transaction: moves the flow, the stock and the purse.
    // `units` is positive when the player sold to the trader.
    void recordSale(const TraderDef&, const std::string& item, int units,
                    int coin);
    void recordPurchase(const TraderDef&, const std::string& item, int units,
                        int coin);
    void noteBarter(const TraderDef&, const std::string& barterId);
    bool barterDone(const std::string& trader, const std::string& barterId) const;

    // Advance every trader's restock clock by `days`. Replenishes stock toward
    // its target, refills the purse, and decays flow -- which is what makes a
    // flooded market recover instead of staying dead forever.
    void advanceDays(const TraderLibrary&, int days);

    void clear() { mStates.clear(); }
    const std::unordered_map<std::string, TraderState>& states() const
    {
        return mStates;
    }
    std::unordered_map<std::string, TraderState>& states() { return mStates; }

private:
    std::unordered_map<std::string, TraderState> mStates;
};

namespace market {

// The supply/demand curve, exposed because it is the one number in this module
// worth testing directly.
//
// flow > 0 (the player has been selling) drives the factor below 1; flow < 0
// drives it above. Asymptotic in both directions -- it approaches
// (1 - elasticity) and (1 + elasticity) rather than crossing them -- so no
// amount of dumping can make an item worthless and no amount of buying can make
// it unaffordable. An unbounded curve is how a trade economy becomes an
// infinite money loop, which §15 names explicitly.
float demandFactor(int flow, float elasticity);

// Wear -> price multiplier, from 1 at pristine down to the item's floor.
float conditionFactor(float condition, float wornValueFloor);

} // namespace market

} // namespace game::rpg
