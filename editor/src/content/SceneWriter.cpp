#include <editor/content/SceneWriter.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace game::content {
namespace {

using Json = nlohmann::ordered_json; // ordered: key order is part of the format

// 4 decimals is finer than a millimetre at this scale and coarse enough that a
// drag which lands on the grid writes 2 rather than 1.9999999.
float canonical(float value)
{
    if (!std::isfinite(value))
        return 0.0f;
    const float rounded = std::round(value * 10000.0f) / 10000.0f;
    return rounded == 0.0f ? 0.0f : rounded; // kills -0
}

Json vec3(const glm::vec3& v)
{
    return Json::array({canonical(v.x), canonical(v.y), canonical(v.z)});
}

bool nearlyEqual(const glm::vec3& a, const glm::vec3& b)
{
    return canonical(a.x) == canonical(b.x) && canonical(a.y) == canonical(b.y) &&
           canonical(a.z) == canonical(b.z);
}

// A reflected component, written as only what differs from its defaults.
//
// One helper for every component whose authored type is the runtime one: the
// portal parameters, the first-person controller and the viewmodel rig were
// three copies of this loop, and a fourth component should not add a fourth.
// Writing only the differences is what keeps a scene file readable -- a rig
// that spells out fourteen numbers on an entity that changed one is a diff
// nobody can review.
template <typename T> Json reflectedNode(const T& value)
{
    const T defaults;
    Json node = Json::object();
    const eng::FieldSpan fields = eng::fieldsOf<T>();
    for (int i = 0; i < fields.count; ++i) {
        const eng::Field& f = fields.data[i];
        const void* now = eng::fieldPtr(&value, f);
        const void* was = eng::fieldPtr(&defaults, f);
        switch (f.type) {
        case eng::FieldType::Float: {
            const float v = *static_cast<const float*>(now);
            if (canonical(v) != canonical(*static_cast<const float*>(was)))
                node[f.name] = canonical(v);
            break;
        }
        case eng::FieldType::Vec3:
        case eng::FieldType::Colour: {
            const glm::vec3& v = *static_cast<const glm::vec3*>(now);
            if (!nearlyEqual(v, *static_cast<const glm::vec3*>(was)))
                node[f.name] = vec3(v);
            break;
        }
        case eng::FieldType::Bool: {
            const bool v = *static_cast<const bool*>(now);
            if (v != *static_cast<const bool*>(was))
                node[f.name] = v;
            break;
        }
        case eng::FieldType::Int: {
            const int v = *static_cast<const int*>(now);
            if (v != *static_cast<const int*>(was))
                node[f.name] = v;
            break;
        }
        case eng::FieldType::String: {
            const std::string& v = *static_cast<const std::string*>(now);
            if (v != *static_cast<const std::string*>(was))
                node[f.name] = v;
            break;
        }
        case eng::FieldType::Quat:
            break; // no reflected component carries one yet
        }
    }
    return node;
}

const char* edgeName(CellPlacement::Edge edge)
{
    switch (edge) {
    case CellPlacement::Edge::North: return "north";
    case CellPlacement::Edge::East: return "east";
    case CellPlacement::Edge::South: return "south";
    case CellPlacement::Edge::West: return "west";
    case CellPlacement::Edge::None: break;
    }
    return nullptr;
}

const char* audioBusName(int value)
{
    switch (static_cast<eng::AudioBus>(value)) {
    case eng::AudioBus::Master:   return "master";
    case eng::AudioBus::Music:    return "music";
    case eng::AudioBus::Ambience: return "ambience";
    case eng::AudioBus::Dialogue: return "dialogue";
    case eng::AudioBus::Weapons:  return "weapons";
    case eng::AudioBus::Sfx:      return "sfx";
    case eng::AudioBus::Ui:       return "ui";
    case eng::AudioBus::Warnings: return "warnings";
    case eng::AudioBus::Count:    break;
    }
    return "sfx";
}

const char* audioPriorityName(int value)
{
    if (value <= static_cast<int>(eng::AudioPriority::Background))
        return "background";
    if (value <= static_cast<int>(eng::AudioPriority::Low))
        return "low";
    if (value <= static_cast<int>(eng::AudioPriority::Normal))
        return "normal";
    if (value <= static_cast<int>(eng::AudioPriority::Important))
        return "important";
    return "critical";
}

// The generated mesh, written as its kind plus only the parameters that kind
// actually uses.
//
// Not reflectedNode: `kind` is a name in the file and an int in the component,
// and writing every field that differs from the defaults would spell out a
// sphere's `size` and a box's `segments` -- numbers the generator never reads,
// which read as settings that do nothing.
Json writePrimitive(const PrimitiveAuthor& p)
{
    using P = eng::ecs::PrimitiveMesh;
    const P defaults;
    Json node = Json::object();
    node["kind"] = eng::ecs::primitiveKindName(p.kind);

    const bool usesSize = p.kind == P::Box || p.kind == P::BeveledBox ||
                          p.kind == P::Plane;
    const bool round = p.kind == P::Sphere || p.kind == P::Capsule ||
                       p.kind == P::Cylinder || p.kind == P::Cone ||
                       p.kind == P::Disc;
    const bool usesHeight = p.kind == P::Capsule || p.kind == P::Cylinder ||
                            p.kind == P::Cone;
    const auto number = [&](const char* key, float value, float was) {
        if (canonical(value) != canonical(was))
            node[key] = canonical(value);
    };
    if (usesSize && !nearlyEqual(p.size, defaults.size))
        node["size"] = vec3(p.size);
    if (round)
        number("radius", p.radius, defaults.radius);
    if (usesHeight)
        number("height", p.height, defaults.height);
    if (p.kind == P::BeveledBox)
        number("bevel", p.bevel, defaults.bevel);
    if (canonical(p.thickness) != canonical(defaults.thickness))
        node["thickness"] = canonical(p.thickness);
    if (round && p.rings != defaults.rings)
        node["rings"] = p.rings;
    if (round && p.segments != defaults.segments)
        node["segments"] = p.segments;
    if (p.subdivisions != defaults.subdivisions)
        node["subdivisions"] = p.subdivisions;
    if (p.inwardFacing)
        node["inward_facing"] = true;
    return node;
}

Json writeEntity(const Entity& entity)
{
    Json out = Json::object();
    out["id"] = entity.id;
    if (!entity.name.empty() && entity.name != entity.id)
        out["name"] = entity.name;
    // Omitted when there is none, so every scene authored before hierarchies
    // existed still writes byte-identical to what it was read from.
    if (!entity.parent.empty())
        out["parent"] = entity.parent;
    if (!entity.prefab.empty())
        out["prefab"] = entity.prefab;
    if (entity.mesh) {
        Json mesh = Json::object();
        mesh["path"] = entity.mesh->path;
        if (canonical(entity.mesh->importScale) != 1.0f)
            mesh["import_scale"] = canonical(entity.mesh->importScale);
        out["mesh"] = std::move(mesh);
    }
    if (entity.primitive)
        out["primitive"] = writePrimitive(*entity.primitive);
    if (!entity.material.empty())
        out["material"] = entity.material;
    if (!entity.castShadows)
        out["cast_shadows"] = false;

    const XformAuthor& xform = entity.transform;
    Json transform = Json::object();
    if (!nearlyEqual(xform.position, glm::vec3(0.0f)))
        transform["position"] = vec3(xform.position);
    if (!nearlyEqual(xform.rotationDegrees, glm::vec3(0.0f)))
        transform["rotation_degrees"] = vec3(xform.rotationDegrees);
    if (!nearlyEqual(xform.scale, glm::vec3(1.0f)))
        transform["scale"] = vec3(xform.scale);
    if (!transform.empty())
        out["transform"] = std::move(transform);

    if (entity.cell) {
        const CellPlacement& cell = *entity.cell;
        Json node = Json::object();
        node["col"] = cell.col;
        node["row"] = cell.row;
        if (const char* edge = edgeName(cell.edge))
            node["edge"] = edge;
        if (cell.span != 1)
            node["span"] = cell.span;
        if (cell.yawQuarters != 0)
            node["yaw_quarters"] = cell.yawQuarters;
        if (canonical(cell.level) != 0.0f)
            node["level"] = canonical(cell.level);
        out["cell"] = std::move(node);
    }
    if (entity.collider) {
        Json node = Json::object();
        node["shape"] = "box";
        node["half_extents"] = vec3(entity.collider->halfExtents);
        if (!nearlyEqual(entity.collider->offset, glm::vec3(0.0f)))
            node["offset"] = vec3(entity.collider->offset);
        out["collider"] = std::move(node);
    }
    if (entity.light) {
        const LightAuthor& light = *entity.light;
        Json node = Json::object();
        node["type"] =
            light.type == LightAuthor::Type::Directional ? "directional" : "point";
        node["colour"] = vec3(light.colour);
        if (light.type == LightAuthor::Type::Point)
            node["range"] = canonical(light.range);
        if (light.castShadows)
            node["cast_shadows"] = true;
        if (light.animation) {
            Json animation = Json::object();
            switch (light.animation->mode) {
            case LightAnimAuthor::Mode::Steady: animation["mode"] = "steady"; break;
            case LightAnimAuthor::Mode::Flicker: animation["mode"] = "flicker"; break;
            case LightAnimAuthor::Mode::Pulse: animation["mode"] = "pulse"; break;
            }
            animation["speed"] = canonical(light.animation->speed);
            animation["amount"] = canonical(light.animation->amount);
            if (canonical(light.animation->phase) != 0.0f)
                animation["phase"] = canonical(light.animation->phase);
            node["animation"] = std::move(animation);
        }
        out["light"] = std::move(node);
    }
    if (entity.camera) {
        const CameraAuthor& camera = *entity.camera;
        Json node = Json::object();
        node["fov_degrees"] = canonical(camera.fovDegrees);
        // Clip planes and priority written only when they are not the default:
        // a scene full of `"near_clip": 0.05` is noise in every diff, and the
        // parser fills the defaults back in.
        if (canonical(camera.nearClip) != canonical(CameraAuthor{}.nearClip))
            node["near_clip"] = canonical(camera.nearClip);
        if (canonical(camera.farClip) != canonical(CameraAuthor{}.farClip))
            node["far_clip"] = canonical(camera.farClip);
        if (camera.priority != 0)
            node["priority"] = camera.priority;
        if (!camera.active)
            node["active"] = false;
        out["camera"] = std::move(node);
    }
    if (!entity.scripts.empty()) {
        // Written in full rather than as a diff against defaults, unlike
        // reflectedNode: there is no meaningful "default script list", and a
        // half-written entry would be a scene that silently lost a prop.
        Json list = Json::array();
        for (const ScriptAuthor& script : entity.scripts) {
            Json node = Json::object();
            node["path"] = script.path;
            if (!script.enabled)
                node["enabled"] = false; // true is the default
            if (!script.props.empty()) {
                Json props = Json::object();
                for (const ScriptPropAuthor& p : script.props) {
                    switch (p.type) {
                    case ScriptPropAuthor::Type::Bool:
                        props[p.key] = p.boolValue;
                        break;
                    case ScriptPropAuthor::Type::Number:
                        props[p.key] = canonical(p.numberValue);
                        break;
                    case ScriptPropAuthor::Type::String:
                        props[p.key] = p.stringValue;
                        break;
                    case ScriptPropAuthor::Type::Vec3:
                        props[p.key] = vec3(p.vecValue);
                        break;
                    case ScriptPropAuthor::Type::Entity: {
                        // Tagged, so the type survives the round trip: a bare
                        // string would read back as a String prop.
                        Json target = Json::object();
                        target["entity"] = p.stringValue;
                        props[p.key] = std::move(target);
                        break;
                    }
                    }
                }
                node["props"] = std::move(props);
            }
            list.push_back(std::move(node));
        }
        out["scripts"] = std::move(list);
    }
    if (entity.spin) {
        const SpinAuthor& spin = *entity.spin;
        Json node = Json::object();
        if (!nearlyEqual(spin.axis, SpinAuthor{}.axis))
            node["axis"] = vec3(spin.axis);
        node["degrees_per_second"] = canonical(spin.degreesPerSecond);
        out["spin"] = std::move(node);
    }
    if (entity.particles) {
        const ParticleAuthor& fx = *entity.particles;
        const ParticleAuthor defaults;
        Json node = Json::object();
        if (fx.effect != defaults.effect)
            node["effect"] = fx.effect;
        if (!nearlyEqual(fx.offset, defaults.offset))
            node["offset"] = vec3(fx.offset);
        if (fx.playing != defaults.playing)
            node["playing"] = fx.playing;
        if (fx.scale != defaults.scale)
            node["scale"] = canonical(fx.scale);
        out["particles"] = std::move(node);
    }
    if (entity.audio) {
        const AudioEmitterAuthor& audio = *entity.audio;
        const AudioEmitterAuthor defaults;
        Json node = Json::object();
        if (!audio.source.empty())
            node["source"] = audio.source;
        if (!nearlyEqual(audio.offset, defaults.offset))
            node["offset"] = vec3(audio.offset);
        if (audio.bus != defaults.bus)
            node["bus"] = audioBusName(audio.bus);
        if (audio.gainDb != defaults.gainDb)
            node["gain_db"] = canonical(audio.gainDb);
        if (audio.pitch != defaults.pitch)
            node["pitch"] = canonical(audio.pitch);
        if (audio.minDistance != defaults.minDistance)
            node["min_distance"] = canonical(audio.minDistance);
        if (audio.maxDistance != defaults.maxDistance)
            node["max_distance"] = canonical(audio.maxDistance);
        if (audio.rolloff != defaults.rolloff)
            node["rolloff"] = canonical(audio.rolloff);
        if (audio.dopplerFactor != defaults.dopplerFactor)
            node["doppler_factor"] = canonical(audio.dopplerFactor);
        if (audio.priority != defaults.priority)
            node["priority"] = audioPriorityName(audio.priority);
        if (audio.loop != defaults.loop)
            node["loop"] = audio.loop;
        if (audio.streaming != defaults.streaming)
            node["stream"] = audio.streaming;
        if (audio.spatialized != defaults.spatialized)
            node["spatial"] = audio.spatialized;
        if (audio.playing != defaults.playing)
            node["autostart"] = audio.playing;
        if (audio.stealable != defaults.stealable)
            node["stealable"] = audio.stealable;
        out["audio"] = std::move(node);
    }
    if (entity.actor)
        out["actor"] = game::actorKindName(*entity.actor);
    if (entity.sounds) {
        // Only the rows an author actually overrode. An empty row means "the
        // cue this actor's type plays", so writing it as "" would turn a
        // default into an authored silence on the next read.
        Json node = Json::object();
        for (const game::ActorActionInfo& info : game::actorActions()) {
            const std::string& cue = entity.sounds->cue(info.action);
            if (!cue.empty())
                node[info.id] = cue;
        }
        out["sounds"] = std::move(node);
    }
    if (entity.audioListener) {
        const AudioListenerAuthor& listener = *entity.audioListener;
        const AudioListenerAuthor defaults;
        Json node = Json::object();
        if (listener.priority != defaults.priority)
            node["priority"] = listener.priority;
        if (listener.active != defaults.active)
            node["active"] = listener.active;
        out["audio_listener"] = std::move(node);
    }
    if (entity.portal)
        out["portal"] = reflectedNode(*entity.portal);
    if (entity.firstPerson)
        out["first_person"] = reflectedNode(*entity.firstPerson);
    if (entity.viewmodelRig)
        out["viewmodel_rig"] = reflectedNode(*entity.viewmodelRig);
    if (entity.shader) {
        const ShaderAuthor& shader = *entity.shader;
        const ShaderAuthor defaults;
        Json node = Json::object();
        // Only what differs from the default. A scene file that spells out six
        // uniforms on every entity is one nobody reads, and the diff of a
        // one-value change would be six lines.
        if (!nearlyEqual(shader.tint, defaults.tint))
            node["tint"] = vec3(shader.tint);
        if (shader.opacity != defaults.opacity)
            node["opacity"] = canonical(shader.opacity);
        if (!nearlyEqual(shader.rimColour, defaults.rimColour))
            node["rim_colour"] = vec3(shader.rimColour);
        if (shader.rimStrength != defaults.rimStrength)
            node["rim_strength"] = canonical(shader.rimStrength);
        if (shader.rimPower != defaults.rimPower)
            node["rim_power"] = canonical(shader.rimPower);
        if (shader.alphaScissor != defaults.alphaScissor)
            node["alpha_scissor"] = canonical(shader.alphaScissor);
        out["shader"] = std::move(node);
    }
    if (entity.orbit) {
        const OrbitAuthor& orbit = *entity.orbit;
        Json node = Json::object();
        // Defaults are omitted: a scene full of `"centre": [0,0,0]` is noise in
        // every diff, and the parser fills them back in.
        if (!nearlyEqual(orbit.centre, OrbitAuthor{}.centre))
            node["centre"] = vec3(orbit.centre);
        if (!nearlyEqual(orbit.axis, OrbitAuthor{}.axis))
            node["axis"] = vec3(orbit.axis);
        node["radius"] = canonical(orbit.radius);
        node["degrees_per_second"] = canonical(orbit.degreesPerSecond);
        if (canonical(orbit.phaseDegrees) != 0.0f)
            node["phase_degrees"] = canonical(orbit.phaseDegrees);
        if (canonical(orbit.height) != 0.0f)
            node["height"] = canonical(orbit.height);
        switch (orbit.facing) {
        case OrbitAuthor::Facing::Free: break; // the default
        case OrbitAuthor::Facing::Centre: node["facing"] = "centre"; break;
        case OrbitAuthor::Facing::Travel: node["facing"] = "travel"; break;
        }
        out["orbit"] = std::move(node);
    }
    if (entity.playerSpawn)
        out["player_spawn"] = true;
    if (entity.exitYawDegrees) {
        Json node = Json::object();
        node["yaw_degrees"] = canonical(*entity.exitYawDegrees);
        out["exit"] = std::move(node);
    }
    if (entity.enemySpawn)
        out["enemy_spawn"] = *entity.enemySpawn;
    if (entity.pickup)
        out["pickup"] = *entity.pickup;
    if (entity.trigger) {
        Json node = Json::object();
        node["size"] = vec3(entity.trigger->size);
        node["event"] = entity.trigger->event;
        out["trigger"] = std::move(node);
    }
    if (entity.marker)
        out["marker"] = *entity.marker;
    return out;
}

} // namespace

