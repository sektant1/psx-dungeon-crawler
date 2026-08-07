#include <eng/ecs/MapSerializer.h>

#include <eng/Log.h>
#include <eng/io/ByteStream.h>
#include <eng/ecs/ComponentRegistry.h>

#include <eng/ecs/Components.h> // eng::ecs::Parent
#include <eng/ecs/components/MeshSource.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace eng::ecs {

using eng::io::ByteReader;
using eng::io::ByteWriter;

namespace {
constexpr char kMagic[8] = {'R', 'A', 'V', 'E', 'N', 'M', 'A', 'P'};
constexpr uint16_t kVersion = 1;
constexpr uint32_t kNoParent = 0xFFFFFFFFu;
constexpr std::size_t kMaxMapBytes = 256u * 1024u * 1024u;
constexpr uint32_t kMaxStrings = 1u << 20;
constexpr uint32_t kMaxStringBytes = 4u * 1024u * 1024u;
constexpr uint32_t kMaxEntities = 1u << 20;
constexpr uint16_t kMaxComponentsPerEntity = 256;
constexpr uint32_t kMaxComponentBytes = 16u * 1024u * 1024u;

bool validParentGraph(const std::vector<uint32_t>& parentOf)
{
    std::vector<uint8_t> state(parentOf.size(), 0); // 0=new, 1=visiting, 2=done
    for (uint32_t start = 0; start < parentOf.size(); ++start) {
        uint32_t current = start;
        while (current != kNoParent && state[current] == 0) {
            state[current] = 1;
            current = parentOf[current];
        }
        if (current != kNoParent && state[current] == 1)
            return false;
        current = start;
        while (current != kNoParent && state[current] == 1) {
            state[current] = 2;
            current = parentOf[current];
        }
    }
    return true;
}
} // namespace

bool writeMap(const std::string& path, const entt::registry& reg,
              const eng::ecs::ComponentRegistry& types)
{
    std::unordered_map<entt::entity, uint32_t> localId;
    std::vector<entt::entity> order;

    // Enumerate all live entities via entity storage (storage() returns a pointer)
    if (auto* stor = reg.storage<entt::entity>()) {
        for (const auto e : *stor) {
            order.push_back(e);
        }
    }
    std::sort(order.begin(), order.end(), [](entt::entity a, entt::entity b) {
        return entt::to_integral(a) < entt::to_integral(b);
    });
    if (order.size() > kMaxEntities) return false;
    for (entt::entity e : order)
        localId.emplace(e, uint32_t(localId.size()));

    std::vector<uint32_t> parentOf(order.size(), kNoParent);
    for (uint32_t i = 0; i < order.size(); ++i) {
        if (const auto* p = reg.try_get<eng::ecs::Parent>(order[i]);
            p && p->value != entt::null) {
            const auto it = localId.find(p->value);
            if (it == localId.end() || it->second == i) return false;
            parentOf[i] = it->second;
        }
    }
    if (!validParentGraph(parentOf)) return false;

    // A duplicate stable id is a programming error in whoever built the
    // registry, not a bad scene -- so it is named. Silently returning false
    // reported it as "failed to write cooked map" against whatever level
    // happened to be cooking, which sends you looking through the level.
    std::unordered_map<uint16_t, const char*> registeredTypeIds;
    for (const eng::ecs::ComponentType& type : types.types()) {
        const auto [at, inserted] =
            registeredTypeIds.emplace(type.stableTypeId, type.name);
        if (!inserted) {
            eng::log::error("map: component id %u is claimed by both '%s' and "
                            "'%s'; a stable id is a file format and cannot be "
                            "shared",
                            unsigned(type.stableTypeId), at->second, type.name);
            return false;
        }
    }

    ByteWriter w;
    w.u32(uint32_t(order.size()));
    for (uint32_t i = 0; i < order.size(); ++i) {
        const entt::entity e = order[i];
        // A MeshRenderer has to say where its geometry comes from -- a file
        // path or a generated description. Neither is an entity that will draw
        // the prototype box in the shipped game, which is exactly the class of
        // content bug a cook should refuse rather than pass on.
        //
        // It used to demand a MeshSource specifically, from when a path was the
        // only answer. A PrimitiveMesh is the other one.
        if (reg.all_of<eng::ecs::MeshRenderer>(e) &&
            !reg.all_of<eng::ecs::MeshSource>(e) &&
            !reg.all_of<eng::ecs::PrimitiveMesh>(e)) {
            eng::log::error("map: entity %u has a MeshRenderer with neither a "
                            "MeshSource nor a PrimitiveMesh, so nothing says "
                            "what it draws",
                            unsigned(i));
            return false;
        }
        w.u32(i);
        w.u32(parentOf[i]);

        std::vector<const eng::ecs::ComponentType*> present;
        for (const eng::ecs::ComponentType& t : types.types())
            if (t.has(reg, e)) present.push_back(&t);
        std::sort(present.begin(), present.end(), [](const auto* a, const auto* b) {
            return a->stableTypeId < b->stableTypeId;
        });
        if (present.size() > kMaxComponentsPerEntity) return false;
        w.u16(uint16_t(present.size()));
        for (const eng::ecs::ComponentType* t : present) {
            w.u16(t->stableTypeId);
            const std::size_t lenAt = w.size();
            w.u32(0); // byteLen placeholder
            const std::size_t start = w.size();
            t->serialize(reg, e, w);
            const std::size_t payloadBytes = w.size() - start;
            if (payloadBytes > kMaxComponentBytes) return false;
            w.patchU32(lenAt, uint32_t(payloadBytes));
        }
    }

    const std::filesystem::path target(path);
    std::filesystem::path temporary = target;
    temporary += ".tmp";
    std::error_code ec;
    std::filesystem::remove(temporary, ec);
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
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
    out.flush();
    const bool wrote = bool(out);
    out.close();
    if (!wrote) {
        std::filesystem::remove(temporary, ec);
        return false;
    }
    std::filesystem::rename(temporary, target, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
        return false;
    }
    return true;
}

