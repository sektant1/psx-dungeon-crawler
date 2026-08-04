// The design rule this test enforces: there is exactly ONE cooker. The editor
// calls game::content::cookToMap in-process and CI runs the scene_cook binary,
// and the two must produce identical bytes -- otherwise "it works in the
// editor" and "it works in the build" drift apart, which is the failure mode
// the shared-content-library boundary exists to prevent.
//
// It also pins determinism: cooking the same scene twice must not vary.

#include <editor/content/SceneCook.h>
#include <editor/content/SceneSource.h>
#include "GameComponents.h"
#include "TestAssets.h"

#include <eng/ecs/Components.h>
#include <eng/ecs/components/MeshSource.h>

#include <entt/entt.hpp>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace game::content;

static void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "CookParityTests: " << message << '\n';
        std::exit(1);
    }
}

static std::vector<char> readAll(const std::string& path)
{
    std::ifstream input(path, std::ios::binary);
    require(bool(input), ("cannot read " + path).c_str());
    return std::vector<char>((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
}

int main()
{
    game::test::mountGameAssets();
    const std::string ritual =
        game::test::asset("scenes/ritual_boss_showroom.scn");
    const std::string kit = game::test::asset("config/kit.toml");
    const std::string outDir = COOK_PARITY_OUT_DIR;
    const std::string inProcess = outDir + "/parity_inprocess.map";
    const std::string viaCli = outDir + "/parity_cli.map";
    const std::string again = outDir + "/parity_again.map";

    SceneDocument document;
    KitCatalog catalog;
    std::string error;
    require(loadSceneSource(ritual, document, error), error.c_str());
    require(KitCatalog::load(kit, catalog, error), error.c_str());

    require(cookToMap(document, catalog, inProcess, error), error.c_str());
    require(cookToMap(document, catalog, again, error), error.c_str());
    require(readAll(inProcess) == readAll(again),
            "cooking the same scene twice gives the same bytes");

    const std::string command = std::string(SCENE_COOK_EXE) + " " + ritual +
                                " --kit " + kit + " --out " + viaCli +
                                " > /dev/null";
    require(std::system(command.c_str()) == 0, "scene_cook CLI succeeds");
    require(readAll(inProcess) == readAll(viaCli),
            "the CLI and the in-process cook produce identical maps");

    // A scene naming a prefab the kit does not have must be refused, not
    // silently cooked with a hole in it.
    SceneDocument broken = document;
    Entity ghost;
    ghost.id = "zzz_ghost";
    ghost.prefab = "kit.not_a_real_piece";
    broken.add(ghost);
    require(!cookToMap(broken, catalog, outDir + "/parity_broken.map", error),
            "an unresolved prefab blocks the cook");
    require(error.find("unresolved") != std::string::npos,
            "and the error names the problem");

    // Compound prefab parts come from kit.toml, not repeated scene entities or
    // viewer-only arguments. Attached parts inherit parent transform and shader.
    {
        SceneDocument compound;
        Entity boss;
        boss.id = "boss";
        boss.prefab = "kit.prop_boss_placeholder";
        boss.shader = ShaderAuthor{};
        compound.add(boss);

        entt::registry registry;
        require(buildRegistry(compound, catalog, registry, error), error.c_str());
        int meshes = 0;
        int attached = 0;
        for (const entt::entity entity : registry.view<eng::ecs::MeshSource>()) {
            ++meshes;
            if (registry.all_of<eng::ecs::Parent,
                                eng::ecs::ShaderParams>(entity))
                ++attached;
        }
        require(meshes == 2, "boss prefab cooks body and sword");
        require(attached == 1,
                "sword is attached and inherits subject shader parameters");
    }

    // The two non-kit ways to be geometry, cooked.
    //
    // Both end as the same pair of runtime components a prefab produces -- a
    // MeshRenderer saying what it wears, plus whatever says where the geometry
    // comes from -- so nothing downstream of the cooker has to know which of
    // the three an author chose. That equivalence is the whole design; without
    // this test it is a comment.
    {
        SceneDocument scene;
        Entity chair;
        chair.id = "chair";
        chair.mesh = MeshAuthor{"meshes/props/Chair.obj", 0.5f};
        chair.material = "Game/PropTerracotta";
        chair.transform.scale = glm::vec3(2.0f);
        scene.add(chair);

        Entity block;
        block.id = "block";
        block.primitive = PrimitiveAuthor{};
        block.primitive->kind = eng::ecs::PrimitiveMesh::Capsule;
        block.primitive->radius = 0.35f;
        block.material = "Game/Kit/Dungeon";
        scene.add(block);

        entt::registry registry;
        require(buildRegistry(scene, catalog, registry, error), error.c_str());

        entt::entity chairEntity = entt::null;
        entt::entity blockEntity = entt::null;
        int renderers = 0;
        for (const entt::entity e : registry.view<eng::ecs::MeshRenderer>()) {
            ++renderers;
            if (registry.all_of<eng::ecs::MeshSource>(e))
                chairEntity = e;
            else if (registry.all_of<eng::ecs::PrimitiveMesh>(e))
                blockEntity = e;
        }
        require(renderers == 2, "both entities cook to a MeshRenderer");
        require(chairEntity != entt::null,
                "a mesh entity cooks to MeshSource, the same component a "
                "prefab produces");
        require(blockEntity != entt::null,
                "a primitive entity cooks to PrimitiveMesh instead");
        require(registry.get<eng::ecs::MeshSource>(chairEntity).path ==
                    "meshes/props/Chair.obj",
                "carrying the authored path unchanged");
        require(registry.get<eng::ecs::MeshRenderer>(chairEntity).material ==
                    "Game/PropTerracotta",
                "and the authored material");
        // The import scale multiplies the transform rather than replacing it,
        // exactly as a kit piece's does: it is a fact about the file, and an
        // author who fixes it once should not have to redo it per instance.
        require(registry.get<eng::ecs::Transform>(chairEntity).scale.x == 1.0f,
                "import scale multiplies the authored transform scale");
        require(registry.get<eng::ecs::PrimitiveMesh>(blockEntity).kind ==
                    eng::ecs::PrimitiveMesh::Capsule,
                "the primitive's parameters survive the cook");
        require(registry.get<eng::ecs::PrimitiveMesh>(blockEntity).radius ==
                    0.35f,
                "including its dimensions");
        require(!registry.all_of<eng::ecs::MeshSource>(blockEntity),
                "and a generated mesh names no file");
    }

    // Architecture collides by virtue of being architecture. A .scn authors a
    // `collider` only as an exception, so without this the cooked map is a
    // room the player falls straight through -- which is exactly what every
    // cooked scene was before floors and walls got their implicit slabs.
    {
        SceneDocument slab;
        Entity tile;
        tile.id = "floor_a";
        tile.prefab = "kit.floor";
        slab.add(tile);

        entt::registry registry;
        require(buildRegistry(slab, catalog, registry, error), error.c_str());
        int colliders = 0;
        glm::vec3 half{0.0f};
        float y = 0.0f;
        for (const entt::entity e : registry.view<game::Collider>()) {
            ++colliders;
            half = registry.get<game::Collider>(e).size;
            y = registry.get<eng::ecs::Transform>(e).position.y;
        }
        require(colliders == 1, "a floor tile cooks to exactly one collider");
        require(half.x >= 1.9f && half.z >= 1.9f,
                "wide enough to carry the whole cell");
        require(y < 0.0f, "and hung under the surface it draws");

        // An authored collider replaces the implicit one rather than adding to
        // it, so a scene can still make one piece passable.
        SceneDocument overridden = slab;
        overridden.entities.front().collider =
            ColliderAuthor{{0.25f, 0.25f, 0.25f}, {0.0f, 1.0f, 0.0f}};
        entt::registry second;
        require(buildRegistry(overridden, catalog, second, error), error.c_str());
        int overrides = 0;
        for (const entt::entity e : second.view<game::Collider>()) {
            ++overrides;
            require(second.get<game::Collider>(e).size.x < 0.5f,
                    "the authored size wins");
        }
        require(overrides == 1, "and it does not stack with the implicit one");
    }

    std::cout << "CookParityTests: ok\n";
    return 0;
}
