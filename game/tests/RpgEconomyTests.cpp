// The economy, the raid state machine, and the save codec.
//
// The three things asserted here are the three that would break the extraction
// loop silently: the demand curve must be bounded (an unbounded one is an
// infinite money loop), the raid machine must refuse to persist mid-raid, and
// the save must round-trip exactly or refuse to load at all.
#include "../src/rpg/RaidState.h"
#include "../src/rpg/RpgSave.h"
#include "../src/rpg/Trading.h"

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
[item.ore]
name = "Ore"
category = "reagent"
stack = 20
weight = 1.0
value = 100
elasticity = 0.4

[item.blade]
name = "Blade"
category = "weapon"
weight = 3.0
value = 200
durability = 100.0
worn_value_floor = 0.25

[item.relic]
name = "Relic"
category = "relic"
value = 1000
quest_item = true
)";

static const char* kTraders = R"(
[trader.smith]
name = "The Smith"
categories = ["reagent", "weapon"]
disinterest = 0.2
buy_margin = 0.5
sell_margin = 1.5
purse = 1000
purse_restock = 1000
[[trader.smith.rank]]
name = "Stranger"
at = 0
price = 1.0
sell = 1.0
[[trader.smith.rank]]
name = "Regular"
at = 20
price = 0.9
sell = 1.2
[[trader.smith.stock]]
item = "blade"
quantity = 2
restock_days = 2
)";

