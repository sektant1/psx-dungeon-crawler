#include <eng/ecs/ComponentRegistry.h>

#include <eng/ecs/Components.h>
#include <eng/ecs/MeshSource.h>
#include <eng/io/ByteStream.h>

namespace eng::ecs {

const ComponentType* ComponentRegistry::find(uint16_t id) const
{
    for (const ComponentType& t : mTypes)
        if (t.stableTypeId == id) return &t;
    return nullptr;
}

namespace {

using io::ByteReader;
using io::ByteWriter;

template <typename T>
void addDefault(entt::registry& r, entt::entity e) { r.emplace_or_replace<T>(e); }
template <typename T>
bool has(const entt::registry& r, entt::entity e) { return r.all_of<T>(e); }
template <typename T>
void remove(entt::registry& r, entt::entity e) { r.remove<T>(e); }

void serName(const entt::registry& r, entt::entity e, ByteWriter& w)
{ w.str(r.get<Name>(e).value); }
void deName(entt::registry& r, entt::entity e, ByteReader& b)
{ r.emplace_or_replace<Name>(e, Name{b.str()}); }

void serTransform(const entt::registry& r, entt::entity e, ByteWriter& w)
{
    const auto& t = r.get<Transform>(e);
    w.vec3(t.position); w.quat(t.rotation); w.vec3(t.scale);
}
void deTransform(entt::registry& r, entt::entity e, ByteReader& b)
{
    Transform t;
    t.position = b.vec3(); t.rotation = b.quat(); t.scale = b.vec3();
    r.emplace_or_replace<Transform>(e, t);
}

void serMesh(const entt::registry& r, entt::entity e, ByteWriter& w)
{
    const auto& m = r.get<MeshRenderer>(e);
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
    MeshRenderer m;
    m.material = material;
    m.castShadows = shadows;
    r.emplace_or_replace<MeshRenderer>(e, m);
}

void serLight(const entt::registry& r, entt::entity e, ByteWriter& w)
{
    const auto& l = r.get<LightRef>(e).desc;
    w.u8(uint8_t(l.type));
    w.vec3(l.colour);
    w.f32(l.range);
    w.u8(l.castShadows ? 1 : 0);
}
void deLight(entt::registry& r, entt::entity e, ByteReader& b)
{
    LightDesc d;
    d.type = LightDesc::Type(b.u8());
    d.colour = b.vec3();
    d.range = b.f32();
    d.castShadows = b.u8() != 0;
    r.emplace_or_replace<LightRef>(e, LightRef{d, {}});
}

} // namespace

void registerEngineComponents(ComponentRegistry& reg)
{
    reg.add({"Name", 1, addDefault<Name>, has<Name>, remove<Name>, serName, deName});
    reg.add({"Transform", 2, addDefault<Transform>, has<Transform>,
             remove<Transform>, serTransform, deTransform});
    reg.add({"MeshRenderer", 3, addDefault<MeshRenderer>, has<MeshRenderer>,
             remove<MeshRenderer>, serMesh, deMesh});
    reg.add({"LightRef", 4, addDefault<LightRef>, has<LightRef>,
             remove<LightRef>, serLight, deLight});
}

} // namespace eng::ecs
