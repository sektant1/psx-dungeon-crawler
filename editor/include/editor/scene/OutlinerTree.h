#pragma once
#include <editor/content/KitCatalog.h>
#include <editor/content/SceneDocument.h>

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

// One row for a single entity, with whatever is parented under it.
struct OutlinerNode {
    game::content::AuthorId id;
    std::string label; // name, or the id when unnamed
    std::string kind;  // "wall", "enemy", ... -- also the colour key
    // Component ids this entity carries, in table order. The row shows the
    // count, and the tooltip the list: a level's rows all look alike, and what
    // actually distinguishes two otherwise identical pillars is which one has
    // the trigger on it.
    std::vector<std::string> components;
    // Entities whose `parent` is this one, already nested. Empty for the flat
    // majority of a blockout, which is what keeps the panel cheap.
    std::vector<OutlinerNode> children;
    // Kept for composed-object search. A child nested under a root no longer
    // has a prefab group row whose key can satisfy the query for it.
    std::string prefab;
    // Which authoring layer the entity is in, for the `layer:` filter term.
    // Empty is the default layer, matched by typing `layer:default`.
    std::string layer;
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
    // A composed object: one authored root and everything parented beneath it.
    // These are never collapsed by prefab -- the whole point of parenting a
    // chandelier's four candles to it is that they are one thing -- so the
    // group holds exactly one node and the tree is inside it.
    bool composed = false;
    // Malformed parent links still have to be visible so the Issues panel can
    // reveal and repair them. These groups sort first and carry an explicit
    // INVALID label rather than relying on colour.
    bool invalid = false;
    // A pile of repeats rather than a parent.
    //
    // This distinction is the tree's canonical meaning, and it was previously
    // only implicit. Two rows in this panel look alike and mean opposite
    // things: a *composed* row is an object and the rows under it are its
    // parts, so clicking it selects one thing; a *bucket* row is a hundred and
    // forty-six unrelated walls that happen to share a prefab, so clicking it
    // selects a hundred and forty-six things. Nothing on screen said which was
    // which, and the second one is destructive to get wrong.
    //
    // So a bucket is now named, drawn with its own glyph (Icon::Stack, not
    // Icon::Group) and says what it is on hover. It is a *view* device -- the
    // document has no such node -- which is also why the hierarchy mode below
    // can switch it off entirely.
    bool bucket = false;
    std::vector<OutlinerNode> nodes;
};

struct OutlinerOptions {
    // Free text matches name, id, kind or prefab. Two prefixes narrow it
    // instead:
    //
    //   has:collider     entities carrying that component
    //   kind:enemy       entities of that kind, exactly
    //   layer:lighting   entities in that layer ("default" for the implicit one)
    //
    // Free text is a substring and matches four different things, which is
    // right for "where did I put stone_arch" and useless for "which of these
    // three hundred rows has a trigger on it" -- and the second question is the
    // one an author asks once a level is dense. Terms are space-separated and
    // AND together, so `has:collider kind:enemy` is the intersection.
    std::string filter;
    bool showGeometry = true;
    // Collapse repeats onto one row.
    //
    // On, which is the default, a blockout's hundred and forty-six identical
    // walls are one bucket row instead of a hundred and forty-six lines that
    // scroll the one enemy spawn off the panel. That is what makes a dense
    // level readable and it is why the grouping exists.
    //
    // Off, the panel is the document's *actual* structure: every entity with no
    // parent at the top level, its children indented under it, nothing invented
    // and nothing merged. One row, one entity, always -- which is the meaning
    // Godot's scene tree has, and the one an author needs when the question is
    // "what is parented to what" rather than "what is in this level".
    //
    // Both are honest views of the same document. Having only the first one
    // meant the parent/child structure of a flat scene was, strictly speaking,
    // not visible anywhere in the editor.
    bool groupRepeats = true;
    // Scope the panel to one entity and its descendants. Set while the viewport
    // is isolated on that entity (see EditorState::IsolationState), so the
    // hierarchy is the object's own tree rather than a level with one object
    // visible in it -- which is the difference between a mode and a filter.
    //
    // An id no entity carries yields an empty tree, which is what the caller
    // wants: the entity was deleted, and the mode is about to leave.
    game::content::AuthorId root;
};

struct OutlinerTree {
    std::vector<OutlinerGroup> groups;
    std::size_t shown = 0;  // entities in `groups`
    std::size_t hidden = 0; // entities the options excluded
};

// Every id in a group, root and descendants alike. Clicking a group row acts on
// all of it -- "give these forty pillars a collider", "move this chandelier" --
// and for a composed object that has to reach past the root.
//
// Inline because it is pure tree walking: the panel and its headless test use
// it, and neither should have to link the catalogue to do so.
inline std::vector<game::content::AuthorId> groupIds(const OutlinerGroup& group)
{
    std::vector<game::content::AuthorId> ids;
    const auto walk = [&ids](auto&& self, const OutlinerNode& node) -> void {
        ids.push_back(node.id);
        for (const OutlinerNode& child : node.children)
            self(self, child);
    };
    for (const OutlinerNode& node : group.nodes)
        walk(walk, node);
    return ids;
}

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
