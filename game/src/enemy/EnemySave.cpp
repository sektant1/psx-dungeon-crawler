#include "EnemySave.h"

#include <eng/Log.h>
#include <eng/io/ByteStream.h>

#include <cstring>
#include <fstream>

namespace game::enemysave {

using eng::io::ByteReader;
using eng::io::ByteWriter;

namespace {

constexpr char kMagic[8] = {'P', 'S', 'X', 'E', 'N', 'M', 'Y', 0};

// Bounds every count the reader will act on. A corrupt or hostile blob must
// cost a rejected load, not a multi-gigabyte allocation from a u32 it claimed
// was an element count.
constexpr uint32_t kMaxEnemies = 1u << 16;
constexpr uint32_t kMaxSpawners = 1u << 12;
constexpr uint32_t kMaxEffects = 64;
constexpr uint32_t kMaxStrings = 1u << 16;
constexpr uint32_t kMaxStringBytes = 1u << 22;

void writeEnemy(ByteWriter& w, const EnemySnapshot& e)
{
    w.str(e.defId);
    w.vec3(e.feet);
    w.vec3(e.velocity);
    w.f32(e.yaw);
    w.u8(e.grounded ? 1 : 0);

    w.f32(e.health);
    w.f32(e.healthMax);
    w.f32(e.invulnTimer);
    w.f32(e.poise);
    w.f32(e.poiseMax);
    w.f32(e.poiseSinceHit);
    w.f32(e.staggerImmuneFor);
    w.f32(e.stamina);
    w.f32(e.staminaMax);
    w.f32(e.staminaSinceSpend);

    w.u8(e.state);
    w.f32(e.stateTime);
    w.f32(e.thinkTimer);
    w.f32(e.lostSightFor);
    w.vec3(e.home);
    w.vec3(e.lastKnownTarget);
    w.u8(e.hasLastKnown ? 1 : 0);
    w.f32(e.strafeSign);
    for (float c : e.cooldown)
        w.f32(c);
    w.u32(e.rng);
    w.f32(e.aggroGrace);

    w.u32(uint32_t(e.spawnerIndex));
    w.str(e.spawnerId);

    w.u32(uint32_t(e.effects.size()));
    for (const EnemySnapshot::Effect& fx : e.effects) {
        w.u8(fx.kind);
        w.f32(fx.magnitude);
        w.f32(fx.remaining);
        w.f32(fx.tickAccum);
    }
}

// Returns false on any overrun or out-of-range count; the caller drops the
// whole load rather than keeping the records it managed to read.
bool readEnemy(ByteReader& r, EnemySnapshot& e)
{
    e.defId = r.str();
    e.feet = r.vec3();
    e.velocity = r.vec3();
    e.yaw = r.f32();
    e.grounded = r.u8() != 0;

    e.health = r.f32();
    e.healthMax = r.f32();
    e.invulnTimer = r.f32();
    e.poise = r.f32();
    e.poiseMax = r.f32();
    e.poiseSinceHit = r.f32();
    e.staggerImmuneFor = r.f32();
    e.stamina = r.f32();
    e.staminaMax = r.f32();
    e.staminaSinceSpend = r.f32();

    e.state = r.u8();
    e.stateTime = r.f32();
    e.thinkTimer = r.f32();
    e.lostSightFor = r.f32();
    e.home = r.vec3();
    e.lastKnownTarget = r.vec3();
    e.hasLastKnown = r.u8() != 0;
    e.strafeSign = r.f32();
    for (float& c : e.cooldown)
        c = r.f32();
    e.rng = r.u32();
    e.aggroGrace = r.f32();

    e.spawnerIndex = int32_t(r.u32());
    e.spawnerId = r.str();

    const uint32_t effects = r.u32();
    if (!r.ok() || effects > kMaxEffects)
        return false;
    e.effects.resize(effects);
    for (EnemySnapshot::Effect& fx : e.effects) {
        fx.kind = r.u8();
        fx.magnitude = r.f32();
        fx.remaining = r.f32();
        fx.tickAccum = r.f32();
    }

    // An xorshift seeded to zero is stuck at zero forever, so an enemy loaded
    // from a corrupt or zero-filled record would stop making decisions rather
    // than fail loudly. Repair it here, where the invariant is known.
    if (e.rng == 0)
        e.rng = 1;
    return r.ok();
}

} // namespace

std::vector<uint8_t> encode(const EnemySaveData& data)
{
    // Records first: writing them interns every string, so the pool is
    // complete by the time it is emitted.
    ByteWriter body;
    body.u32(uint32_t(data.enemies.size()));
    for (const EnemySnapshot& e : data.enemies)
        writeEnemy(body, e);
    body.u32(uint32_t(data.spawners.size()));
    for (const SpawnerSnapshot& s : data.spawners) {
        body.str(s.id);
        body.u8(s.armed ? 1 : 0);
        body.u8(s.exhausted ? 1 : 0);
        body.u32(uint32_t(s.wavesSpawned));
        body.f32(s.timer);
        body.u32(uint32_t(s.spawnedTotal));
    }

    std::vector<uint8_t> out;
    auto put32 = [&out](uint32_t v) {
        for (int i = 0; i < 4; ++i)
            out.push_back(uint8_t((v >> (8 * i)) & 0xFF));
    };
    auto put16 = [&out](uint16_t v) {
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

std::optional<EnemySaveData> decode(const uint8_t* data, std::size_t size)
{
    if (!data || size < 16 || std::memcmp(data, kMagic, 8) != 0) {
        eng::log::error("enemysave: not an enemy save blob");
        return std::nullopt;
    }
    std::size_t at = 8;
    auto get16 = [&]() -> uint16_t {
        const uint16_t v = uint16_t(data[at] | (uint16_t(data[at + 1]) << 8));
        at += 2;
        return v;
    };
    auto get32 = [&]() -> uint32_t {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
            v |= uint32_t(data[at + std::size_t(i)]) << (8 * i);
        at += 4;
        return v;
    };

    const uint16_t version = get16();
    const uint16_t flags = get16();
    if (version != kVersion || flags != 0) {
        eng::log::error("enemysave: version %u (expected %u); refusing to load",
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
    EnemySaveData out;

    const uint32_t enemies = r.u32();
    if (!r.ok() || enemies > kMaxEnemies)
        return std::nullopt;
    out.enemies.resize(enemies);
    for (EnemySnapshot& e : out.enemies)
        if (!readEnemy(r, e))
            return std::nullopt;

    const uint32_t spawners = r.u32();
    if (!r.ok() || spawners > kMaxSpawners)
        return std::nullopt;
    out.spawners.resize(spawners);
    for (SpawnerSnapshot& s : out.spawners) {
        s.id = r.str();
        s.armed = r.u8() != 0;
        s.exhausted = r.u8() != 0;
        s.wavesSpawned = int32_t(r.u32());
        s.timer = r.f32();
        s.spawnedTotal = int32_t(r.u32());
    }
    if (!r.ok())
        return std::nullopt;
    return out;
}

bool writeFile(const std::string& path, const EnemySaveData& data)
{
    const std::vector<uint8_t> bytes = encode(data);
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        eng::log::error("enemysave: cannot open '%s' for writing", path.c_str());
        return false;
    }
    out.write(reinterpret_cast<const char*>(bytes.data()),
              std::streamsize(bytes.size()));
    if (!out) {
        eng::log::error("enemysave: write to '%s' failed", path.c_str());
        return false;
    }
    eng::log::info("enemysave: wrote %d enemies, %d spawners (%d bytes) to %s",
                   int(data.enemies.size()), int(data.spawners.size()),
                   int(bytes.size()), path.c_str());
    return true;
}

std::optional<EnemySaveData> readFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        eng::log::error("enemysave: cannot open '%s'", path.c_str());
        return std::nullopt;
    }
    const std::streamsize size = in.tellg();
    if (size <= 0)
        return std::nullopt;
    in.seekg(0);
    // Braces, not parentheses: `bytes(std::size_t(size))` is a function
    // declaration, not a vector.
    std::vector<uint8_t> bytes;
    bytes.resize(std::size_t(size));
    if (!in.read(reinterpret_cast<char*>(bytes.data()), size)) {
        eng::log::error("enemysave: read of '%s' failed", path.c_str());
        return std::nullopt;
    }
    return decode(bytes);
}

} // namespace game::enemysave
