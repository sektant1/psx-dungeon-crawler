#pragma once
#include <string>
namespace game {
// Play an authored .map as a standalone eng::Application (own Engine + Physics).
// Reached from `game <file.map>`; returns the process exit code. It mounts the
// game content set itself, so the caller passes no asset root.
int runMap(const std::string& mapPath);
}
