#include "ComponentRegistry.h"

#include "ByteStream.h"
#include "GameComponents.h"
#include "MeshSource.h"

#include <eng/ecs/Components.h>

namespace mapio {

const ComponentType* ComponentRegistry::find(uint16_t id) const
{
    for (const ComponentType& t : mTypes)
        if (t.stableTypeId == id) return &t;
    return nullptr;
}

namespace {

template <typename T>
void addDefault(entt::registry& r, entt::entity e) { r.emplace_or_replace<T>(e); }
template <typename T>
bool has(const entt::registry& r, entt::entity e) { return r.all_of<T>(e); }
template <typename T>
void remove(entt::registry& r, entt::entity e) { r.remove<T>(e); }

void serName(const entt::registry& r, entt::entity e, ByteWriter& w)
{ w.str(r.get<eng::ecs::Name>(e).value); }
void deName(entt::registry& r, entt::entity e, ByteReader& b)
{ r.emplace_or_replace<eng::ecs::Name>(e, eng::ecs::Name{b.str()}); }

void serTransform(const entt::registry& r, entt::entity e, ByteWriter& w)
{
    const auto& t = r.get<eng::ecs::Transform>(e);
    w.vec3(t.position); w.quat(t.rotation); w.vec3(t.scale);
}
void deTransform(entt::registry& r, entt::entity e, ByteReader& b)
{
    eng::ecs::Transform t;
    t.position = b.vec3(); t.rotation = b.quat(); t.scale = b.vec3();
    r.emplace_or_replace<eng::ecs::Transform>(e, t);
}

void serMesh(const entt::registry& r, entt::entity e, ByteWriter& w)
{
    const auto& m = r.get<eng::ecs::MeshRenderer>(e);
    w.str(r.get<MeshSource>(e).path);
    w.str(m.material);
    w.u8(m.castShadows ? 1 : 0);
}
void deMesh(entt::registry& r, entt::entity e, ByteReader& b)
{
    const std::string path = b.str();
    const std::string material = b.str();
    const bool shadows = b.u8() != 0;
    r.emplace_or_replace<MeshSource>(e, MeshSource{path});
    eng::ecs::MeshRenderer m;
    m.material = material;
    m.castShadows = shadows;
    r.emplace_or_replace<eng::ecs::MeshRenderer>(e, m);
}

void serLight(const entt::registry& r, entt::entity e, ByteWriter& w)
{
    const auto& l = r.get<eng::ecs::LightRef>(e).desc;
    w.u8(uint8_t(l.type));
    w.vec3(l.colour);
    w.f32(l.range);
    w.u8(l.castShadows ? 1 : 0);
}
void deLight(entt::registry& r, entt::entity e, ByteReader& b)
{
    eng::LightDesc d;
    d.type = eng::LightDesc::Type(b.u8());
    d.colour = b.vec3();
    d.range = b.f32();
    d.castShadows = b.u8() != 0;
    r.emplace_or_replace<eng::ecs::LightRef>(e, eng::ecs::LightRef{d, {}});
}

void serCollider(const entt::registry& r, entt::entity e, ByteWriter& w)
{
    const auto& c = r.get<game::Collider>(e);
    w.u8(uint8_t(c.shape)); w.vec3(c.size); w.u8(uint8_t(c.layer));
}
void deCollider(entt::registry& r, entt::entity e, ByteReader& b)
{
    game::Collider c;
    c.shape = eng::ShapeKind(b.u8()); c.size = b.vec3();
    c.layer = eng::CollisionLayer(b.u8());
    r.emplace_or_replace<game::Collider>(e, c);
}

void serExit(const entt::registry& r, entt::entity e, ByteWriter& w)
{ w.f32(r.get<game::Exit>(e).yawDegrees); }
void deExit(entt::registry& r, entt::entity e, ByteReader& b)
{ r.emplace_or_replace<game::Exit>(e, game::Exit{b.f32()}); }

void serEnemy(const entt::registry& r, entt::entity e, ByteWriter& w)
{ w.str(r.get<game::EnemySpawn>(e).type); }
void deEnemy(entt::registry& r, entt::entity e, ByteReader& b)
{ r.emplace_or_replace<game::EnemySpawn>(e, game::EnemySpawn{b.str()}); }

void serPickup(const entt::registry& r, entt::entity e, ByteWriter& w)
{ w.str(r.get<game::Pickup>(e).type); }
void dePickup(entt::registry& r, entt::entity e, ByteReader& b)
{ r.emplace_or_replace<game::Pickup>(e, game::Pickup{b.str()}); }

void serTrigger(const entt::registry& r, entt::entity e, ByteWriter& w)
{
    const auto& t = r.get<game::Trigger>(e);
    w.u8(uint8_t(t.shape)); w.vec3(t.size); w.str(t.event);
}
void deTrigger(entt::registry& r, entt::entity e, ByteReader& b)
{
    game::Trigger t;
    t.shape = eng::ShapeKind(b.u8()); t.size = b.vec3(); t.event = b.str();
    r.emplace_or_replace<game::Trigger>(e, t);
}

void serEmpty(const entt::registry&, entt::entity, ByteWriter&) {}
void dePlayerSpawn(entt::registry& r, entt::entity e, ByteReader&)
{ r.emplace_or_replace<game::PlayerSpawn>(e); }

ComponentRegistry buildCore()
{
    ComponentRegistry reg;
    using eng::ecs::Name; using eng::ecs::Transform;
    using eng::ecs::MeshRenderer; using eng::ecs::LightRef;

    reg.add({"Name", 1, addDefault<Name>, has<Name>, remove<Name>, serName, deName});
    reg.add({"Transform", 2, addDefault<Transform>, has<Transform>,
             remove<Transform>, serTransform, deTransform});
    reg.add({"MeshRenderer", 3, addDefault<MeshRenderer>, has<MeshRenderer>,
             remove<MeshRenderer>, serMesh, deMesh});
    reg.add({"LightRef", 4, addDefault<LightRef>, has<LightRef>,
             remove<LightRef>, serLight, deLight});

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

const ComponentRegistry& coreRegistry()
{
    static const ComponentRegistry reg = buildCore();
    return reg;
}

} // namespace mapio
