#include <eng/io/ByteStream.h>
#include "ComponentRegistry.h"
#include "GameComponents.h"

#include <eng/ecs/Components.h>

#include <cstdlib>
#include <iostream>
#include <set>

using namespace mapio;
using namespace eng::ecs;
using namespace eng::io;

static void require(bool c, const char* m)
{
    if (!c) { std::cerr << "ComponentRegistryTests: " << m << '\n'; std::exit(1); }
}

int main()
{
    const ComponentRegistry& reg = coreRegistry();

    std::set<uint16_t> ids;
    for (const ComponentType& t : reg.types()) {
        require(t.name != nullptr, "type has a name");
        require(t.addDefault && t.has && t.remove && t.serialize && t.deserialize,
                "type has all function pointers");
        require(ids.insert(t.stableTypeId).second, "stableTypeId is unique");
    }

    const ComponentType* transform = reg.find(2 /* Transform */);
    require(transform != nullptr, "Transform is registered under id 2");

    entt::registry src;
    entt::entity e = src.create();
    src.emplace<eng::ecs::Transform>(e, glm::vec3(1, 2, 3),
                                     glm::quat(1, 0, 0, 0), glm::vec3(2, 2, 2));
    require(transform->has(src, e), "has() sees the emplaced component");

    ByteWriter w;
    transform->serialize(src, e, w);

    entt::registry dst;
    entt::entity d = dst.create();
    ByteReader r(w.bytes().data(), w.bytes().size(), w.pool());
    transform->deserialize(dst, d, r, uint32_t(w.bytes().size()));
    require(r.ok(), "deserialize stayed in bounds");

    const auto& t = dst.get<eng::ecs::Transform>(d);
    require(t.position == glm::vec3(1, 2, 3), "position survives round-trip");
    require(t.scale == glm::vec3(2, 2, 2), "scale survives round-trip");

    transform->remove(dst, d);
    require(!transform->has(dst, d), "remove() drops the component");

    const ComponentType* collider = reg.find(10 /* Collider */);
    require(collider != nullptr, "Collider is registered under id 10");
    ByteWriter legacyCollider;
    legacyCollider.u8(uint8_t(eng::ShapeKind::Box));
    legacyCollider.vec3(glm::vec3(1.0f));
    legacyCollider.u8(0); // legacy payload ended at layer; no sensor byte
    entt::entity legacyEntity = dst.create();
    ByteReader legacyReader(legacyCollider.bytes().data(),
                            legacyCollider.bytes().size(),
                            legacyCollider.pool());
    collider->deserialize(dst, legacyEntity, legacyReader,
                          uint32_t(legacyCollider.bytes().size()));
    require(legacyReader.ok(), "legacy collider payload stays in bounds");
    require(!dst.get<game::Collider>(legacyEntity).sensor,
            "legacy collider defaults to a solid body");

    // --- PrimitiveMesh -----------------------------------------------------
    //
    // The newest engine component, and the first one whose id had to step over
    // the game's block (29-31). Its payload is a file format like any other, so
    // a full round trip is what stops a reordered field table reinterpreting
    // every generated mesh already on disk.
    const ComponentType* primitive = reg.find(32 /* PrimitiveMesh */);
    require(primitive != nullptr, "PrimitiveMesh is registered under id 32");
    require(primitive->fields && primitive->fieldCount > 0,
            "and carries a field table, so it needs no hand-written codec");

    entt::entity shape = src.create();
    eng::ecs::PrimitiveMesh authored;
    authored.kind = eng::ecs::PrimitiveMesh::Capsule;
    authored.size = glm::vec3(2.0f, 3.0f, 4.0f);
    authored.radius = 0.35f;
    authored.height = 1.8f;
    authored.bevel = 0.2f;
    authored.rings = 9;
    authored.segments = 21;
    authored.subdivisions = 2;
    authored.inwardFacing = true;
    src.emplace<eng::ecs::PrimitiveMesh>(shape, authored);

    ByteWriter shapeBytes;
    primitive->serialize(src, shape, shapeBytes);
    entt::entity shapeCopy = dst.create();
    ByteReader shapeReader(shapeBytes.bytes().data(), shapeBytes.bytes().size(),
                           shapeBytes.pool());
    primitive->deserialize(dst, shapeCopy, shapeReader,
                           uint32_t(shapeBytes.bytes().size()));
    require(shapeReader.ok(), "the primitive payload stays in bounds");
    const auto& decoded = dst.get<eng::ecs::PrimitiveMesh>(shapeCopy);
    require(decoded.kind == authored.kind, "kind survives the round trip");
    require(decoded.size == authored.size, "size does");
    require(decoded.radius == authored.radius && decoded.height == authored.height,
            "radius and height do");
    require(decoded.bevel == authored.bevel, "bevel does");
    require(decoded.rings == authored.rings &&
                decoded.segments == authored.segments &&
                decoded.subdivisions == authored.subdivisions,
            "the tessellation counts do");
    require(decoded.inwardFacing, "and the winding flag does");

    // A truncated payload must decode to defaults rather than to garbage: that
    // is the promise the field-table format makes about appended fields, and
    // the reason a new field can be added without invalidating old maps.
    ByteWriter shortBytes;
    shortBytes.u32(uint32_t(eng::ecs::PrimitiveMesh::Sphere));
    entt::entity partial = dst.create();
    ByteReader shortReader(shortBytes.bytes().data(), shortBytes.bytes().size(),
                           shortBytes.pool());
    primitive->deserialize(dst, partial, shortReader,
                           uint32_t(shortBytes.bytes().size()));
    const auto& sparse = dst.get<eng::ecs::PrimitiveMesh>(partial);
    require(sparse.kind == eng::ecs::PrimitiveMesh::Sphere,
            "a short payload keeps what it did carry");
    require(sparse.radius == eng::ecs::PrimitiveMesh{}.radius,
            "and defaults the rest");

    std::cout << "ComponentRegistryTests OK\n";
    return 0;
}