std::string serializeSceneSource(const SceneDocument& document)
{
    // Sorted by id, so the file order never depends on the order things were
    // created in this session.
    std::vector<const Entity*> sorted;
    sorted.reserve(document.entities.size());
    for (const Entity& entity : document.entities)
        sorted.push_back(&entity);
    std::sort(sorted.begin(), sorted.end(),
              [](const Entity* a, const Entity* b) { return a->id < b->id; });

    Json root = Json::object();
    root["$schema"] = "../schemas/scene.schema.json";
    root["format"] = "raven-scene";
    root["version"] = 2;
    root["id"] = document.id;
    // Omitted when unset, so every scene written before levels could carry a
    // palette still round-trips byte for byte.
    if (!document.palette.empty())
        root["palette"] = document.palette;
    Json entities = Json::array();
    for (const Entity* entity : sorted)
        entities.push_back(writeEntity(*entity));
    root["entities"] = std::move(entities);

    return root.dump(2) + "\n";
}

bool writeSceneSource(const std::string& path, const SceneDocument& document,
                      std::string& error)
{
    error.clear();
    for (const Entity& entity : document.entities) {
        // An absolute path in a scene is how the previous editor produced maps
        // that only opened on the machine that authored them.
        if (!entity.prefab.empty() &&
            std::filesystem::path(entity.prefab).is_absolute()) {
            error = "entity '" + entity.id + "' has an absolute prefab path";
            return false;
        }
    }

    const std::string text = serializeSceneSource(document);
    const std::filesystem::path target(path);
    const std::filesystem::path temp =
        target.parent_path() / (target.filename().string() + ".tmp");
    {
        std::ofstream output(temp, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = path + ": cannot open for writing";
            return false;
        }
        output.write(text.data(), std::streamsize(text.size()));
        if (!output) {
            error = path + ": write failed";
            return false;
        }
    }
    std::error_code code;
    std::filesystem::rename(temp, target, code);
    if (code) {
        std::filesystem::remove(temp, code);
        error = path + ": atomic rename failed";
        return false;
    }
    return true;
}

} // namespace game::content
