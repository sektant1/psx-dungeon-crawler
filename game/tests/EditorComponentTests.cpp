// The component registry: the editor's "any entity can carry any component"
// model, checked without a window.
//
// The property that matters: a panel never asks what *kind* an entity is. It
// asks the table what the entity has, what it is missing, and how to add or
// drop one -- so adding a component type cannot require touching a panel.

#include "EntityComponents.h"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace ed;
using game::content::Entity;

static void require(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "EditorComponentTests: " << message << '\n';
        std::exit(1);
    }
}

int main()
{
    const std::vector<ComponentType>& types = componentTypes();
    require(!types.empty(), "the table is populated");

    // --- every entry is coherent ------------------------------------------
    for (const ComponentType& type : types) {
        require(type.id && *type.id, "every component has an id");
        require(type.label && *type.label,
                std::string(type.id) + " has a label");
        require(type.has != nullptr, std::string(type.id) + " can be queried");
        // Addable implies removable: a panel that can create a component must
        // be able to undo that without reaching for the undo stack.
        if (type.add)
            require(type.remove != nullptr,
                    std::string(type.id) + " can be removed once added");
        require(findComponentType(type.id) == &type,
                std::string(type.id) + " is findable by id");
    }
    require(findComponentType("no_such_component") == nullptr,
            "an unknown id resolves to nothing");

    // --- add / remove round-trips for every addable component --------------
    ComponentDefaults defaults;
    defaults.prefab = "kit.wall";
    for (const ComponentType& type : types) {
        if (!type.add || (type.addable && !type.addable(defaults)))
            continue;
        Entity entity;
        entity.id = "probe";
        require(!type.has(entity), std::string(type.id) + " starts absent");
        type.add(entity, defaults);
        require(type.has(entity),
                std::string(type.id) + " is present once added");
        type.remove(entity);
        require(!type.has(entity),
                std::string(type.id) + " is gone once removed");
    }

    // --- mesh needs a brush, and says so -----------------------------------
    const ComponentType* mesh = findComponentType("mesh");
    require(mesh && mesh->addable, "mesh gates on the defaults");
    require(!mesh->addable(ComponentDefaults{}),
            "mesh cannot be added without a brush prefab");
    require(mesh->addable(defaults), "mesh can be added with one");

    // --- components compose freely on one entity ---------------------------
    Entity wall;
    wall.id = "wall_0001";
    mesh->add(wall, defaults);
    findComponentType("light")->add(wall, defaults);
    findComponentType("trigger")->add(wall, defaults);
    require(componentsOf(wall).size() == 3,
            "a wall may also be a light and a trigger");
    require(hasComponent(wall, "light"), "hasComponent agrees");
    for (const ComponentType* missing : missingComponents(wall))
        require(!missing->has(wall), "missing components really are missing");
    require(!isGeometry(wall), "gameplay components take it out of the fold");

    // --- geometry is a kit piece and nothing else --------------------------
    Entity plain;
    plain.id = "wall_0002";
    mesh->add(plain, defaults);
    require(isGeometry(plain), "a bare kit piece is geometry");
    findComponentType("collider")->add(plain, defaults);
    require(isGeometry(plain), "a collider on it is still geometry");
    findComponentType("marker")->add(plain, defaults);
    require(!isGeometry(plain), "a marker on it is not");

    // --- dropping a mesh drops what only made sense with it ----------------
    Entity pinned;
    pinned.id = "wall_0003";
    mesh->add(pinned, defaults);
    pinned.material = "Kit/Other";
    pinned.cell = game::content::CellPlacement{};
    mesh->remove(pinned);
    require(pinned.prefab.empty() && pinned.material.empty() && !pinned.cell,
            "removing the mesh clears the material override and the grid cell");

    std::cout << "EditorComponentTests: ok\n";
    return 0;
}