int main()
{
    // --- the demand curve is bounded ---------------------------------------
    {
        // Neutral at zero flow, whatever the elasticity.
        check(nearly(market::demandFactor(0, 0.4f), 1.0f), "no flow, no effect");
        // Zero elasticity pins the price -- right for currency and for anything
        // with an official price.
        check(nearly(market::demandFactor(500, 0.0f), 1.0f),
              "zero elasticity ignores flow entirely");

        // Selling pushes the price down, buying pushes it up.
        check(market::demandFactor(10, 0.4f) < 1.0f, "selling depresses");
        check(market::demandFactor(-10, 0.4f) > 1.0f, "buying inflates");

        // And neither can run away. This is the assertion that matters: an
        // unbounded curve is how a trade economy becomes an infinite money
        // loop, which AGENTS.md §15 names explicitly.
        for (int flow = -100000; flow <= 100000; flow += 997) {
            const float f = market::demandFactor(flow, 0.4f);
            check(f >= 0.6f - 1e-4f && f <= 1.4f + 1e-4f,
                  "the demand factor stays inside its elasticity band");
        }
        check(market::demandFactor(1000000, 0.4f) > 0.0f,
              "no amount of dumping makes an item worthless");

        // Monotone: more selling is never a better price.
        float previous = 2.0f;
        for (int flow = 0; flow < 200; flow += 7) {
            const float f = market::demandFactor(flow, 0.4f);
            check(f <= previous + 1e-5f, "the curve is monotone in flow");
            previous = f;
        }
    }

    // --- condition -----------------------------------------------------------
    {
        check(nearly(market::conditionFactor(1.0f, 0.25f), 1.0f),
              "pristine is full price");
        check(nearly(market::conditionFactor(0.0f, 0.25f), 0.25f),
              "worn out is the floor, not zero");
        check(nearly(market::conditionFactor(0.5f, 0.0f), 0.5f),
              "a zero floor is linear");
        // Out-of-range condition is clamped rather than extrapolated.
        check(nearly(market::conditionFactor(2.0f, 0.25f), 1.0f),
              "over-pristine is still full price");
        check(nearly(market::conditionFactor(-1.0f, 0.25f), 0.25f),
              "negative condition is the floor");
    }

    // --- quoting ------------------------------------------------------------
    ItemLibrary items;
    check(items.loadFromString(kItems), "items load");
    TraderLibrary traders;
    check(traders.loadFromString(kTraders), "traders load");
    const TraderDef* smith = traders.find("smith");
    check(smith != nullptr, "the smith exists");

    Market market;
    market.sync(traders);
    check(market.state("smith") != nullptr, "sync created the state");
    check(market.state("smith")->purse == 1000, "with the authored purse");
    check(market.state("smith")->stock["blade"] == 2, "and the authored stock");

    {
        // A trader buys below and sells above; that spread is what pays them.
        const PriceQuote sell = market.sellQuote(*smith, items, "ore", 1.0f, 0);
        const PriceQuote buy = market.buyQuote(*smith, items, "ore", 0);
        check(sell.tradeable && buy.tradeable, "ore is tradeable");
        check(sell.unitPrice < buy.unitPrice, "the spread runs the right way");
        check(sell.unitPrice == 50, "100 * 0.5 buy margin");
        check(buy.unitPrice == 150, "100 * 1.5 sell margin");

        // Standing improves both sides.
        const PriceQuote regularSell =
            market.sellQuote(*smith, items, "ore", 1.0f, 25);
        const PriceQuote regularBuy = market.buyQuote(*smith, items, "ore", 25);
        check(regularSell.unitPrice > sell.unitPrice, "a regular is paid more");
        check(regularBuy.unitPrice < buy.unitPrice, "and charged less");

        // Wear is a real trading decision, not a cosmetic bar.
        const PriceQuote worn = market.sellQuote(*smith, items, "blade", 0.0f, 0);
        const PriceQuote fresh = market.sellQuote(*smith, items, "blade", 1.0f, 0);
        check(worn.unitPrice < fresh.unitPrice, "a worn blade is worth less");
        check(worn.unitPrice > 0, "but not nothing");

        // A quest item is refused, with a reason the player can be shown.
        const PriceQuote refused =
            market.sellQuote(*smith, items, "relic", 1.0f, 0);
        check(!refused.tradeable, "a quest item cannot be sold");
        check(!refused.refusal.empty(), "and says why");

        // An unknown item is refused rather than quoted at zero.
        check(!market.sellQuote(*smith, items, "nonesuch", 1.0f, 0).tradeable,
              "an unknown item is refused");
    }

    // --- flow moves the price, and the restock clock brings it back ---------
    {
        const int before = market.sellQuote(*smith, items, "ore", 1.0f, 0).unitPrice;
        market.recordSale(*smith, "ore", 40, 2000);
        const int flooded = market.sellQuote(*smith, items, "ore", 1.0f, 0).unitPrice;
        check(flooded < before, "flooding the market depresses the price");
        check(market.state("smith")->stock["ore"] == 40,
              "and what was sold is on their shelf");

        // Days pass; the market recovers rather than staying dead forever.
        market.advanceDays(traders, 8);
        const int recovered =
            market.sellQuote(*smith, items, "ore", 1.0f, 0).unitPrice;
        check(recovered > flooded, "the market recovers over days");
        check(recovered <= before, "but not instantly");
    }

    // Restock replenishes toward the target, not to it, so a cleaned-out
    // trader takes several days -- something the player plans around.
    {
        Market fresh;
        fresh.sync(traders);
        fresh.recordPurchase(*smith, "blade", 2, 300);
        check(fresh.state("smith")->stock["blade"] == 0, "cleaned out");
        fresh.advanceDays(traders, 2);
        const int after = fresh.state("smith")->stock["blade"];
        check(after > 0 && after <= 2, "partially restocked");
    }

    // A barter is one-time when it says so.
    {
        check(!market.barterDone("smith", "deal"), "not done yet");
        market.noteBarter(*smith, "deal");
        check(market.barterDone("smith", "deal"), "and now it is");
    }

    // =====================================================================
    // The raid state machine
    // =====================================================================
    {
        RaidState raid;
        check(raid.phase() == RaidPhase::Safehouse, "starts at home");
        check(raid.mayPersist(), "and may write");
        check(!raid.inRaid(), "and is not in a raid");

        int transitions = 0;
        raid.changed.subscribe([&](RaidPhase, RaidPhase) { ++transitions; });

        raid.beginRaid(2);
        check(raid.phase() == RaidPhase::Initialising, "initialising");
        check(!raid.mayPersist(), "a raid being set up may not write");
        raid.enterActive();
        check(raid.inRaid(), "in the raid");
        // THE rule the whole loop rests on.
        check(!raid.mayPersist(), "a live raid may never write the profile");

        raid.setExtractionDuration(2.0f);
        raid.beginExtraction();
        check(raid.phase() == RaidPhase::Extracting, "the countdown started");
        check(!raid.mayPersist(), "and still may not write");
        check(!raid.tick(1.0f), "one second in, not out yet");
        check(raid.extractionRemaining() > 0.0f, "time left");

        // Stepping out resets the countdown rather than pausing it, or the
        // extraction is not a commitment.
        raid.cancelExtraction();
        check(raid.phase() == RaidPhase::Active, "back in the raid");
        check(nearly(raid.extractionRemaining(), 0.0f), "the countdown reset");

        raid.beginExtraction();
        check(!raid.tick(1.0f), "one second");
        check(raid.tick(1.5f), "and out on the second tick");
        check(raid.phase() == RaidPhase::Extracted, "extracted");
        check(raid.mayPersist(), "and NOW it may write");
        check(!raid.tick(1.0f), "ticking an extracted raid does nothing");
        check(transitions >= 5, "every step was announced");
    }

    // Dying mid-extraction is legal, and is the best story this system tells.
    {
        RaidState raid;
        raid.beginRaid(1);
        raid.enterActive();
        raid.beginExtraction();
        raid.die("deep_warden");
        check(raid.phase() == RaidPhase::Died, "killed on the threshold");
        check(raid.lastKiller() == "deep_warden", "and it knows by what");
        check(raid.mayPersist(), "a death is a state boundary; it must be saved");
    }

    // An illegal transition is refused rather than teleporting the machine.
    {
        RaidState raid;
        raid.enterActive(); // from Safehouse: not legal
        check(raid.phase() == RaidPhase::Safehouse, "the illegal move was refused");
        raid.beginExtraction(); // not in a raid
        check(raid.phase() == RaidPhase::Safehouse, "and so was this one");
    }

    // =====================================================================
    // The save codec
    // =====================================================================
    {
        RpgSaveData data;
        data.skills = {{"blades", 13034431}, {"lockpicking", 83}};
        data.characterXp = 250000;
        data.backpack = {{"ore", 12, true, 1.0f}, {"blade", 1, false, 0.42f}};
        data.stash = {{"relic", 3, false, 1.0f}};
        data.equipped = {"", "", "blade", "", "", "", "", ""};
        data.currency = 4321;
        data.quests = {{"gather", 2, {3}}, {"slay", 1, {1}}};
        data.flags = {"lodge.known", "met.smith"};
        data.counters = {{"killed_by.rat", 3}};
        data.standings = {{"smith", 25}};
        data.day = 17;
        data.deepestDepth = 6;
        TraderSnapshot ts;
        ts.id = "smith";
        ts.purse = 640;
        ts.daysSinceRestock = 2;
        ts.flow = {{"ore", 40}};
        ts.stock = {{"blade", 1}};
        ts.completedBarters = {"deal"};
        data.traders = {ts};
        data.stations = {{"strongbox", 2}, {"hearth", 1}};

        const std::vector<uint8_t> bytes = rpgsave::encode(data);
        check(!bytes.empty(), "it encodes to something");

        const std::optional<RpgSaveData> back = rpgsave::decode(bytes);
        check(back.has_value(), "and decodes again");
        if (back) {
            check(back->skills == data.skills, "skills round-trip");
            check(back->characterXp == data.characterXp, "character xp");
            check(back->backpack.size() == 2, "the pack came back");
            check(back->backpack[0].item == "ore", "with its items");
            check(back->backpack[0].foundThisRun,
                  "and its provenance, which is what a death reads");
            check(nearly(back->backpack[1].condition, 0.42f),
                  "and its wear, which is what a trader reads");
            check(back->stash.size() == 1, "the stash came back");
            check(back->equipped.size() == 8, "every slot, positionally");
            check(back->equipped[2] == "blade", "including the occupied one");
            check(back->currency == 4321, "coin");
            check(back->quests.size() == 2, "both quests");
            check(back->quests[0].counts == data.quests[0].counts, "with progress");
            check(back->flags == data.flags, "flags");
            check(back->counters == data.counters, "counters");
            check(back->standings == data.standings, "standing");
            check(back->day == 17 && back->deepestDepth == 6, "time and depth");
            check(back->traders.size() == 1, "the trader came back");
            check(back->traders[0].flow == ts.flow,
                  "with the trade history the price curve reads");
            check(back->traders[0].completedBarters == ts.completedBarters,
                  "and its one-time barters");
            check(back->stations == data.stations, "the safehouse");
        }

        // Encoding is deterministic: two encodes of one state are one blob,
        // which is what makes a save diffable and this test meaningful.
        check(rpgsave::encode(data) == bytes, "encoding is deterministic");

        // --- and it refuses what it should ---------------------------------
        check(!rpgsave::decode(nullptr, 0).has_value(), "null is refused");
        check(!rpgsave::decode(bytes.data(), 4).has_value(),
              "a runt blob is refused");

        std::vector<uint8_t> wrongMagic = bytes;
        wrongMagic[0] = 'X';
        check(!rpgsave::decode(wrongMagic).has_value(),
              "a foreign blob is refused");

        // A save that loads *wrong* is worse than one that refuses to load, so
        // an unrecognised version is rejected rather than read through.
        std::vector<uint8_t> wrongVersion = bytes;
        wrongVersion[8] = uint8_t(rpgsave::kVersion + 7);
        check(!rpgsave::decode(wrongVersion).has_value(),
              "a future version is refused, not guessed at");

        // Truncation at every length must be refused, never half-read.
        for (std::size_t cut = 16; cut < bytes.size(); cut += 3)
            check(!rpgsave::decode(bytes.data(), cut).has_value() ||
                      cut == bytes.size(),
                  "a truncated blob is refused");
    }

    // An empty profile round-trips too -- a brand new character is a real save.
    {
        const RpgSaveData empty;
        const std::optional<RpgSaveData> back =
            rpgsave::decode(rpgsave::encode(empty));
        check(back.has_value(), "an empty save round-trips");
        check(back && back->skills.empty() && back->characterXp == 0,
              "and comes back empty");
    }

    if (failures == 0)
        std::printf("RpgEconomyTests OK\n");
    return failures == 0 ? 0 : 1;
}
