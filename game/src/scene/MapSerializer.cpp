#include "MapSerializer.h"

#include "ByteStream.h"
#include "ComponentRegistry.h"

#include <eng/ecs/Components.h> // eng::ecs::Parent

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <unordered_map>
#include <vector>

namespace mapio {

namespace {
constexpr char kMagic[8] = {'P', 'S', 'X', 'M', 'A', 'P', '\0', 0};
constexpr uint16_t kVersion = 1;
constexpr uint32_t kNoParent = 0xFFFFFFFFu;
} // namespace

bool writeMap(const std::string& path, const entt::registry& reg,
              const ComponentRegistry& types)
{
    std::unordered_map<entt::entity, uint32_t> localId;
    std::vector<entt::entity> order;

    // Enumerate all live entities via entity storage (storage() returns a pointer)
    if (auto* stor = reg.storage<entt::entity>()) {
        for (const auto e : *stor) {
            localId.emplace(e, uint32_t(order.size()));
            order.push_back(e);
        }
    }

    ByteWriter w;
    w.u32(uint32_t(order.size()));
    for (entt::entity e : order) {
        w.u32(localId[e]);
        uint32_t parent = kNoParent;
        if (const auto* p = reg.try_get<eng::ecs::Parent>(e))
            if (p->value != entt::null) {
                auto it = localId.find(p->value);
                if (it != localId.end()) parent = it->second;
            }
        w.u32(parent);

        std::vector<const ComponentType*> present;
        for (const ComponentType& t : types.types())
            if (t.has(reg, e)) present.push_back(&t);
        w.u16(uint16_t(present.size()));
        for (const ComponentType* t : present) {
            w.u16(t->stableTypeId);
            const std::size_t lenAt = w.size();
            w.u32(0); // byteLen placeholder
            const std::size_t start = w.size();
            t->serialize(reg, e, w);
            w.patchU32(lenAt, uint32_t(w.size() - start));
        }
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    // Emit every header integer little-endian to match the LE reader (rd32),
    // so the format is host-endian independent.
    auto put16 = [&](uint16_t v) {
        const char b[2] = {char(v & 0xFF), char((v >> 8) & 0xFF)};
        out.write(b, 2);
    };
    auto put32 = [&](uint32_t v) {
        const char b[4] = {char(v & 0xFF), char((v >> 8) & 0xFF),
                           char((v >> 16) & 0xFF), char((v >> 24) & 0xFF)};
        out.write(b, 4);
    };
    out.write(kMagic, 8);
    put16(kVersion);
    put16(0); // flags

    put32(uint32_t(w.pool().size()));
    for (const std::string& s : w.pool()) {
        put32(uint32_t(s.size()));
        out.write(s.data(), std::streamsize(s.size()));
    }

    out.write(reinterpret_cast<const char*>(w.bytes().data()),
              std::streamsize(w.bytes().size()));
    return bool(out);
}

bool readMap(const std::string& path, entt::registry& outReg,
             const ComponentRegistry& types)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    if (file.size() < 12) return false;
    if (std::memcmp(file.data(), kMagic, 8) != 0) return false;
    const uint16_t ver = uint16_t(file[8]) | (uint16_t(file[9]) << 8); // LE
    if (ver > kVersion) return false;

    const uint8_t* p = file.data() + 12;
    const uint8_t* end = file.data() + file.size();

    auto rd32 = [&](const uint8_t*& q) -> uint32_t {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= uint32_t(q[i]) << (8 * i);
        q += 4; return v;
    };
    if (end - p < 4) return false;
    const uint32_t poolCount = rd32(p);
    std::vector<std::string> pool;
    pool.reserve(poolCount);
    for (uint32_t i = 0; i < poolCount; ++i) {
        if (end - p < 4) return false;
        const uint32_t len = rd32(p);
        if (std::size_t(end - p) < len) return false;
        pool.emplace_back(reinterpret_cast<const char*>(p), len);
        p += len;
    }

    ByteReader r(p, std::size_t(end - p), pool);
    const uint32_t count = r.u32();

    std::vector<entt::entity> byLocal(count, static_cast<entt::entity>(entt::null));
    std::vector<uint32_t> parentOf(count, kNoParent);
    for (uint32_t i = 0; i < count && r.ok(); ++i) {
        const uint32_t local = r.u32();
        const uint32_t parent = r.u32();
        if (local >= count) return false;
        entt::entity e = outReg.create();
        byLocal[local] = e;
        parentOf[local] = parent;
        const uint16_t comps = r.u16();
        for (uint16_t c = 0; c < comps && r.ok(); ++c) {
            const uint16_t typeId = r.u16();
            const uint32_t len = r.u32();
            const std::size_t before = r.remaining();
            const ComponentType* t = types.find(typeId);
            if (t) {
                t->deserialize(outReg, e, r);
                const std::size_t consumed = before - r.remaining();
                if (consumed > len) return false; // deserializer desynced past its payload
                if (consumed < len) r.skip(len - consumed);
            } else {
                r.skip(len);
            }
        }
    }
    if (!r.ok()) return false;

    for (uint32_t local = 0; local < count; ++local) {
        if (parentOf[local] == kNoParent) continue;
        if (parentOf[local] >= count) return false;
        outReg.emplace_or_replace<eng::ecs::Parent>(
            byLocal[local], eng::ecs::Parent{byLocal[parentOf[local]]});
    }
    return true;
}

bool dumpMap(const std::string& path, const ComponentRegistry& types)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    if (file.size() < 12 || std::memcmp(file.data(), kMagic, 8) != 0) return false;
    const uint16_t ver = uint16_t(file[8]) | (uint16_t(file[9]) << 8); // LE

    const uint8_t* p = file.data() + 12;
    const uint8_t* end = file.data() + file.size();
    auto rd32 = [&](const uint8_t*& q) -> uint32_t {
        uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= uint32_t(q[i]) << (8 * i);
        q += 4; return v;
    };
    std::printf("PSXMAP version %u\n", unsigned(ver));
    if (end - p < 4) return false;
    const uint32_t poolCount = rd32(p);
    std::printf("string pool (%u):\n", poolCount);
    std::vector<std::string> pool;
    for (uint32_t i = 0; i < poolCount; ++i) {
        if (end - p < 4) return false;
        const uint32_t len = rd32(p);
        if (std::size_t(end - p) < len) return false;
        pool.emplace_back(reinterpret_cast<const char*>(p), len);
        std::printf("  [%u] \"%s\"\n", i, pool.back().c_str());
        p += len;
    }

    ByteReader r(p, std::size_t(end - p), pool);
    const uint32_t count = r.u32();
    std::printf("entities (%u):\n", count);
    for (uint32_t i = 0; i < count && r.ok(); ++i) {
        const uint32_t local = r.u32();
        const uint32_t parent = r.u32();
        const uint16_t comps = r.u16();
        std::printf("  entity %u parent=%d components=%u\n", local,
                    parent == kNoParent ? -1 : int(parent), unsigned(comps));
        for (uint16_t c = 0; c < comps && r.ok(); ++c) {
            const uint16_t typeId = r.u16();
            const uint32_t len = r.u32();
            const ComponentType* t = types.find(typeId);
            std::printf("    - %s (id %u, %u bytes)\n",
                        t ? t->name : "<unknown>", unsigned(typeId), len);
            r.skip(len);
        }
    }
    return r.ok();
}

} // namespace mapio
