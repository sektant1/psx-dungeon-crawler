// The save codec. Pure: no registry, no world. A save format is exactly the
// kind of thing that is cheap to test exhaustively and expensive to get wrong
// in the field, so this leans on the former.
#include "../src/enemy/EnemySave.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace game;
using namespace game::enemysave;

static int failures = 0;
static void check(bool c, const char* m)
{
    if (!c) { std::fprintf(stderr, "FAIL: %s\n", m); ++failures; }
}
static bool nearly(float a, float b) { return std::fabs(a - b) < 1e-4f; }

static EnemySnapshot sample()
{
    EnemySnapshot e;
    e.defId = "hollow_soldier";
    e.feet = {1.5f, -2.25f, 30.0f};
    e.velocity = {0.5f, 0.0f, -3.25f};
    e.yaw = 1.75f;
    e.grounded = false;

    e.health = 43.5f;
    e.healthMax = 80.0f;
    e.invulnTimer = 0.125f;
    e.poise = 12.0f;
    e.poiseMax = 50.0f;
    e.poiseSinceHit = 0.75f;
    e.staggerImmuneFor = 0.25f;
    e.stamina = 66.0f;
    e.staminaMax = 100.0f;
    e.staminaSinceSpend = 1.5f;

    e.state = uint8_t(EnemyState::Circle);
    e.stateTime = 0.4f;
    e.thinkTimer = 0.15f;
    e.lostSightFor = 2.5f;
    e.home = {0.0f, 0.0f, 20.0f};
    e.lastKnownTarget = {3.0f, 0.0f, 25.0f};
    e.hasLastKnown = true;
    e.strafeSign = -1.0f;
    for (int i = 0; i < 8; ++i)
        e.cooldown[i] = float(i) * 0.25f;
    e.rng = 0xDEADBEEFu;
    e.aggroGrace = 1.25f;

    e.spawnerIndex = 3;
    e.spawnerId = "lane_soldier";
    e.effects.push_back({uint8_t(CrowdControl::Burn), 6.0f, 2.5f, 0.25f});
    e.effects.push_back({uint8_t(CrowdControl::Chill), 0.4f, 1.0f, 0.0f});
    return e;
}

static bool same(const EnemySnapshot& a, const EnemySnapshot& b)
{
    if (a.defId != b.defId || a.spawnerId != b.spawnerId ||
        a.spawnerIndex != b.spawnerIndex || a.state != b.state ||
        a.hasLastKnown != b.hasLastKnown || a.grounded != b.grounded ||
        a.rng != b.rng || a.effects.size() != b.effects.size())
        return false;
    const float* fa[] = {&a.yaw, &a.health, &a.healthMax, &a.invulnTimer,
                         &a.poise, &a.poiseMax, &a.poiseSinceHit,
                         &a.staggerImmuneFor, &a.stamina, &a.staminaMax,
                         &a.staminaSinceSpend, &a.stateTime, &a.thinkTimer,
                         &a.lostSightFor, &a.strafeSign, &a.aggroGrace};
    const float* fb[] = {&b.yaw, &b.health, &b.healthMax, &b.invulnTimer,
                         &b.poise, &b.poiseMax, &b.poiseSinceHit,
                         &b.staggerImmuneFor, &b.stamina, &b.staminaMax,
                         &b.staminaSinceSpend, &b.stateTime, &b.thinkTimer,
                         &b.lostSightFor, &b.strafeSign, &b.aggroGrace};
    for (size_t i = 0; i < sizeof(fa) / sizeof(fa[0]); ++i)
        if (!nearly(*fa[i], *fb[i]))
            return false;
    for (int i = 0; i < 8; ++i)
        if (!nearly(a.cooldown[i], b.cooldown[i]))
            return false;
    if (a.feet != b.feet || a.velocity != b.velocity || a.home != b.home ||
        a.lastKnownTarget != b.lastKnownTarget)
        return false;
    for (size_t i = 0; i < a.effects.size(); ++i) {
        if (a.effects[i].kind != b.effects[i].kind ||
            !nearly(a.effects[i].magnitude, b.effects[i].magnitude) ||
            !nearly(a.effects[i].remaining, b.effects[i].remaining) ||
            !nearly(a.effects[i].tickAccum, b.effects[i].tickAccum))
            return false;
    }
    return true;
}

