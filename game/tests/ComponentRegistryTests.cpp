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

    std::cout << "ComponentRegistryTests OK\n";
    return 0;
}
