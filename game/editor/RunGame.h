#pragma once
#include <string>

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
RunHandle launchGame(const std::string& gameExe, const std::string& mapPath,
                     const std::string& logPath);

// Non-blocking. Returns true while the child is alive; when it exits, `exitCode`
// receives its status and the handle is cleared.
bool pollGame(RunHandle& handle, int& exitCode);
void stopGame(RunHandle& handle);

// The game binary that sits beside this editor in the build tree. Derived from
// argv[0] rather than configured, because the two are always built together.
std::string siblingExecutable(const std::string& argv0, const std::string& name);

} // namespace ed
