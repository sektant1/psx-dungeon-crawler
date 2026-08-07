#pragma once

#include <eng/ecs/Reflect.h>
#include <eng/io/ByteStream.h>

#include <entt/entt.hpp>

#include <cstdint>
#include <vector>

namespace eng::ecs {

// One serialisable/editable component type. Function pointers keep the table
// POD and free of virtual dispatch, so a registry is cheap to copy and can be
// built per world rather than being a process-wide singleton.
struct ComponentType {
    const char* name = nullptr;
    // Persisted in scene files, never reused. The engine reserves ids below
    // kFirstApplicationTypeId for its own components so an application can add
    // types without colliding with a future engine one.
    uint16_t stableTypeId = 0;

    void (*addDefault)(entt::registry&, entt::entity) = nullptr;
    bool (*has)(const entt::registry&, entt::entity) = nullptr;
    void (*remove)(entt::registry&, entt::entity) = nullptr;
    void (*serialize)(const entt::registry&, entt::entity, io::ByteWriter&) = nullptr;
    // payloadBytes lets a component add optional trailing fields while still
    // decoding older payloads with safe defaults.
    void (*deserialize)(entt::registry&, entt::entity, io::ByteReader&,
                        uint32_t payloadBytes) = nullptr;

    // --- reflection ------------------------------------------------------
    // The component's own data, for the inspector and for the generic
    // serialiser (see Reflect.h). Optional: the four types that predate this
    // keep their hand-written serialisers because the .map payloads they emit
    // are already shipped, and a component whose fields are not all POD (a
    // handle written back by a reconciler, a shared_ptr to a definition) simply
    // declares the ones that are.
    const Field* fields = nullptr;
    int fieldCount = 0;
    // The live component on an entity, as raw bytes, or null when absent. This
    // plus `fields` is everything a generic editor needs.
    void* (*instance)(entt::registry&, entt::entity) = nullptr;

    // Whether the add-component menu may offer this type. False for components
    // a system owns rather than an author (NodeRef, BodyRef) and for those that
    // are meaningless alone.
    bool authorable = true;
};

// Encodes/decodes one field. The generic serialisers are built out of these;
// an inspector uses `fields` and the raw pointer directly.
void writeField(io::ByteWriter& w, const void* component, const Field& f);
void readField(io::ByteReader& b, void* component, const Field& f);

// Generic serialisers driven by fieldsOf<T>(). Point a new component's
// serialize/deserialize at these and its format follows its field list --
// which is what makes adding a component one table entry rather than four
// edits in four files.
//
// Format: fields in declaration order, each in its natural encoding. New fields
// must be APPENDED: decoding stops when the payload runs out, so an older file
// loads into a newer component with the new fields at their defaults.
// Reordering or removing a field reinterprets every payload already on disk.
template <typename T>
void serializeFields(const entt::registry& r, entt::entity e, io::ByteWriter& w)
{
    const T& c = r.get<T>(e);
    const FieldSpan fields = fieldsOf<T>();
    for (int i = 0; i < fields.count; ++i)
        writeField(w, &c, fields.data[i]);
}

template <typename T>
void deserializeFields(entt::registry& r, entt::entity e, io::ByteReader& b,
                       uint32_t payloadBytes)
{
    (void)payloadBytes; // the reader is already bounded to this component
    T c;                // starts at the component's own defaults
    const FieldSpan fields = fieldsOf<T>();
    for (int i = 0; i < fields.count && b.ok(); ++i)
        readField(b, &c, fields.data[i]);
    r.emplace_or_replace<T>(e, c);
}

// One table entry for a fully reflected component. `id` is its stable
// serialisation id; the field list comes from fieldsOf<T>().
template <typename T>
ComponentType reflectedComponent(const char* name, uint16_t id,
                                 bool authorable = true)
{
    const FieldSpan fields = fieldsOf<T>();
    ComponentType t;
    t.name = name;
    t.stableTypeId = id;
    t.addDefault = [](entt::registry& r, entt::entity e) {
        r.emplace_or_replace<T>(e);
    };
    t.has = [](const entt::registry& r, entt::entity e) {
        return r.all_of<T>(e);
    };
    t.remove = [](entt::registry& r, entt::entity e) { r.remove<T>(e); };
    t.serialize = &serializeFields<T>;
    t.deserialize = &deserializeFields<T>;
    t.fields = fields.data;
    t.fieldCount = fields.count;
    t.instance = [](entt::registry& r, entt::entity e) -> void* {
        return r.try_get<T>(e);
    };
    t.authorable = authorable;
    return t;
}

// Where an application's own component ids start. The engine numbers below it
// and never reuses an id.
//
// 64 rather than 10 because ids 10-17 were already spent by this repository's
// game before the engine outgrew its first reservation, and a stable id is a
// file format: renumbering them would reinterpret every .map on disk. Those
// eight stay where they are, the engine grew past them (18+), and the next
// application type takes 64. The gap is the cheapest possible fix and the
// registry's uniqueness test is what keeps the two blocks honest.
inline constexpr uint16_t kFirstApplicationTypeId = 64;

// Ordered list of component types. Serialisers, inspectors and add-component
// menus all iterate this.
class ComponentRegistry {
public:
    void add(const ComponentType& t) { mTypes.push_back(t); }
    const std::vector<ComponentType>& types() const { return mTypes; }
    const ComponentType* find(uint16_t stableTypeId) const;

private:
    std::vector<ComponentType> mTypes;
};

// Appends the engine's own components -- Name, Transform, MeshRenderer,
// LightRef -- with their reserved ids. Applications call this, then add their
// own types from kFirstApplicationTypeId up.
void registerEngineComponents(ComponentRegistry&);

// The engine's components and nothing else, built once and shared.
//
// This is the table a runtime uses when it has no application vocabulary of
// its own to add -- raven_player playing somebody's project, a headless test
// reading a .map. An application that registers its own types keeps building
// its own table (this game's is mapio::coreRegistry()) and passes that instead;
// the two never meet, which is the whole point of the registry being a
// parameter everywhere rather than a global.
const ComponentRegistry& engineRegistry();

// Copies every entity of `src` into `dst`, carrying the components `types`
// knows about. Returns how many entities were added.
//
// This is what a loader does now that a World is shared. Reading a map used to
// end in `registry = std::move(parsed)`, which was fine while the map owned the
// only registry in the level and is destructive now that it does not: the swap
// would take the player, the enemies and every other live entity with it.
//
// Entity ids are NOT preserved -- the copies are fresh entities in `dst`. That
// is safe only because no serialisable component stores an entity id; if one
// ever does, it needs a remap table here, and this comment is where to look.
// `group`, when non-zero, stamps each copy with an EntityGroup so the caller
// can destroy exactly this batch later (eng::ecs::World::destroyGroup).
std::size_t copyEntities(entt::registry& dst, const entt::registry& src,
                         const ComponentRegistry& types, uint32_t group = 0);

} // namespace eng::ecs
