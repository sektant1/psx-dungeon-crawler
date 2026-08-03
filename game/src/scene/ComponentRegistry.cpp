#include "ComponentRegistry.h"

#include "GameComponents.h"

#include <eng/io/ByteStream.h>

#include <cmath>

// The first-person rig's field table. Lives beside the registration that uses
// it, the way the engine keeps its own: the ranges here are what the editor's
// sliders and the .scn clamp read, so a value nobody can author is a value
// nobody can ship by accident.
namespace eng {

template <> FieldSpan fieldsOf<game::ViewmodelRig>()
{
    using R = game::ViewmodelRig;
    static const Field f[] = {
        ENG_FIELD(R, offset, FieldType::Vec3),
        ENG_FIELD(R, rotation, FieldType::Vec3),
        ENG_FIELD_RANGE(R, scale, FieldType::Float, 0.05f, 3.0f),
        ENG_FIELD_RANGE(R, bobScale, FieldType::Float, 0.0f, 4.0f),
        ENG_FIELD_RANGE(R, swayScale, FieldType::Float, 0.0f, 4.0f),
        ENG_FIELD_RANGE(R, recoilScale, FieldType::Float, 0.0f, 4.0f),
        ENG_FIELD_RANGE(R, bobReferenceSpeed, FieldType::Float, 1.0f, 14.0f),
        ENG_FIELD_RANGE(R, bobRollDegrees, FieldType::Float, 0.0f, 12.0f),
        ENG_FIELD_RANGE(R, swayReturn, FieldType::Float, 0.0f, 30.0f),
        ENG_FIELD_RANGE(R, swayMax, FieldType::Float, 0.0f, 0.25f),
        ENG_FIELD_RANGE(R, swayRollDegrees, FieldType::Float, 0.0f, 15.0f),
        ENG_FIELD_RANGE(R, landingDip, FieldType::Float, 0.0f, 0.3f),
        ENG_FIELD_RANGE(R, landingRecovery, FieldType::Float, 0.5f, 30.0f),
        ENG_FIELD(R, motionEnabled, FieldType::Bool),
    };
    return {f, int(std::size(f))};
}

} // namespace eng

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

void serActor(const entt::registry& r, entt::entity e, ByteWriter& w)
{ w.u8(uint8_t(r.get<game::Actor>(e).kind)); }
void deActor(entt::registry& r, entt::entity e, ByteReader& b, uint32_t bytes)
{
    if (bytes < 1) { b.invalidate(); return; }
    const uint8_t kind = b.u8();
    if (kind >= game::kActorKindCount) { b.invalidate(); return; }
    r.emplace_or_replace<game::Actor>(e, game::Actor{game::ActorKind(kind)});
}

// The whole table, action by action, in vocabulary order: a count first so a
// map written by an older build (fewer actions) still reads, and an id per row
// so reordering the vocabulary cannot silently move a cue from "death" to
// "dodge".
void serActorSounds(const entt::registry& r, entt::entity e, ByteWriter& w)
{
    const game::ActorSoundSet& set = r.get<game::ActorSounds>(e).set;
    uint32_t named = 0;
    for (const game::ActorActionInfo& info : game::actorActions())
        if (!set.cue(info.action).empty())
            ++named;
    w.u32(named);
    for (const game::ActorActionInfo& info : game::actorActions()) {
        const std::string& cue = set.cue(info.action);
        if (cue.empty())
            continue;
        w.str(info.id);
        w.str(cue);
    }
}
void deActorSounds(entt::registry& r, entt::entity e, ByteReader& b,
                   uint32_t bytes)
{
    if (bytes < 4) { b.invalidate(); return; }
    const uint32_t named = b.u32();
    if (named > game::kActorActionCount) { b.invalidate(); return; }
    game::ActorSounds sounds;
    for (uint32_t i = 0; i < named; ++i) {
        const std::string action = b.str();
        const std::string cue = b.str();
        // An action this build does not know is skipped rather than fatal: a
        // map cooked by a newer editor must still load in an older game with
        // one sound missing, not fail to load a level.
        if (const game::ActorActionInfo* info = game::findActorAction(action))
            sounds.set.set(info->action, cue);
    }
    r.emplace_or_replace<game::ActorSounds>(e, std::move(sounds));
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
    // 18..28 belong to the engine (see registerEngineComponents: the engine's
    // second block starts above the game's 10-17 reservation), so the game
    // continues at 29. A stable id is a file format and a duplicate is not a
    // warning: writeMap refuses to emit a map whose registry has two types on
    // one id, which is a cook that fails with no bad entity to point at.
    reg.add({"Actor", 29, addDefault<game::Actor>, has<game::Actor>,
             remove<game::Actor>, serActor, deActor});
    reg.add({"ActorSounds", 30, addDefault<game::ActorSounds>,
             has<game::ActorSounds>, remove<game::ActorSounds>,
             serActorSounds, deActorSounds});
    // Authored on the camera the player looks through, next to the engine's
    // FirstPersonController. One line, because the field table below is the
    // payload format, the inspector rows and the add-menu entry at once.
    reg.add(eng::ecs::reflectedComponent<game::ViewmodelRig>("ViewmodelRig",
                                                             31));
    return reg;
}

} // namespace

const eng::ecs::ComponentRegistry& coreRegistry()
{
    static const eng::ecs::ComponentRegistry reg = buildCore();
    return reg;
}

} // namespace mapio
