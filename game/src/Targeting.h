#pragma once

#include <glm/glm.hpp>

#include <vector>

enum class TargetKind { Torch, PortalDown, PortalUp, Prop, Actor };

struct GameplayTarget {
    TargetKind kind;
    int id = -1;
    glm::vec3 position{0.0f}; // centre of the thing, not its footprint
    float reach = 2.5f;
    // How big the target is, for the aim test. A torch head is small; a portal
    // membrane is over two metres across, and a fixed view cone made it
    // *unusable at point-blank range* (see aimedTarget).
    float radius = 0.45f;
    // Index into whatever catalog owns this target's description (the prop
    // catalog, today). Presentation resolves names through it, so the
    // targeting seam still carries no strings and no pointers. Deliberately
    // last: every existing target site builds this aggregate positionally.
    int catalogIndex = -1;
};

namespace game {

// Stable value copied from a targeting result for UI and other presentation.
// No pointer into a system-owned scratch buffer crosses the boundary.
struct InteractionFocus {
    bool available = false;
    TargetKind kind = TargetKind::Torch;
    int id = -1;
    bool active = false; // currently lit for a torch; unused by portals
    float distance = 0.0f;
    int catalogIndex = -1; // see GameplayTarget::catalogIndex
};

} // namespace game

// Selects the nearest target whose volume the aim ray enters within reach.
// Torch and portal adapters feed the same seam, so arbitration policy lives
// once.
const GameplayTarget* aimedTarget(const std::vector<GameplayTarget>& targets,
                                  glm::vec3 eye, glm::vec3 forward);
