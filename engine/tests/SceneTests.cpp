#include <eng/ecs/Scene.h>

#include <cstdlib>
#include <iostream>

using namespace eng::ecs;

static void require(bool c, const char* m) {
    if (!c) { std::cerr << "SceneTests: " << m << '\n'; std::exit(1); }
}

int main() {
    Scene scene;

    const entt::entity a = scene.create("alpha");
    const entt::entity b = scene.create();
    require(scene.registry().valid(a), "created entity is valid");
    require(a != b, "distinct entities");
    require(scene.registry().get<Name>(a).value == "alpha", "name stored");
    require(!scene.registry().all_of<Name>(b), "no Name when unnamed");

    require(scene.registry().all_of<Transform>(a), "entity has Transform");
    require(scene.registry().all_of<Dirty>(a), "new entity is Dirty");

    scene.destroy(a);
    require(!scene.registry().valid(a), "destroyed entity invalid");
    require(scene.registry().valid(b), "sibling survives");

    std::cout << "SceneTests OK\n";
    return 0;
}