int main()
{
    // Round-trip: every field survives, byte for byte in meaning.
    {
        EnemySaveData data;
        data.enemies.push_back(sample());
        data.spawners.push_back({"lane_soldier", true, false, 2, 1.5f, 6});
        data.spawners.push_back({"lane_rats", false, true, 1, 0.0f, 5});

        const std::vector<uint8_t> bytes = encode(data);
        check(!bytes.empty(), "encode produced a blob");

        const auto back = decode(bytes);
        check(back.has_value(), "decode accepted its own output");
        if (back) {
            check(back->enemies.size() == 1, "enemy count survives");
            check(same(data.enemies[0], back->enemies[0]),
                  "every enemy field survives the round trip");
            check(back->spawners.size() == 2, "spawner count survives");
            check(back->spawners[0].id == "lane_soldier" &&
                      back->spawners[0].armed &&
                      back->spawners[0].wavesSpawned == 2 &&
                      nearly(back->spawners[0].timer, 1.5f) &&
                      back->spawners[0].spawnedTotal == 6,
                  "spawner state survives");
            check(back->spawners[1].exhausted, "a spent spawner stays spent");
        }
    }

    // Empty is a legal save, not an error: a cleared arena has no enemies.
    {
        const auto back = decode(encode(EnemySaveData{}));
        check(back.has_value(), "an empty save round-trips");
        check(back && back->enemies.empty() && back->spawners.empty(),
              "and stays empty");
    }

    // Many enemies, shared definition ids -> the string pool interns.
    {
        EnemySaveData data;
        for (int i = 0; i < 200; ++i) {
            EnemySnapshot e = sample();
            e.feet.x = float(i);
            data.enemies.push_back(e);
        }
        const std::vector<uint8_t> bytes = encode(data);
        const auto back = decode(bytes);
        check(back && back->enemies.size() == 200, "200 enemies round-trip");
        check(back && nearly(back->enemies[199].feet.x, 199.0f),
              "the last record is intact");
        // Two distinct strings across 200 records; interning means the pool
        // does not grow with the record count.
        check(bytes.size() < 200 * 200,
              "shared ids are interned, not repeated per record");
    }

    // --- rejection ---------------------------------------------------------

    check(!decode(nullptr, 0).has_value(), "null blob rejected");
    check(!decode(std::vector<uint8_t>{}).has_value(), "empty blob rejected");

    {
        std::vector<uint8_t> junk(64, 0xAB);
        check(!decode(junk).has_value(), "garbage rejected (bad magic)");
    }

    // Wrong version: refuse rather than read fields that may have moved.
    {
        EnemySaveData data;
        data.enemies.push_back(sample());
        std::vector<uint8_t> bytes = encode(data);
        bytes[8] = uint8_t(kVersion + 1);
        check(!decode(bytes).has_value(), "a future version is refused");
        bytes[8] = uint8_t(kVersion);
        check(decode(bytes).has_value(), "...and the right one still loads");
    }

    // Truncation at every length. None may crash, and none may return a
    // half-filled result: a partly-restored fight is worse than a refused load.
    {
        EnemySaveData data;
        data.enemies.push_back(sample());
        data.spawners.push_back({"lane_soldier", true, false, 2, 1.5f, 6});
        const std::vector<uint8_t> full = encode(data);
        int accepted = 0;
        for (size_t n = 0; n < full.size(); ++n) {
            std::vector<uint8_t> cut(full.begin(), full.begin() + long(n));
            if (decode(cut).has_value())
                ++accepted;
        }
        check(accepted == 0, "no truncation of a valid blob decodes");
        check(decode(full).has_value(), "the untruncated blob still decodes");
    }

    // A corrupt count must not be believed. The header caps every count for
    // exactly this: an unchecked u32 element count is an allocation primitive.
    {
        EnemySaveData data;
        data.enemies.push_back(sample());
        std::vector<uint8_t> bytes = encode(data);
        // The enemy count is the first u32 of the body. Find the body by
        // walking the header the same way decode does.
        size_t at = 12; // magic(8) + version(2) + flags(2)
        auto get32 = [&]() {
            uint32_t v = 0;
            for (int i = 0; i < 4; ++i)
                v |= uint32_t(bytes[at + size_t(i)]) << (8 * i);
            at += 4;
            return v;
        };
        const uint32_t poolCount = get32();
        for (uint32_t i = 0; i < poolCount; ++i)
            at += get32(); // get32 steps past the length; then skip the chars
        at += 4;           // body length
        // Overwrite the enemy count with something enormous.
        for (int i = 0; i < 4; ++i)
            bytes[at + size_t(i)] = 0xFF;
        check(!decode(bytes).has_value(),
              "an absurd element count is rejected, not allocated");
    }

    // A zero RNG seed is repaired: xorshift seeded to zero stays zero, and an
    // enemy whose RNG never advances stops making decisions instead of failing.
    {
        EnemySaveData data;
        EnemySnapshot e = sample();
        e.rng = 0;
        data.enemies.push_back(e);
        const auto back = decode(encode(data));
        check(back && back->enemies[0].rng != 0,
              "a zero RNG seed is repaired on load");
    }

    if (failures == 0) std::printf("EnemySaveTests OK\n");
    return failures ? 1 : 0;
}
