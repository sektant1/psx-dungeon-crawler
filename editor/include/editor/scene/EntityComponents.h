#pragma once
#include <editor/content/SceneDocument.h>

#include <string>
#include <string_view>
#include <cstddef>
#include <optional>
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

// The entities the game defines that are not kit pieces.
//
// Lives here rather than inside EditorApp because the Place tool's brush names
// one, and the brush is editor *state*: an enum nested in the app class meant
// every consumer of it had to include the whole application header.
enum class Gameplay {
    // A transform and nothing else, to hang other entities from. Composing a
    // room's dressing needs a parent that is not itself a barrel: without one,
    // "move all of this" means picking one prop to be the root and then being
    // unable to delete it without taking the rest.
    Group,
    PlayerSpawn,
    Portal,
    Exit,
    Marker,
    EnemySpawn,
    Pickup,
    Trigger,
    AudioEmitter,
    PointLight,
    DirectionalLight,
};

// The author-facing name, for the Catalog list, the brush readout and the undo
// label. One table, so those three can never disagree about what a thing is
// called.
const char* gameplayName(Gameplay kind);

// Whether the Place tool can paint this kind under the cursor.
//
// False for the directional light alone: it is a scene-wide key light that is
// aimed rather than positioned, and painting a row of them down a corridor is
// not a thing anyone means to do. It stays a one-shot button.
bool gameplayIsPaintable(Gameplay kind);

// Every paintable kind, in the order the Catalog lists them.
const std::vector<Gameplay>& paintableGameplay();

// What the Place tool will drop next.
//
// A variant rather than a prefab string, because a light and a wall are the
// same gesture to the author -- point at the level, click -- and were two
// different ones in the editor purely because a light has no mesh to name. The
// yaw lives here too: it is a property of the thing about to be placed, and as
// a stray member on the app it was writable by nobody and read by three
// callers.
struct Brush {
    enum class Kind { Piece, Gameplay, Particles };

    Kind kind = Kind::Piece;
    std::string prefab;                    // Kind::Piece -- a kit.toml id
    Gameplay gameplay = Gameplay::Marker;  // Kind::Gameplay
    std::string effect;                    // Kind::Particles -- particles.toml id
    int yawQuarters = 0;                   // quarter turns about Y

    bool empty() const
    {
        return (kind == Kind::Piece && prefab.empty()) ||
               (kind == Kind::Particles && effect.empty());
    }
    // Quarter turns wrap; there is no "past three quarters".
    void rotate(int quarters)
    {
        yawQuarters = ((yawQuarters + quarters) % 4 + 4) % 4;
    }
    float yawDegrees() const { return float(yawQuarters) * 90.0f; }
};

// What an entity is made of, in the order it should always be read.
//
// The inspector used to show components in table order, which was the order
// they happened to be written in -- so "where is the material" depended on the
// entity, and two entities with the same parts read differently. A fixed
// grouping is what makes the panel scannable: the same question is always
// answered in the same place on the screen.
//
// Order is deliberate, coarse to fine:
//   Appearance  what it looks like. First after the transform, because it is
//               what an author is adjusting most of the time and what the
//               separate Material/Particles docks used to hold.
//   Physical    what it occupies and how it moves.
//   Gameplay    what it means to the game.
//   Placement   how it is pinned to the level's grid -- rarely touched.
enum class ComponentGroup {
    Appearance,
    Physical,
    Gameplay,
    Placement,
};

const char* componentGroupName(ComponentGroup group);

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
    // Which band it is read in. Every list of components -- the inspector, the
    // add menu, the outliner tooltip -- sorts by this, so an entity's parts are
    // always in the same order regardless of what it happens to carry.
    ComponentGroup group = ComponentGroup::Gameplay;
    // Whether this component means anything on this entity. Null -- which is
    // every component but one -- means "anything can carry it", the rule the
    // format has always followed.
    //
    // The exception is the actor sound table: a wall performs no actions, so
    // offering it a cue per action is a page of questions with no answers. A
    // predicate rather than a check inside the panel, so the gate lives next to
    // the component it gates and every list obeys it at once.
    bool (*applies)(const game::content::Entity& entity) = nullptr;
};

// game::content::actorKindOf, re-exported: the panels ask this constantly and
// the answer is a property of the document, not of the editor.
using game::content::actorKindOf;
using game::content::isActor;

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

struct ComponentPresence {
    std::size_t present = 0;
    std::size_t total = 0;

    bool all() const { return total > 0 && present == total; }
    bool none() const { return present == 0; }
    bool mixed() const { return present > 0 && present < total; }
};

ComponentPresence
componentPresence(const ComponentType& type,
                  const std::vector<const game::content::Entity*>& entities);
// Components missing from at least one selected entity. This keeps Add
// Component useful when the primary already has a component another selected
// entity does not.
std::vector<const ComponentType*>
missingComponents(const std::vector<const game::content::Entity*>& entities);

// True when the entity is level geometry: a kit piece carrying no gameplay
// component. The outliner folds these away, because they are most of a level by
// count and the least interesting to click.
bool isGeometry(const game::content::Entity& entity);

} // namespace ed
