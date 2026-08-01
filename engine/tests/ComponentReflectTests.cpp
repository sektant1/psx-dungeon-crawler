#include <eng/ecs/ComponentRegistry.h>
#include <eng/ecs/Components.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <set>
#include <string>

using namespace eng;
using namespace eng::ecs;

static void require(bool c, const char* m)
{
    if (!c) {
        std::cerr << "ComponentReflectTests: " << m << '\n';
        std::exit(1);
    }
}

static const ComponentType* byName(const ComponentRegistry& reg, const char* n)
{
    for (const ComponentType& t : reg.types())
        if (std::strcmp(t.name, n) == 0)
            return &t;
    return nullptr;
}

// Round-trips one entity's component through its own serialiser, the way
// copyEntities and the .map reader do. The decoded registry is returned by
// out-parameter rather than the component itself, because an empty component
// (a tag) has no value to return -- entt::get<> on one yields void.
template <typename T>
static void roundTrip(const ComponentType& type, const T& value,
                      entt::registry& dst, entt::entity& out)
{
    entt::registry src;
    const entt::entity a = src.create();
    src.emplace<T>(a, value);

    io::ByteWriter w;
    type.serialize(src, a, w);

    out = dst.create();
    io::ByteReader r(w.bytes().data(), w.size(), w.pool());
    type.deserialize(dst, out, r, uint32_t(w.size()));
    require(dst.all_of<T>(out), "deserialize emplaced the component");
}

template <typename T>
static T roundTripValue(const ComponentType& type, const T& value)
{
    entt::registry dst;
    entt::entity e = entt::null;
    roundTrip(type, value, dst, e);
    return dst.get<T>(e);
}

