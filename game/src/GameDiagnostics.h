#pragma once
#include <eng/Log.h>

namespace game {

// CPU frame-phase timings. The game loop writes per-phase milliseconds into ms[]
// each iteration; logSummary() emits a periodic line so profiling survives
// without any on-screen UI (the ImGui debug windows were removed). A future UI
// can read ms[]/frameHist directly.
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

    // Log a one-line per-phase breakdown. Call every N frames from the loop.
    void logSummary() const {
        float total = 0.0f;
        for (float m : ms) total += m;
        eng::log::info(
            "Frame %.2f ms (%.0f fps) | Physics %.2f World %.2f Player %.2f "
            "Weapons %.2f Render %.2f",
            total, total > 0.0f ? 1000.0f / total : 0.0f, ms[Physics], ms[World],
            ms[Player], ms[Weapons], ms[Render]);
    }
};

} // namespace game