bool readMap(const std::string& path, entt::registry& outReg,
              const eng::ecs::ComponentRegistry& types)
{
    std::error_code fileError;
    const std::uintmax_t fileBytes = std::filesystem::file_size(path, fileError);
    if (fileError || fileBytes > kMaxMapBytes) return false;
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    if (file.size() < 12) return false;
    if (std::memcmp(file.data(), kMagic, 8) != 0) return false;
    const uint16_t ver = uint16_t(file[8]) | (uint16_t(file[9]) << 8); // LE
    const uint16_t flags = uint16_t(file[10]) | (uint16_t(file[11]) << 8);
    if (ver != kVersion || flags != 0) return false;

    const uint8_t* p = file.data() + 12;
    const uint8_t* end = file.data() + file.size();

    auto rd32 = [&](const uint8_t*& q) -> uint32_t {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= uint32_t(q[i]) << (8 * i);
        q += 4; return v;
    };
    if (end - p < 4) return false;
    const uint32_t poolCount = rd32(p);
    if (poolCount > kMaxStrings || std::size_t(end - p) / 4 < poolCount)
        return false;
    std::vector<std::string> pool;
    pool.reserve(poolCount);
    for (uint32_t i = 0; i < poolCount; ++i) {
        if (end - p < 4) return false;
        const uint32_t len = rd32(p);
        if (len > kMaxStringBytes) return false;
        if (std::size_t(end - p) < len) return false;
        pool.emplace_back(reinterpret_cast<const char*>(p), len);
        p += len;
    }

    ByteReader r(p, std::size_t(end - p), pool);
    const uint32_t count = r.u32();
    if (!r.ok() || count > kMaxEntities) return false;

    entt::registry parsed;
    std::vector<entt::entity> byLocal(count, static_cast<entt::entity>(entt::null));
    std::vector<uint32_t> parentOf(count, kNoParent);
    std::vector<bool> seen(count, false);
    for (uint32_t i = 0; i < count && r.ok(); ++i) {
        const uint32_t local = r.u32();
        const uint32_t parent = r.u32();
        if (local >= count || seen[local]) return false;
        if (parent != kNoParent && (parent >= count || parent == local))
            return false;
        seen[local] = true;
        entt::entity e = parsed.create();
        byLocal[local] = e;
        parentOf[local] = parent;
        const uint16_t comps = r.u16();
        if (comps > kMaxComponentsPerEntity) return false;
        std::unordered_set<uint16_t> seenTypes;
        for (uint16_t c = 0; c < comps && r.ok(); ++c) {
            const uint16_t typeId = r.u16();
            const uint32_t len = r.u32();
            if (!seenTypes.insert(typeId).second || len > kMaxComponentBytes)
                return false;
            auto payload = r.slice(len);
            if (!payload) return false;
            const eng::ecs::ComponentType* t = types.find(typeId);
            if (t) {
                t->deserialize(parsed, e, *payload, len);
                if (!payload->ok()) return false;
            }
        }
    }
    if (!r.ok() || r.remaining() != 0) return false;
    if (!std::all_of(seen.begin(), seen.end(), [](bool value) { return value; }))
        return false;
    if (!validParentGraph(parentOf)) return false;

    for (uint32_t local = 0; local < count; ++local) {
        if (parentOf[local] == kNoParent) continue;
        const entt::entity child = byLocal[local];
        const entt::entity parent = byLocal[parentOf[local]];
        if (parsed.all_of<eng::ecs::Transform>(child) &&
            !parsed.all_of<eng::ecs::Transform>(parent))
            return false;
        parsed.emplace_or_replace<eng::ecs::Parent>(
            child, eng::ecs::Parent{parent});
        parsed.get_or_emplace<eng::ecs::Children>(parent).value.push_back(child);
    }
    outReg = std::move(parsed);
    return true;
}

