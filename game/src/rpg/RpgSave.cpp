#include "RpgSave.h"

#include <eng/Log.h>
#include <eng/io/ByteStream.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <unordered_map>

namespace game::rpg::rpgsave {

using eng::io::ByteReader;
using eng::io::ByteWriter;

namespace {

constexpr char kMagic[8] = {'R', 'A', 'V', 'N', 'R', 'P', 'G', 0};

// Bounds every count the reader will act on. A corrupt or hostile blob must
// cost a rejected load, not a multi-gigabyte allocation from a u32 it claimed
// was an element count.
constexpr uint32_t kMaxStacks = 1u << 14;
constexpr uint32_t kMaxQuests = 1u << 12;
constexpr uint32_t kMaxObjectives = 64;
constexpr uint32_t kMaxNamed = 1u << 14;
constexpr uint32_t kMaxSlots = 64;
constexpr uint32_t kMaxStrings = 1u << 16;
constexpr uint32_t kMaxStringBytes = 1u << 22;

void writeStacks(ByteWriter& w, const std::vector<ItemStackSnapshot>& stacks)
{
    w.u32(uint32_t(stacks.size()));
    for (const ItemStackSnapshot& s : stacks) {
        w.str(s.item);
        w.u32(uint32_t(s.count));
        w.u8(s.foundThisRun ? 1 : 0);
        w.f32(s.condition);
        w.u8(s.secured ? 1 : 0);
    }
}

bool readStacks(ByteReader& r, std::vector<ItemStackSnapshot>& out)
{
    const uint32_t n = r.u32();
    if (!r.ok() || n > kMaxStacks)
        return false;
    out.resize(n);
    for (ItemStackSnapshot& s : out) {
        s.item = r.str();
        s.count = int32_t(r.u32());
        s.foundThisRun = r.u8() != 0;
        s.condition = r.f32();
        s.secured = r.u8() != 0;
    }
    return r.ok();
}

void writeNamed(ByteWriter& w,
                const std::vector<std::pair<std::string, int32_t>>& pairs)
{
    w.u32(uint32_t(pairs.size()));
    for (const auto& [key, value] : pairs) {
        w.str(key);
        w.u32(uint32_t(value));
    }
}

bool readNamed(ByteReader& r,
               std::vector<std::pair<std::string, int32_t>>& out)
{
    const uint32_t n = r.u32();
    if (!r.ok() || n > kMaxNamed)
        return false;
    out.clear();
    out.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        const std::string key = r.str();
        out.emplace_back(key, int32_t(r.u32()));
    }
    return r.ok();
}

} // namespace

std::vector<uint8_t> encode(const RpgSaveData& data)
{
    ByteWriter body;

    body.u32(uint32_t(data.skills.size()));
    for (const auto& [id, experience] : data.skills) {
        body.str(id);
        body.u64(uint64_t(experience));
    }
    body.u64(uint64_t(data.characterXp));

    writeStacks(body, data.backpack);
    writeStacks(body, data.stash);

    body.u32(uint32_t(data.equipped.size()));
    for (const std::string& s : data.equipped)
        body.str(s);
    body.u32(uint32_t(data.currency));

    body.u32(uint32_t(data.quests.size()));
    for (const QuestSnapshot& q : data.quests) {
        body.str(q.id);
        body.u8(q.state);
        body.u32(uint32_t(q.counts.size()));
        for (int32_t c : q.counts)
            body.u32(uint32_t(c));
    }

    body.u32(uint32_t(data.flags.size()));
    for (const std::string& f : data.flags)
        body.str(f);
    writeNamed(body, data.counters);
    writeNamed(body, data.standings);
    body.u32(uint32_t(data.day));
    body.u32(uint32_t(data.deepestDepth));

    body.u32(uint32_t(data.traders.size()));
    for (const TraderSnapshot& t : data.traders) {
        body.str(t.id);
        body.u32(uint32_t(t.purse));
        body.u32(uint32_t(t.daysSinceRestock));
        writeNamed(body, t.flow);
        writeNamed(body, t.stock);
        body.u32(uint32_t(t.completedBarters.size()));
        for (const std::string& b : t.completedBarters)
            body.str(b);
    }

    writeNamed(body, data.stations);

    // Header: magic, version, flags, the string pool, then the body. Same
    // layout as the enemy save, so the two are recognisable as one format
    // family and a future combined save is a container over both.
    std::vector<uint8_t> out;
    const auto put32 = [&out](uint32_t v) {
        for (int i = 0; i < 4; ++i)
            out.push_back(uint8_t((v >> (8 * i)) & 0xFF));
    };
    const auto put16 = [&out](uint16_t v) {
        out.push_back(uint8_t(v & 0xFF));
        out.push_back(uint8_t((v >> 8) & 0xFF));
    };

    out.insert(out.end(), kMagic, kMagic + 8);
    put16(kVersion);
    put16(0); // flags, reserved

    const std::vector<std::string>& pool = body.pool();
    put32(uint32_t(pool.size()));
    for (const std::string& s : pool) {
        put32(uint32_t(s.size()));
        out.insert(out.end(), s.begin(), s.end());
    }

    put32(uint32_t(body.size()));
    out.insert(out.end(), body.bytes().begin(), body.bytes().end());
    return out;
}

