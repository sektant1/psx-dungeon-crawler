#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Seeded BSP dungeon generator. Emits a grid in DungeonMap's ASCII format
// (# . A L S C, space). Deterministic: same seed -> identical grid.
namespace gen {
std::vector<std::string> generate(uint32_t seed);
} // namespace gen
