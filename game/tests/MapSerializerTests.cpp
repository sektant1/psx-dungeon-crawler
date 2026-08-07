#include "GameComponents.h"
#include "ComponentRegistry.h"
#include <eng/ecs/MapSerializer.h>
#include <eng/ecs/components/MeshSource.h>

#include <eng/ecs/Components.h>

#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

static void require(bool c, const char* m)
{
    if (!c) { std::cerr << "MapSerializerTests: " << m << '\n'; std::exit(1); }
}

static std::vector<uint8_t> readBytes(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in),
            std::istreambuf_iterator<char>()};
}

static void writeBytes(const std::string& path, const std::vector<uint8_t>& bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              std::streamsize(bytes.size()));
}

int main()
{
    const std::string path = "map_serializer_test.map";

    entt::registry src;
    entt::entity room = src.create();
    src.emplace<eng::ecs::Name>(room, eng::ecs::Name{"Room"});
    src.emplace<eng::ecs::Transform>(room, glm::vec3(4, 0, 0),
                                     glm::quat(1, 0, 0, 0), glm::vec3(1));
    src.emplace<eng::ecs::MeshSource>(room, eng::ecs::MeshSource{"meshes/tiles/floor.obj"});
    eng::ecs::MeshRenderer mr; mr.material = "Game/DungeonTile"; mr.castShadows = true;
    src.emplace<eng::ecs::MeshRenderer>(room, mr);
    game::Collider collider;
    collider.sensor = true;
    src.emplace<game::Collider>(room, collider);

    entt::entity torch = src.create();
    src.emplace<eng::ecs::Name>(torch, eng::ecs::Name{"Torch"});
    src.emplace<eng::ecs::Transform>(torch, glm::vec3(0, 2, 0),
                                     glm::quat(1, 0, 0, 0), glm::vec3(1));
    eng::LightDesc ld; ld.type = eng::LightDesc::Type::Point;
    ld.colour = glm::vec3(1, 0.6f, 0.3f); ld.range = 6.5f;
    src.emplace<eng::ecs::LightRef>(torch, eng::ecs::LightRef{ld, {}});
    src.emplace<eng::ecs::Parent>(torch, eng::ecs::Parent{room});

    require(eng::ecs::writeMap(path, src, mapio::coreRegistry()), "write succeeds");

    entt::registry dst;
    require(eng::ecs::readMap(path, dst, mapio::coreRegistry()), "read succeeds");

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
    require(dst.get<eng::ecs::MeshSource>(dRoom).path == "meshes/tiles/floor.obj",
            "mesh path round-trips");
    require(dst.get<eng::ecs::MeshRenderer>(dRoom).material == "Game/DungeonTile",
            "material round-trips");
    require(dst.all_of<game::Collider>(dRoom), "collider round-trips");
    require(dst.get<game::Collider>(dRoom).sensor,
            "collider sensor policy round-trips");
    require(dst.get<eng::ecs::LightRef>(dTorch).desc.range == 6.5f,
            "light range round-trips");
    require(dst.get<eng::ecs::Parent>(dTorch).value == dRoom,
            "parent link is remapped correctly");
    const auto& children = dst.get<eng::ecs::Children>(dRoom).value;
    require(children.size() == 1 && children.front() == dTorch,
            "parent children bookkeeping is reconstructed");

    const std::string deterministicPath = "map_serializer_deterministic.map";
    require(eng::ecs::writeMap(deterministicPath, src, mapio::coreRegistry()),
            "second deterministic write succeeds");
    require(readBytes(path) == readBytes(deterministicPath),
            "same registry cooks to identical map bytes");

    eng::ecs::ComponentRegistry tiny;
    for (const eng::ecs::ComponentType& t : mapio::coreRegistry().types())
        if (t.stableTypeId == 2) tiny.add(t);
    entt::registry partial;
    require(eng::ecs::readMap(path, partial, tiny), "read with tiny registry succeeds");
    int partialCount = 0;
    partial.view<eng::ecs::Transform>().each([&](auto...) { ++partialCount; });
    require(partialCount == 2, "all entities load even with unknown components");

    const std::vector<uint8_t> validBytes = readBytes(path);
    std::vector<uint8_t> truncated = validBytes;
    truncated.pop_back();
    const std::string corruptPath = "map_serializer_corrupt.map";
    writeBytes(corruptPath, truncated);
    entt::registry preserved;
    const entt::entity sentinel = preserved.create();
    preserved.emplace<eng::ecs::Name>(sentinel, eng::ecs::Name{"keep"});
    require(!eng::ecs::readMap(corruptPath, preserved, mapio::coreRegistry()),
            "truncated component payload is rejected");
    require(preserved.valid(sentinel) &&
                preserved.get<eng::ecs::Name>(sentinel).value == "keep",
            "failed read leaves the destination registry unchanged");

    std::vector<uint8_t> oldVersion = validBytes;
    oldVersion[8] = 0;
    const std::string oldVersionPath = "map_serializer_old_version.map";
    writeBytes(oldVersionPath, oldVersion);
    require(!eng::ecs::readMap(oldVersionPath, preserved, mapio::coreRegistry()),
            "versions without an explicit reader are rejected");

    entt::registry cyclic;
    const entt::entity cycleA = cyclic.create();
    const entt::entity cycleB = cyclic.create();
    cyclic.emplace<eng::ecs::Transform>(cycleA);
    cyclic.emplace<eng::ecs::Transform>(cycleB);
    cyclic.emplace<eng::ecs::Parent>(cycleA, eng::ecs::Parent{cycleB});
    cyclic.emplace<eng::ecs::Parent>(cycleB, eng::ecs::Parent{cycleA});
    require(!eng::ecs::writeMap("map_serializer_cycle.map", cyclic,
                             mapio::coreRegistry()),
            "cyclic hierarchy is rejected before writing");

    entt::registry incompleteMesh;
    const entt::entity meshOnly = incompleteMesh.create();
    incompleteMesh.emplace<eng::ecs::Transform>(meshOnly);
    incompleteMesh.emplace<eng::ecs::MeshRenderer>(meshOnly);
    require(!eng::ecs::writeMap("map_serializer_bad_mesh.map", incompleteMesh,
                             mapio::coreRegistry()),
            "persisted MeshRenderer requires a source asset reference");

    require(eng::ecs::dumpMap(path, mapio::coreRegistry()), "dumpMap succeeds on a valid file");
    require(!eng::ecs::dumpMap("does_not_exist.map", mapio::coreRegistry()),
            "dumpMap fails on a missing file");

    std::remove(path.c_str());
    std::remove(deterministicPath.c_str());
    std::remove(corruptPath.c_str());
    std::remove(oldVersionPath.c_str());
    std::cout << "MapSerializerTests OK\n";
    return 0;
}
