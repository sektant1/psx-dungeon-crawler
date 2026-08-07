#include <editor/content/SceneValidate.h>

#include <editor/content/SceneInstancing.h>

#include <editor/content/SceneContract.h>
#include <eng/assets/AssetRoot.h>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace game::content {
namespace {

// Top-level `[<section>.<id>]` keys in a TOML file, without parsing it.
//
// The same one line of file shape ed::tomlSectionIds knows, reimplemented here
// rather than depended on because game_content must not link the editor's own
// library -- the cooker runs headless in CI and this check has to run there
// too, which is the whole point of it.
std::set<std::string> tomlIds(const std::string& path, const std::string& section)
{
    std::set<std::string> ids;
    std::ifstream in(path);
    if (!in)
        return ids;
    const std::string prefix = "[" + section + ".";
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos || line[start] != '[')
            continue;
        const std::size_t close = line.find(']', start);
        if (close == std::string::npos)
            continue;
        std::string header = line.substr(start, close - start + 1);
        if (header.rfind(prefix, 0) != 0)
            continue;
        // "[item.ashen_moss]" -> "ashen_moss"; a sub-table
        // ("[item.x.use]") is skipped, because it is not an id.
        std::string id = header.substr(prefix.size(),
                                       header.size() - prefix.size() - 1);
        if (id.find('.') == std::string::npos && !id.empty())
            ids.insert(std::move(id));
    }
    return ids;
}

// The ids the game defines, read once per validate() call. Empty when the file
// is missing, which downgrades the check to nothing rather than failing every
// scene -- a content tree being worked on must still cook.
struct GameIds {
    std::set<std::string> items;
    std::set<std::string> enemies;
    // The roster. npcs.toml rather than dialogue.toml, for the reason that file
    // exists: being a person is not the same as having a conversation written.
    std::set<std::string> people;

    static GameIds load()
    {
        GameIds ids;
        if (const std::filesystem::path p =
                eng::assets::resolve("config/items.toml");
            !p.empty())
            ids.items = tomlIds(p.string(), "item");
        if (const std::filesystem::path p =
                eng::assets::resolve("config/enemies.toml");
            !p.empty())
            ids.enemies = tomlIds(p.string(), "enemy");
        if (const std::filesystem::path p =
                eng::assets::resolve("config/npcs.toml");
            !p.empty())
            ids.people = tomlIds(p.string(), "npc");
        return ids;
    }
};

