#include <editor/scene/OutlinerTree.h>

#include <editor/scene/EntityComponents.h>

#include <eng/assets/AssetName.h>

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <unordered_set>

namespace ed {
namespace {

using game::content::Entity;
using game::content::KitCatalog;
using game::content::LightAuthor;
using game::content::SceneDocument;

std::string lower(std::string_view text)
{
    std::string out(text);
    for (char& c : out)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool contains(const std::string& haystackLower, const std::string& needleLower)
{
    return haystackLower.find(needleLower) != std::string::npos;
}

std::string entityLabel(const Entity& entity)
{
    if (!entity.name.empty() && entity.name != entity.id)
        return entity.name;
    return eng::assets::friendlyAssetLabel(entity.id);
}

// One search term: free text, or one of the three prefixes.
struct Term {
    enum class Kind { Text, HasComponent, IsKind, InLayer } kind = Kind::Text;
    std::string value; // already lowered
};

std::vector<Term> parseFilter(const std::string& filterLower)
{
    std::vector<Term> terms;
    std::size_t at = 0;
    while (at < filterLower.size()) {
        const std::size_t end = filterLower.find(' ', at);
        std::string word = filterLower.substr(
            at, end == std::string::npos ? std::string::npos : end - at);
        at = end == std::string::npos ? filterLower.size() : end + 1;
        if (word.empty())
            continue;
        Term term;
        if (word.rfind("has:", 0) == 0) {
            term.kind = Term::Kind::HasComponent;
            term.value = word.substr(4);
        }
        else if (word.rfind("kind:", 0) == 0) {
            term.kind = Term::Kind::IsKind;
            term.value = word.substr(5);
        }
        // `layer:lighting`. The third narrowing prefix, and the one that makes
        // the hierarchy agree with the Layers panel: hiding a layer takes it
        // out of the viewport, and this takes it out of the list.
        else if (word.rfind("layer:", 0) == 0) {
            term.kind = Term::Kind::InLayer;
            term.value = word.substr(6);
        }
        else {
            term.value = std::move(word);
        }
        // A bare "has:" is somebody midway through typing, not a request to
        // match everything -- dropping it keeps the list steady under the
        // cursor instead of flashing empty between keystrokes.
        if (!term.value.empty())
            terms.push_back(std::move(term));
    }
    return terms;
}

// Component ids on an entity, lowered, in the editor table's order.
std::vector<std::string> componentIds(const Entity& entity)
{
    std::vector<std::string> ids;
    for (const ComponentType* type : componentsOf(entity))
        ids.push_back(type->id);
    return ids;
}

// Does one entity satisfy every term? `text` is what free text is matched
// against: label, id, kind and prefab, already joined and lowered by the
// caller, which is the only part that differs between a composed node and a
// flat one.
bool matchesTerms(const std::vector<Term>& terms, const std::string& text,
                  const std::string& kind,
                  const std::vector<std::string>& components,
                  const std::string& layer)
{
    for (const Term& term : terms) {
        switch (term.kind) {
        case Term::Kind::Text:
            if (!contains(text, term.value))
                return false;
            break;
        case Term::Kind::InLayer:
            // Exact, like kind: above and for the same reason. "default" names
            // the implicit layer, whose id is empty and which therefore cannot
            // be typed any other way.
            if (lower(layer).empty() ? term.value != "default"
                                     : lower(layer) != term.value)
                return false;
            break;
        case Term::Kind::IsKind:
            // Exact, not a substring: `kind:light` must not also bring back
            // every entity whose kind merely contains those letters, or the
            // narrowing prefix would be no narrower than the free text.
            if (lower(kind) != term.value)
                return false;
            break;
        case Term::Kind::HasComponent: {
            bool found = false;
            for (const std::string& id : components)
                found = found || contains(lower(id), term.value);
            if (!found)
                return false;
            break;
        }
        }
    }
    return true;
}

// What the kit says a piece is, ignoring anything gameplay put on this one.
const char* prefabKind(const Entity& entity, const KitCatalog& catalog)
{
    if (const game::content::KitPiece* piece = catalog.find(entity.prefab))
        return socketName(piece->socket);
    return "MISSING";
}

std::size_t countNodes(const OutlinerNode& node)
{
    std::size_t total = 1;
    for (const OutlinerNode& child : node.children)
        total += countNodes(child);
    return total;
}

bool subtreeMatches(const OutlinerNode& node, const std::vector<Term>& terms)
{
    if (matchesTerms(terms,
                     lower(node.label) + " " + lower(node.id) + " " +
                         lower(node.kind) + " " + lower(node.prefab),
                     node.kind, node.components, node.layer))
        return true;
    for (const OutlinerNode& child : node.children)
        if (subtreeMatches(child, terms))
            return true;
    return false;
}

} // namespace

namespace {

// One row and everything parented beneath it, children sorted by id so the
// panel never reshuffles under the cursor. Cycles cannot reach here: the
// document's own walk drops them, and a child is only ever visited from the
// parent it names.
OutlinerNode buildNode(const SceneDocument& document, const KitCatalog& catalog,
                       const Entity& entity,
                       std::unordered_set<std::string>& visited)
{
    OutlinerNode node;
    node.id = entity.id;
    node.label = entityLabel(entity);
    node.kind = entityKind(entity, catalog);
    node.components = componentIds(entity);
    node.prefab = entity.prefab;
    node.layer = entity.layer;
    visited.insert(entity.id);

    std::vector<const Entity*> children = document.childrenOf(entity.id);
    std::sort(children.begin(), children.end(),
              [](const Entity* a, const Entity* b) { return a->id < b->id; });
    for (const Entity* child : children) {
        if (visited.count(child->id) != 0)
            continue; // break self-parenting and cycles for presentation
        node.children.push_back(buildNode(document, catalog, *child, visited));
    }
    return node;
}

} // namespace

const char* entityKind(const Entity& entity, const KitCatalog& catalog)
{
    if (entity.playerSpawn)
        return "spawn";
    if (entity.exitYawDegrees)
        return "exit";
    if (entity.enemySpawn)
        return "enemy";
    if (entity.pickup)
        return "pickup";
    if (entity.trigger)
        return "trigger";
    if (entity.camera)
        return "camera";
    if (entity.light)
        return entity.light->type == LightAuthor::Type::Directional ? "sun"
                                                                    : "light";
    if (entity.marker)
        return "marker";
    if (!entity.prefab.empty()) {
        if (const game::content::KitPiece* piece = catalog.find(entity.prefab))
            return socketName(piece->socket);
        return "MISSING";
    }
    if (entity.collider)
        return "volume";
    return "node";
}

OutlinerTree buildOutliner(const SceneDocument& document,
                           const KitCatalog& catalog,
                           const OutlinerOptions& options)
{
    OutlinerTree tree;
    const std::vector<Term> terms = parseFilter(lower(options.filter));
    std::unordered_set<std::string> visited;

    // Scoped to one object: its whole subtree, as one composed group, and
    // nothing else. Returned early rather than filtered out below, because the
    // grouping rules that follow are about reading a level -- collapsing a
    // hundred identical walls is exactly wrong for the four parts of one prop.
    if (!options.root.empty()) {
        const Entity* root = document.find(options.root);
        if (!root)
            return tree; // deleted; the caller is about to leave the mode
        OutlinerGroup group;
        group.key = root->id;
        group.kind = entityKind(*root, catalog);
        group.composed = true;
        group.nodes.push_back(buildNode(document, catalog, *root, visited));
        group.label = group.nodes.front().label;
        const std::size_t count = countNodes(group.nodes.front());
        if (!terms.empty() && !subtreeMatches(group.nodes.front(), terms)) {
            tree.hidden = count;
            return tree;
        }
        tree.shown = count;
        tree.groups.push_back(std::move(group));
        return tree;
    }

    // Which entities are part of a composed object: they have a parent, or
    // something is parented to them. Everything else -- the flat majority of a
    // blockout -- keeps the prefab grouping below.
    std::unordered_map<std::string, bool> isParent;
    for (const Entity& entity : document.entities)
        if (!entity.parent.empty())
            isParent[entity.parent] = true;
    const auto composed = [&](const Entity& entity) {
        return !entity.parent.empty() || isParent.count(entity.id) != 0;
    };

    // Composed objects first, one group per root, whole subtree inside.
    for (const Entity& entity : document.entities) {
        if (!composed(entity) || !entity.parent.empty())
            continue; // not composed, or not the root of its own chain

        OutlinerGroup group;
        group.key = entity.id;
        group.kind = entityKind(entity, catalog);
        group.composed = true;
        group.nodes.push_back(buildNode(document, catalog, entity, visited));
        group.label = group.nodes.front().label;

        // The filter matches the whole object, and keeps or drops it whole. A
        // chandelier with two of its four candles hidden is not a shorter list,
        // it is a broken one.
        const std::size_t count = countNodes(group.nodes.front());
        if (!terms.empty() && !subtreeMatches(group.nodes.front(), terms)) {
            tree.hidden += count;
            continue;
        }
        tree.shown += count;
        tree.groups.push_back(std::move(group));
    }

    std::unordered_map<std::string, std::size_t> index;
    for (const Entity& entity : document.entities) {
        if (composed(entity))
            continue; // already placed, as part of its object
        visited.insert(entity.id);
        const char* kind = entityKind(entity, catalog);
        const bool geometry = isGeometry(entity);
        if (geometry && !options.showGeometry) {
            ++tree.hidden;
            continue;
        }
        const std::string label = entityLabel(entity);
        // Group by prefab where there is one -- that is the "same node,
        // different id" the panel exists to collapse -- and by kind otherwise,
        // which puts the loose lights and markers together.
        const std::string key =
            entity.prefab.empty() ? std::string(kind) : entity.prefab;

        const std::vector<std::string> components = componentIds(entity);
        if (!terms.empty() &&
            !matchesTerms(terms,
                          lower(label) + " " + lower(entity.id) + " " +
                              lower(kind) + " " + lower(key),
                          kind, components, entity.layer)) {
            ++tree.hidden;
            continue;
        }

        auto found = index.find(key);
        if (found == index.end()) {
            found = index.emplace(key, tree.groups.size()).first;
            OutlinerGroup group;
            group.key = key;
            group.label = key;
            // A prefab group is named for the *piece*, never for a member's
            // gameplay role: putting a light on one of a hundred and sixty
            // doors must not retag the group "light" and move it.
            group.kind =
                entity.prefab.empty() ? kind : prefabKind(entity, catalog);
            group.geometry = !entity.prefab.empty();
            tree.groups.push_back(std::move(group));
        }
        OutlinerGroup& group = tree.groups[found->second];
        group.nodes.push_back(OutlinerNode{
            entity.id, label, kind, components, {}, entity.prefab});
        ++tree.shown;
    }

    // Anything still unvisited named a parent but had no valid root: an orphan,
    // a self-parent, or a cycle. Omitting these rows made the exact entities
    // reported by validation impossible to select and repair.
    std::vector<const Entity*> invalidRoots;
    for (const Entity& entity : document.entities)
        if (visited.count(entity.id) == 0)
            invalidRoots.push_back(&entity);
    std::sort(invalidRoots.begin(), invalidRoots.end(),
              [](const Entity* a, const Entity* b) { return a->id < b->id; });
    for (const Entity* entity : invalidRoots) {
        if (visited.count(entity->id) != 0)
            continue;
        OutlinerGroup group;
        group.key = "__invalid__" + entity->id;
        group.kind = "INVALID";
        group.composed = true;
        group.invalid = true;
        group.nodes.push_back(buildNode(document, catalog, *entity, visited));
        group.nodes.front().label =
            "[invalid hierarchy] " + group.nodes.front().label;
        group.nodes.front().kind = "INVALID";
        group.label = group.nodes.front().label;

        const std::size_t count = countNodes(group.nodes.front());
        if (!terms.empty() && !subtreeMatches(group.nodes.front(), terms)) {
            tree.hidden += count;
            continue;
        }
        tree.shown += count;
        tree.groups.push_back(std::move(group));
    }

    for (OutlinerGroup& group : tree.groups)
        std::sort(group.nodes.begin(), group.nodes.end(),
                  [](const OutlinerNode& a, const OutlinerNode& b) {
                      return a.id < b.id;
                  });
    std::sort(tree.groups.begin(), tree.groups.end(),
              [](const OutlinerGroup& a, const OutlinerGroup& b) {
                  // Kit groups after the loose gameplay ones, whatever a
                  // member happens to carry: the panel's order is a property of
                  // the level's structure, not of the last edit.
                  if (a.invalid != b.invalid)
                      return a.invalid;
                  if (a.geometry != b.geometry)
                      return !a.geometry;
                  return a.label < b.label;
              });
    return tree;
}

} // namespace ed
