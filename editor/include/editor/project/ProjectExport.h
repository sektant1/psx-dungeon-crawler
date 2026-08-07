#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace ed {

// Turning a project into something somebody else can run.
//
// The output is a directory holding the player binary, the project's content
// with every scene cooked, and the engine content the renderer cannot start
// without. No toolchain, no source tree, no editor: copy the directory to
// another machine and run the executable.
//
// It leans on two things that already exist rather than inventing a package
// format. `eng::assets::init` already discovers `<exe>/assets`, so a binary
// sitting beside an assets/ directory finds its content with no new code; and
// `raven_player` already plays a project directory, so an exported build is the
// same runtime doing the same thing it does in the editor's F5.

struct ExportOptions {
    // The project to export. Must contain project.toml.
    std::string projectDir;
    // Where to write. Created if missing. Refused if it exists and is not
    // empty, unless `overwrite` -- writing a build into somebody's home
    // directory and merging with whatever was there is not recoverable.
    std::string outDir;
    bool overwrite = false;

    // The name the shipped executable gets. Empty uses the project's name,
    // lowercased with spaces as dashes -- "My Game" ships as `my-game`.
    std::string executableName;

    // Where to find the player to ship. Empty looks beside the exporting
    // binary, which is where a build tree puts them both.
    std::string playerPath;

    // The engine content root to take the builtin pack from. Empty uses the
    // mounted one.
    std::string engineAssets;

    // Ship the player with its debug symbols. Off by default: this tree builds
    // RelWithDebInfo, so an unstripped player is 214 MB of which 200 MB is
    // DWARF that is useless to whoever was handed the binary.
    bool keepDebugSymbols = false;
};

// What an export did, so a caller can report it rather than guess.
struct ExportReport {
    bool ok = false;
    std::string error; // set when !ok

    std::vector<std::string> cookedScenes;  // logical paths, as authored
    std::vector<std::string> skippedScenes; // that failed to cook, with reasons
    std::size_t filesCopied = 0;
    std::size_t bytesCopied = 0;
    std::string executable; // absolute path to the shipped binary
};

// The directories of the engine's content pack a runtime cannot start without.
//
// Deliberately a list rather than "everything": the content tree this engine
// ships beside is 412 MB, of which 302 MB is `source/` -- pipeline inputs that
// nothing ever loads -- and 92 MB is this game's meshes, which somebody else's
// project has no use for. Exporting the lot would make every game built with
// this engine a 400 MB download of somebody else's art.
//
// What is here is what the renderer and the loading screen resolve by name at
// startup: shaders and compositors to compile, fonts and ui to draw with,
// materials and their textures, the particle library, and the config the engine
// reads. A project's own content is copied separately and shadows any of it.
//
// This is the honest version of the "carve out engine builtins" job the project
// runtime has owed since M1: it does not reorganise assets/, it states which
// parts of it are the engine's. Splitting the tree for real would let this list
// become "the engine pack", and until then this is where the answer lives.
const std::vector<std::string>& engineRuntimeDirectories();

// Cooks every scene in the project, copies what is needed, and writes the
// player beside it. Never touches the project directory itself.
ExportReport exportProject(const ExportOptions& options);

} // namespace ed