bool dumpMap(const std::string& path, const eng::ecs::ComponentRegistry& types)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    if (file.size() < 12 || file.size() > kMaxMapBytes ||
        std::memcmp(file.data(), kMagic, 8) != 0)
        return false;
    const uint16_t ver = uint16_t(file[8]) | (uint16_t(file[9]) << 8); // LE
    const uint16_t flags = uint16_t(file[10]) | (uint16_t(file[11]) << 8);
    if (ver != kVersion || flags != 0) return false;

    const uint8_t* p = file.data() + 12;
    const uint8_t* end = file.data() + file.size();
    auto rd32 = [&](const uint8_t*& q) -> uint32_t {
        uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= uint32_t(q[i]) << (8 * i);
        q += 4; return v;
    };
    std::printf("RAVENMAP version %u\n", unsigned(ver));
    if (end - p < 4) return false;
    const uint32_t poolCount = rd32(p);
    if (poolCount > kMaxStrings || std::size_t(end - p) / 4 < poolCount)
        return false;
    std::printf("string pool (%u):\n", poolCount);
    std::vector<std::string> pool;
    for (uint32_t i = 0; i < poolCount; ++i) {
        if (end - p < 4) return false;
        const uint32_t len = rd32(p);
        if (len > kMaxStringBytes) return false;
        if (std::size_t(end - p) < len) return false;
        pool.emplace_back(reinterpret_cast<const char*>(p), len);
        std::printf("  [%u] \"%s\"\n", i, pool.back().c_str());
        p += len;
    }

    ByteReader r(p, std::size_t(end - p), pool);
    const uint32_t count = r.u32();
    if (!r.ok() || count > kMaxEntities) return false;
    std::printf("entities (%u):\n", count);
    for (uint32_t i = 0; i < count && r.ok(); ++i) {
        const uint32_t local = r.u32();
        const uint32_t parent = r.u32();
        const uint16_t comps = r.u16();
        if (comps > kMaxComponentsPerEntity) return false;
        std::printf("  entity %u parent=%d components=%u\n", local,
                    parent == kNoParent ? -1 : int(parent), unsigned(comps));
        for (uint16_t c = 0; c < comps && r.ok(); ++c) {
            const uint16_t typeId = r.u16();
            const uint32_t len = r.u32();
            if (len > kMaxComponentBytes) return false;
            const eng::ecs::ComponentType* t = types.find(typeId);
            std::printf("    - %s (id %u, %u bytes)\n",
                        t ? t->name : "<unknown>", unsigned(typeId), len);
            r.skip(len);
        }
    }
    return r.ok() && r.remaining() == 0;
}

} // namespace eng::ecs
