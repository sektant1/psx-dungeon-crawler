#include "GameComponents.h"
#include "MapSerializer.h"
#include "MeshSource.h"

#include <eng/ecs/Components.h>

#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <string>

static void require(bool c, const char* m)
{
    if (!c) { std::cerr << "MapSerializerTests: " << m << '\n'; std::exit(1); }
}

int main()
{
    const std::string path = "map_serializer_test.map";

    entt::registry src;
    entt::entity room = src.create();
    src.emplace<eng::ecs::Name>(room, eng::ecs::Name{"Room"});
    src.emplace<eng::ecs::Transform>(room, glm::vec3(4, 0, 0),
                                     glm::quat(1, 0, 0, 0), glm::vec3(1));
    src.emplace<mapio::MeshSource>(room, mapio::MeshSource{"meshes/tiles/floor.obj"});
    eng::ecs::MeshRenderer mr; mr.material = "Game/DungeonTile"; mr.castShadows = true;
    src.emplace<eng::ecs::MeshRenderer>(room, mr);
    src.emplace<game::Collider>(room, game::Collider{});

    entt::entity torch = src.create();
    src.emplace<eng::ecs::Name>(torch, eng::ecs::Name{"Torch"});
    src.emplace<eng::ecs::Transform>(torch, glm::vec3(0, 2, 0),
                                     glm::quat(1, 0, 0, 0), glm::vec3(1));
    eng::LightDesc ld; ld.type = eng::LightDesc::Type::Point;
    ld.colour = glm::vec3(1, 0.6f, 0.3f); ld.range = 6.5f;
    src.emplace<eng::ecs::LightRef>(torch, eng::ecs::LightRef{ld, {}});
    src.emplace<eng::ecs::Parent>(torch, eng::ecs::Parent{room});

    require(mapio::writeMap(path, src, mapio::coreRegistry()), "write succeeds");

    entt::registry dst;
    require(mapio::readMap(path, dst, mapio::coreRegistry()), "read succeeds");

    int srcCount = 0, dstCount = 0;
    src.view<eng::ecs::Transform>().each([&](auto...) { ++srcCount; });
    dst.view<eng::ecs::Transform>().each([&](auto...) { ++dstCount; });
    require(srcCount == dstCount && dstCount == 2, "entity count round-trips");

    entt::entity dRoom = entt::null, dTorch = entt::null;
    dst.view<eng::ecs::Name>().each([&](entt::entity e, const eng::ecs::Name& n) {
        if (n.value == "Room") dRoom = e;
        if (n.value == "Torch") dTorch = e;
    });
    require(dRoom != entt::null && dTorch != entt::null, "both entities present");
    require(dst.get<eng::ecs::Transform>(dRoom).position == glm::vec3(4, 0, 0),
            "room transform round-trips");
    require(dst.get<mapio::MeshSource>(dRoom).path == "meshes/tiles/floor.obj",
            "mesh path round-trips");
    require(dst.get<eng::ecs::MeshRenderer>(dRoom).material == "Game/DungeonTile",
            "material round-trips");
    require(dst.all_of<game::Collider>(dRoom), "collider round-trips");
    require(dst.get<eng::ecs::LightRef>(dTorch).desc.range == 6.5f,
            "light range round-trips");
    require(dst.get<eng::ecs::Parent>(dTorch).value == dRoom,
            "parent link is remapped correctly");

    mapio::ComponentRegistry tiny;
    for (const mapio::ComponentType& t : mapio::coreRegistry().types())
        if (t.stableTypeId == 2) tiny.add(t);
    entt::registry partial;
    require(mapio::readMap(path, partial, tiny), "read with tiny registry succeeds");
    int partialCount = 0;
    partial.view<eng::ecs::Transform>().each([&](auto...) { ++partialCount; });
    require(partialCount == 2, "all entities load even with unknown components");

    std::remove(path.c_str());
    std::cout << "MapSerializerTests OK\n";
    return 0;
}