std::optional<RpgSaveData> decode(const uint8_t* data, std::size_t size)
{
    if (!data || size < 16 || std::memcmp(data, kMagic, 8) != 0) {
        eng::log::error("rpgsave: not an rpg save blob");
        return std::nullopt;
    }
    std::size_t at = 8;
    const auto get16 = [&]() -> uint16_t {
        const uint16_t v = uint16_t(data[at] | (uint16_t(data[at + 1]) << 8));
        at += 2;
        return v;
    };
    const auto get32 = [&]() -> uint32_t {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
            v |= uint32_t(data[at + std::size_t(i)]) << (8 * i);
        at += 4;
        return v;
    };

    const uint16_t version = get16();
    const uint16_t flags = get16();
    if (version != kVersion || flags != 0) {
        eng::log::error("rpgsave: version %u (expected %u); refusing to load",
                        unsigned(version), unsigned(kVersion));
        return std::nullopt;
    }

    if (at + 4 > size)
        return std::nullopt;
    const uint32_t poolCount = get32();
    if (poolCount > kMaxStrings)
        return std::nullopt;
    std::vector<std::string> pool;
    pool.reserve(poolCount);
    uint32_t poolBytes = 0;
    for (uint32_t i = 0; i < poolCount; ++i) {
        if (at + 4 > size)
            return std::nullopt;
        const uint32_t len = get32();
        poolBytes += len;
        if (len > kMaxStringBytes || poolBytes > kMaxStringBytes ||
            at + len > size)
            return std::nullopt;
        pool.emplace_back(reinterpret_cast<const char*>(data + at), len);
        at += len;
    }

    if (at + 4 > size)
        return std::nullopt;
    const uint32_t bodyBytes = get32();
    if (at + bodyBytes > size)
        return std::nullopt;

    ByteReader r(data + at, bodyBytes, pool);
    RpgSaveData out;

    const uint32_t skills = r.u32();
    if (!r.ok() || skills > kMaxNamed)
        return std::nullopt;
    out.skills.resize(skills);
    for (auto& [id, experience] : out.skills) {
        id = r.str();
        experience = int64_t(r.u64());
    }
    out.characterXp = int64_t(r.u64());
    if (!r.ok())
        return std::nullopt;

    if (!readStacks(r, out.backpack) || !readStacks(r, out.stash))
        return std::nullopt;

    const uint32_t slots = r.u32();
    if (!r.ok() || slots > kMaxSlots)
        return std::nullopt;
    out.equipped.resize(slots);
    for (std::string& s : out.equipped)
        s = r.str();
    out.currency = int32_t(r.u32());
    if (!r.ok())
        return std::nullopt;

    const uint32_t quests = r.u32();
    if (!r.ok() || quests > kMaxQuests)
        return std::nullopt;
    out.quests.resize(quests);
    for (QuestSnapshot& q : out.quests) {
        q.id = r.str();
        q.state = r.u8();
        const uint32_t counts = r.u32();
        if (!r.ok() || counts > kMaxObjectives)
            return std::nullopt;
        q.counts.resize(counts);
        for (int32_t& c : q.counts)
            c = int32_t(r.u32());
    }

    const uint32_t flagCount = r.u32();
    if (!r.ok() || flagCount > kMaxNamed)
        return std::nullopt;
    out.flags.resize(flagCount);
    for (std::string& f : out.flags)
        f = r.str();

    if (!readNamed(r, out.counters) || !readNamed(r, out.standings))
        return std::nullopt;
    out.day = int32_t(r.u32());
    out.deepestDepth = int32_t(r.u32());

    const uint32_t traders = r.u32();
    if (!r.ok() || traders > kMaxNamed)
        return std::nullopt;
    out.traders.resize(traders);
    for (TraderSnapshot& t : out.traders) {
        t.id = r.str();
        t.purse = int32_t(r.u32());
        t.daysSinceRestock = int32_t(r.u32());
        if (!readNamed(r, t.flow) || !readNamed(r, t.stock))
            return std::nullopt;
        const uint32_t barters = r.u32();
        if (!r.ok() || barters > kMaxNamed)
            return std::nullopt;
        t.completedBarters.resize(barters);
        for (std::string& b : t.completedBarters)
            b = r.str();
    }

    if (!readNamed(r, out.stations))
        return std::nullopt;

    if (!r.ok())
        return std::nullopt;
    return out;
}

