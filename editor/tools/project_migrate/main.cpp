// raven_migrate -- bring scenes authored against this game into a project.
//
// Headless, like the cooker and the exporter. It writes .scn files and does not
// cook: cooking is `raven_export` and the editor's F5, and a migration that
// also cooked would hide which of the two had failed.

#include <editor/project/ProjectMigrate.h>

#include <eng/assets/AssetRoot.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace {

int usage()
{
    std::puts(
        "usage: raven_migrate --out <project-dir> [options]\n"
        "\n"
        "  --out <dir>       the project to create or add to\n"
        "  --scenes <dir>    where the .scn files are (default: the mounted\n"
        "                    content pack's scenes/)\n"
        "  --name <name>     the project's display name\n"
        "  --main <scene>    what it opens on (default: the biggest scene)\n"
        "\n"
        "Every scene that marks a player spawn gains an equivalent\n"
        "first-person rig, because `player_spawn` is one of this game's own\n"
        "components and a project runtime does not read it. The marker stays,\n"
        "so a migrated scene still plays in the game exactly as before.\n"
        "\n"
        "Gameplay markers this runtime has no systems for -- exits, enemy\n"
        "spawns, pickups, triggers -- are reported per scene, not translated.");
    return 2;
}

} // namespace

int main(int argc, char** argv)
{
    eng::assets::init();
    eng::assets::mount("game");

    ed::MigrateOptions options;
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        const auto value = [&](std::string& out) {
            if (i + 1 >= argc)
                return false;
            out = argv[++i];
            return true;
        };
        if (std::strcmp(arg, "--out") == 0) {
            if (!value(options.projectDir)) return usage();
        } else if (std::strcmp(arg, "--scenes") == 0) {
            if (!value(options.sceneDir)) return usage();
        } else if (std::strcmp(arg, "--name") == 0) {
            if (!value(options.projectName)) return usage();
        } else if (std::strcmp(arg, "--main") == 0) {
            if (!value(options.mainScene)) return usage();
        } else if (std::strcmp(arg, "-h") == 0 ||
                   std::strcmp(arg, "--help") == 0) {
            usage();
            return 0;
        } else {
            return usage();
        }
    }
    if (options.projectDir.empty())
        return usage();
    if (options.sceneDir.empty())
        options.sceneDir = (eng::assets::packDir("content") / "scenes").string();
    if (options.projectName.empty())
        options.projectName = "Migrated";

    const ed::MigrateReport report = ed::migrateScenes(options);
    if (!report.ok) {
        std::fprintf(stderr, "raven_migrate: %s\n", report.error.c_str());
        return 1;
    }

    std::printf("raven_migrate: %s\n", report.projectDir.c_str());
    int failed = 0;
    for (const ed::MigratedScene& scene : report.scenes) {
        if (!scene.error.empty()) {
            std::fprintf(stderr, "  FAILED %s: %s\n", scene.logical.c_str(),
                         scene.error.c_str());
            ++failed;
            continue;
        }
        std::printf("  %-34s %4d entities%s", scene.logical.c_str(),
                    scene.entities,
                    scene.addedPlayerRig ? "  +first_person rig" : "");
        if (!scene.droppedMarkers.empty()) {
            std::printf("  [not read by the player:");
            for (const auto& [name, count] : scene.droppedMarkers)
                std::printf(" %s x%d", name.c_str(), count);
            std::printf("]");
        }
        std::printf("\n");
    }
    if (failed > 0)
        std::fprintf(stderr, "raven_migrate: %d scene(s) failed\n", failed);
    return failed == 0 ? 0 : 1;
}
