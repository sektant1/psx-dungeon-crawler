#pragma once
#include <glm/glm.hpp>

class DungeonMap;
namespace eng { class Physics; }

namespace game {

// CPU frame-phase timings for the Diagnostics window. The game loop writes
// per-phase milliseconds into ms[] each iteration and pushes the frame total
// into a rolling history for the plot.
struct ProfHud {
    enum Phase { Physics, World, Player, Weapons, Render, kCount };
    static constexpr const char* kNames[kCount] = {
        "Physics", "World", "Player", "Weapons", "Render"};
    float ms[kCount] = {0.0f};
    static constexpr int kHist = 120;
    float frameHist[kHist] = {0.0f};
    int histHead = 0;
    void pushFrame(float totalMs) {
        frameHist[histHead] = totalMs;
        histHead = (histHead + 1) % kHist;
    }
};

// Read-only generated-grid inspector: projects the dungeon grid into its own
// ImGui window (owns its Begin/End). The dungeon owns the data; this only draws.
void drawDungeonMap(const DungeonMap& map, glm::vec3 playerPos);

// Standalone Diagnostics window: rolling frame-time plot, per-phase CPU bar
// graph with a numbered legend, and physics body counts.
void drawDiagnostics(const ProfHud& prof, eng::Physics& physics);

} // namespace game
