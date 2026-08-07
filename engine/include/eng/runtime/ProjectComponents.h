#pragma once

#include <eng/Reflect.h>
#include <eng/ecs/ComponentRegistry.h>

#include <glm/glm.hpp>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace eng::runtime {

// Components a project declares for itself, in TOML, with no C++.
//
// Until this existed the component vocabulary was fixed at compile time: the
// engine's set, plus whatever the application registered. That was the last
// thing in the authoring track that a project could not do -- somebody making a
// game with this engine could author scenes, script behaviour and ship a build,
// but could not say "my enemies have Health" without editing the engine.
//
//     [component.Health]
//     id = 64
//     fields.max     = { type = "float", default = 100.0, min = 0.0, max = 999.0 }
//     fields.current = { type = "float", default = 100.0 }
//
// The payload is a flat byte buffer laid out exactly like a C++ struct would
// be, which is the whole trick: eng::Field already addresses fields by offset,
// so the generic serialiser, the inspector and the Lua component bindings all
// work on a declared component with no special case anywhere. A project
// component and an engine one are indistinguishable to everything downstream.
//
// That layout is also why v1 takes only POD field types. A std::string inside a
// byte buffer needs construction, destruction and a copy that knows about it;
// every other type is bytes. `string` is diagnosed rather than half-supported,
// and an author who needs one has `properties` on the entity already.

// One declared field.
struct SchemaField {
    std::string name;
    FieldType type = FieldType::Float;
    // Inspector range, for Int and Float. max <= min means unbounded.
    float min = 0.0f;
    float max = 0.0f;
    // The value a freshly added component carries. Bool/Int/Float read
    // `number`; Vec3/Colour read `vec`.
    double number = 0.0;
    glm::vec3 vec{0.0f};

    // Where this field sits in the payload, and how big it is. Assigned when
    // the schema is loaded, by the same rules a compiler would use.
    uint16_t offset = 0;
    uint16_t size = 0;
};

struct ComponentSchema {
    std::string name;
    // Stable across saves, exactly like an engine component's: it is what a
    // .map records, so renumbering one reinterprets every scene on disk.
    // Must be >= eng::ecs::kFirstApplicationTypeId.
    uint16_t stableTypeId = 0;
    std::vector<SchemaField> fields;
    // Total payload size, and the entt storage this component lives in --
    // one storage per declared component, keyed by a hash of the name, which
    // is what lets many declared components share one C++ payload type.
    uint16_t payloadBytes = 0;
    uint32_t storageId = 0;
};

// The declared set for one project. Owns the storage the registry's `const
// char*` names and `const Field*` tables point at, so it must outlive any
// ComponentRegistry it has been registered into.
class ProjectComponents {
public:
    ProjectComponents();
    ~ProjectComponents();
    ProjectComponents(ProjectComponents&&) noexcept;
    ProjectComponents& operator=(ProjectComponents&&) noexcept;
    ProjectComponents(const ProjectComponents&) = delete;
    ProjectComponents& operator=(const ProjectComponents&) = delete;

    // Reads `file`. A missing file is NOT an error -- a project that declares
    // no components is the normal case -- and leaves the set empty.
    //
    // Anything malformed IS an error, and the whole file is refused rather than
    // half-applied: a component that silently lost a field would corrupt every
    // scene saved after it.
    bool load(const std::filesystem::path& file, std::string& error);

    // The file a project declares its components in.
    static constexpr const char* kFileName = "components.toml";

    // Appends every declared component to `reg`. Fails on an id that collides
    // with something already in it, which is the one mistake that silently
    // reinterprets existing scenes.
    bool registerInto(ecs::ComponentRegistry& reg, std::string& error) const;

    const std::vector<ComponentSchema>& schemas() const;
    bool empty() const;

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

// The payload every declared component is stored as: its fields, laid out at
// the offsets the schema assigned.
//
// One C++ type for all of them, told apart by which entt storage they are in --
// entt keys storages by an id, not only by type, which is exactly the mechanism
// this needs and the reason no code generation is involved.
struct DynamicComponent {
    std::vector<std::byte> bytes;
};

} // namespace eng::runtime
