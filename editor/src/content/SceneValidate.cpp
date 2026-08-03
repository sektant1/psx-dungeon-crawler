#include <editor/content/SceneValidate.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace game::content {
namespace {

bool finite(const glm::vec3& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

void add(std::vector<Issue>& issues, Severity severity, std::string code,
         std::string message, AuthorId entity, QuickFix fix = QuickFix::None,
         glm::vec3 position = glm::vec3(0.0f))
{
    issues.push_back({severity, std::move(code), std::move(message),
                      std::move(entity), fix, position});
}

// The name a cell is known by inside this file. Reachability, the orphan check
// and the walkable map all address cells the same way, so they agree on what
// "the cell at (col,row)" means without any of them re-deriving it.
//
// Deliberately flat in Y: a level's work planes stack in the same columns, and
// treating them as one plane is what cell.wall_orphan already does. A real
// multi-storey reachability check needs stairs to be a modelled connection, and
// the kit has no such piece yet.
std::string cellName(int col, int row)
{
    return std::to_string(col) + ',' + std::to_string(row);
}

// The boundary BETWEEN two cells, named from the side that owns it, so that the
// north edge of (c,r) and the south edge of (c,r-1) are one and the same thing.
// Without this normalisation a room walled from the inside and a room walled
// from the outside would flood differently, which is exactly the bug the check
// is meant to catch.
std::string edgeName(int col, int row, CellPlacement::Edge edge)
{
    switch (edge) {
    case CellPlacement::Edge::North: return "h," + cellName(col, row);
    case CellPlacement::Edge::South: return "h," + cellName(col, row + 1);
    case CellPlacement::Edge::West: return "v," + cellName(col, row);
    case CellPlacement::Edge::East: return "v," + cellName(col + 1, row);
    case CellPlacement::Edge::None: break;
    }
    return {};
}

// What the flood fill needs to know about one cell: you can stand on it when
// something floors it and nothing solid fills it. Both are accumulated rather
// than overwritten, because a block dropped on a floored cell seals it even
// though the floor is still there.
struct CellState {
    int col = 0;
    int row = 0;
    bool floor = false;
    bool solid = false;
    bool walkable() const { return floor && !solid; }
};

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

// World-space footprint of a wall piece on the XZ plane. Only the two
// orientations a grid-placed wall can have (along X or along Z), which is all
// the corner check needs.
struct Footprint {
    AuthorId id;
    float x0, z0, x1, z1;
    bool alongX; // true: width runs along X (yaw 0/180)
};

bool wallFootprint(const Entity& entity, const KitPiece& piece, float scale,
                   Footprint& out)
{
    if (piece.socket != Socket::Wall && piece.socket != Socket::Opening)
        return false;
    const glm::vec3 size = piece.sizeMeters(scale);
    const float yaw = entity.transform.rotationDegrees.y;
    const int quarter = ((int(std::lround(yaw / 90.0f)) % 4) + 4) % 4;
    out.alongX = (quarter % 2) == 0;
    const float sx = out.alongX ? size.x : size.z;
    const float sz = out.alongX ? size.z : size.x;
    const glm::vec3& p = entity.transform.position;
    out.id = entity.id;
    out.x0 = p.x - sx * 0.5f;
    out.x1 = p.x + sx * 0.5f;
    out.z0 = p.z - sz * 0.5f;
    out.z1 = p.z + sz * 0.5f;
    return true;
}

bool nearly(float a, float b) { return std::fabs(a - b) < 0.05f; }

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
    // The grid as the player experiences it, gathered in this one pass so the
    // reachability flood below costs O(cells) instead of re-walking the entity
    // list per cell. This runs on every document change in the editor.
    std::map<std::string, CellState> cellStates;
    std::set<std::string> blockedEdges;
    // The first of each; duplicates are spawn.duplicate's problem, not ours.
    const Entity* spawnEntity = nullptr;
    const Entity* exitEntity = nullptr;

    for (const Entity& entity : document.entities) {
        if (entity.playerSpawn) {
            ++playerSpawns;
            if (!spawnEntity) spawnEntity = &entity;
        }
        if (entity.exitYawDegrees) {
            ++exits;
            if (!exitEntity) exitEntity = &entity;
        }

        // A broken parent chain is silent in the viewport -- worldTransform()
        // resolves the entity as if the missing link were the world, so it
        // simply sits somewhere else than the author meant -- which is exactly
        // why it has to be reported here.
        if (!entity.parent.empty()) {
            if (entity.parent == entity.id) {
                add(issues, Severity::Error, "parent.self",
                    "entity is its own parent", entity.id, QuickFix::ClearParent);
            } else if (!document.contains(entity.parent)) {
                add(issues, Severity::Error, "parent.missing",
                    "parent '" + entity.parent + "' is not in this scene",
                    entity.id, QuickFix::ClearParent);
            } else {
                const std::vector<AuthorId> below =
                    document.descendantsOf(entity.id);
                if (std::find(below.begin(), below.end(), entity.parent) !=
                    below.end()) {
                    add(issues, Severity::Error, "parent.cycle",
                        "parent chain loops through '" + entity.parent + "'",
                        entity.id, QuickFix::ClearParent);
                }
            }
        }

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
            // A piece that declares required components, on an entity that
            // does not have them. This is a warning rather than an error
            // because the scene still cooks and runs -- it just renders wrong,
            // which is the harder kind of bug to notice. A portal membrane
            // without its `portal` component draws as a flat rectangle of the
            // material's static colour, with nothing anywhere saying why.
            if (piece) {
                for (const std::string& component : piece->components) {
                    if (component == "portal" && !entity.portal) {
                        add(issues, Severity::Warning, "prefab.component_missing",
                            "'" + entity.prefab + "' needs a portal component "
                            "to animate; without it the membrane is flat",
                            entity.id, QuickFix::AddPortalComponent);
                    }
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
            const int span = cell.span > 0 ? cell.span : 1;
            if (piece->socket == Socket::Floor || piece->socket == Socket::Fill) {
                for (int step = 0; step < span; ++step) {
                    const int col = cell.yawQuarters % 2 == 0 ? cell.col + step
                                                              : cell.col;
                    const int row = cell.yawQuarters % 2 == 0 ? cell.row
                                                              : cell.row + step;
                    walkableCells[cellName(col, row)] =
                        piece->socket == Socket::Floor;
                    CellState& state = cellStates[cellName(col, row)];
                    state.col = col;
                    state.row = row;
                    state.floor = state.floor || piece->socket == Socket::Floor;
                    state.solid = state.solid || piece->socket == Socket::Fill;
                }
            }
            if (piece->socket == Socket::Wall) {
                // Only a Wall stops the player. An Opening -- an arch, a door
                // frame -- claims the same slot precisely so that the level can
                // say "there is a way through here".
                //
                // A spanning wall runs along the boundary it stands on: a
                // north/south edge runs along X, an east/west edge along Z.
                for (int step = 0; step < span; ++step) {
                    const bool alongX = cell.edge == CellPlacement::Edge::North ||
                                        cell.edge == CellPlacement::Edge::South;
                    const int col = alongX ? cell.col + step : cell.col;
                    const int row = alongX ? cell.row : cell.row + step;
                    const std::string edge = edgeName(col, row, cell.edge);
                    if (!edge.empty())
                        blockedEdges.insert(edge);
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
        if (entity.audio) {
            const AudioEmitterAuthor& audio = *entity.audio;
            if (audio.source.empty()) {
                add(issues, Severity::Error, "audio.no_source",
                    "audio emitter has no clip", entity.id);
            } else {
                const std::filesystem::path source(audio.source);
                if (source.is_absolute() || audio.source.find("..") !=
                                                std::string::npos) {
                    add(issues, Severity::Error, "audio.non_portable_source",
                        "audio clip must be a logical path inside the content pack",
                        entity.id);
                } else if (!assetRoot.empty()) {
                    std::error_code code;
                    if (!std::filesystem::is_regular_file(
                            std::filesystem::path(assetRoot) / source, code)) {
                        add(issues, Severity::Error, "audio.source_missing",
                            "audio clip '" + audio.source + "' is not on disk",
                            entity.id);
                    }
                }
            }
            if (audio.pitch <= 0.0f || audio.minDistance < 0.0f ||
                audio.maxDistance <= audio.minDistance || audio.rolloff < 0.0f ||
                audio.dopplerFactor < 0.0f) {
                add(issues, Severity::Error, "audio.invalid_settings",
                    "audio pitch and attenuation settings are invalid", entity.id);
            }
        }
        if (entity.sounds) {
            // A sound table on something that performs no actions is cooked and
            // read by nothing. It is a warning rather than an error: the entity
            // is still loadable, and the fix (add an Actor component, or drop
            // the table) is the author's call.
            const std::optional<game::ActorKind> kind = actorKindOf(entity);
            if (!kind) {
                add(issues, Severity::Warning, "sounds.not_an_actor",
                    "sound table on an entity that is not a player, NPC or "
                    "enemy -- nothing will play it",
                    entity.id);
            }
            for (const game::ActorActionInfo& info : game::actorActions()) {
                const std::string& cue = entity.sounds->cue(info.action);
                if (cue.empty())
                    continue;
                if (cue.find_first_of(" \t") != std::string::npos) {
                    add(issues, Severity::Error, "sounds.invalid_cue",
                        "'" + cue + "' is not a cue id (no spaces)", entity.id);
                }
                if (kind && !game::actorPerforms(*kind, info.action)) {
                    add(issues, Severity::Warning, "sounds.action_not_performed",
                        std::string("a ") + game::actorKindName(*kind) +
                            " never performs '" + info.id + "'",
                        entity.id);
                }
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
        const std::string own = cellName(entity.cell->col, entity.cell->row);
        if (walkableCells.find(own) == walkableCells.end()) {
            add(issues, Severity::Warning, "cell.wall_orphan",
                "stands on a cell with no floor", entity.id,
                QuickFix::RemoveEntity);
        }
    }

    // Convex corner gaps. A wall is one cell wide and sits entirely OUTSIDE the
    // boundary it faces (the inset, so its inner face lands on the boundary),
    // which means two perpendicular runs stop against each other's outer line
    // and leave a thickness-by-thickness notch at the corner between them.
    //
    // Invisible from inside a sealed room, and glaring anywhere the outside of
    // a corner is visible: an opening, a balcony, or the editor's own view. The
    // kit's answer is a pillar, which is what pillars are for in a modular set.
    {
        std::vector<Footprint> walls;
        // Anything solid that is not a wall: posts, blocks, props. A notch with
        // one of these standing in it is filled, and reporting it anyway would
        // make the room tool's own output look broken.
        std::vector<glm::vec3> fillers;
        for (const Entity& entity : document.entities) {
            if (entity.prefab.empty())
                continue;
            const KitPiece* piece = catalog.find(entity.prefab);
            if (!piece)
                continue;
            Footprint footprint;
            if (wallFootprint(entity, *piece, catalog.scale(), footprint)) {
                walls.push_back(footprint);
            } else if (piece->socket == Socket::Prop ||
                       piece->socket == Socket::Fill) {
                fillers.push_back(entity.transform.position);
            }
        }
        std::vector<std::pair<float, float>> reported;
        for (std::size_t i = 0; i < walls.size(); ++i) {
            for (std::size_t j = i + 1; j < walls.size(); ++j) {
                const Footprint& a = walls[i];
                const Footprint& b = walls[j];
                if (a.alongX == b.alongX)
                    continue; // parallel runs cannot make a corner
                // Diagonally adjacent: they share exactly one corner point and
                // no area, which is the notch.
                for (const float ax : {a.x0, a.x1}) {
                    for (const float az : {a.z0, a.z1}) {
                        for (const float bx : {b.x0, b.x1}) {
                            for (const float bz : {b.z0, b.z1}) {
                                if (!nearly(ax, bx) || !nearly(az, bz))
                                    continue;
                                // Overlapping (a real join) rather than
                                // touching at a point? Then there is no hole.
                                const bool overlaps =
                                    a.x0 < b.x1 - 0.05f && b.x0 < a.x1 - 0.05f &&
                                    a.z0 < b.z1 - 0.05f && b.z0 < a.z1 - 0.05f;
                                if (overlaps)
                                    continue;
                                // Already plugged?
                                bool filled = false;
                                for (const glm::vec3& filler : fillers) {
                                    filled = filled ||
                                             (std::fabs(filler.x - ax) < 0.75f &&
                                              std::fabs(filler.z - az) < 0.75f);
                                }
                                if (filled)
                                    continue;
                                bool seen = false;
                                for (const auto& [rx, rz] : reported)
                                    seen = seen || (nearly(rx, ax) && nearly(rz, az));
                                if (seen)
                                    continue;
                                reported.emplace_back(ax, az);
                                add(issues, Severity::Warning, "cell.corner_gap",
                                    "walls '" + a.id + "' and '" + b.id +
                                        "' meet at a corner with a " +
                                        "hole between them; a pillar fills it",
                                    a.id, QuickFix::FillCornerGap,
                                    glm::vec3(ax, 0.0f, az));
                            }
                        }
                    }
                }
            }
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

    // Can the player actually walk from the spawn to the exit? Every other rule
    // here checks that the data is well formed; this one checks that the level
    // is playable. A room built with the exit behind an unbroken wall ring
    // validates clean, cooks clean and is unfinishable, and once layouts are
    // generated rather than hand-placed that has to be caught automatically.
    //
    // Skipped when there is no spawn or no exit: spawn.missing and exit.missing
    // already name that cause, and saying it twice only pads the panel. Skipped
    // too when nothing is placed on the grid, because a scene authored with free
    // transforms (the older shipped scenes) has no cells to flood and a silent
    // pass is honest -- there is no topology here to be wrong about.
    if (spawnEntity && exitEntity && !cellStates.empty()) {
        const auto cellOf = [&](const Entity& entity, int& col, int& row) {
            // A spawn or exit is a bare marker with no prefab, so it usually has
            // no authored cell; where it does, that intent beats the transform.
            if (entity.cell) {
                col = entity.cell->col;
                row = entity.cell->row;
                return;
            }
            pointToCell(grid, entity.transform.position, col, row);
        };
        const auto walkable = [&](const std::string& name) {
            const auto found = cellStates.find(name);
            return found != cellStates.end() && found->second.walkable();
        };

        int spawnCol = 0, spawnRow = 0;
        cellOf(*spawnEntity, spawnCol, spawnRow);
        // A spawn standing off the authored grid gives the flood nowhere to
        // start from, and every cell would come back unreachable. That is a
        // different fault with a different fix, and this rule cannot tell it
        // apart from "the grid is somewhere else entirely", so it stays quiet.
        if (walkable(cellName(spawnCol, spawnRow))) {
            std::set<std::string> reached;
            std::vector<std::pair<int, int>> pending;
            reached.insert(cellName(spawnCol, spawnRow));
            pending.emplace_back(spawnCol, spawnRow);
            while (!pending.empty()) {
                const auto [col, row] = pending.back();
                pending.pop_back();
                const std::pair<int, int> steps[] = {
                    {0, -1}, {0, 1}, {-1, 0}, {1, 0}};
                const CellPlacement::Edge edges[] = {
                    CellPlacement::Edge::North, CellPlacement::Edge::South,
                    CellPlacement::Edge::West, CellPlacement::Edge::East};
                for (int i = 0; i < 4; ++i) {
                    if (blockedEdges.count(edgeName(col, row, edges[i])))
                        continue;
                    const std::string next =
                        cellName(col + steps[i].first, row + steps[i].second);
                    if (!walkable(next) || reached.count(next))
                        continue;
                    reached.insert(next);
                    pending.emplace_back(col + steps[i].first,
                                         row + steps[i].second);
                }
            }

            int exitCol = 0, exitRow = 0;
            cellOf(*exitEntity, exitCol, exitRow);
            if (!reached.count(cellName(exitCol, exitRow))) {
                // No quick fix: the cure is to knock a hole in one of several
                // walls or to move the exit, and which one is a design decision
                // the editor has no business guessing.
                add(issues, Severity::Error, "exit.unreachable",
                    "no walkable path leads from the player spawn to the exit "
                    "at cell (" + std::to_string(exitCol) + ", " +
                        std::to_string(exitRow) + ")",
                    exitEntity->id, QuickFix::None,
                    cellCentre(grid, exitCol, exitRow, 0.0f));
            }

            // Floor the player can never stand on. One issue for the whole
            // group with an example location, never one per cell: a level that
            // splits in half strands hundreds of cells and would bury every
            // other issue in the panel.
            int stranded = 0;
            int firstCol = 0, firstRow = 0;
            for (const auto& [name, state] : cellStates) {
                if (!state.walkable() || reached.count(name))
                    continue;
                if (stranded == 0) {
                    firstCol = state.col;
                    firstRow = state.row;
                }
                ++stranded;
            }
            if (stranded > 0) {
                add(issues, Severity::Warning, "cell.unreachable",
                    std::to_string(stranded) +
                        " walkable cells are cut off from the player spawn, "
                        "one of them at (" +
                        std::to_string(firstCol) + ", " +
                        std::to_string(firstRow) + ")",
                    {}, QuickFix::None,
                    cellCentre(grid, firstCol, firstRow, 0.0f));
            }
        }
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
    case QuickFix::FillCornerGap: {
        // Placed where the two runs meet, at the wall's own base height. The
        // pillar is 1.13 m across against a 1 m notch, so it covers the hole
        // with a little to spare rather than fitting it exactly -- a flush fit
        // would z-fight with both walls.
        const Entity* wall = document.find(issue.entity);
        const KitPiece* pillar = catalog.find("kit.pillar");
        if (!wall || !pillar)
            return false;
        Entity post;
        post.id = document.allocateId("corner_pillar");
        post.name = "Corner Pillar";
        post.prefab = "kit.pillar";
        post.transform.position = issue.position;
        post.transform.position.y = wall->transform.position.y;
        document.add(post);
        return true;
    }
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
    case QuickFix::AddPortalComponent: {
        if (entity->portal)
            return false;
        entity->portal = PortalAuthor{};
        document.touch();
        return true;
    }
    case QuickFix::ClearParent: {
        if (entity->parent.empty()) return false;
        // The entity keeps the place it was drawn in: its transform was local
        // to a parent, and dropping the link without baking the chain would
        // teleport it somewhere the author never put it.
        const WorldTransform world = document.worldTransform(entity->id);
        entity->parent.clear();
        entity->transform.position = world.position;
        entity->transform.rotationDegrees = authorRotationDegrees(world.orientation);
        entity->transform.scale = world.scale;
        document.touch();
        return true;
    }
    default:
        return false;
    }
}

} // namespace game::content