bool writeFile(const std::string& path, const RpgSaveData& data)
{
    const std::vector<uint8_t> bytes = encode(data);

    // Atomic, because this is the only copy of a profile that took hours to
    // build and the write happens at exactly the moment a player is most
    // likely to alt-F4 (they just extracted, or just died). Write a temporary,
    // flush it, keep the previous file as .bak, then rename over the target --
    // rename is atomic within a filesystem, so an interrupted save leaves
    // either the old profile or the new one, never half of either.
    const std::filesystem::path target(path);
    const std::filesystem::path temp = std::filesystem::path(path + ".tmp");
    const std::filesystem::path backup = std::filesystem::path(path + ".bak");

    std::error_code ec;
    if (target.has_parent_path())
        std::filesystem::create_directories(target.parent_path(), ec);

    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            eng::log::error("rpgsave: cannot open '%s' for writing",
                            temp.string().c_str());
            return false;
        }
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  std::streamsize(bytes.size()));
        out.flush();
        if (!out) {
            eng::log::error("rpgsave: write to '%s' failed",
                            temp.string().c_str());
            std::filesystem::remove(temp, ec);
            return false;
        }
    }

    if (std::filesystem::exists(target, ec)) {
        std::filesystem::remove(backup, ec);
        std::filesystem::rename(target, backup, ec);
        // A failed backup is not a reason to refuse the save; it only costs
        // the one-generation history.
        if (ec)
            eng::log::error("rpgsave: could not keep a backup of '%s': %s",
                            path.c_str(), ec.message().c_str());
        ec.clear();
    }
    std::filesystem::rename(temp, target, ec);
    if (ec) {
        eng::log::error("rpgsave: could not publish '%s': %s", path.c_str(),
                        ec.message().c_str());
        std::filesystem::remove(temp, ec);
        return false;
    }

    eng::log::info("rpgsave: character xp %lld, %d carried, %d stashed, %d "
                   "quests (%d bytes) -> %s",
                   (long long)data.characterXp, int(data.backpack.size()),
                   int(data.stash.size()), int(data.quests.size()),
                   int(bytes.size()), path.c_str());
    return true;
}

std::optional<RpgSaveData> readFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        eng::log::error("rpgsave: cannot open '%s'", path.c_str());
        return std::nullopt;
    }
    const std::streamsize size = in.tellg();
    if (size <= 0)
        return std::nullopt;
    in.seekg(0);
    std::vector<uint8_t> bytes;
    bytes.resize(std::size_t(size));
    if (!in.read(reinterpret_cast<char*>(bytes.data()), size)) {
        eng::log::error("rpgsave: read of '%s' failed", path.c_str());
        return std::nullopt;
    }
    return decode(bytes);
}

// ---------------------------------------------------------------------------
// capture / restore
// ---------------------------------------------------------------------------

namespace {

std::vector<ItemStackSnapshot> snapshotOf(const Container& c)
{
    std::vector<ItemStackSnapshot> out;
    out.reserve(c.stacks().size());
    for (const ItemStack& s : c.stacks())
        out.push_back({s.item, int32_t(s.count), s.foundThisRun, s.condition,
                       s.secured});
    return out;
}

void restoreInto(Container& c, const std::vector<ItemStackSnapshot>& snaps)
{
    c.clear();
    // Written straight into the stack list rather than through add(): a save
    // must round-trip exactly, and add() would re-apply the weight and slot
    // limits that were in force when the content was different. A player whose
    // pack is over capacity because a designer lowered the limit should see a
    // full pack, not a deleted one.
    for (const ItemStackSnapshot& s : snaps)
        if (s.count > 0)
            c.stacks().push_back(
                {s.item, int(s.count), s.foundThisRun, s.condition, s.secured});
}

} // namespace

