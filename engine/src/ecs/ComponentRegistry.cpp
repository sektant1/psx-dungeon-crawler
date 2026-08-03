#include <eng/ecs/ComponentRegistry.h>

#include <eng/ecs/Components.h>
#include <eng/ecs/components/MeshSource.h>
#include <eng/io/ByteStream.h>

#include <cmath>
#include <iterator>
#include <unordered_map>

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

bool finite(const glm::vec3& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool finite(const glm::quat& q)
{
    return std::isfinite(q.w) && std::isfinite(q.x) && std::isfinite(q.y) &&
           std::isfinite(q.z);
}

// A tag carries no data, but its payload cannot be empty: the format prefixes
// each component with a length, and a zero-length one is how a truncated file
// reads. One byte, so presence is distinguishable from damage.
void serTag(const entt::registry&, entt::entity, ByteWriter& w) { w.u8(1); }
template <typename T>
void deTag(entt::registry& r, entt::entity e, ByteReader& b, uint32_t bytes)
{
    if (bytes < 1) { b.invalidate(); return; }
    b.u8();
    r.emplace_or_replace<T>(e);
}

void serName(const entt::registry& r, entt::entity e, ByteWriter& w)
{ w.str(r.get<Name>(e).value); }
void deName(entt::registry& r, entt::entity e, ByteReader& b, uint32_t bytes)
{
    if (bytes < 4) { b.invalidate(); return; }
    r.emplace_or_replace<Name>(e, Name{b.str()});
}

void serTransform(const entt::registry& r, entt::entity e, ByteWriter& w)
{
    const auto& t = r.get<Transform>(e);
    w.vec3(t.position); w.quat(t.rotation); w.vec3(t.scale);
}
void deTransform(entt::registry& r, entt::entity e, ByteReader& b, uint32_t bytes)
{
    if (bytes < 40) { b.invalidate(); return; }
    Transform t;
    t.position = b.vec3(); t.rotation = b.quat(); t.scale = b.vec3();
    if (!finite(t.position) || !finite(t.rotation) || !finite(t.scale) ||
        glm::dot(t.rotation, t.rotation) < 1e-8f) {
        b.invalidate();
        return;
    }
    t.rotation = glm::normalize(t.rotation);
    r.emplace_or_replace<Transform>(e, t);
}

void serMesh(const entt::registry& r, entt::entity e, ByteWriter& w)
{
    const auto& m = r.get<MeshRenderer>(e);
    w.str(r.get<MeshSource>(e).path);
    w.str(m.material);
    w.u8(m.castShadows ? 1 : 0);
}
void deMesh(entt::registry& r, entt::entity e, ByteReader& b, uint32_t bytes)
{
    if (bytes < 9) { b.invalidate(); return; }
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
void deLight(entt::registry& r, entt::entity e, ByteReader& b, uint32_t bytes)
{
    if (bytes < 18) { b.invalidate(); return; }
    LightDesc d;
    const uint8_t type = b.u8();
    d.type = LightDesc::Type(type);
    d.colour = b.vec3();
    d.range = b.f32();
    d.castShadows = b.u8() != 0;
    if (type > uint8_t(LightDesc::Type::Point) || !finite(d.colour) ||
        !std::isfinite(d.range) || d.range < 0.0f) {
        b.invalidate();
        return;
    }
    r.emplace_or_replace<LightRef>(e, LightRef{d, {}});
}

} // namespace

void writeField(io::ByteWriter& w, const void* c, const Field& f)
{
    const void* p = fieldPtr(c, f);
    switch (f.type) {
    case FieldType::Bool:   w.u8(*static_cast<const bool*>(p) ? 1 : 0); break;
    case FieldType::Int:    w.u32(uint32_t(*static_cast<const int*>(p))); break;
    case FieldType::Float:  w.f32(*static_cast<const float*>(p)); break;
    case FieldType::Vec3:
    case FieldType::Colour: w.vec3(*static_cast<const glm::vec3*>(p)); break;
    case FieldType::Quat:   w.quat(*static_cast<const glm::quat*>(p)); break;
    case FieldType::String: w.str(*static_cast<const std::string*>(p)); break;
    }
}

void readField(io::ByteReader& b, void* c, const Field& f)
{
    void* p = fieldPtr(c, f);
    switch (f.type) {
    case FieldType::Bool:   *static_cast<bool*>(p) = b.u8() != 0; break;
    case FieldType::Int:    *static_cast<int*>(p) = int(b.u32()); break;
    case FieldType::Float: {
        // A NaN read out of a corrupt file propagates into a transform and
        // takes a node, a body or a whole frame with it. Reject the payload
        // instead, exactly as the hand-written decoders above do.
        const float v = b.f32();
        if (!std::isfinite(v)) { b.invalidate(); return; }
        *static_cast<float*>(p) = v;
        break;
    }
    case FieldType::Vec3:
    case FieldType::Colour: {
        const glm::vec3 v = b.vec3();
        if (!finite(v)) { b.invalidate(); return; }
        *static_cast<glm::vec3*>(p) = v;
        break;
    }
    case FieldType::Quat: {
        const glm::quat q = b.quat();
        if (!finite(q) || glm::dot(q, q) < 1e-8f) { b.invalidate(); return; }
        *static_cast<glm::quat*>(p) = glm::normalize(q);
        break;
    }
    case FieldType::String: *static_cast<std::string*>(p) = b.str(); break;
    }
}

// --- field tables --------------------------------------------------------
// One entry per editable member. This is the whole description of a component:
// the .map payload, the inspector row and the add-component menu all come from
// it, so a new field is one line here and nothing else.

} // namespace eng::ecs

