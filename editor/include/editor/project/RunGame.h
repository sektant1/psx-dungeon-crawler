#pragma once
#include <string>
#include <vector>

namespace ed {

// Launching a playtest.
//
// The game runs as a SEPARATE PROCESS, never in-editor. Two reasons: the editor
// survives a crash in the game, and the map it plays is the cooked file on disk
// -- so what gets tested is exactly what the CI cooker produces, not some
// in-memory state only the editor has.
struct RunHandle {
    int pid = -1;
    std::string error;
    bool running() const { return pid > 0; }
};

// Spawns `gameExe mapPath`, redirecting the child's output to `logPath` so a
// failed launch leaves evidence instead of vanishing.
//
// `env` is a list of "KEY=VALUE" strings handed to the child on top of the
// editor's own environment, and any inherited variable with the same key is
// *dropped* rather than duplicated -- a stale PSX_PLAY_FROM in the editor's
// environment silently overriding the choice made here is the exact bug this
// rule exists for, and every switch added since has the same shape.
//
// Environment rather than argv on purpose: every one of these is a variable the
// game already reads, so the editor is choosing between the game's own options
// instead of growing a second set of them.
RunHandle launchGame(const std::string& gameExe, const std::string& mapPath,
                     const std::string& logPath,
                     const std::vector<std::string>& env = {});

// The environment a playtest is launched with, as "KEY=VALUE" strings.
//
// A pure function of the settings, so what F5 does is testable without
// spawning anything. Empty values are omitted rather than exported empty: the
// game tests for the variable's *presence* in several places, and PSX_DEBUG_UI=
// would read as "on".
struct PlaytestEnvironment {
    std::string playFrom;    // empty for the scene's own spawn
    std::string renderPreset; // empty for the engine default
    bool console = false;
    bool colliders = false;
    bool fullscreen = false;
};
std::vector<std::string> playtestEnvironment(const PlaytestEnvironment&);

// Non-blocking. Returns true while the child is alive; when it exits, `exitCode`
// receives its status and the handle is cleared.
bool pollGame(RunHandle& handle, int& exitCode);
void stopGame(RunHandle& handle);

// The game binary that sits beside this editor in the build tree. Derived from
// argv[0] rather than configured, because the two are always built together.
std::string siblingExecutable(const std::string& argv0, const std::string& name);

} // namespace ed
