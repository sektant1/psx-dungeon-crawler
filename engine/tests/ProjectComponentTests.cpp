// Components a project declares in TOML, with no C++.
//
// The property that matters is that a declared component is indistinguishable
// from a compiled-in one to everything downstream: it serialises through the
// same writeMap, reflects through the same Field table, and reads back byte for
// byte. So this test does what the runtime does -- declare, register, author,
// write, read -- and checks the values survive.

#include <eng/ecs/ComponentRegistry.h>
#include <eng/ecs/MapSerializer.h>
#include <eng/runtime/ProjectComponents.h>

#include <entt/entt.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace eng;
using namespace eng::runtime;
namespace fs = std::filesystem;

static void require(bool c, const char* m)
{
    if (!c) {
        std::cerr << "ProjectComponentTests: " << m << '\n';
        std::exit(1);
    }
}

static fs::path scratch()
{
    const fs::path dir = fs::temp_directory_path() / "raven_project_components";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

static fs::path write(const char* name, const std::string& text)
{
    const fs::path path = scratch() / name;
    std::ofstream out(path, std::ios::trunc);
    out << text;
    return path;
}

// A field's bytes, read out of a live component the way the inspector and the
// Lua bindings do: through `instance` plus the offset in `fields`.
template <typename T>
static T fieldValue(const ecs::ComponentType& type, entt::registry& reg,
                    entt::entity e, const char* name)
{
    void* bytes = type.instance(reg, e);
    require(bytes != nullptr, "the component should be present");
    for (int i = 0; i < type.fieldCount; ++i) {
        if (std::strcmp(type.fields[i].name, name) != 0)
            continue;
        T value{};
        std::memcpy(&value,
                    static_cast<const std::byte*>(bytes) + type.fields[i].offset,
                    sizeof(T));
        return value;
    }
    require(false, "no such field");
    return T{};
}

static const ecs::ComponentType& typeNamed(const ecs::ComponentRegistry& reg,
                                           const char* name)
{
    for (const ecs::ComponentType& type : reg.types())
        if (std::strcmp(type.name, name) == 0)
            return type;
    require(false, "no such component");
    std::abort();
}

static void testDeclareRegisterAndRoundTrip()
{
    const fs::path file = write("components.toml", R"(
[component.Health]
id = 64
fields.max = { type = "float", default = 100.0, min = 0.0, max = 999.0 }
fields.current = { type = "float", default = 100.0 }
fields.invulnerable = { type = "bool", default = false }

[component.Team]
id = 65
fields.index = { type = "int", default = 1 }
fields.colour = { type = "colour", default = [1.0, 0.5, 0.25] }
)");

    ProjectComponents declared;
    std::string error;
    require(declared.load(file, error), error.c_str());
    require(declared.schemas().size() == 2, "both components load");

    ecs::ComponentRegistry registry;
    ecs::registerEngineComponents(registry);
    require(declared.registerInto(registry, error), error.c_str());

    const ecs::ComponentType& health = typeNamed(registry, "Health");
    require(health.stableTypeId == 64, "the declared id is used");
    require(health.fieldCount == 3, "every field is reflected");

    // Adding one gives it the declared defaults, which is what makes the
    // inspector's "add component" produce something sensible.
    entt::registry reg;
    const entt::entity e = reg.create();
    health.addDefault(reg, e);
    require(health.has(reg, e), "it is present after being added");
    require(fieldValue<float>(health, reg, e, "max") == 100.0f,
            "a float default is honoured");
    require(fieldValue<uint8_t>(health, reg, e, "invulnerable") == 0,
            "and a bool one");

    // Author a value the way the inspector would: straight into the bytes at
    // the reflected offset.
    void* bytes = health.instance(reg, e);
    for (int i = 0; i < health.fieldCount; ++i) {
        if (std::strcmp(health.fields[i].name, "current") == 0) {
            const float wounded = 37.5f;
            std::memcpy(static_cast<std::byte*>(bytes) + health.fields[i].offset,
                        &wounded, sizeof(float));
        }
    }

    const ecs::ComponentType& team = typeNamed(registry, "Team");
    team.addDefault(reg, e);
    require(fieldValue<int32_t>(team, reg, e, "index") == 1, "int default");
    const glm::vec3 colour = fieldValue<glm::vec3>(team, reg, e, "colour");
    require(colour.x == 1.0f && colour.y == 0.5f && colour.z == 0.25f,
            "a vector default is honoured");

    // Through the same .map codec every compiled-in component uses.
    const std::string map = (scratch() / "declared.map").string();
    require(ecs::writeMap(map, reg, registry), "the scene writes");

    entt::registry loaded;
    require(ecs::readMap(map, loaded, registry), "and reads back");
    entt::entity restored = entt::null;
    for (const entt::entity candidate : loaded.view<entt::entity>()) {
        restored = candidate;
        break;
    }
    require(restored != entt::null, "the entity survives");
    require(health.has(loaded, restored), "and carries the declared component");
    require(fieldValue<float>(health, loaded, restored, "current") == 37.5f,
            "with the value that was authored, not the default");
    require(fieldValue<float>(health, loaded, restored, "max") == 100.0f,
            "and the untouched fields too");
    const glm::vec3 back = fieldValue<glm::vec3>(team, loaded, restored,
                                                 "colour");
    require(back.x == 1.0f && back.z == 0.25f, "a second component too");
}

// Two components must not share a storage, or one would overwrite the other.
static void testComponentsAreIndependent()
{
    const fs::path file = write("independent.toml", R"(
[component.Alpha]
id = 64
fields.value = { type = "int", default = 1 }
[component.Beta]
id = 65
fields.value = { type = "int", default = 2 }
)");

    ProjectComponents declared;
    std::string error;
    require(declared.load(file, error), error.c_str());
    ecs::ComponentRegistry registry;
    ecs::registerEngineComponents(registry);
    require(declared.registerInto(registry, error), error.c_str());

    const ecs::ComponentType& alpha = typeNamed(registry, "Alpha");
    const ecs::ComponentType& beta = typeNamed(registry, "Beta");

    entt::registry reg;
    const entt::entity e = reg.create();
    alpha.addDefault(reg, e);
    require(alpha.has(reg, e), "alpha is there");
    require(!beta.has(reg, e), "and beta is not -- separate storages");

    beta.addDefault(reg, e);
    require(fieldValue<int32_t>(alpha, reg, e, "value") == 1,
            "adding beta does not overwrite alpha");
    require(fieldValue<int32_t>(beta, reg, e, "value") == 2, "or vice versa");

    alpha.remove(reg, e);
    require(!alpha.has(reg, e), "removing one...");
    require(beta.has(reg, e), "...leaves the other");
}

static void testRefusals()
{
    ProjectComponents declared;
    std::string error;

    // A missing file is the normal case, not an error.
    require(declared.load(scratch() / "no-such-file.toml", error),
            "a project that declares nothing is fine");
    require(declared.empty(), "and declares nothing");

    // An id in the engine's reserved range would make a project's scenes
    // decode as engine components.
    require(!declared.load(write("lowid.toml", R"(
[component.Bad]
id = 3
fields.x = { type = "float" }
)"), error), "a reserved id is refused");
    require(error.find("id must be") != std::string::npos, "and says why");

    // Strings: refused with the reason and the alternative, rather than
    // half-supported.
    require(!declared.load(write("stringfield.toml", R"(
[component.Bad]
id = 64
fields.name = { type = "string" }
)"), error), "a string field is refused");
    require(error.find("propert") != std::string::npos,
            "and points at entity properties instead");

    require(!declared.load(write("nofields.toml", R"(
[component.Bad]
id = 64
)"), error), "a component with no fields is refused");

    require(!declared.load(write("dupe.toml", R"(
[component.A]
id = 64
fields.x = { type = "float" }
[component.B]
id = 64
fields.x = { type = "float" }
)"), error), "two components sharing an id are refused");

    // Colliding with an engine component is caught at registration, which is
    // the only place that can see the other table.
    ProjectComponents clash;
    require(clash.load(write("clash.toml", R"(
[component.Mine]
id = 64
fields.x = { type = "float" }
)"), error), error.c_str());
    ecs::ComponentRegistry registry;
    ecs::registerEngineComponents(registry);
    // 64 is free in the engine's table; register twice to force the collision.
    require(clash.registerInto(registry, error), error.c_str());
    require(!clash.registerInto(registry, error),
            "registering the same id twice is refused");
}

int main()
{
    testDeclareRegisterAndRoundTrip();
    testComponentsAreIndependent();
    testRefusals();
    std::puts("ProjectComponentTests: ok");
    return 0;
}