void syncEquipmentModifiers(const ItemLibrary& library, const Equipment& worn,
                            CharacterSheet& sheet)
{
    // Clear every slot first, then re-push the occupied ones: an empty slot
    // must remove its group, and doing it in two passes means a swap cannot
    // leave the old item's group behind.
    for (int i = 0; i < Equipment::kSlotCount; ++i)
        sheet.clearModifiers(Inventory::sourceFor(EquipSlot(i)));
    for (const Equipment::SlotModifiers& m : worn.modifiers(library))
        sheet.setModifiers(m.source, m.modifiers);
}

RpgSaveData capture(const SkillSet& skills, const Inventory& inventory,
                    const QuestBook& quests, const WorldState& world,
                    const Market& market, const Hideout& hideout)
{
    RpgSaveData data;
    for (const auto& [id, experience] : skills.raw())
        data.skills.emplace_back(id, experience);
    // Sorted for the same reason the counters are: two captures of the same
    // state must produce identical bytes.
    std::sort(data.skills.begin(), data.skills.end());
    data.characterXp = skills.characterExperience();

    data.backpack = snapshotOf(inventory.backpack);
    data.stash = snapshotOf(inventory.stash);
    data.equipped.resize(std::size_t(Equipment::kSlotCount));
    for (int i = 0; i < Equipment::kSlotCount; ++i)
        data.equipped[std::size_t(i)] = inventory.equipment.at(EquipSlot(i));
    data.currency = int32_t(inventory.currency);

    for (const std::unique_ptr<Quest>& q : quests.quests()) {
        if (!q)
            continue;
        QuestSnapshot snap;
        snap.id = q->id();
        snap.state = uint8_t(q->state());
        // One counter, because a Quest subclass owns exactly one. The vector
        // is kept because the format should not have to change the day a
        // multi-goal quest kind arrives.
        snap.counts.push_back(int32_t(q->progress()));
        data.quests.push_back(std::move(snap));
    }

    data.flags = world.flags();
    for (const auto& [key, value] : world.counters())
        data.counters.emplace_back(key, int32_t(value));
    for (const auto& [key, value] : world.standings())
        data.standings.emplace_back(key, int32_t(value));
    // Sorted, so two captures of the same state produce identical bytes --
    // which is what makes a save diffable and a round-trip test meaningful.
    std::sort(data.counters.begin(), data.counters.end());
    std::sort(data.standings.begin(), data.standings.end());
    data.day = int32_t(world.day());
    data.deepestDepth = int32_t(world.deepestDepth());

    for (const auto& [id, state] : market.states()) {
        TraderSnapshot snap;
        snap.id = id;
        snap.purse = int32_t(state.purse);
        snap.daysSinceRestock = int32_t(state.daysSinceRestock);
        for (const auto& [item, flow] : state.flow)
            snap.flow.emplace_back(item, int32_t(flow));
        for (const auto& [item, held] : state.stock)
            snap.stock.emplace_back(item, int32_t(held));
        snap.completedBarters = state.completedBarters;
        // Sorted for the same reason the counters are: two captures of the
        // same state must produce identical bytes.
        std::sort(snap.flow.begin(), snap.flow.end());
        std::sort(snap.stock.begin(), snap.stock.end());
        std::sort(snap.completedBarters.begin(), snap.completedBarters.end());
        data.traders.push_back(std::move(snap));
    }
    std::sort(data.traders.begin(), data.traders.end(),
              [](const TraderSnapshot& a, const TraderSnapshot& b) {
                  return a.id < b.id;
              });

    for (const auto& [id, level] : hideout.levels())
        data.stations.emplace_back(id, int32_t(level));
    std::sort(data.stations.begin(), data.stations.end());
    return data;
}

