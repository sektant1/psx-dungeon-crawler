#include <editor/project/ProjectMigrate.h>

#include <editor/content/SceneSource.h>
#include <editor/content/SceneWriter.h>

#include <eng/Log.h>
#include <eng/runtime/Project.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace ed {
namespace {

// What this game's scenes carry that a project runtime has no systems for.
// Counted and reported rather than translated: see the header.
struct MarkerCount {
    const char* name;
    int count;
};

std::vector<std::pair<std::string, int>> countMarkers(
    const game::content::SceneDocument& document)
{
    int exits = 0, enemies = 0, pickups = 0, triggers = 0, markers = 0, npcs = 0;
    for (const game::content::Entity& e : document.entities) {
        if (e.exitYawDegrees) ++exits;
        if (e.enemySpawn) ++enemies;
        if (e.pickup) ++pickups;
        if (e.trigger) ++triggers;
        if (e.marker) ++markers;
        if (e.npc) ++npcs;
    }
    std::vector<std::pair<std::string, int>> out;
    for (const MarkerCount& entry :
         {MarkerCount{"exit", exits}, MarkerCount{"enemy_spawn", enemies},
          MarkerCount{"pickup", pickups}, MarkerCount{"trigger", triggers},
          MarkerCount{"marker", markers}, MarkerCount{"npc", npcs}}) {
        if (entry.count > 0)
            out.emplace_back(entry.name, entry.count);
    }
    return out;
}

// The one transformation. Returns true when a rig was added.
//
// The marker is kept, not replaced: with both, this game reads its own
// PlayerSpawn and the player reads the rig, so one file plays in both. The
// validator counts a rig as a spawn only when nothing is marked, so carrying
// both does not report two spawns (which would be an error, and would stop the
// scene cooking at all).
bool addPlayerRig(game::content::SceneDocument& document)
{
    // A scene that already states how the player moves needs nothing: either
    // it has a rig, or it has an authored camera and is a shot that plays
    // itself.
    for (const game::content::Entity& e : document.entities) {
        if (e.firstPerson || e.thirdPerson || e.screen)
            return false;
    }

    for (game::content::Entity& e : document.entities) {
        if (!e.playerSpawn)
            continue;
        // On the marker's own entity, so the rig is where the spawn is with no
        // transform arithmetic and no second entity to keep in step.
        game::content::FirstPersonAuthor rig;
        rig.active = true;
        e.firstPerson = rig;
        return true;
    }
    return false;
}

} // namespace

MigrateReport migrateScenes(const MigrateOptions& options)
{
    MigrateReport report;
    std::error_code ec;

    const fs::path sceneDir(options.sceneDir);
    if (!fs::is_directory(sceneDir, ec)) {
        report.error = options.sceneDir + " is not a directory";
        return report;
    }

    const fs::path projectDir = fs::absolute(options.projectDir);
    report.projectDir = projectDir.string();

    // Collected and sorted first, so a migration is reproducible and its report
    // reads the same twice.
    std::vector<fs::path> sources;
    for (fs::directory_iterator it(sceneDir, ec), end; it != end;
         it.increment(ec)) {
        if (ec)
            break;
        if (!it->is_regular_file() || it->path().extension() != ".scn")
            continue;
        // Autosaves are the editor's crash insurance, not content.
        if (it->path().string().find(".autosave.") != std::string::npos)
            continue;
        sources.push_back(it->path());
    }
    std::sort(sources.begin(), sources.end());
    if (sources.empty()) {
        report.error = "no .scn files in " + options.sceneDir;
        return report;
    }

    // Read and transform everything before writing anything: a migration that
    // half-populated a project would leave something that looks finished.
    struct Pending {
        fs::path target;
        game::content::SceneDocument document;
        MigratedScene record;
    };
    std::vector<Pending> pending;
    for (const fs::path& source : sources) {
        Pending item;
        item.record.source = source.string();
        item.record.logical = "scenes/" + source.filename().string();
        item.target = projectDir / item.record.logical;

        std::string error;
        if (!game::content::loadSceneSource(source.string(), item.document,
                                            error)) {
            item.record.error = error;
            report.scenes.push_back(item.record);
            continue;
        }
        item.record.entities = int(item.document.entities.size());
        item.record.droppedMarkers = countMarkers(item.document);
        item.record.addedPlayerRig = addPlayerRig(item.document);
        pending.push_back(std::move(item));
    }

    if (pending.empty()) {
        report.error = "every scene failed to load; nothing was written";
        return report;
    }

    // The project itself. createProject refuses an existing one, which is what
    // makes re-running a migration safe: the scenes are rewritten, the
    // project.toml an author may have edited is not.
    if (!eng::runtime::isProjectDir(projectDir)) {
        if (!eng::runtime::createProject(projectDir, options.projectName)) {
            report.error = "could not create a project at " +
                           projectDir.string();
            return report;
        }
        // The starter content is scaffolding, and this project has real scenes.
        fs::remove(projectDir / "scenes" / "main.scn", ec);
        fs::remove(projectDir / "scripts" / "cube.lua", ec);
    }

    for (Pending& item : pending) {
        fs::create_directories(item.target.parent_path(), ec);
        std::string error;
        if (!game::content::writeSceneSource(item.target.string(), item.document,
                                            error)) {
            item.record.error = error;
        }
        report.scenes.push_back(item.record);
    }

    // What the project opens on: the biggest scene, which for a tree of demos
    // and probes is the actual level rather than a five-entity test.
    std::string main = options.mainScene;
    if (main.empty()) {
        int best = -1;
        for (const MigratedScene& scene : report.scenes) {
            if (scene.error.empty() && scene.entities > best) {
                best = scene.entities;
                main = scene.logical;
            }
        }
    }
    if (!main.empty()) {
        // Rewritten in place rather than through a TOML writer: project.toml is
        // hand-editable by design, and this changes exactly one line of it.
        const fs::path file = projectDir / eng::runtime::kProjectFile;
        std::ifstream in(file);
        std::string text((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
        in.close();
        const std::size_t at = text.find("main_scene = ");
        if (at != std::string::npos) {
            const std::size_t eol = text.find('\n', at);
            text.replace(at, eol - at, "main_scene = \"" + main + "\"");
            std::ofstream out(file, std::ios::trunc);
            out << text;
        }
    }

    report.ok = true;
    eng::log::info("migrate: %zu scene(s) into %s", report.scenes.size(),
                   projectDir.c_str());
    return report;
}

} // namespace ed
