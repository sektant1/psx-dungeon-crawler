#include "Commands.h"

#include "ByteStream.h"
#include "ComponentRegistry.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace editor {

Command makeCreateEntity(entt::registry& reg, std::string name,
                         entt::entity* outEntity)
{
    auto slot = std::make_shared<entt::entity>(entt::null);
    std::string nm = std::move(name);
    Command c;
    c.apply = [&reg, slot, nm, outEntity] {
        entt::entity e = reg.create();
        reg.emplace<eng::ecs::Name>(e, eng::ecs::Name{nm});
        reg.emplace<eng::ecs::Transform>(e);
        *slot = e;
        if (outEntity) *outEntity = e;
    };
    c.revert = [&reg, slot] {
        if (reg.valid(*slot)) reg.destroy(*slot);
    };
    return c;
}

Command makeSetTransform(entt::registry& reg, entt::entity e,
                         eng::ecs::Transform next)
{
    auto prev = std::make_shared<eng::ecs::Transform>();
    auto captured = std::make_shared<bool>(false);
    Command c;
    // Capture the pre-command transform on the FIRST apply only. Re-capturing
    // on redo would clobber it with the (already-reverted) value.
    c.apply = [&reg, e, next, prev, captured] {
        if (!*captured) {
            if (reg.all_of<eng::ecs::Transform>(e))
                *prev = reg.get<eng::ecs::Transform>(e);
            *captured = true;
        }
        reg.emplace_or_replace<eng::ecs::Transform>(e, next);
    };
    c.revert = [&reg, e, prev] {
        reg.emplace_or_replace<eng::ecs::Transform>(e, *prev);
    };
    return c;
}

Command makeDeleteEntity(entt::registry& reg, entt::entity e)
{
    auto blob = std::make_shared<std::vector<uint8_t>>();
    auto pool = std::make_shared<std::vector<std::string>>();
    auto slot = std::make_shared<entt::entity>(e);
    Command c;
    c.apply = [&reg, slot, blob, pool] {
        mapio::ByteWriter w;
        const mapio::ComponentRegistry& types = mapio::coreRegistry();
        std::vector<const mapio::ComponentType*> present;
        for (const mapio::ComponentType& t : types.types())
            if (t.has(reg, *slot)) present.push_back(&t);
        w.u16(uint16_t(present.size()));
        for (const mapio::ComponentType* t : present) {
            w.u16(t->stableTypeId);
            t->serialize(reg, *slot, w);
        }
        *blob = w.bytes();
        *pool = w.pool();
        reg.destroy(*slot);
    };
    c.revert = [&reg, slot, blob, pool] {
        entt::entity e2 = reg.create();
        mapio::ByteReader r(blob->data(), blob->size(), *pool);
        const uint16_t count = r.u16();
        const mapio::ComponentRegistry& types = mapio::coreRegistry();
        for (uint16_t i = 0; i < count && r.ok(); ++i) {
            const uint16_t id = r.u16();
            if (const mapio::ComponentType* t = types.find(id))
                t->deserialize(reg, e2, r);
        }
        *slot = e2;
    };
    return c;
}

} // namespace editor
