#pragma once
#include "KitCatalog.h"
#include "SceneDocument.h"

#include <string>
#include <vector>

namespace ed {

// The outliner's model: entities grouped by what they are.
//
// A blockout is hundreds of entities that differ only by id -- forty
// `kit.wall_stone` rows scroll the one enemy spawn off the panel. Grouping the
// repeats under a single collapsible row is what turns the list back into a
// tree you can read, the way a Godot scene tree reads: a handful of top rows,
// the detail one click away.
//
// Built by value rather than as pointers into the document: the panel runs
// commands (delete, duplicate) while iterating, and a vector<Entity>
// reallocates under them.

// One row for a single entity.
struct OutlinerNode {
    game::content::AuthorId id;
    std::string label; // name, or the id when unnamed
    std::string kind;  // "wall", "enemy", ... -- also the colour key
};

// The entities that share a kind: one prefab id, or one gameplay kind for the
// entities that have no prefab.
struct OutlinerGroup {
    std::string key;   // "kit.wall_stone" / "enemy"
    std::string label; // shown on the group row
    std::string kind;  // the kit piece's socket, or the gameplay kind
    // A kit group: named by prefab, sorted below the loose gameplay entities.
    // Not "everything in it is level dressing" -- one of these doors may carry
    // a trigger, and it still belongs under the door row.
    bool geometry = false;
    std::vector<OutlinerNode> nodes;
};

struct OutlinerOptions {
    std::string filter; // matches name, id, kind or prefab
    bool showGeometry = true;
};

struct OutlinerTree {
    std::vector<OutlinerGroup> groups;
    std::size_t shown = 0;  // entities in `groups`
    std::size_t hidden = 0; // entities the options excluded
};

// What an entity reads as in one word. Gameplay wins over geometry: an entity
// that is both a pillar and an enemy spawn is listed as the spawn, because that
// is what somebody is looking for it by.
const char* entityKind(const game::content::Entity& entity,
                       const game::content::KitCatalog& catalog);

// Deterministic: gameplay groups first, then geometry, each alphabetically, and
// nodes sorted by id within a group. Two authors on the same scene see the same
// panel, and a placement never reshuffles the list under the cursor.
OutlinerTree buildOutliner(const game::content::SceneDocument& document,
                           const game::content::KitCatalog& catalog,
                           const OutlinerOptions& options);

} // namespace ed
