// raven_export -- turn a project into something somebody else can run.
//
// Headless, like the cooker: a build machine ships a game without a GPU, and
// the editor calls the same function in-process. It links the authoring half
// (it has to cook), which is why it is a tool and not part of the player -- the
// binary it *ships* still has no game or editor code in it at all.

#include <editor/project/ProjectExport.h>

#include <eng/Log.h>
#include <eng/assets/AssetRoot.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace {

int usage()
{
    std::puts(
        "usage: raven_export <project-dir> --out <dir> [options]\n"
        "\n"
        "  --out <dir>       where to write the build\n"
        "  --name <exe>      the shipped executable's name (default: the\n"
        "                    project's, lowercased and dashed)\n"
        "  --player <path>   the raven_player to ship (default: build/)\n"
        "  --assets <dir>    engine content root (default: the mounted one)\n"
        "  --overwrite       write into a directory that is not empty\n"
        "\n"
        "The result is a directory holding the player, the engine content it\n"
        "needs, and the project with every scene cooked. Run the executable\n"
        "inside it; nothing else has to be installed.");
    return 2;
}

} // namespace

int main(int argc, char** argv)
{
    // Mounted so the exporter can resolve the engine pack and the kit by name,
    // the same way every other tool in this tree finds content.
    eng::assets::init();
    eng::assets::mount("game");

    ed::ExportOptions options;
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        const auto value = [&](std::string& out) {
            if (i + 1 >= argc)
                return false;
            out = argv[++i];
            return true;
        };
        if (std::strcmp(arg, "--out") == 0) {
            if (!value(options.outDir)) return usage();
        } else if (std::strcmp(arg, "--name") == 0) {
            if (!value(options.executableName)) return usage();
        } else if (std::strcmp(arg, "--player") == 0) {
            if (!value(options.playerPath)) return usage();
        } else if (std::strcmp(arg, "--assets") == 0) {
            if (!value(options.engineAssets)) return usage();
        } else if (std::strcmp(arg, "--overwrite") == 0) {
            options.overwrite = true;
        } else if (std::strcmp(arg, "-h") == 0 ||
                   std::strcmp(arg, "--help") == 0) {
            usage();
            return 0;
        } else if (arg[0] != '-' && options.projectDir.empty()) {
            options.projectDir = arg;
        } else {
            return usage();
        }
    }
    if (options.projectDir.empty() || options.outDir.empty())
        return usage();

    const ed::ExportReport report = ed::exportProject(options);

    // Everything that did not cook, before the verdict: a build that shipped
    // with three of its four levels missing has to say so loudly, and saying it
    // after "done" is saying it where nobody reads.
    for (const std::string& skipped : report.skippedScenes)
        std::fprintf(stderr, "raven_export: skipped %s\n", skipped.c_str());

    if (!report.ok) {
        std::fprintf(stderr, "raven_export: %s\n", report.error.c_str());
        return 1;
    }

    std::printf("raven_export: %s\n", report.executable.c_str());
    std::printf("  %zu scene(s) cooked, %zu file(s), %.1f MB\n",
                report.cookedScenes.size(), report.filesCopied,
                double(report.bytesCopied) / (1024.0 * 1024.0));
    if (!report.skippedScenes.empty())
        std::printf("  %zu scene(s) SKIPPED -- see above\n",
                    report.skippedScenes.size());
    return report.skippedScenes.empty() ? 0 : 1;
}
