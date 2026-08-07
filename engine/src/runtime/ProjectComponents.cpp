#include <eng/runtime/ProjectComponents.h>

#include <eng/Log.h>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <algorithm>
#include <cstring>
#include <deque>

namespace fs = std::filesystem;

namespace eng::runtime {
namespace {

// How big each field type is in the payload, and how it must be aligned.
//
// The same answers a compiler would give for the equivalent struct, because
// that is precisely what the buffer is pretending to be: eng::Field addresses
// by offset, and serializeFields reads a glm::vec3 out of those bytes with a
// memcpy, so a misaligned or mis-sized field is a silently wrong value rather
// than a diagnosed one.
struct Layout {
    uint16_t size;
    uint16_t align;
};

bool layoutOf(FieldType type, Layout& out)
{
    switch (type) {
    case FieldType::Bool:   out = {1, 1}; return true;
    case FieldType::Int:    out = {4, 4}; return true;
    case FieldType::Float:  out = {4, 4}; return true;
    case FieldType::Vec3:
    case FieldType::Colour: out = {12, 4}; return true;
    case FieldType::Quat:   out = {16, 4}; return true;
    case FieldType::String:
        // Deliberately unsupported; see the header. A std::string in a byte
        // buffer needs a constructor, a destructor and a copy that knows about
        // it, and every other type here is bytes.
        return false;
    }
    return false;
}

bool typeFromName(const std::string& name, FieldType& out)
{
    if (name == "bool")   { out = FieldType::Bool;   return true; }
    if (name == "int")    { out = FieldType::Int;    return true; }
    if (name == "float")  { out = FieldType::Float;  return true; }
    if (name == "vec3")   { out = FieldType::Vec3;   return true; }
    if (name == "colour" || name == "color") {
        out = FieldType::Colour;
        return true;
    }
    if (name == "quat")   { out = FieldType::Quat;   return true; }
    return false;
}

uint16_t alignUp(uint16_t value, uint16_t alignment)
{
    const uint16_t remainder = value % alignment;
    return remainder == 0 ? value : uint16_t(value + alignment - remainder);
}

// Writes a field's default into a fresh payload.
void writeDefault(std::byte* bytes, const SchemaField& field)
{
    switch (field.type) {
    case FieldType::Bool: {
        const uint8_t value = field.number != 0.0 ? 1 : 0;
        std::memcpy(bytes + field.offset, &value, 1);
        break;
    }
    case FieldType::Int: {
        const int32_t value = int32_t(field.number);
        std::memcpy(bytes + field.offset, &value, 4);
        break;
    }
    case FieldType::Float: {
        const float value = float(field.number);
        std::memcpy(bytes + field.offset, &value, 4);
        break;
    }
    case FieldType::Vec3:
    case FieldType::Colour:
        std::memcpy(bytes + field.offset, &field.vec, 12);
        break;
    case FieldType::Quat: {
        const float identity[4] = {1.0f, 0.0f, 0.0f, 0.0f};
        std::memcpy(bytes + field.offset, identity, 16);
        break;
    }
    case FieldType::String:
        break; // refused at load
    }
}

// The entt storage a declared component lives in. A hash of the name, so two
// projects' components never collide and a component keeps its storage across
// runs whatever order the file declares things in.
uint32_t storageIdFor(const std::string& name)
{
    return uint32_t(entt::hashed_string::value(name.c_str()));
}

using Storage = entt::storage<DynamicComponent>;

Storage& storageFor(entt::registry& reg, uint32_t id)
{
    return reg.storage<DynamicComponent>(entt::id_type{id});
}

const Storage* storageFor(const entt::registry& reg, uint32_t id)
{
    return reg.storage<DynamicComponent>(entt::id_type{id});
}

} // namespace

struct ProjectComponents::Impl {
    std::vector<ComponentSchema> schemas;
    // Stable storage for what ComponentType points at by raw pointer. A vector
    // that reallocates would dangle every name and field table already handed
    // to a registry, which is why these are deques of stable elements rather
    // than being rebuilt per registration.
    mutable std::deque<std::string> names;
    mutable std::deque<std::vector<Field>> fieldTables;
};

ProjectComponents::ProjectComponents() : mImpl(std::make_unique<Impl>()) {}
ProjectComponents::~ProjectComponents() = default;
ProjectComponents::ProjectComponents(ProjectComponents&&) noexcept = default;
ProjectComponents& ProjectComponents::operator=(ProjectComponents&&) noexcept =
    default;

const std::vector<ComponentSchema>& ProjectComponents::schemas() const
{
    return mImpl->schemas;
}

bool ProjectComponents::empty() const
{
    return mImpl->schemas.empty();
}

bool ProjectComponents::load(const fs::path& file, std::string& error)
{
    error.clear();
    mImpl->schemas.clear();

    std::error_code ec;
    if (!fs::is_regular_file(file, ec))
        return true; // declaring no components is the normal case

    toml::parse_result parsed = toml::parse_file(file.string());
    if (!parsed) {
        error = file.string() + ": " +
                std::string(parsed.error().description());
        return false;
    }

    const toml::table* components = parsed.table()["component"].as_table();
    if (!components) {
        error = file.string() + ": no [component.<Name>] tables";
        return false;
    }

    std::vector<ComponentSchema> loaded;
    for (auto&& [key, value] : *components) {
        const toml::table* entry = value.as_table();
        if (!entry) {
            error = file.string() + ": [component." + std::string(key.str()) +
                    "] is not a table";
            return false;
        }

        ComponentSchema schema;
        schema.name = std::string(key.str());

        const int64_t id = (*entry)["id"].value_or(int64_t(0));
        if (id < int64_t(ecs::kFirstApplicationTypeId) ||
            id > int64_t(UINT16_MAX)) {
            // Below the reservation is where the engine's own ids live, and a
            // project taking one would make its scenes decode as engine
            // components. Named rather than clamped: this is a file format.
            error = schema.name + ": id must be >= " +
                    std::to_string(ecs::kFirstApplicationTypeId);
            return false;
        }
        schema.stableTypeId = uint16_t(id);

        const toml::table* fields = (*entry)["fields"].as_table();
        if (!fields || fields->empty()) {
            error = schema.name + ": declares no fields";
            return false;
        }

        uint16_t offset = 0;
        for (auto&& [fieldKey, fieldValue] : *fields) {
            const toml::table* spec = fieldValue.as_table();
            if (!spec) {
                error = schema.name + "." + std::string(fieldKey.str()) +
                        ": expected { type = ..., default = ... }";
                return false;
            }
            SchemaField field;
            field.name = std::string(fieldKey.str());

            const std::string typeName =
                (*spec)["type"].value_or(std::string("float"));
            if (!typeFromName(typeName, field.type)) {
                error = schema.name + "." + field.name + ": '" + typeName +
                        "' is not a field type (bool, int, float, vec3, "
                        "colour, quat)" +
                        (typeName == "string"
                             ? " -- strings are not supported in a declared "
                               "component; use the entity's properties"
                             : "");
                return false;
            }

            Layout layout{};
            if (!layoutOf(field.type, layout)) {
                error = schema.name + "." + field.name +
                        ": this type cannot live in a declared component";
                return false;
            }

            if (const toml::array* v = (*spec)["default"].as_array()) {
                for (std::size_t axis = 0; axis < 3 && axis < v->size(); ++axis)
                    field.vec[int(axis)] =
                        float((*v)[axis].value_or(0.0));
            } else {
                field.number = (*spec)["default"].value_or(0.0);
            }
            field.min = float((*spec)["min"].value_or(0.0));
            field.max = float((*spec)["max"].value_or(0.0));

            offset = alignUp(offset, layout.align);
            field.offset = offset;
            field.size = layout.size;
            offset = uint16_t(offset + layout.size);

            schema.fields.push_back(std::move(field));
        }

        // Rounded to the widest member, as a struct would be. Nothing reads
        // past the last field, but a payload whose size does not match its
        // layout is the kind of thing that only breaks once something else
        // changes.
        schema.payloadBytes = alignUp(offset, 4);
        schema.storageId = storageIdFor(schema.name);
        loaded.push_back(std::move(schema));
    }

    // Ids unique within the file. Across the engine's table too, but that is
    // registerInto's job -- it is the one that can see the other table.
    for (std::size_t i = 0; i < loaded.size(); ++i) {
        for (std::size_t j = i + 1; j < loaded.size(); ++j) {
            if (loaded[i].stableTypeId == loaded[j].stableTypeId) {
                error = loaded[i].name + " and " + loaded[j].name +
                        " share id " +
                        std::to_string(loaded[i].stableTypeId);
                return false;
            }
        }
    }

    mImpl->schemas = std::move(loaded);
    log::info("project: %zu declared component(s) from %s",
              mImpl->schemas.size(), file.c_str());
    return true;
}

bool ProjectComponents::registerInto(ecs::ComponentRegistry& reg,
                                     std::string& error) const
{
    error.clear();
    for (const ComponentSchema& schema : mImpl->schemas) {
        for (const ecs::ComponentType& existing : reg.types()) {
            if (existing.stableTypeId == schema.stableTypeId) {
                error = schema.name + " uses id " +
                        std::to_string(schema.stableTypeId) +
                        ", which is already " + existing.name;
                return false;
            }
        }

        // Stable copies for the raw pointers ComponentType holds.
        mImpl->names.push_back(schema.name);
        const char* name = mImpl->names.back().c_str();

        mImpl->fieldTables.emplace_back();
        std::vector<Field>& table = mImpl->fieldTables.back();
        table.reserve(schema.fields.size());
        for (const SchemaField& field : schema.fields) {
            mImpl->names.push_back(field.name);
            table.push_back(Field{mImpl->names.back().c_str(), field.type,
                                  field.offset, field.min, field.max});
        }

        const uint32_t storage = schema.storageId;
        const uint16_t bytes = schema.payloadBytes;
        const std::vector<SchemaField> fields = schema.fields;

        ecs::ComponentType type;
        type.name = name;
        type.stableTypeId = schema.stableTypeId;
        type.fields = table.data();
        type.fieldCount = int(table.size());

        type.addDefault = [storage, bytes, fields](entt::registry& r,
                                                   entt::entity e) {
            DynamicComponent value;
            value.bytes.assign(bytes, std::byte{0});
            for (const SchemaField& field : fields)
                writeDefault(value.bytes.data(), field);
            Storage& s = storageFor(r, storage);
            if (s.contains(e))
                s.erase(e);
            s.emplace(e, std::move(value));
        };
        type.has = [storage](const entt::registry& r, entt::entity e) {
            const Storage* s = storageFor(r, storage);
            return s != nullptr && s->contains(e);
        };
        type.remove = [storage](entt::registry& r, entt::entity e) {
            Storage& s = storageFor(r, storage);
            if (s.contains(e))
                s.erase(e);
        };
        type.instance = [storage](entt::registry& r,
                                  entt::entity e) -> void* {
            Storage& s = storageFor(r, storage);
            if (!s.contains(e))
                return nullptr;
            return s.get(e).bytes.data();
        };
        // The payload is written as raw bytes rather than field by field. It
        // is already a flat POD block, its layout is recorded in the schema
        // that produced it, and a byte blob makes the .map for a declared
        // component exactly as cheap as one for a compiled-in component.
        type.serialize = [storage](const entt::registry& r, entt::entity e,
                                   io::ByteWriter& w) {
            const Storage* s = storageFor(r, storage);
            if (s == nullptr || !s->contains(e))
                return;
            const DynamicComponent& value = s->get(e);
            for (const std::byte b : value.bytes)
                w.u8(uint8_t(b));
        };
        type.deserialize = [storage, bytes, fields](entt::registry& r,
                                                    entt::entity e,
                                                    io::ByteReader& b,
                                                    uint32_t payloadBytes) {
            DynamicComponent value;
            value.bytes.assign(bytes, std::byte{0});
            // Defaults first, so a payload written before a field was added
            // decodes with that field at its default rather than at zero --
            // the same forward compatibility a hand-written deserialiser gets
            // from checking payloadBytes.
            for (const SchemaField& field : fields)
                writeDefault(value.bytes.data(), field);

            const uint32_t take = std::min<uint32_t>(payloadBytes, bytes);
            for (uint32_t i = 0; i < take; ++i)
                value.bytes[i] = std::byte{b.u8()};
            // Anything the file carries past what this schema knows about is
            // read and dropped: the stream has to be left where the reader
            // expects it, or every component after this one decodes garbage.
            for (uint32_t i = take; i < payloadBytes; ++i)
                (void)b.u8();

            Storage& s = storageFor(r, storage);
            if (s.contains(e))
                s.erase(e);
            s.emplace(e, std::move(value));
        };

        reg.add(type);
    }
    return true;
}

} // namespace eng::runtime