bool finite(const glm::vec3& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// Whether a .lua file compiles. Compiled with luaL_loadbuffer and never run:
// validating a script must not require a world, and a cooker that executes
// authored content is a cooker that can be made to do anything. No libraries
// are opened, because a chunk that is only compiled needs none.
bool luaChunkParses(const std::string& file, std::string& error)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        error = "cannot open the file";
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string source = ss.str();

    lua_State* L = luaL_newstate();
    if (L == nullptr) {
        error = "out of memory";
        return false;
    }
    const std::string chunkName = "@" + file;
    const int status =
        luaL_loadbuffer(L, source.data(), source.size(), chunkName.c_str());
    if (status != LUA_OK) {
        const char* message = lua_tostring(L, -1);
        error = message != nullptr ? message : "syntax error";
    }
    lua_close(L);
    return status == LUA_OK;
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
    const GameIds gameIds = GameIds::load();
    const GridConfig grid = GridConfig::fromCatalog(catalog);

    // The scene's own contract, first: a scene nobody can look through is
    // wrong in a way no per-entity check can see, and it was the one failure
    // this validator had no code for. See SceneContract.h.
    //
    // Reported from the shared table rather than restated here, so the panel,
    // the validator and the cooker cannot disagree about what a scene needs.
    // Two of the six roles reach this list, and the line between them matters.
    //
    // An UNFILLED role is an issue only when it is an Error -- a scene nobody
    // can look through, a world with nowhere to start. The Warning-severity
    // holes (no authored audio listener, no directional key light) are normal:
    // the player's camera hears when a scene authors no listener, and a torch-
    // lit dungeon has no directional light by design. Reporting those would put
    // two warnings on every correct level in the repo, which is exactly how a
    // panel gets ignored. They are still shown in the Scene section, where they
    // read as information rather than as faults.
    //
    // An OVER-FILLED role is always an issue when the role cannot have two --
    // two audio listeners leaves positional audio undefined, and nothing else
    // in the editor would ever say so.
    {
        const ContractReport contract = sceneContract(document);
        for (const RoleStatus& status : contract.roles) {
            if (!status.applicable || status.severity == Severity::Info)
                continue;
            if (status.count == 1)
                continue;
            const bool unfilledError =
                status.count == 0 && status.severity == Severity::Error;
            const bool ambiguous =
                status.count > 1 && status.fix != QuickFix::None;
            if (!unfilledError && !ambiguous)
                continue;
            Issue issue;
            issue.severity = status.severity;
            issue.code = unfilledError ? "scene.role_unfilled"
                                       : "scene.role_ambiguous";
            issue.message = std::string(sceneRoleName(status.role)) + ": " +
                            status.detail;
            issue.entity = status.filledBy;
            issue.fix = unfilledError ? status.fix : QuickFix::None;
            issues.push_back(std::move(issue));
        }
    }

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

    // Every name an Entity-typed script prop could legitimately point at.
    // Gathered up front because a lever may reference a door authored after it,
    // and a check that walked the list in order would call that a dangling
    // reference. Both id and name: the host resolves by Name at runtime, and
    // authors reach for whichever reads better in the inspector.
    std::set<std::string> entityNames;
    for (const Entity& entity : document.entities) {
        if (!entity.id.empty()) entityNames.insert(entity.id);
        if (!entity.name.empty()) entityNames.insert(entity.name);
    }

    // --- scene instancing --------------------------------------------------
    // Checked before anything else looks at the document, because everything
    // else is looking at a document that has NOT been expanded: validate() runs
    // on what the author wrote, and the cooker expands its own copy. So an
    // instance is one entity here however many it becomes later, and the only
    // things worth saying about it are whether it will expand at all and
    // whether it is a coherent thing to have authored.
    {
        std::vector<AuthorId> placements;
        for (const Entity& entity : document.entities) {
            if (!entity.instance)
                continue;
            placements.push_back(entity.id);
            // An entity that both instances a scene and draws something of its
            // own was never meaningful: the placement is a node the contents
            // hang from, and a mesh on it would be a second object nobody
            // placed. Caught here rather than silently dropped at cook.
            if (!entity.prefab.empty() || entity.mesh || entity.primitive) {
                add(issues, Severity::Error, "instance.also_draws",
                    "'" + entity.id +
                        "' instances a scene and also has geometry of its "
                        "own; the placement should be an empty node",
                    entity.id);
            }
        }

        if (!placements.empty()) {
            // The expansion is the authority on what is wrong -- cycles, depth,
            // a missing or malformed file -- so ask it rather than
            // reimplementing those rules and letting the two disagree. It works
            // on a copy, so this costs one expansion and changes nothing.
            SceneDocument probe = document;
            std::string expandError;
            if (!expandInstances(probe, assetRoot, expandError)) {
                add(issues, Severity::Error, "instance.unresolved", expandError,
                    placements.front());
            }
        }
    }

    // An active first-person rig is a spawn: it says how the player moves and,
    // by its transform, where they stand, which is what
    // eng::runtime::SceneRuntime::playerSpawn reads. It counts because
    // PlayerSpawn is one of THIS game's markers, so a scene authored in a
    // project -- with no game components in it at all -- has no other way to
    // say where the player starts.
    //
    // A rig only counts when the document has no PlayerSpawn at all. The two
    // are routinely on DIFFERENT entities (see MapRuntime::playerRig: "a level
    // that puts the rig on its player spawn instead should still be read"), and
    // counting both made such a level report two spawns and fail to cook.
    const bool hasMarkedSpawn =
        std::any_of(document.entities.begin(), document.entities.end(),
                    [](const Entity& e) { return e.playerSpawn; });
    for (const Entity& entity : document.entities) {
        const bool rigSpawn = !hasMarkedSpawn && entity.firstPerson &&
                              entity.firstPerson->active;
        if (entity.playerSpawn || rigSpawn) {
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

        // A layer nobody declares. Not an error: the entity is intact and the
        // level plays -- layers are dropped at cook -- but it is invisible from
        // the Layers panel's declared rows, so it has to be said out loud or a
        // botched merge quietly parks a room in a layer nothing can reach.
        if (!document.hasLayer(entity.layer)) {
            add(issues, Severity::Warning, "layer.undeclared",
                "layer '" + entity.layer + "' is not declared by this scene",
                entity.id);
        }

        // A free-form property with no key cannot be looked up by anything, and
        // is what a half-typed row in the inspector leaves behind.
        for (const PropertyAuthor& prop : entity.properties) {
            if (prop.key.empty()) {
                add(issues, Severity::Warning, "property.no_key",
                    "a free-form property has no name", entity.id);
            } else if (prop.type == PropertyAuthor::Type::Entity &&
                       !prop.stringValue.empty() &&
                       entityNames.count(prop.stringValue) == 0) {
                add(issues, Severity::Warning, "property.unresolved",
                    "property '" + prop.key + "' names entity '" +
                        prop.stringValue +
                        "', which this scene does not contain",
                    entity.id);
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

        // --- scripts ------------------------------------------------------
        // Three checks, in the order they can fail. All errors: a script that
        // does not resolve or does not parse produces an entity that silently
        // does nothing at runtime, which is exactly the class of bug the
        // content checks exist to turn into a build failure.
        for (const ScriptAuthor& script : entity.scripts) {
            if (script.path.empty()) {
                add(issues, Severity::Error, "script.no_path",
                    "a script entry has no path", entity.id);
                continue;
            }
            if (assetRoot.empty())
                continue; // no tree to resolve against (unit tests)

            std::error_code code;
            std::filesystem::path file = script.path;
            if (!std::filesystem::exists(file, code))
                file = std::filesystem::path(assetRoot) / script.path;
            // Then the mounted packs, for the same reason the kit meshes above
            // check them: a project mounts its content over the engine's, so a
            // scene of its own may legitimately use a script that ships with
            // the engine, and the runtime resolves it exactly this way.
            if (!std::filesystem::exists(file, code))
                file = eng::assets::resolve(script.path);
            if (file.empty() || !std::filesystem::exists(file, code)) {
                add(issues, Severity::Error, "script.missing",
                    "script '" + script.path + "' is not on disk", entity.id);
                continue;
            }

            // Compiled, never run. Validating a script must not require a
            // world, a renderer or a physics system -- and a cooker that
            // executes authored content is a cooker that can be made to do
            // anything.
            std::string parseError;
            if (!luaChunkParses(file.string(), parseError)) {
                add(issues, Severity::Error, "script.syntax",
                    "script '" + script.path + "' does not parse: " + parseError,
                    entity.id);
            }

            // An Entity prop naming nothing in this scene. A warning rather
            // than an error: the host already logs it and hands the script nil,
            // and a level may legitimately ship with the collaborator cut. It
            // is still worth saying at build time, because the runtime symptom
            // is a door that simply never opens.
            for (const ScriptPropAuthor& prop : script.props) {
                if (prop.type != ScriptPropAuthor::Type::Entity) continue;
                if (prop.stringValue.empty()) {
                    add(issues, Severity::Warning, "script.prop_empty",
                        "script '" + script.path + "' prop '" + prop.key +
                            "' names no entity",
                        entity.id);
                } else if (entityNames.count(prop.stringValue) == 0) {
                    add(issues, Severity::Warning, "script.prop_unresolved",
                        "script '" + script.path + "' prop '" + prop.key +
                            "' names entity '" + prop.stringValue +
                            "', which this scene does not contain",
                        entity.id);
                }
            }
        }

        const KitPiece* piece = nullptr;
        if (!entity.prefab.empty()) {
            piece = catalog.find(entity.prefab);
            if (!piece) {
                add(issues, Severity::Error, "prefab.unresolved",
                    "prefab '" + entity.prefab + "' is not in the kit catalogue",
                    entity.id);
            } else if (!assetRoot.empty() && !piece->isGroup()) {
                std::error_code code;
                std::filesystem::path mesh =
                    std::filesystem::path(assetRoot) / piece->meshPath;
                // Then the mounted packs, which is where a PROJECT's kit meshes
                // actually live: a project mounts its own content over the
                // engine's, so a scene of its own using engine geometry is the
                // documented arrangement, not a broken reference. Checking one
                // root only reported every kit piece in a migrated scene as
                // missing while the runtime resolved all of them.
                if (!std::filesystem::exists(mesh, code))
                    mesh = eng::assets::resolve(piece->meshPath);
                if (mesh.empty() || !std::filesystem::exists(mesh, code)) {
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
        // An id the game does not define is the silent-hole failure this whole
        // file exists to catch: the entity is in the scene, in the outliner and
        // in the cooked map, and at runtime nothing appears. Errors rather than
        // warnings, because the cooker refuses errors and a level that ships
        // with a hole in it is worse than one that will not cook.
        if (entity.pickup && !gameIds.items.empty() &&
            !gameIds.items.count(*entity.pickup)) {
            add(issues, Severity::Error, "pickup.unknown_item",
                "pickup '" + *entity.pickup +
                    "' is not an item items.toml defines; nothing will be here",
                entity.id);
        }
        if (entity.enemySpawn && !gameIds.enemies.empty() &&
            !gameIds.enemies.count(*entity.enemySpawn)) {
            add(issues, Severity::Error, "enemy.unknown_id",
                "enemy '" + *entity.enemySpawn +
                    "' is not one enemies.toml defines; nothing will spawn",
                entity.id);
        }
        if (entity.npc) {
            // Empty is the state a freshly added component starts in, so it is
            // the one an unfinished village is full of. Named separately from
            // the unknown-id case because the fix is different: one is "pick
            // somebody", the other is "you picked somebody who is not there".
            if (entity.npc->empty()) {
                add(issues, Severity::Error, "npc.no_id",
                    "an NPC with no id is nobody: there is no conversation to "
                    "open and no shop to stock",
                    entity.id);
            } else if (!gameIds.people.empty() &&
                       !gameIds.people.count(*entity.npc)) {
                add(issues, Severity::Error, "npc.unknown_id",
                    "npc '" + *entity.npc +
                        "' is not somebody npcs.toml describes; nobody will "
                        "be here",
                    entity.id);
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

    // Entities that are an exact copy of another: same prefab, same parent,
    // same transform. Not overlapping -- identical, and therefore invisible.
    //
    // tech_demo carried 68 copies of one door in one doorway and 183 redundant
    // entities out of 422, which is the Place tool stamping while the button
    // was held. Nothing showed it: the level looked right, because the copies
    // sit exactly on top of each other. It cost a draw call each (831 batches
    // against 465 once removed) and z-fought between coplanar faces.
    {
        std::unordered_map<std::string, AuthorId> firstSeen;
        for (const Entity& entity : document.entities) {
            if (entity.prefab.empty())
                continue;
            char buffer[256];
            std::snprintf(buffer, sizeof(buffer),
                          "%s|%s|%.4f,%.4f,%.4f|%.3f,%.3f,%.3f|%.4f,%.4f,%.4f",
                          entity.parent.c_str(), entity.prefab.c_str(),
                          entity.transform.position.x,
                          entity.transform.position.y,
                          entity.transform.position.z,
                          entity.transform.rotationDegrees.x,
                          entity.transform.rotationDegrees.y,
                          entity.transform.rotationDegrees.z,
                          entity.transform.scale.x, entity.transform.scale.y,
                          entity.transform.scale.z);
            const auto inserted = firstSeen.emplace(buffer, entity.id);
            if (!inserted.second) {
                add(issues, Severity::Warning, "cell.duplicate_placement",
                    "an exact copy of '" + inserted.first->second +
                        "': same prefab, parent and transform",
                    entity.id, QuickFix::RemoveEntity);
            }
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

    // A component scene has no player to place, and neither does a shot (it
    // plays itself through its own camera) or a screen (it is a flat page).
    // sceneContract already decides which of those a scene is, so this asks it
    // rather than re-deriving the rule and disagreeing with the panel -- which
    // is exactly what happened: the Contract panel reported clip_demo.scn as
    // playable while scene_cook refused to cook it at all.
    // Only a shot and a screen genuinely have no player: a shot plays itself
    // through its own camera, a screen is a flat page. Everything else needs a
    // spawn -- INCLUDING an empty scene, which is the case this must not go
    // quiet on. Asking the contract for the Spawn role's applicability instead
    // did exactly that: an empty scene is SceneKind::Empty, isWorld() is false
    // for it, and a scene with nothing in it stopped reporting the one thing
    // most obviously wrong with it.
    const ContractReport contract = sceneContract(document);
    const bool playsItself = contract.kind == SceneKind::Shot ||
                             contract.kind == SceneKind::Screen;
    const bool needsSpawn = !document.component && !playsItself;
    if (playerSpawns == 0 && needsSpawn) {
        add(issues, Severity::Error, "spawn.missing",
            "the scene has no player spawn", {}, QuickFix::AddPlayerSpawn);
    } else if (playerSpawns > 1) {
        add(issues, Severity::Error, "spawn.duplicate",
            "the scene has " + std::to_string(playerSpawns) +
                " player spawns; it must have exactly one",
            {});
    }
    if (exits == 0 && !document.component && needsSpawn) {
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
    // The three view fixes go through setSceneView(), which *swaps* the shape
    // on the camera the scene already has rather than adding a second view. See
    // SceneContract.h for why the entity is reused.
    case QuickFix::AddFirstPersonView:
        return !setSceneView(document, SceneKind::FirstPerson).empty();
    case QuickFix::AddThirdPersonView:
        return !setSceneView(document, SceneKind::ThirdPerson).empty();
    case QuickFix::AddShotCamera:
        return !setSceneView(document, SceneKind::Shot).empty();
    case QuickFix::AddAudioListener: {
        // On its own entity rather than on the camera: a listener that rode the
        // camera could not be moved off it, and "the player hears from their
        // head but the scene is framed from a crane" is a real shot.
        Entity listener;
        listener.id = document.allocateId("audio_listener");
        listener.name = "Audio Listener";
        listener.audioListener = AudioListenerAuthor{};
        document.add(listener);
        return true;
    }
    case QuickFix::AddKeyLight: {
        Entity light;
        light.id = document.allocateId("key_light");
        light.name = "Key Light";
        LightAuthor authored;
        authored.type = LightAuthor::Type::Directional;
        // Aimed down and across rather than straight down: a top-down key
        // flattens every wall in a dungeon into the same value.
        authored.colour = {1.0f, 0.96f, 0.88f};
        light.light = authored;
        light.transform.rotationDegrees = {-45.0f, -35.0f, 0.0f};
        document.add(light);
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