namespace eng {

// The field tables. Written in `eng` rather than `eng::ecs` because that is
// where fieldsOf is declared, and a specialisation has to live in the namespace
// that declares the template. The components they describe keep their own
// namespace in each entry, which is also a reminder of what these are: a
// description of data that the renderer, the serialiser and the inspector all
// read, and only one of those three knows what an entity is.

template <> FieldSpan fieldsOf<ecs::MaterialOverride>()
{
    static const Field f[] = {
        ENG_FIELD(ecs::MaterialOverride, material, FieldType::String),
    };
    return {f, int(std::size(f))};
}

template <> FieldSpan fieldsOf<ecs::ShaderParams>()
{
    static const Field f[] = {
        ENG_FIELD(ecs::ShaderParams, tint, FieldType::Colour),
        ENG_FIELD_RANGE(ecs::ShaderParams, opacity, FieldType::Float, 0.0f, 1.0f),
        ENG_FIELD(ecs::ShaderParams, rimColour, FieldType::Colour),
        ENG_FIELD_RANGE(ecs::ShaderParams, rimStrength, FieldType::Float, 0.0f, 4.0f),
        ENG_FIELD_RANGE(ecs::ShaderParams, rimPower, FieldType::Float, 0.25f, 16.0f),
        ENG_FIELD_RANGE(ecs::ShaderParams, alphaScissor, FieldType::Float, 0.0f, 1.0f),
    };
    return {f, int(std::size(f))};
}

// Every entry's name is the uniform name -- see PortalParams.h. The ranges are
// what the shader stays coherent within; outside them the pattern does not
// merely look different, it stops reading as a portal.
template <> FieldSpan fieldsOf<ecs::PortalParams>()
{
    using P = ecs::PortalParams;
    static const Field f[] = {
        ENG_FIELD_RANGE(P, portalFlowSpeed, FieldType::Float, -2.0f, 2.0f),
        ENG_FIELD_RANGE(P, portalSwirlSpeed, FieldType::Float, -2.0f, 2.0f),
        ENG_FIELD_RANGE(P, portalTwist, FieldType::Float, -1.5f, 1.5f),
        ENG_FIELD_RANGE(P, portalArms, FieldType::Float, 1.0f, 12.0f),
        ENG_FIELD_RANGE(P, portalArmWidth, FieldType::Float, 0.05f, 0.95f),
        ENG_FIELD_RANGE(P, portalDepthScale, FieldType::Float, 0.05f, 3.0f),
        ENG_FIELD_RANGE(P, portalParallax, FieldType::Float, 0.0f, 2.0f),
        ENG_FIELD_RANGE(P, portalFieldWeight, FieldType::Float, 0.0f, 1.0f),
        ENG_FIELD_RANGE(P, portalCoreRadius, FieldType::Float, 0.0f, 0.5f),
        ENG_FIELD_RANGE(P, portalCoreBoost, FieldType::Float, 0.0f, 4.0f),
        ENG_FIELD_RANGE(P, portalRimRadius, FieldType::Float, 0.0f, 1.0f),
        ENG_FIELD_RANGE(P, portalRimWidth, FieldType::Float, 0.001f, 0.5f),
        ENG_FIELD_RANGE(P, portalRimIntensity, FieldType::Float, 0.0f, 4.0f),
        ENG_FIELD_RANGE(P, portalEdgeFade, FieldType::Float, 0.0f, 0.5f),
        ENG_FIELD(P, surfaceDark, FieldType::Colour),
        ENG_FIELD(P, surfaceMid, FieldType::Colour),
        ENG_FIELD(P, surfaceBright, FieldType::Colour),
        ENG_FIELD(P, surfaceCore, FieldType::Colour),
        ENG_FIELD_RANGE(P, surfaceStepFps, FieldType::Float, 0.0f, 60.0f),
        ENG_FIELD_RANGE(P, surfaceTexelSize, FieldType::Float, 0.005f, 0.5f),
        ENG_FIELD_RANGE(P, surfacePixelGrid, FieldType::Float, 4.0f, 256.0f),
        ENG_FIELD_RANGE(P, surfaceDither, FieldType::Float, 0.0f, 1.0f),
        ENG_FIELD_RANGE(P, surfaceBrightness, FieldType::Float, 0.0f, 4.0f),
        ENG_FIELD(P, surfaceGlowColour, FieldType::Colour),
        ENG_FIELD_RANGE(P, surfaceGlowStrength, FieldType::Float, 0.0f, 4.0f),
        ENG_FIELD_RANGE(P, surfaceGlowThreshold, FieldType::Float, 0.0f, 2.0f),
    };
    return {f, int(std::size(f))};
}

template <> FieldSpan fieldsOf<ecs::ParticleEmitter>()
{
    static const Field f[] = {
        ENG_FIELD(ecs::ParticleEmitter, effect, FieldType::String),
        ENG_FIELD(ecs::ParticleEmitter, offset, FieldType::Vec3),
        ENG_FIELD(ecs::ParticleEmitter, playing, FieldType::Bool),
        ENG_FIELD_RANGE(ecs::ParticleEmitter, scale, FieldType::Float, 0.01f, 16.0f),
    };
    return {f, int(std::size(f))};
}

template <> FieldSpan fieldsOf<ecs::Visibility>()
{
    static const Field f[] = {
        ENG_FIELD(ecs::Visibility, visible, FieldType::Bool),
    };
    return {f, int(std::size(f))};
}

template <> FieldSpan fieldsOf<ecs::Lifetime>()
{
    static const Field f[] = {
        ENG_FIELD_RANGE(ecs::Lifetime, remaining, FieldType::Float, 0.0f, 60.0f),
    };
    return {f, int(std::size(f))};
}

template <> FieldSpan fieldsOf<ecs::Spin>()
{
    static const Field f[] = {
        ENG_FIELD(ecs::Spin, axis, FieldType::Vec3),
        ENG_FIELD_RANGE(ecs::Spin, degreesPerSecond, FieldType::Float, -720.0f, 720.0f),
    };
    return {f, int(std::size(f))};
}

template <> FieldSpan fieldsOf<ecs::LightAnimation>()
{
    static const Field f[] = {
        // The range is the enum: an out-of-range mode decodes to a number the
        // system's switch falls through to Steady on, which is the one failure
        // mode that is not visible as a bug.
        ENG_FIELD_RANGE(ecs::LightAnimation, mode, FieldType::Int, 0.0f, 2.0f),
        ENG_FIELD_RANGE(ecs::LightAnimation, speed, FieldType::Float, 0.0f, 30.0f),
        ENG_FIELD_RANGE(ecs::LightAnimation, amount, FieldType::Float, 0.0f, 1.0f),
        ENG_FIELD_RANGE(ecs::LightAnimation, phase, FieldType::Float, 0.0f, 10.0f),
        // `time` is deliberately absent: it is where the flame currently is,
        // not how it was authored, and saving it would reload mid-gutter.
    };
    return {f, int(std::size(f))};
}

template <> FieldSpan fieldsOf<ecs::Orbit>()
{
    static const Field f[] = {
        ENG_FIELD(ecs::Orbit, centre, FieldType::Vec3),
        ENG_FIELD(ecs::Orbit, axis, FieldType::Vec3),
        ENG_FIELD_RANGE(ecs::Orbit, radius, FieldType::Float, 0.0f, 200.0f),
        ENG_FIELD_RANGE(ecs::Orbit, degreesPerSecond, FieldType::Float, -720.0f, 720.0f),
        ENG_FIELD_RANGE(ecs::Orbit, phaseDegrees, FieldType::Float, 0.0f, 360.0f),
        ENG_FIELD_RANGE(ecs::Orbit, height, FieldType::Float, -50.0f, 50.0f),
        // The range is the enum: an out-of-range facing decodes to a number the
        // system's comparisons treat as Free, which is the one failure mode
        // that is not visible as a bug.
        ENG_FIELD_RANGE(ecs::Orbit, facing, FieldType::Int, 0.0f, 2.0f),
        // `travelled` is deliberately absent: it is where the entity currently
        // is, not how it was authored, and saving it would reload mid-arc.
    };
    return {f, int(std::size(f))};
}

template <> FieldSpan fieldsOf<ecs::Camera>()
{
    static const Field f[] = {
        ENG_FIELD_RANGE(ecs::Camera, fovDegrees, FieldType::Float, 10.0f, 140.0f),
        ENG_FIELD_RANGE(ecs::Camera, nearClip, FieldType::Float, 0.01f, 5.0f),
        ENG_FIELD_RANGE(ecs::Camera, farClip, FieldType::Float, 1.0f, 5000.0f),
        ENG_FIELD_RANGE(ecs::Camera, priority, FieldType::Int, -100.0f, 100.0f),
        ENG_FIELD(ecs::Camera, active, FieldType::Bool),
    };
    return {f, int(std::size(f))};
}

template <> FieldSpan fieldsOf<ecs::FirstPersonController>()
{
    using C = ecs::FirstPersonController;
    static const Field f[] = {
        ENG_FIELD_RANGE(C, moveSpeed, FieldType::Float, 0.5f, 20.0f),
        ENG_FIELD_RANGE(C, mouseSensitivity, FieldType::Float, 0.0002f, 0.02f),
        ENG_FIELD_RANGE(C, baseFovDegrees, FieldType::Float, 40.0f, 130.0f),
        ENG_FIELD_RANGE(C, sprintFovKick, FieldType::Float, 0.0f, 25.0f),
        ENG_FIELD_RANGE(C, bobAmount, FieldType::Float, 0.0f, 0.2f),
        ENG_FIELD_RANGE(C, bobSpeed, FieldType::Float, 0.0f, 25.0f),
        ENG_FIELD(C, active, FieldType::Bool),
    };
    return {f, int(std::size(f))};
}

template <> FieldSpan fieldsOf<ecs::AudioEmitter>()
{
    using E = ecs::AudioEmitter;
    static const Field f[] = {
        ENG_FIELD(E, source, FieldType::String),
        ENG_FIELD(E, offset, FieldType::Vec3),
        ENG_FIELD_RANGE(E, bus, FieldType::Int, 0.0f,
                        float(static_cast<int>(AudioBus::Count) - 1)),
        ENG_FIELD_RANGE(E, gainDb, FieldType::Float, -80.0f, 12.0f),
        ENG_FIELD_RANGE(E, pitch, FieldType::Float, 0.25f, 4.0f),
        ENG_FIELD_RANGE(E, minDistance, FieldType::Float, 0.0f, 1000.0f),
        ENG_FIELD_RANGE(E, maxDistance, FieldType::Float, 0.01f, 10000.0f),
        ENG_FIELD_RANGE(E, rolloff, FieldType::Float, 0.0f, 8.0f),
        ENG_FIELD_RANGE(E, dopplerFactor, FieldType::Float, 0.0f, 4.0f),
        ENG_FIELD_RANGE(E, priority, FieldType::Int,
                        float(static_cast<int>(AudioPriority::Background)),
                        float(static_cast<int>(AudioPriority::Critical))),
        ENG_FIELD(E, loop, FieldType::Bool),
        ENG_FIELD(E, streaming, FieldType::Bool),
        ENG_FIELD(E, spatialized, FieldType::Bool),
        ENG_FIELD(E, playing, FieldType::Bool),
        ENG_FIELD(E, stealable, FieldType::Bool),
    };
    return {f, int(std::size(f))};
}

template <> FieldSpan fieldsOf<ecs::AudioListener>()
{
    static const Field f[] = {
        ENG_FIELD_RANGE(ecs::AudioListener, priority, FieldType::Int, -100.0f,
                        100.0f),
        ENG_FIELD(ecs::AudioListener, active, FieldType::Bool),
    };
    return {f, int(std::size(f))};
}

template <> FieldSpan fieldsOf<ecs::RigidBody>()
{
    static const Field f[] = {
        ENG_FIELD_RANGE(ecs::RigidBody, mass, FieldType::Float, 0.01f, 1000.0f),
        ENG_FIELD_RANGE(ecs::RigidBody, gravityFactor, FieldType::Float, 0.0f, 4.0f),
        ENG_FIELD_RANGE(ecs::RigidBody, friction, FieldType::Float, 0.0f, 2.0f),
        ENG_FIELD_RANGE(ecs::RigidBody, restitution, FieldType::Float, 0.0f, 1.0f),
        ENG_FIELD(ecs::RigidBody, kinematic, FieldType::Bool),
        ENG_FIELD(ecs::RigidBody, continuous, FieldType::Bool),
    };
    return {f, int(std::size(f))};
}

namespace ecs {


std::size_t copyEntities(entt::registry& dst, const entt::registry& src,
                         const ComponentRegistry& types, uint32_t group)
{
    std::unordered_map<entt::entity, entt::entity> remap;

    // Pass 1: one fresh entity per source entity, carrying every registered
    // component. Round-tripping through the component's own serialiser rather
    // than adding a copy function pointer per type: every serialisable
    // component already has to have that pair, and a second mechanism would be
    // one more thing to forget when a component is added. Level load only --
    // never per frame.
    if (const auto* storage = src.storage<entt::entity>()) {
        for (const entt::entity e : *storage) {
            const entt::entity out = dst.create();
            remap.emplace(e, out);
            if (group != 0)
                dst.emplace<EntityGroup>(out, EntityGroup{group});
            // Only World::create() tags Dirty, and nothing here went through
            // it: a deserialised entity would never have its world transform
            // computed, so it would sit at the origin, silently, while the
            // scene still rendered. Tagged here rather than by each caller
            // because every caller of this function needs it and one forgetting
            // looks like a broken map rather than a missing line.
            dst.emplace<Dirty>(out);
            for (const ComponentType& t : types.types()) {
                if (!t.has || !t.has(src, e) || !t.serialize || !t.deserialize)
                    continue;
                io::ByteWriter w;
                t.serialize(src, e, w);
                io::ByteReader r(w.bytes().data(), w.size(), w.pool());
                t.deserialize(dst, out, r, uint32_t(w.size()));
            }
        }
    }

    // Pass 2: the hierarchy. Parent is the one component that stores an entity
    // id, and it is not in the type table -- the map format writes the graph
    // separately -- so it is rebuilt here through the remap. Without this a
    // copied map came out flat, and a child's world transform silently became
    // its local one.
    //
    // Walks the source storage rather than the remap table: a hash map's order
    // is not reproducible, and child order decides the order nodes are created
    // in, which is a difference a deterministic capture can see.
    if (const auto* storage = src.storage<entt::entity>()) {
        for (const entt::entity from : *storage) {
            const auto* parent = src.try_get<Parent>(from);
            if (!parent || parent->value == entt::null)
                continue;
            const auto to = remap.find(from);
            const auto up = remap.find(parent->value);
            if (to == remap.end() || up == remap.end())
                continue;
            dst.emplace_or_replace<Parent>(to->second, Parent{up->second});
            dst.get_or_emplace<Children>(up->second).value.push_back(to->second);
        }
    }
    return remap.size();
}

void registerEngineComponents(ComponentRegistry& reg)
{
    reg.add({"Name", 1, addDefault<Name>, has<Name>, remove<Name>, serName, deName});
    reg.add({"Transform", 2, addDefault<Transform>, has<Transform>,
             remove<Transform>, serTransform, deTransform});
    reg.add({"MeshRenderer", 3, addDefault<MeshRenderer>, has<MeshRenderer>,
             remove<MeshRenderer>, serMesh, deMesh});
    reg.add({"LightRef", 4, addDefault<LightRef>, has<LightRef>,
             remove<LightRef>, serLight, deLight});

    // Ids 5-9 are the rest of the engine's reservation (kFirstApplicationTypeId
    // is 10). These carry field tables, so each is one line: the payload
    // format, the inspector rows and the add-menu entry all come from
    // fieldsOf<T>() rather than from four hand-written functions.
    reg.add(reflectedComponent<MaterialOverride>("MaterialOverride", 5));
    reg.add(reflectedComponent<ParticleEmitter>("ParticleEmitter", 6));
    reg.add(reflectedComponent<RigidBody>("RigidBody", 7));
    // No fields: a tag is its own value. Still registered, because it has to
    // save, load and appear in the add menu like anything else.
    reg.add({"RenderNode", 8, addDefault<RenderNode>, has<RenderNode>,
             remove<RenderNode>, serTag, deTag<RenderNode>});
    reg.add({"KinematicControl", 9, addDefault<KinematicControl>,
             has<KinematicControl>, remove<KinematicControl>, serTag,
             deTag<KinematicControl>});

    // Ids 10-17 are NOT free: they were handed to the game before the engine
    // ever needed a tenth component, and a stable id is a file format -- every
    // .map already on disk names game::Collider by 10. So the engine continues
    // above them and the reservation moves up (kFirstApplicationTypeId), which
    // costs nothing and keeps both blocks growing without meeting.
    reg.add(reflectedComponent<Visibility>("Visibility", 18));
    reg.add(reflectedComponent<Lifetime>("Lifetime", 19));
    reg.add(reflectedComponent<Spin>("Spin", 20));
    reg.add(reflectedComponent<LightAnimation>("LightAnimation", 21));
    reg.add(reflectedComponent<Camera>("Camera", 22));
    reg.add(reflectedComponent<Orbit>("Orbit", 23));
    // 24 was the one gap left in the engine's block; the player's tuning is
    // exactly the kind of component it was being kept for.
    reg.add(reflectedComponent<FirstPersonController>("FirstPersonController",
                                                      24));
    reg.add(reflectedComponent<ShaderParams>("ShaderParams", 25));
    reg.add(reflectedComponent<PortalParams>("PortalParams", 26));
    reg.add(reflectedComponent<AudioEmitter>("AudioEmitter", 27));
    reg.add(reflectedComponent<AudioListener>("AudioListener", 28));
}

} // namespace ecs
} // namespace eng