int main()
{
    ComponentRegistry reg;
    registerEngineComponents(reg);

    // Stable ids are a file format: two types sharing one means a map loads the
    // wrong component into the wrong entity, silently.
    std::set<uint16_t> ids;
    for (const ComponentType& t : reg.types()) {
        require(t.stableTypeId != 0, "every type has a stable id");
        require(t.stableTypeId < kFirstApplicationTypeId,
                "engine ids stay inside the engine's reservation");
        require(ids.insert(t.stableTypeId).second, "stable ids are unique");
        require(t.has && t.addDefault && t.remove, "the type table is complete");
        require(t.serialize && t.deserialize, "every type round-trips");
        require(reg.find(t.stableTypeId) == &t, "find() resolves by id");
    }

    // --- a reflected component describes itself --------------------------
    const ComponentType* rb = byName(reg, "RigidBody");
    require(rb != nullptr, "RigidBody is registered");
    require(rb->fieldCount == 6, "RigidBody publishes its fields");
    require(rb->instance != nullptr, "a reflected type exposes its instance");

    // The field offsets have to actually address the members -- a wrong
    // offsetof compiles fine and corrupts a neighbouring field at runtime.
    {
        entt::registry r;
        const entt::entity e = r.create();
        r.emplace<RigidBody>(e);
        auto* raw = static_cast<RigidBody*>(rb->instance(r, e));
        require(raw == &r.get<RigidBody>(e), "instance() is the live component");
        for (int i = 0; i < rb->fieldCount; ++i) {
            const Field& f = rb->fields[i];
            if (std::strcmp(f.name, "mass") == 0) {
                *static_cast<float*>(fieldPtr(raw, f)) = 42.0f;
                require(r.get<RigidBody>(e).mass == 42.0f,
                        "writing through a field offset hits the member");
            }
            if (std::strcmp(f.name, "kinematic") == 0) {
                *static_cast<bool*>(fieldPtr(raw, f)) = true;
                require(r.get<RigidBody>(e).kinematic,
                        "a bool field addresses the bool member");
                require(r.get<RigidBody>(e).mass == 42.0f,
                        "and does not spill into its neighbour");
            }
        }
    }

    // --- generic serialisation carries every field -----------------------
    {
        RigidBody in;
        in.mass = 7.5f;
        in.gravityFactor = 0.0f;
        in.friction = 0.25f;
        in.restitution = 0.9f;
        in.kinematic = true;
        in.continuous = true;
        const RigidBody out = roundTripValue(*rb, in);
        require(out.mass == in.mass && out.gravityFactor == in.gravityFactor &&
                    out.friction == in.friction &&
                    out.restitution == in.restitution &&
                    out.kinematic == in.kinematic &&
                    out.continuous == in.continuous,
                "every reflected field survives a round trip");
    }
    {
        const ComponentType* em = byName(reg, "ParticleEmitter");
        require(em != nullptr, "ParticleEmitter is registered");
        ParticleEmitter in;
        in.effect = "torch_smoke";
        in.offset = {0.0f, 1.5f, -0.25f};
        in.playing = false;
        in.scale = 2.0f;
        const ParticleEmitter out = roundTripValue(*em, in);
        require(out.effect == in.effect, "a string field round-trips");
        require(out.offset == in.offset, "a vec3 field round-trips");
        require(out.playing == in.playing && out.scale == in.scale,
                "bool and float fields round-trip");
    }
    {
        const ComponentType* mo = byName(reg, "MaterialOverride");
        require(mo != nullptr, "MaterialOverride is registered");
        MaterialOverride in;
        in.material = "Game/Walls/Cracked";
        const MaterialOverride out = roundTripValue(*mo, in);
        require(out.material == in.material, "the override name round-trips");
    }
    {
        const ComponentType* sp = byName(reg, "ShaderParams");
        require(sp != nullptr, "ShaderParams is registered");
        ShaderParams in;
        in.tint = {0.2f, 0.9f, 0.4f};
        in.opacity = 0.5f;
        in.rimColour = {1.0f, 0.3f, 0.1f};
        in.rimStrength = 2.5f;
        in.rimPower = 6.0f;
        in.alphaScissor = 0.35f;
        const ShaderParams out = roundTripValue(*sp, in);
        require(out.tint == in.tint && out.rimColour == in.rimColour,
                "a colour field round-trips like a vec3");
        require(out.opacity == in.opacity && out.rimStrength == in.rimStrength &&
                    out.rimPower == in.rimPower &&
                    out.alphaScissor == in.alphaScissor,
                "every shader uniform survives a round trip");
    }

    {
        const ComponentType* vis = byName(reg, "Visibility");
        require(vis != nullptr, "Visibility is registered");
        const Visibility out = roundTripValue(*vis, Visibility{false});
        require(!out.visible, "a hidden entity comes back hidden");
    }
    {
        const ComponentType* life = byName(reg, "Lifetime");
        require(life != nullptr, "Lifetime is registered");
        require(roundTripValue(*life, Lifetime{2.5f}).remaining == 2.5f,
                "a lifetime round-trips");
    }
    {
        const ComponentType* spin = byName(reg, "Spin");
        require(spin != nullptr, "Spin is registered");
        const Spin in{{0.0f, 0.0f, 1.0f}, -180.0f};
        const Spin out = roundTripValue(*spin, in);
        require(out.axis == in.axis && out.degreesPerSecond == in.degreesPerSecond,
                "an axis and a rate round-trip");
    }
    {
        const ComponentType* anim = byName(reg, "LightAnimation");
        require(anim != nullptr, "LightAnimation is registered");
        LightAnimation in;
        in.mode = LightAnimation::Pulse;
        in.speed = 3.0f;
        in.amount = 0.75f;
        in.phase = 1.25f;
        in.time = 99.0f; // runtime state: must not travel
        const LightAnimation out = roundTripValue(*anim, in);
        require(out.mode == in.mode && out.speed == in.speed &&
                    out.amount == in.amount && out.phase == in.phase,
                "the authored fields round-trip");
        require(out.time == 0.0f,
                "and the un-reflected runtime clock does not, so a reloaded "
                "flame starts where a new one would");
    }

    {
        const ComponentType* cam = byName(reg, "Camera");
        require(cam != nullptr, "Camera is registered");
        Camera in;
        in.fovDegrees = 52.0f;
        in.nearClip = 0.1f;
        in.farClip = 120.0f;
        in.priority = 10;
        in.active = false;
        const Camera out = roundTripValue(*cam, in);
        require(out.fovDegrees == in.fovDegrees && out.nearClip == in.nearClip &&
                    out.farClip == in.farClip && out.priority == in.priority &&
                    out.active == in.active,
                "the whole lens round-trips, including the int field");
    }

    // A tag has no fields but still has to save and load, or an entity comes
    // back from a file missing the thing that made it interesting.
    {
        const ComponentType* tag = byName(reg, "RenderNode");
        require(tag != nullptr, "a tag component is registered");
        require(tag->fieldCount == 0, "a tag publishes no fields");
        entt::registry dst;
        entt::entity e = entt::null;
        roundTrip(*tag, RenderNode{}, dst, e); // asserts the tag came back
    }

    // --- a truncated payload must not produce garbage --------------------
    // The failure this guards is silent: a half-read float is a plausible
    // number, and a NaN in a transform takes a node or a body with it.
    {
        entt::registry src;
        const entt::entity a = src.create();
        RigidBody body;
        body.mass = 3.0f;
        src.emplace<RigidBody>(a, body);
        io::ByteWriter w;
        rb->serialize(src, a, w);

        entt::registry dst;
        const entt::entity b = dst.create();
        io::ByteReader r(w.bytes().data(), w.size() / 2, w.pool());
        rb->deserialize(dst, b, r, uint32_t(w.size() / 2));
        require(!r.ok(), "a truncated payload is reported, not guessed at");
        const RigidBody& partial = dst.get<RigidBody>(b);
        require(std::isfinite(partial.mass) && std::isfinite(partial.friction),
                "fields past the truncation keep their defaults");
    }

    // --- add / remove is uniform across every type -----------------------
    // What "any entity can carry any component" actually means: the menu drives
    // these pointers and never switches on a type.
    {
        entt::registry r;
        const entt::entity e = r.create();
        for (const ComponentType& t : reg.types()) {
            require(!t.has(r, e), "a fresh entity has nothing");
            t.addDefault(r, e);
            require(t.has(r, e), "addDefault adds it");
            t.remove(r, e);
            require(!t.has(r, e), "remove takes it away again");
        }
    }

    std::cout << "ComponentReflectTests OK\n";
    return 0;
}
