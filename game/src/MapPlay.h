#pragma once
#include <string>
namespace game {
// Play an authored .map as a standalone eng::Application (own Engine + Physics).
// Reached from `game <file.map>`; returns the process exit code.
int runMap(const std::string& assetDir, const std::string& mapPath);
}
