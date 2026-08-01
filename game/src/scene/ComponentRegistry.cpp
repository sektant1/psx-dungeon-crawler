#include "ComponentRegistry.h"

#include "GameComponents.h"

#include <eng/io/ByteStream.h>

#include <cmath>

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

bool finite(const glm::vec3& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool validShape(uint8_t shape)
{
    return shape <= uint8_t(eng::ShapeKind::Cylinder);
}
void serCollider(const entt::registry& r, entt::entity e, ByteWriter& w)
{
    const auto& c = r.get<game::Collider>(e);
    w.u8(uint8_t(c.shape)); w.vec3(c.size); w.u8(uint8_t(c.layer));
    w.u8(c.sensor ? 1 : 0);
}
void deCollider(entt::registry& r, entt::entity e, ByteReader& b,
                uint32_t payloadBytes)
{
    if (payloadBytes < 14) { b.invalidate(); return; }
    game::Collider c;
    const uint8_t shape = b.u8();
    c.shape = eng::ShapeKind(shape); c.size = b.vec3();
    c.layer = eng::CollisionLayer(b.u8());
    // Version-1 collider payloads ended after layer (14 bytes). The sensor bit
    // is an optional trailing field so those maps remain readable as solids.
    if (payloadBytes >= 15)
        c.sensor = b.u8() != 0;
    const bool validSize = finite(c.size) && c.size.x > 0.0f &&
        (c.shape == eng::ShapeKind::Sphere ||
         (c.shape == eng::ShapeKind::Box
              ? c.size.y > 0.0f && c.size.z > 0.0f
              : c.size.y >= 0.0f));
    if (!validShape(shape) || !validSize ||
        c.layer >= eng::kMaxCollisionLayers) {
        b.invalidate();
        return;
    }
    r.emplace_or_replace<game::Collider>(e, c);
}

void serExit(const entt::registry& r, entt::entity e, ByteWriter& w)
{ w.f32(r.get<game::Exit>(e).yawDegrees); }
void deExit(entt::registry& r, entt::entity e, ByteReader& b, uint32_t bytes)
{
    if (bytes < 4) { b.invalidate(); return; }
    const float yaw = b.f32();
    if (!std::isfinite(yaw)) { b.invalidate(); return; }
    r.emplace_or_replace<game::Exit>(e, game::Exit{yaw});
}

void serEnemy(const entt::registry& r, entt::entity e, ByteWriter& w)
{ w.str(r.get<game::EnemySpawn>(e).type); }
void deEnemy(entt::registry& r, entt::entity e, ByteReader& b, uint32_t bytes)
{
    if (bytes < 4) { b.invalidate(); return; }
    const std::string type = b.str();
    if (type.empty()) { b.invalidate(); return; }
    r.emplace_or_replace<game::EnemySpawn>(e, game::EnemySpawn{type});
}

void serPickup(const entt::registry& r, entt::entity e, ByteWriter& w)
{ w.str(r.get<game::Pickup>(e).type); }
void dePickup(entt::registry& r, entt::entity e, ByteReader& b, uint32_t bytes)
{
    if (bytes < 4) { b.invalidate(); return; }
    const std::string type = b.str();
    if (type.empty()) { b.invalidate(); return; }
    r.emplace_or_replace<game::Pickup>(e, game::Pickup{type});
}

void serTrigger(const entt::registry& r, entt::entity e, ByteWriter& w)
{
    const auto& t = r.get<game::Trigger>(e);
    w.u8(uint8_t(t.shape)); w.vec3(t.size); w.str(t.event);
}
void deTrigger(entt::registry& r, entt::entity e, ByteReader& b, uint32_t bytes)
{
    if (bytes < 17) { b.invalidate(); return; }
    game::Trigger t;
    const uint8_t shape = b.u8();
    t.shape = eng::ShapeKind(shape); t.size = b.vec3(); t.event = b.str();
    if (!validShape(shape) || !finite(t.size) || t.size.x <= 0.0f ||
        (t.shape == eng::ShapeKind::Box &&
         (t.size.y <= 0.0f || t.size.z <= 0.0f)) || t.event.empty()) {
        b.invalidate();
        return;
    }
    r.emplace_or_replace<game::Trigger>(e, t);
}

void serMarker(const entt::registry& r, entt::entity e, ByteWriter& w)
{ w.str(r.get<game::SceneMarker>(e).type); }
void deMarker(entt::registry& r, entt::entity e, ByteReader& b, uint32_t bytes)
{
    if (bytes < 4) { b.invalidate(); return; }
    const std::string type = b.str();
    if (type.empty()) { b.invalidate(); return; }
    r.emplace_or_replace<game::SceneMarker>(e, game::SceneMarker{type});
}

void serEnvironment(const entt::registry& r, entt::entity e, ByteWriter& w)
{ w.str(r.get<game::SceneEnvironment>(e).palette); }
void deEnvironment(entt::registry& r, entt::entity e, ByteReader& b,
                   uint32_t bytes)
{
    if (bytes < 4) { b.invalidate(); return; }
    const std::string palette = b.str();
    if (palette.empty()) { b.invalidate(); return; }
    r.emplace_or_replace<game::SceneEnvironment>(
        e, game::SceneEnvironment{palette});
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
    reg.add({"SceneMarker", 16, addDefault<game::SceneMarker>,
             has<game::SceneMarker>, remove<game::SceneMarker>,
             serMarker, deMarker});
    reg.add({"SceneEnvironment", 17, addDefault<game::SceneEnvironment>,
             has<game::SceneEnvironment>, remove<game::SceneEnvironment>,
             serEnvironment, deEnvironment});
    return reg;
}

} // namespace

const eng::ecs::ComponentRegistry& coreRegistry()
{
    static const eng::ecs::ComponentRegistry reg = buildCore();
    return reg;
}

} // namespace mapio
