#include "SceneValidate.h"

#include <cmath>
#include <filesystem>
#include <map>
#include <string>

namespace game::content {
namespace {

bool finite(const glm::vec3& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

void add(std::vector<Issue>& issues, Severity severity, std::string code,
         std::string message, AuthorId entity, QuickFix fix = QuickFix::None)
{
    issues.push_back({severity, std::move(code), std::move(message),
                      std::move(entity), fix});
}

// Key for "two pieces claiming the same slot": a cell for floor/fill, a cell
// edge for wall/opening. Props do not claim anything.
std::string slotKey(const CellPlacement& cell, Socket socket)
{
    std::string key = std::to_string(cell.col) + ',' + std::to_string(cell.row) +
                      ',' + std::to_string(int(std::lround(cell.level * 100.0f)));
    switch (socket) {
    case Socket::Wall:
    case Socket::Opening: key += ",edge" + std::to_string(int(cell.edge)); break;
    case Socket::Fill: key += ",fill"; break;
    case Socket::Floor: key += ",floor"; break;
    case Socket::Prop: break;
    }
    return key;
}

} // namespace

const char* severityName(Severity severity)
{
    switch (severity) {
    case Severity::Error: return "error";
    case Severity::Warning: return "warning";
    case Severity::Info: return "info";
    }
    return "info";
}

std::vector<Issue> validate(const SceneDocument& document,
                            const KitCatalog& catalog,
                            const std::string& assetRoot)
{
    std::vector<Issue> issues;
    const GridConfig grid = GridConfig::fromCatalog(catalog);

    int playerSpawns = 0;
    int exits = 0;
    std::map<std::string, AuthorId> claimedSlots;
    // Cells that have something to stand on, so an orphan wall can be spotted.
    std::map<std::string, bool> walkableCells;

    for (const Entity& entity : document.entities) {
        if (entity.playerSpawn)
            ++playerSpawns;
        if (entity.exitYawDegrees)
            ++exits;

        if (!finite(entity.transform.position) ||
            !finite(entity.transform.rotationDegrees) ||
            !finite(entity.transform.scale)) {
            add(issues, Severity::Error, "transform.non_finite",
                "transform contains a non-finite number", entity.id,
                QuickFix::ResetTransform);
        } else if (entity.transform.scale.x <= 0.0f ||
                   entity.transform.scale.y <= 0.0f ||
                   entity.transform.scale.z <= 0.0f) {
            add(issues, Severity::Error, "scale.zero",
                "scale must be positive on every axis", entity.id,
                QuickFix::ResetTransform);
        }

        const KitPiece* piece = nullptr;
        if (!entity.prefab.empty()) {
            piece = catalog.find(entity.prefab);
            if (!piece) {
                add(issues, Severity::Error, "prefab.unresolved",
                    "prefab '" + entity.prefab + "' is not in the kit catalogue",
                    entity.id);
            } else if (!assetRoot.empty()) {
                std::error_code code;
                const std::filesystem::path mesh =
                    std::filesystem::path(assetRoot) / piece->meshPath;
                if (!std::filesystem::exists(mesh, code)) {
                    add(issues, Severity::Error, "prefab.mesh_missing",
                        "mesh '" + piece->meshPath + "' is not on disk",
                        entity.id);
                }
            }
        }

        if (entity.cell && piece) {
            const CellPlacement& cell = *entity.cell;
            if (socketUsesGrid(piece->socket)) {
                const std::string key = slotKey(cell, piece->socket);
                const auto claimed = claimedSlots.find(key);
                if (claimed != claimedSlots.end()) {
                    const bool solid = piece->socket == Socket::Fill;
                    add(issues, solid ? Severity::Error : Severity::Warning,
                        solid ? "cell.fill_conflict" : "cell.overlap",
                        "shares a grid slot with '" + claimed->second + "'",
                        entity.id, QuickFix::RemoveEntity);
                } else {
                    claimedSlots.emplace(key, entity.id);
                }
            }
            if (piece->socket == Socket::Floor || piece->socket == Socket::Fill) {
                for (int step = 0; step < (cell.span > 0 ? cell.span : 1); ++step) {
                    const int col = cell.yawQuarters % 2 == 0 ? cell.col + step
                                                              : cell.col;
                    const int row = cell.yawQuarters % 2 == 0 ? cell.row
                                                              : cell.row + step;
                    walkableCells[std::to_string(col) + ',' +
                                  std::to_string(row)] =
                        piece->socket == Socket::Floor;
                }
            }

            // The transform is derived from the cell; if they disagree, someone
            // moved the piece freely and the grid intent is now a lie.
            const XformAuthor derived =
                placementToTransform(grid, catalog, *piece, cell);
            const float drift =
                glm::length(derived.position - entity.transform.position);
            if (drift > 0.01f) {
                add(issues, Severity::Warning, "cell.transform_drift",
                    "position no longer matches its authored cell", entity.id,
                    QuickFix::SnapToCell);
            }
        }

        if (entity.light) {
            if (entity.light->type == LightAuthor::Type::Point &&
                !(entity.light->range > 0.0f)) {
                add(issues, Severity::Error, "light.no_range",
                    "a point light needs a positive range", entity.id,
                    QuickFix::SetDefaultRange);
            }
        }
        if (entity.collider) {
            const glm::vec3& half = entity.collider->halfExtents;
            if (!(half.x > 0.0f) || !(half.y > 0.0f) || !(half.z > 0.0f)) {
                add(issues, Severity::Error, "collider.degenerate",
                    "collider half-extents must be positive on every axis",
                    entity.id, QuickFix::SetDefaultHalfExtents);
            }
        }
        if (entity.trigger && entity.trigger->event.empty()) {
            add(issues, Severity::Error, "trigger.no_event",
                "a trigger with no event does nothing", entity.id);
        }
        if (entity.marker && entity.marker->find('.') == std::string::npos) {
            // Markers are a deliberately open vocabulary, so this is a typo
            // check against the "group.name" convention, not a whitelist.
            add(issues, Severity::Warning, "marker.unknown",
                "marker '" + *entity.marker +
                    "' does not follow the group.name convention",
                entity.id);
        }
    }

    // Walls floating with no cell to belong to: usually a leftover after
    // deleting the floor under them.
    for (const Entity& entity : document.entities) {
        if (!entity.cell || entity.prefab.empty())
            continue;
        const KitPiece* piece = catalog.find(entity.prefab);
        if (!piece || (piece->socket != Socket::Wall &&
                       piece->socket != Socket::Opening))
            continue;
        const std::string own = std::to_string(entity.cell->col) + ',' +
                                std::to_string(entity.cell->row);
        if (walkableCells.find(own) == walkableCells.end()) {
            add(issues, Severity::Warning, "cell.wall_orphan",
                "stands on a cell with no floor", entity.id,
                QuickFix::RemoveEntity);
        }
    }

    if (playerSpawns == 0) {
        add(issues, Severity::Error, "spawn.missing",
            "the scene has no player spawn", {}, QuickFix::AddPlayerSpawn);
    } else if (playerSpawns > 1) {
        add(issues, Severity::Error, "spawn.duplicate",
            "the scene has " + std::to_string(playerSpawns) +
                " player spawns; it must have exactly one",
            {});
    }
    if (exits == 0) {
        add(issues, Severity::Warning, "exit.missing",
            "the scene has no exit, so it cannot be left", {});
    }
    return issues;
}

bool blocksCook(const std::vector<Issue>& issues)
{
    for (const Issue& issue : issues)
        if (issue.severity == Severity::Error) return true;
    return false;
}

bool applyQuickFix(SceneDocument& document, const KitCatalog& catalog,
                   const Issue& issue)
{
    switch (issue.fix) {
    case QuickFix::None:
        return false;
    case QuickFix::AddPlayerSpawn: {
        Entity spawn;
        spawn.id = document.allocateId("player_spawn");
        spawn.name = "Player Spawn";
        spawn.playerSpawn = true;
        document.add(spawn);
        return true;
    }
    default:
        break;
    }

    Entity* entity = document.find(issue.entity);
    if (!entity)
        return false;
    switch (issue.fix) {
    case QuickFix::RemoveEntity:
        return document.remove(issue.entity);
    case QuickFix::SetDefaultRange:
        if (!entity->light) return false;
        entity->light->range = 8.0f;
        document.touch();
        return true;
    case QuickFix::SetDefaultHalfExtents: {
        if (!entity->collider) return false;
        glm::vec3& half = entity->collider->halfExtents;
        // Only repair the degenerate axes; a deliberately thin slab stays thin.
        if (!(half.x > 0.0f)) half.x = 0.5f;
        if (!(half.y > 0.0f)) half.y = 0.5f;
        if (!(half.z > 0.0f)) half.z = 0.5f;
        document.touch();
        return true;
    }
    case QuickFix::SnapToCell: {
        if (!entity->cell) return false;
        const KitPiece* piece = catalog.find(entity->prefab);
        if (!piece) return false;
        entity->transform = placementToTransform(GridConfig::fromCatalog(catalog),
                                                 catalog, *piece, *entity->cell);
        document.touch();
        return true;
    }
    case QuickFix::ResetTransform:
        entity->transform = XformAuthor{};
        document.touch();
        return true;
    default:
        return false;
    }
}

} // namespace game::content
