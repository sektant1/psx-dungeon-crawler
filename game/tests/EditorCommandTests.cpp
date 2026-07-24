#include "CommandStack.h"
#include "Commands.h"

#include <eng/ecs/Components.h>

#include <cstdlib>
#include <iostream>

using namespace editor;

static void require(bool c, const char* m)
{
    if (!c) { std::cerr << "EditorCommandTests: " << m << '\n'; std::exit(1); }
}

int main()
{
    entt::registry reg;
    CommandStack stack;

    entt::entity created = entt::null;
    stack.run(makeCreateEntity(reg, "Box", &created));
    require(reg.valid(created), "create command makes a live entity");
    require(reg.all_of<eng::ecs::Transform>(created), "created entity has a transform");

    eng::ecs::Transform moved;
    moved.position = glm::vec3(5, 0, 0);
    stack.run(makeSetTransform(reg, created, moved));
    require(reg.get<eng::ecs::Transform>(created).position == glm::vec3(5, 0, 0),
            "set-transform applies");
    require(stack.undo(), "undo available");
    require(reg.get<eng::ecs::Transform>(created).position == glm::vec3(0, 0, 0),
            "undo restores previous transform");
    require(stack.redo(), "redo available");
    require(reg.get<eng::ecs::Transform>(created).position == glm::vec3(5, 0, 0),
            "redo re-applies transform");

    stack.run(makeDeleteEntity(reg, created));
    require(!reg.valid(created), "delete removes the entity");
    require(stack.undo(), "undo delete");
    bool found = false;
    reg.view<eng::ecs::Transform>().each([&](entt::entity, const eng::ecs::Transform& t) {
        if (t.position == glm::vec3(5, 0, 0)) found = true;
    });
    require(found, "undo delete restores the entity with its components");

    std::cout << "EditorCommandTests OK\n";
    return 0;
}
