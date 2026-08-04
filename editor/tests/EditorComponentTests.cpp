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
    defaults.meshPath = "meshes/props/Chair.obj";
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

    // --- the three ways to be geometry ------------------------------------
    //
    // A kit piece, a mesh file and a primitive all end as one MeshRenderer, and
    // an entity carrying two of them is refused by the scene format. The table
    // is what stops one being authored: each of the three only applies while
    // the other two are absent.
    const ComponentType* prefab = findComponentType("prefab");
    const ComponentType* mesh = findComponentType("mesh");
    const ComponentType* primitive = findComponentType("primitive");
    require(prefab && mesh && primitive, "all three geometry components exist");

    require(prefab->addable, "the kit piece gates on the defaults");
    require(!prefab->addable(ComponentDefaults{}),
            "a kit piece cannot be added without a brush prefab");
    require(prefab->addable(defaults), "it can be added with one");
    require(mesh->addable && !mesh->addable(ComponentDefaults{}),
            "a mesh cannot be added without a path from the catalogue");
    require(mesh->addable(defaults), "it can be added with one");
    require(primitive->addable && primitive->addable(ComponentDefaults{}),
            "a primitive needs nothing: the engine generates it");

    // Mutual exclusion, in both directions and for every pair.
    const ComponentType* geometry[] = {prefab, mesh, primitive};
    for (const ComponentType* held : geometry) {
        Entity entity;
        entity.id = "geometry";
        held->add(entity, defaults);
        require(held->has(entity), std::string(held->id) + " is present");
        for (const ComponentType* other : geometry) {
            if (other == held)
                continue;
            require(other->applies && !other->applies(entity),
                    std::string(other->id) + " is not offered beside " +
                        held->id);
        }
        require(held->applies && held->applies(entity),
                std::string(held->id) + " still applies to itself");
        // And it is not offered a second time, nor are its rivals.
        for (const ComponentType* offered : missingComponents(entity))
            require(offered != held, "a component already held is not offered");
    }

    // A primitive added from the menu is the unit box: the one shape whose
    // defaults need no explanation, and the thing every greybox starts as.
    Entity block;
    primitive->add(block, defaults);
    require(block.primitive &&
                block.primitive->kind == eng::ecs::PrimitiveMesh::Box,
            "a fresh primitive is a box");
    require(block.primitive->size == glm::vec3(1.0f),
            "and a unit one, so adding it changes nothing on screen");

    // A mesh takes the browser's path rather than inventing one.
    Entity chair;
    mesh->add(chair, defaults);
    require(chair.mesh && chair.mesh->path == defaults.meshPath,
            "a fresh mesh names what the catalogue selected");
    require(chair.mesh->importScale == 1.0f,
            "and is not silently rescaled");

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
    prefab->add(wall, defaults);
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
    prefab->add(plain, defaults);
    require(isGeometry(plain), "a bare kit piece is geometry");
    findComponentType("collider")->add(plain, defaults);
    require(isGeometry(plain), "a collider on it is still geometry");
    findComponentType("marker")->add(plain, defaults);
    require(!isGeometry(plain), "a marker on it is not");

    // --- dropping geometry drops what only made sense with it --------------
    Entity pinned;
    pinned.id = "wall_0003";
    prefab->add(pinned, defaults);
    pinned.material = "Kit/Other";
    pinned.cell = game::content::CellPlacement{};
    prefab->remove(pinned);
    require(pinned.prefab.empty() && pinned.material.empty() && !pinned.cell,
            "removing the kit piece clears the material override and the cell");

    // The same rule for the other two. A material is chosen for a mesh -- an
    // atlas material tuned to a wall means nothing on the sphere that replaces
    // it -- so it goes with the geometry rather than outliving it.
    Entity dropped;
    dropped.id = "prop_0001";
    mesh->add(dropped, defaults);
    dropped.material = "Game/PropTerracotta";
    mesh->remove(dropped);
    require(!dropped.mesh && dropped.material.empty(),
            "removing a mesh clears the material it was wearing");
    primitive->add(dropped, defaults);
    dropped.material = "Game/PropTerracotta";
    primitive->remove(dropped);
    require(!dropped.primitive && dropped.material.empty(),
            "removing a primitive clears the material it was wearing");

    std::cout << "EditorComponentTests: ok\n";
    return 0;
}
