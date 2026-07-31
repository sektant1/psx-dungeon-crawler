#pragma once
#include "SceneDocument.h"

#include <string>
#include <string_view>
#include <vector>

namespace ed {

// The authored entity, seen as a bag of components.
//
// game::content::Entity stores its parts as optional fields rather than an
// entt::registry -- the document has to load headless and survive undo by id --
// but the *authoring* model is the ECS one the cooker emits: any entity may
// carry any component, and SceneCook turns each field it finds into its runtime
// component independently. Nothing in the format couples "is a light" to "is
// not a wall".
//
// This table is the single description of that set. A panel asks it what an
// entity has, what it could still be given, and how to add or drop one; it
// never switches over entity kinds. Adding a component type is one entry here
// plus one drawer in the inspector.
struct ComponentDefaults {
    // The Place tool's current brush, which is the only sane default for a
    // mesh: prefab ids come from kit.toml, so the table cannot invent one.
    std::string prefab;
};

struct ComponentType {
    const char* id;    // stable key; matches the .scn field name
    const char* label; // inspector header and add-menu row
    const char* hint;  // one line under the add-menu row
    bool (*has)(const game::content::Entity& entity);
    // Null when the component cannot be created from the menu (a grid cell is
    // derived from placement, not authored by hand).
    void (*add)(game::content::Entity& entity,
                const ComponentDefaults& defaults);
    // Null for the parts every entity always has (id, name, transform).
    void (*remove)(game::content::Entity& entity);
    // Whether `add` can run right now given `defaults` -- Mesh needs a brush.
    bool (*addable)(const ComponentDefaults& defaults);
};

// The table, in the order panels should present it.
const std::vector<ComponentType>& componentTypes();
const ComponentType* findComponentType(std::string_view id);

bool hasComponent(const game::content::Entity& entity, std::string_view id);
// Everything on `entity`, in table order.
std::vector<const ComponentType*>
componentsOf(const game::content::Entity& entity);
// Everything it could still be given, in table order. Components with no `add`
// never appear here.
std::vector<const ComponentType*>
missingComponents(const game::content::Entity& entity);

// True when the entity is level geometry: a kit piece carrying no gameplay
// component. The outliner folds these away, because they are most of a level by
// count and the least interesting to click.
bool isGeometry(const game::content::Entity& entity);

} // namespace ed
