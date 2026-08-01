#pragma once
#include <string>
namespace game {
// Play an authored .map as a standalone eng::Application (own Engine + Physics).
// Reached from `game <file.map>`; returns the process exit code. It mounts the
// game content set itself, so the caller passes no asset root.
// Plays a cooked map in the stripped loop. argc/argv are forwarded so the
// engine's own switches work here too -- --record above all, because a scene
// with an authored camera plays itself and is therefore the one thing in this
// project that can be captured without a hand on the mouse.
int runMap(const std::string& mapPath, int argc = 0, char** argv = nullptr);

// Does this cooked map author a Camera?
//
// Asked before either loop starts, because it is what decides which one runs:
// placing a camera in a scene *is* saying "look through this", and nothing else
// in the game does. Reads the file into a scratch registry -- cheap next to
// building a level, and the alternative was a flag the author had to remember.
//
// False for a map that is missing or malformed, so a broken file still reaches
// the loop that reports it rather than being diagnosed here.
bool mapHasCamera(const std::string& mapPath);
}
