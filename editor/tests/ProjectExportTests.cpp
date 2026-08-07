// Exporting a project: what lands in the build, and what the exporter refuses.
//
// Runs the real export against a project it builds in a temp directory, with a
// stand-in for the player binary and a stand-in engine content root -- so it
// asserts the layout and the refusals without needing a 15 MB binary or the
// shipped asset tree.

#include <editor/project/ProjectExport.h>

#include <eng/runtime/Project.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace ed;
namespace fs = std::filesystem;

static void require(bool c, const char* m)
{
    if (!c) {
        std::cerr << "ProjectExportTests: " << m << '\n';
        std::exit(1);
    }
}

static fs::path scratch()
{
    const fs::path dir = fs::temp_directory_path() / "raven_export_tests";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

static void write(const fs::path& path, const std::string& text)
{
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    out << text;
}

// A minimal engine content root: one file in each directory the exporter
// considers required, so the copy is observable without the real 14 MB tree.
static fs::path makeEngineRoot(const fs::path& base)
{
    const fs::path root = base / "engine-assets";
    for (const std::string& dir : engineRuntimeDirectories())
        write(root / dir / "placeholder.txt", dir);
    return root;
}

// A project with one playable scene and one component scene.
static fs::path makeProject(const fs::path& base)
{
    const fs::path dir = base / "my-game";
    require(eng::runtime::createProject(dir, "My Game"),
            "the project should be created");

    write(dir / "scenes" / "prop.scn", R"({
  "format": "raven-scene", "version": 2, "id": "scene.prop",
  "component": true,
  "entities": [
    { "id": "block", "primitive": { "kind": "box", "size": [1.0, 1.0, 1.0] } }
  ]
}
)");
    return dir;
}

static ExportOptions baseOptions(const fs::path& project, const fs::path& out,
                                 const fs::path& engine, const fs::path& player)
{
    ExportOptions options;
    options.projectDir = project.string();
    options.outDir = out.string();
    options.engineAssets = engine.string();
    options.playerPath = player.string();
    // `strip` on a text file would fail noisily and prove nothing.
    options.keepDebugSymbols = true;
    return options;
}

static void testExportLayout()
{
    const fs::path base = scratch();
    const fs::path engine = makeEngineRoot(base);
    const fs::path project = makeProject(base);
    const fs::path player = base / "raven_player";
    write(player, "#!/bin/sh\necho player\n");
    const fs::path out = base / "build";

    const ExportReport report =
        exportProject(baseOptions(project, out, engine, player));
    require(report.ok, report.error.c_str());

    // The executable is named from the project, lowercased and dashed.
    require(fs::is_regular_file(out / "my-game"),
            "the player ships under the project's name");
    require(report.executable == (out / "my-game").string(),
            "and the report says where it went");

    // The engine's content, under assets/, with a manifest the exporter wrote
    // rather than the source tree's.
    require(fs::is_regular_file(out / "assets" / "assets.toml"),
            "a manifest is generated");
    require(fs::is_regular_file(out / "assets" / "shaders" / "placeholder.txt"),
            "engine shaders are copied");
    require(fs::is_regular_file(out / "assets" / "fonts" / "placeholder.txt"),
            "and fonts");

    // The project, under project/, which is where the player looks when it is
    // run with no argument.
    require(fs::is_regular_file(out / "project" / "project.toml"),
            "the project is shipped");
    require(eng::runtime::isProjectDir(out / "project"),
            "and is recognisable as a project, which is what makes the "
            "no-argument launch work");
    require(fs::is_regular_file(out / "project" / "scripts" / "cube.lua"),
            "scripts are shipped -- they are read at runtime");

    // Both scenes cooked, including the component one: it is not playable, but
    // it is spawnable, so it needs a .map.
    require(report.cookedScenes.size() == 2, "both scenes cooked");
    require(report.skippedScenes.empty(), "and none skipped");
    require(fs::is_regular_file(out / "project" / ".raven" / "cooked" /
                                "main.map"),
            "the main scene is cooked into the build");
    require(fs::is_regular_file(out / "project" / ".raven" / "cooked" /
                                "prop.map"),
            "and so is the component scene, which is spawnable at runtime");

    require(report.filesCopied > 0 && report.bytesCopied > 0,
            "the report counts what it moved");
}

// Empty project directories still have to appear: the project's assets.toml
// declares them as resource locations, and a pack naming a directory that is
// not there warns on every launch of the shipped game.
static void testEmptyResourceDirsSurvive()
{
    const fs::path base = scratch();
    const fs::path engine = makeEngineRoot(base);
    const fs::path project = makeProject(base);
    const fs::path player = base / "raven_player";
    write(player, "player");
    const fs::path out = base / "build";

    require(exportProject(baseOptions(project, out, engine, player)).ok,
            "export");
    require(fs::is_directory(out / "project" / "materials"),
            "an empty declared resource directory is still created");
    require(fs::is_directory(out / "project" / "textures"), "likewise");
}

static void testRefusals()
{
    const fs::path base = scratch();
    const fs::path engine = makeEngineRoot(base);
    const fs::path project = makeProject(base);
    const fs::path player = base / "raven_player";
    write(player, "player");

    // Not a project.
    {
        ExportOptions options =
            baseOptions(base / "nope", base / "b1", engine, player);
        const ExportReport report = exportProject(options);
        require(!report.ok, "a directory with no project.toml is refused");
    }

    // A non-empty output directory: the one somebody types by mistake usually
    // has something in it, and merging a build into it is not recoverable.
    {
        const fs::path out = base / "occupied";
        write(out / "important.txt", "do not lose me");
        ExportOptions options = baseOptions(project, out, engine, player);
        const ExportReport report = exportProject(options);
        require(!report.ok, "a non-empty output directory is refused");
        require(fs::is_regular_file(out / "important.txt"),
                "and nothing in it is touched");

        options.overwrite = true;
        require(exportProject(options).ok, "--overwrite goes ahead");
    }

    // A missing player: the build would be content with nothing to run it.
    {
        ExportOptions options =
            baseOptions(project, base / "b2", engine, base / "no-such-player");
        const ExportReport report = exportProject(options);
        require(!report.ok, "a missing player binary is refused");
        require(report.error.find("player") != std::string::npos,
                "and says so");
    }
}

// A project whose scenes all fail to cook must not produce a build: it would
// open to a black window on somebody else's machine.
static void testNoPlayableSceneIsRefused()
{
    const fs::path base = scratch();
    const fs::path engine = makeEngineRoot(base);
    const fs::path project = makeProject(base);
    const fs::path player = base / "raven_player";
    write(player, "player");

    std::error_code ec;
    fs::remove_all(project / "scenes", ec);
    write(project / "scenes" / "broken.scn", "{ this is not json");

    const ExportReport report =
        exportProject(baseOptions(project, base / "b3", engine, player));
    require(!report.ok, "a build with no cooked scene is refused");
    require(!report.skippedScenes.empty(),
            "and the broken scene is reported, not silently dropped");
}

int main()
{
    testExportLayout();
    testEmptyResourceDirsSurvive();
    testRefusals();
    testNoPlayableSceneIsRefused();
    std::puts("ProjectExportTests: ok");
    return 0;
}
