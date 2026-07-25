#pragma once
#include "Targeting.h"

#include <glm/glm.hpp>

#include <functional>
#include <vector>

namespace eng { class Input; }

class LiveLevel;

namespace game {

struct GameContext;

// Player look-interaction: each frame it collects the level's gameplay targets,
// resolves the one under the crosshair, and dispatches the interact key --
// toggling torches directly and firing portal descend/ascend through
// caller-supplied callbacks (which own the level-stack: depth, seeds, and the
// level rebuild). Owns a reusable target buffer to avoid per-frame allocation.
// (The on-screen "Press [E]" prompt was removed with the ImGui UI.)
class InteractionSystem {
public:
    using TransitionFn = std::function<void()>;

    void update(GameContext& ctx, LiveLevel& level, int depth,
                glm::vec3 eye, glm::vec3 forward,
                const TransitionFn& onDescend, const TransitionFn& onAscend);

private:
    std::vector<GameplayTarget> mTargets;
};

} // namespace game
