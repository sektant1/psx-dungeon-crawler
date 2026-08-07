// eng::runtime::SceneRuntime: reading a cooked .map into a world with the
// engine's own component table.
//
// The property under test is the one that makes a project playable: the
// runtime is handed a component table rather than knowing one, so a scene
// written with a richer vocabulary still loads through the poorer one, minus
// exactly the components the poorer one has never heard of.
//
// Headless: no renderer, no window, no physics. Everything here is registry
// work, which is what makes it cheap enough to be exhaustive.

#include <eng/ecs/ComponentRegistry.h>
#include <eng/ecs/Components.h>
#include <eng/ecs/MapSerializer.h>
#include <eng/ecs/World.h>
#include <eng/ecs/components/FirstPersonController.h>
#include <eng/runtime/SceneRuntime.h>

#include <entt/entt.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

static void require(bool c, const char* m)
{
    if (!c) {
        std::cerr << "SceneRuntimeTests: " << m << '\n';
        std::exit(1);
    }
}

static fs::path scratch(const char* name)
{
    const fs::path dir = fs::temp_directory_path() / "raven_scene_runtime_test";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir / name;
}

// A component the engine's table has never heard of, standing in for anything
// an application registers on top -- a game's Exit, a project's own type.
struct Unknown {
    float value = 0.0f;
};

namespace eng {
template <> FieldSpan fieldsOf<Unknown>()
{
    static const Field f[] = {ENG_FIELD(Unknown, value, FieldType::Float)};
    return {f, int(std::size(f))};
}
} // namespace eng

// The engine's table plus one application component, which is the shape every
// real application's table has.
static const eng::ecs::ComponentRegistry& richRegistry()
{
    static const eng::ecs::ComponentRegistry table = [] {
        eng::ecs::ComponentRegistry reg;
        eng::ecs::registerEngineComponents(reg);
        reg.add(eng::ecs::reflectedComponent<Unknown>(
            "Unknown", eng::ecs::kFirstApplicationTypeId));
        return reg;
    }();
    return table;
}

// Writes a scene with a floor, a named entity, an authored first-person rig
// and one application-only component.
static std::string writeScene()
{
    entt::registry reg;

    const entt::entity floor = reg.create();
    reg.emplace<eng::ecs::Name>(floor, eng::ecs::Name{"Floor"});
    reg.emplace<eng::ecs::Transform>(floor).position = {0.0f, -0.1f, 0.0f};

    const entt::entity rig = reg.create();
    reg.emplace<eng::ecs::Name>(rig, eng::ecs::Name{"Player"});
    reg.emplace<eng::ecs::Transform>(rig).position = {3.0f, 1.5f, -2.0f};
    reg.emplace<eng::ecs::FirstPersonController>(rig).active = true;

    const entt::entity odd = reg.create();
    reg.emplace<eng::ecs::Name>(odd, eng::ecs::Name{"Odd"});
    reg.emplace<eng::ecs::Transform>(odd);
    reg.emplace<Unknown>(odd, Unknown{42.0f});

    const std::string path = scratch("scene.map").string();
    require(eng::ecs::writeMap(path, reg, richRegistry()), "the scene should write");
    return path;
}

static void testLoadWithTheEngineTable()
{
    const std::string path = writeScene();

    eng::ecs::World world;
    eng::runtime::SceneRuntime scene(world, 0u, eng::ecs::engineRegistry());
    require(scene.load(path), "a scene should load with the engine table");

    // Every entity arrives; only the component the table does not know is
    // dropped. That is the difference between "the player can open somebody's
    // scene" and "the player refuses scenes it did not author".
    require(scene.registry().view<const eng::ecs::Name>().size() == 3,
            "all three entities load");
    require(scene.registry().view<const Unknown>().empty(),
            "the unknown component is skipped, not refused");

    // The authored rig is an engine component, so the engine table reads it --
    // which is what makes playerSpawn work for a project with no game markers.
    const auto rig = scene.rig();
    require(rig.controller.has_value(), "the authored first-person rig is read");
    require(!rig.thirdPerson.has_value(), "no third-person rig was authored");
    require(!rig.screen.has_value(), "no screen camera was authored");
    require(!scene.hasAuthoredCamera(), "no camera was authored");

    const glm::vec3 spawn = scene.playerSpawn();
    require(spawn.x == 3.0f && spawn.z == -2.0f,
            "the spawn comes from the authored rig");
}

static void testLoadWithTheApplicationTable()
{
    const std::string path = writeScene();

    eng::ecs::World world;
    eng::runtime::SceneRuntime scene(world, 0u, richRegistry());
    require(scene.load(path), "the same scene loads with the richer table");
    require(scene.registry().view<const Unknown>().size() == 1,
            "the application component survives its own table");
}

static void testMissingAndMalformed()
{
    eng::ecs::World world;
    eng::runtime::SceneRuntime scene(world, 0u, eng::ecs::engineRegistry());
    require(!scene.load(scratch("does-not-exist.map").string()),
            "a missing file is a failed load");
    require(scene.registry().view<const eng::ecs::Name>().empty(),
            "a failed load adds nothing");

    // A spawn with no scene at all: just above the origin, which is where a
    // floor built at y=0 wants the player.
    const glm::vec3 spawn = scene.playerSpawn();
    require(spawn.y > 0.0f, "the fallback spawn is above the floor");
}

static void testBuildHookRunsBeforeSync()
{
    const std::string path = writeScene();

    eng::ecs::World world;
    eng::runtime::SceneRuntime scene(world, 0u, eng::ecs::engineRegistry());
    require(scene.load(path), "the scene loads");

    // The seam an application turns its own authored components into engine
    // ones through. It must see the loaded scene, which is the whole reason it
    // runs where it does.
    int seen = 0;
    scene.buildAll([&seen](entt::registry& reg) {
        seen = int(reg.view<const eng::ecs::Name>().size());
    });
    require(seen == 3, "the build hook sees the loaded scene");
}

static void testGroupTagging()
{
    const std::string path = writeScene();

    eng::ecs::World world;
    eng::runtime::SceneRuntime scene(world, 7u, eng::ecs::engineRegistry());
    require(scene.load(path), "the scene loads into a group");
    // What makes a scene change destroy exactly this scene's entities and
    // leave the player standing.
    require(scene.registry().view<const eng::ecs::EntityGroup>().size() == 3,
            "every entity is stamped with the group");
}

static void testMapHasCamera()
{
    const std::string path = writeScene();
    require(!eng::runtime::mapHasCamera(path, eng::ecs::engineRegistry()),
            "a scene with no camera reports none");
    require(!eng::runtime::mapHasCamera(scratch("nope.map").string(),
                                        eng::ecs::engineRegistry()),
            "a missing file reports no camera rather than throwing");

    entt::registry reg;
    const entt::entity shot = reg.create();
    reg.emplace<eng::ecs::Transform>(shot);
    reg.emplace<eng::ecs::Camera>(shot);
    const std::string withCamera = scratch("shot.map").string();
    require(eng::ecs::writeMap(withCamera, reg, eng::ecs::engineRegistry()),
            "the shot writes");
    require(eng::runtime::mapHasCamera(withCamera, eng::ecs::engineRegistry()),
            "an authored camera is found");
}

int main()
{
    testLoadWithTheEngineTable();
    testLoadWithTheApplicationTable();
    testMissingAndMalformed();
    testBuildHookRunsBeforeSync();
    testGroupTagging();
    testMapHasCamera();
    std::puts("SceneRuntimeTests: ok");
    return 0;
}
