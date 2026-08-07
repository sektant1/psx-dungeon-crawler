#pragma once

#include <filesystem>
#include <string>

namespace eng::runtime {

// What a project.toml says, parsed and validated.
//
// A "project" is the unit raven_player plays and the editor authors: a
// directory with a project.toml describing it and an assets.toml declaring its
// content. That is the whole of it -- there is no project database, no
// registry of installed projects, and nothing outside the directory has to
// know it exists. Copying the directory copies the game.
//
// Pure data with no renderer and no window, which is what lets the editor read
// one to populate a dialog, a test read one from a temp directory, and the
// player read one to boot -- all through the same code, so "what does this
// field mean" has one answer.
//
// The file is read with eng::Config, so it is the same TOML dialect the rest
// of the project's configuration uses, and [bindings] arrives already parsed
// as action -> list of SDL key names. That table is not copied into this
// struct: the input map belongs to the Config the app hands to the engine, and
// duplicating it here would create a second answer to "what is bound to W".
struct Project {
    // Absolute, canonical. Every relative path in the file resolves against
    // it, and it is what eng::assets::mountProject is given.
    std::filesystem::path dir;

    // [project]
    std::string name;      // display name; defaults to the directory's name
    std::string mainScene; // required, relative to dir: "scenes/main.scn"

    // [window]
    std::string windowTitle; // defaults to `name`
    int windowWidth = 1280;
    int windowHeight = 720;

    // [render] preset -- a name eng::renderPresetFromName understands. Empty
    // leaves the choice to RAVEN_RENDER_PRESET and then the engine default,
    // which is also what an unrecognised name falls back to.
    std::string renderPreset;

    // [scripts] root -- the logical directory the script host resolves paths
    // against and watches for hot reload.
    std::string scriptRoot = "scripts";

    // The cooked map for `mainScene`, under dir/.raven/cooked. Cooking is the
    // editor's job; this only says where the player looks for the result, so
    // that both sides derive the path rather than agreeing on a convention.
    std::filesystem::path cookedMainScene() const;

    // dir/.raven -- per-user working state (cooked maps, logs). Not content:
    // nothing here is authored and none of it is worth committing.
    std::filesystem::path workDir() const;
};

// The file name a project directory is recognised by.
inline constexpr const char* kProjectFile = "project.toml";

// Read <dir>/project.toml. Returns false and logs a named reason on a missing
// or malformed file, or on a missing [project] main_scene -- a project with no
// scene to open has nothing to play, and diagnosing that here is much kinder
// than a black window later. Everything else has a default.
bool loadProject(const std::filesystem::path& dir, Project& out);

// Is there a project here? Cheap enough to call on every entry of a file
// dialog, which is what it is for.
bool isProjectDir(const std::filesystem::path& dir);

// Write a new project: project.toml, assets.toml, scenes/, scripts/, and a
// starter scene and script. Returns false and logs if the directory cannot be
// created or is already a project -- overwriting somebody's game because they
// picked the wrong directory in a file dialog is not a recoverable mistake.
//
// The starter content is deliberately the smallest thing that proves the loop
// works end to end: a camera, a lit floor, one cube, and a script on the cube
// that reads the movement actions. Somebody who makes a new project and presses
// F5 should see something move.
bool createProject(const std::filesystem::path& dir, const std::string& name);

} // namespace eng::runtime
