#include "ComponentRegistry.h"

#include "GameComponents.h"

#include <eng/io/ByteStream.h>

namespace mapio {

namespace {

using eng::ecs::ComponentRegistry;
using eng::io::ByteReader;
using eng::io::ByteWriter;

template <typename T>
void addDefault(entt::registry& r, entt::entity e) { r.emplace_or_replace<T>(e); }
template <typename T>
bool has(const entt::registry& r, entt::entity e) { return r.all_of<T>(e); }
template <typename T>
void remove(entt::registry& r, entt::entity e) { r.remove<T>(e); }




void serCollider(const entt::registry& r, entt::entity e, ByteWriter& w)
{
    const auto& c = r.get<game::Collider>(e);
    w.u8(uint8_t(c.shape)); w.vec3(c.size); w.u8(uint8_t(c.layer));
    w.u8(c.sensor ? 1 : 0);
}
void deCollider(entt::registry& r, entt::entity e, ByteReader& b,
                uint32_t payloadBytes)
{
    game::Collider c;
    c.shape = eng::ShapeKind(b.u8()); c.size = b.vec3();
    c.layer = eng::CollisionLayer(b.u8());
    // Version-1 collider payloads ended after layer (14 bytes). The sensor bit
    // is an optional trailing field so those maps remain readable as solids.
    if (payloadBytes >= 15)
        c.sensor = b.u8() != 0;
    r.emplace_or_replace<game::Collider>(e, c);
}

void serExit(const entt::registry& r, entt::entity e, ByteWriter& w)
{ w.f32(r.get<game::Exit>(e).yawDegrees); }
void deExit(entt::registry& r, entt::entity e, ByteReader& b, uint32_t)
{ r.emplace_or_replace<game::Exit>(e, game::Exit{b.f32()}); }

void serEnemy(const entt::registry& r, entt::entity e, ByteWriter& w)
{ w.str(r.get<game::EnemySpawn>(e).type); }
void deEnemy(entt::registry& r, entt::entity e, ByteReader& b, uint32_t)
{ r.emplace_or_replace<game::EnemySpawn>(e, game::EnemySpawn{b.str()}); }

void serPickup(const entt::registry& r, entt::entity e, ByteWriter& w)
{ w.str(r.get<game::Pickup>(e).type); }
void dePickup(entt::registry& r, entt::entity e, ByteReader& b, uint32_t)
{ r.emplace_or_replace<game::Pickup>(e, game::Pickup{b.str()}); }

void serTrigger(const entt::registry& r, entt::entity e, ByteWriter& w)
{
    const auto& t = r.get<game::Trigger>(e);
    w.u8(uint8_t(t.shape)); w.vec3(t.size); w.str(t.event);
}
void deTrigger(entt::registry& r, entt::entity e, ByteReader& b, uint32_t)
{
    game::Trigger t;
    t.shape = eng::ShapeKind(b.u8()); t.size = b.vec3(); t.event = b.str();
    r.emplace_or_replace<game::Trigger>(e, t);
}

void serEmpty(const entt::registry&, entt::entity, ByteWriter&) {}
void dePlayerSpawn(entt::registry& r, entt::entity e, ByteReader&, uint32_t)
{ r.emplace_or_replace<game::PlayerSpawn>(e); }

ComponentRegistry buildCore()
{
    ComponentRegistry reg;
    eng::ecs::registerEngineComponents(reg);

    reg.add({"Collider", 10, addDefault<game::Collider>, has<game::Collider>,
             remove<game::Collider>, serCollider, deCollider});
    reg.add({"PlayerSpawn", 11, addDefault<game::PlayerSpawn>,
             has<game::PlayerSpawn>, remove<game::PlayerSpawn>,
             serEmpty, dePlayerSpawn});
    reg.add({"Exit", 12, addDefault<game::Exit>, has<game::Exit>,
             remove<game::Exit>, serExit, deExit});
    reg.add({"EnemySpawn", 13, addDefault<game::EnemySpawn>,
             has<game::EnemySpawn>, remove<game::EnemySpawn>, serEnemy, deEnemy});
    reg.add({"Pickup", 14, addDefault<game::Pickup>, has<game::Pickup>,
             remove<game::Pickup>, serPickup, dePickup});
    reg.add({"Trigger", 15, addDefault<game::Trigger>, has<game::Trigger>,
             remove<game::Trigger>, serTrigger, deTrigger});
    return reg;
}

} // namespace

const eng::ecs::ComponentRegistry& coreRegistry()
{
    static const eng::ecs::ComponentRegistry reg = buildCore();
    return reg;
}

} // namespace mapio
