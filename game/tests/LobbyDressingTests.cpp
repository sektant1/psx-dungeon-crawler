#include "LobbyDressing.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "LobbyDressingTests: " << message << '\n';
        std::exit(1);
    }
}
} // namespace

int main()
{
    const std::string path = "/tmp/lobby_dressing_test.toml";
    {
        std::ofstream f(path);
        f << "[[prop]]\n"
             "meshes = [\"prop_jutesack.obj\"]\n"
             "materials = [\"Game/PropJute\"]\n"
             "position = [1.5, 0.0, -2.0]\n"
             "yaw = 30.0\n"
             "cast_shadows = false\n"
             "\n"
             "[[prop]]\n"
             "meshes = [\"prop_barrel_p0.obj\", \"prop_barrel_p1.obj\"]\n"
             "materials = [\"Game/PropPlanks\", \"Game/PropBauerhaus\"]\n"
             "position = [-8.8, 0.0, 4.5]\n";
    }

    std::vector<LobbyProp> props;
    std::string err;
    require(parseLobbyDressing(path, props, err),
            "well-formed file must parse");
    require(props.size() == 2, "two [[prop]] entries expected");

    // First: single-mesh sack with an explicit yaw and cast_shadows override.
    require(props[0].meshes.size() == 1, "prop 0 has one mesh");
    require(props[0].materials.size() == 1, "prop 0 has one material");
    require(props[0].meshes[0] == "prop_jutesack.obj", "prop 0 mesh name");
    require(props[0].position.x == 1.5f && props[0].position.z == -2.0f,
            "prop 0 position parsed");
    require(props[0].yawDeg == 30.0f, "prop 0 yaw parsed");
    require(props[0].castShadows == false, "prop 0 cast_shadows override");

    // Second: two-part barrel; yaw + scale default, cast_shadows default true.
    require(props[1].meshes.size() == 2, "prop 1 has two meshes");
    require(props[1].materials.size() == 2, "prop 1 has two materials");
    require(props[1].yawDeg == 0.0f, "prop 1 yaw defaults to 0");
    require(props[1].scale.x == 1.0f && props[1].scale.y == 1.0f &&
                props[1].scale.z == 1.0f,
            "prop 1 scale defaults to 1");
    require(props[1].castShadows == true, "prop 1 cast_shadows defaults true");

    std::remove(path.c_str());
    std::cout << "LobbyDressingTests: OK\n";
    return 0;
}
