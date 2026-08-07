// raven_player -- plays a project.
//
// The whole binary is an argument parse and a ProjectApp. That is the point:
// everything a game made in this engine needs at runtime is in eng_runtime, so
// a project is played by a program that knows nothing about any particular
// game. If this file ever grows gameplay, the thing it grew belongs one layer
// down.
//
//     raven_player <project-dir>     play a project
//     raven_player <scene.map>       play one cooked scene, no project
//
// The second form exists because it costs nothing: a cooked map is what a
// project's scene compiles to, so "play this file" is the same code path with
// the project fields left empty.

#include <eng/Log.h>
#include <eng/app/Application.h>
#include <eng/render/GifRecorder.h>
#include <eng/runtime/Project.h>
#include <eng/runtime/ProjectApp.h>

#include <cstdio>
#include <filesystem>
#include <string>

namespace {

void usage()
{
    std::puts("usage: raven_player <project-dir | scene.map>\n"
              "\n"
              "  <project-dir>  a directory containing project.toml\n"
              "  <scene.map>    one cooked scene, played without a project\n"
              "\n"
              "environment:\n"
              "  RAVEN_PLAY_MAP    play this cooked map instead of the "
              "project's main scene\n"
              "  RAVEN_PLAY_FROM   x,y,z to start at instead of the spawn\n"
              "  RAVEN_RENDER_PRESET  override the project's render preset");
}

} // namespace

int main(int argc, char** argv)
{
    std::string target;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            usage();
            return 0;
        }
        // Everything else is the engine's own (--preset, --record, ...) and is
        // parsed downstream from argv. The first non-switch is what to play.
        if (!arg.empty() && arg[0] != '-' && target.empty())
            target = arg;
    }

    if (target.empty()) {
        usage();
        return 2;
    }

    std::error_code ec;
    eng::runtime::Project project;
    if (std::filesystem::is_directory(target, ec)) {
        if (!eng::runtime::loadProject(target, project))
            return 1;
    } else if (std::filesystem::is_regular_file(target, ec)) {
        // A bare cooked scene. No project directory, so ProjectApp mounts
        // nothing extra and falls back to the engine's own config.
        project.name = std::filesystem::path(target).stem().string();
        project.mainScene = target;
    } else {
        eng::log::error("raven_player: no such project or scene: %s",
                        target.c_str());
        return 1;
    }

    eng::runtime::ProjectApp app(std::move(project));
    app.setRecording(eng::GifRecorder::optionsFromArgs(argc, argv));
    return eng::runApplication(app, argc, argv);
}
