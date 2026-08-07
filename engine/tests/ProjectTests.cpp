// eng::runtime::Project: what a project.toml says, and what createProject
// writes.
//
// Headless and file-based, which is the point: a project is a directory, so
// the only honest test of reading one is to write one and read it back.

#include <eng/runtime/Project.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

static void require(bool c, const char* m)
{
    if (!c) {
        std::cerr << "ProjectTests: " << m << '\n';
        std::exit(1);
    }
}

// A scratch directory that cleans up after itself, so a failing test does not
// leave the next run a project it did not make.
struct TempDir {
    fs::path path;
    explicit TempDir(const char* name)
        : path(fs::temp_directory_path() / ("raven_project_test_" +
                                            std::string(name)))
    {
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path, ec);
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

static void write(const fs::path& path, const std::string& text)
{
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    out << text;
}

static void testCreateThenLoad()
{
    TempDir tmp("create");
    const fs::path dir = tmp.path / "my-game";

    require(eng::runtime::createProject(dir, "My Game"),
            "createProject should succeed on an empty directory");
    require(eng::runtime::isProjectDir(dir),
            "a created project should be recognised as one");

    // The starter content is part of the contract: a new project that opens to
    // nothing is a new project nobody can tell is working.
    require(fs::is_regular_file(dir / "project.toml"), "project.toml written");
    require(fs::is_regular_file(dir / "assets.toml"), "assets.toml written");
    require(fs::is_regular_file(dir / "scenes" / "main.scn"),
            "starter scene written");
    require(fs::is_regular_file(dir / "scripts" / "cube.lua"),
            "starter script written");

    eng::runtime::Project project;
    require(eng::runtime::loadProject(dir, project), "the project should load");
    require(project.name == "My Game", "name round-trips");
    require(project.mainScene == "scenes/main.scn", "main_scene round-trips");
    require(project.windowTitle == "My Game", "window title defaults to name");
    require(project.windowWidth == 1280 && project.windowHeight == 720,
            "window size round-trips");
    require(project.renderPreset == "psx", "render preset round-trips");
    require(project.scriptRoot == "scripts", "script root round-trips");

    // The cooked path is derived on both sides rather than agreed by
    // convention, so this is the assertion that keeps the editor and the
    // player looking in the same place.
    require(project.cookedMainScene() ==
                dir / ".raven" / "cooked" / "main.map",
            "cooked main scene sits under the work directory");
    require(project.workDir() == dir / ".raven", "work dir is .raven");

    // Creating over an existing project must fail: overwriting somebody's game
    // because they picked the wrong directory is not recoverable.
    require(!eng::runtime::createProject(dir, "Again"),
            "createProject should refuse an existing project");
}

static void testDefaults()
{
    TempDir tmp("defaults");
    const fs::path dir = tmp.path / "sparse";
    write(dir / "project.toml", "[project]\nmain_scene = \"scenes/a.scn\"\n");

    eng::runtime::Project project;
    require(eng::runtime::loadProject(dir, project),
            "a project with only main_scene should load");
    // The directory's name, not "Untitled": somebody who omits the key almost
    // certainly meant the thing the folder is called.
    require(project.name == "sparse", "name defaults to the directory name");
    require(project.windowTitle == "sparse", "title defaults to the name");
    require(project.windowWidth == 1280, "width has a default");
    require(project.renderPreset.empty(), "no preset means the engine chooses");
    require(project.scriptRoot == "scripts", "script root has a default");
}

static void testFailures()
{
    TempDir tmp("failures");

    eng::runtime::Project project;
    require(!eng::runtime::loadProject(tmp.path / "nope", project),
            "a missing directory is not a project");
    require(!eng::runtime::isProjectDir(tmp.path / "nope"),
            "isProjectDir is false for a missing directory");

    // A project with no scene to open has nothing to play, and diagnosing that
    // here is much kinder than a black window later.
    const fs::path noScene = tmp.path / "no-scene";
    write(noScene / "project.toml", "[project]\nname = \"X\"\n");
    require(!eng::runtime::loadProject(noScene, project),
            "a project with no main_scene must be refused");

    const fs::path malformed = tmp.path / "malformed";
    write(malformed / "project.toml", "[project\nname = broken\n");
    require(!eng::runtime::loadProject(malformed, project),
            "a malformed project.toml must be refused");
}

static void testPathsAreRelativeToTheProject()
{
    TempDir tmp("paths");
    const fs::path dir = tmp.path / "nested" / "deeper";
    write(dir / "project.toml",
          "[project]\nmain_scene = \"levels/one.scn\"\n");

    eng::runtime::Project project;
    require(eng::runtime::loadProject(dir, project), "loads from a nested dir");
    // Canonical, so two spellings of the same directory produce one project
    // directory -- which is what makes the recents list dedupe correctly.
    require(project.dir.is_absolute(), "the project dir is absolute");
    require(project.cookedMainScene().filename() == "one.map",
            "the cooked name follows the scene's stem");
}

int main()
{
    testCreateThenLoad();
    testDefaults();
    testFailures();
    testPathsAreRelativeToTheProject();
    std::puts("ProjectTests: ok");
    return 0;
}
