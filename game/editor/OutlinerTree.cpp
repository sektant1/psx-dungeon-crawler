#include "OutlinerTree.h"

#include "EntityComponents.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

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

// What the kit says a piece is, ignoring anything gameplay put on this one.
const char* prefabKind(const Entity& entity, const KitCatalog& catalog)
{
    if (const game::content::KitPiece* piece = catalog.find(entity.prefab))
        return socketName(piece->socket);
    return "MISSING";
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
    const std::string filter = lower(options.filter);

    std::unordered_map<std::string, std::size_t> index;
    for (const Entity& entity : document.entities) {
        const char* kind = entityKind(entity, catalog);
        const bool geometry = isGeometry(entity);
        if (geometry && !options.showGeometry) {
            ++tree.hidden;
            continue;
        }
        const std::string label = entity.name.empty() ? entity.id : entity.name;
        // Group by prefab where there is one -- that is the "same node,
        // different id" the panel exists to collapse -- and by kind otherwise,
        // which puts the loose lights and markers together.
        const std::string key =
            entity.prefab.empty() ? std::string(kind) : entity.prefab;

        if (!filter.empty() && !contains(lower(label), filter) &&
            !contains(lower(entity.id), filter) &&
            !contains(lower(kind), filter) && !contains(lower(key), filter)) {
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
        group.nodes.push_back(OutlinerNode{entity.id, label, kind});
        ++tree.shown;
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
                  if (a.geometry != b.geometry)
                      return !a.geometry;
                  return a.label < b.label;
              });
    return tree;
}

} // namespace ed