std::vector<std::string> restoreQuests(const RpgSaveData& data,
                                       const QuestLibrary& library,
                                       QuestBook& book)
{
    std::vector<std::string> missing;
    book.clear();
    for (const QuestSnapshot& snap : data.quests) {
        if (!library.find(snap.id)) {
            missing.push_back(snap.id);
            continue;
        }
        if (!book.assignUnchecked(library, snap.id))
            continue;
        Quest* q = book.find(snap.id);
        if (!q)
            continue;
        const QuestState state = snap.state < uint8_t(QuestState::Count)
                                     ? QuestState(snap.state)
                                     : QuestState::Active;
        if (!snap.counts.empty())
            q->setProgress(int(snap.counts.front()));
        if (state != QuestState::Active) {
            // Leave the channels: a finished quest must not keep counting.
            q->disable();
            q->restoreState(state);
        }
        // An Active quest stays subscribed from assignUnchecked. It is NOT
        // re-run through tryComplete here: a quest that the save recorded as
        // active with full progress was mid-turn-in, and completing it again
        // would fire the completion channel a second time.
    }
    return missing;
}

std::vector<std::string> restore(const RpgSaveData& data,
                                 const ItemLibrary& items,
                                 const QuestLibrary& quests,
                                 const TraderLibrary& traders, SkillSet& skills,
                                 CharacterSheet& sheet, Inventory& inventory,
                                 QuestBook& book, WorldState& world,
                                 Market& market, Hideout& hideout)
{
    {
        std::unordered_map<std::string, int64_t> experience;
        for (const auto& [id, value] : data.skills)
            experience[id] = value;
        // Experience for a skill the content has since dropped is kept in the
        // map rather than discarded: renaming a skill back would restore it,
        // and deleting a player's training because of a content edit is not a
        // failure mode worth having.
        skills.setRaw(std::move(experience), data.characterXp);
    }
    sheet.setModifiers(stats::kSkillModifierSource, skills.modifiers());

    restoreInto(inventory.backpack, data.backpack);
    restoreInto(inventory.stash, data.stash);
    inventory.equipment.clear();
    for (std::size_t i = 0; i < data.equipped.size() &&
                            i < std::size_t(Equipment::kSlotCount);
         ++i) {
        if (data.equipped[i].empty())
            continue;
        if (const ItemLibrary::Ref def = items.find(data.equipped[i]))
            inventory.equipment.equip(*def);
    }
    inventory.currency = int(data.currency);
    // Rebuilt rather than saved: a rebalanced breastplate takes effect on the
    // next load instead of being frozen into every existing save.
    syncEquipmentModifiers(items, inventory.equipment, sheet);

    world.clear();
    for (const std::string& f : data.flags)
        world.setFlag(f);
    for (const auto& [key, value] : data.counters)
        world.addCounter(key, int(value));
    for (const auto& [key, value] : data.standings)
        world.addStanding(key, int(value));
    world.setDay(int(data.day));
    world.noteDepth(int(data.deepestDepth));

    // The economy. sync() first, so a trader added since the save was written
    // opens at its authored stock rather than empty; the snapshot then
    // overwrites whatever the player has actually moved.
    market.clear();
    market.sync(traders);
    for (const TraderSnapshot& snap : data.traders) {
        TraderState* state = market.state(snap.id);
        if (!state) {
            // The trader has been removed from the content. Their history goes
            // with them, which is the one case where dropping saved state is
            // right: there is nobody left for it to be about.
            eng::log::error("rpgsave: the save knows trader '%s', which "
                            "traders.toml no longer defines", snap.id.c_str());
            continue;
        }
        state->purse = int(snap.purse);
        state->daysSinceRestock = int(snap.daysSinceRestock);
        state->flow.clear();
        for (const auto& [item, flow] : snap.flow)
            state->flow[item] = int(flow);
        for (const auto& [item, held] : snap.stock)
            state->stock[item] = int(held);
        state->completedBarters = snap.completedBarters;
    }

    hideout.clear();
    for (const auto& [id, level] : data.stations)
        hideout.setLevel(id, int(level));

    std::vector<std::string> unknown = restoreQuests(data, quests, book);

    // Items the content no longer defines. Kept in the containers (deleting a
    // player's inventory because of a rename is not an acceptable failure) and
    // reported so a developer sees it.
    const auto note = [&](const std::vector<ItemStackSnapshot>& stacks) {
        for (const ItemStackSnapshot& s : stacks)
            if (!items.find(s.item))
                unknown.push_back(s.item);
    };
    note(data.backpack);
    note(data.stash);
    std::sort(unknown.begin(), unknown.end());
    unknown.erase(std::unique(unknown.begin(), unknown.end()), unknown.end());
    return unknown;
}

} // namespace game::rpg::rpgsave
