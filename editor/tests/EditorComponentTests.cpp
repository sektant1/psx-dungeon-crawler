// The component registry: the editor's "any entity can carry any component"
// model, checked without a window.
//
// The property that matters: a panel never asks what *kind* an entity is. It
// asks the table what the entity has, what it is missing, and how to add or
// drop one -- so adding a component type cannot require touching a panel.

#include <editor/scene/EntityComponents.h>

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

    const ComponentType* portal = findComponentType("portal");
    require(portal && portal->group == ComponentGroup::Appearance,
            "portal parameters are editable appearance");
    Entity membrane;
    portal->add(membrane, defaults);
    require(membrane.portal.has_value(),
            "adding Portal creates the shader parameter block");

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

    // --- component presence across a selection -----------------------------
    Entity second;
    second.id = "second";
    const std::vector<const Entity*> selection = {&wall, &second};
    const ComponentPresence lightPresence =
        componentPresence(*findComponentType("light"), selection);
    require(lightPresence.present == 1 && lightPresence.total == 2 &&
                lightPresence.mixed(),
            "selection presence reports a component carried by only one item");
    bool offersLight = false;
    for (const ComponentType* missing : missingComponents(selection))
        offersLight = offersLight || std::string(missing->id) == "light";
    require(offersLight,
            "Add Component still offers what the primary has when another "
            "selected entity is missing it");

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
