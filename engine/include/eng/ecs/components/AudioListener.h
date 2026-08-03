#pragma once

namespace eng::ecs {

// Candidate listener carried by an entity transform. AudioSync selects the
// highest-priority active listener when its World is allowed to drive audio.
struct AudioListener {
    int priority = 0;
    bool active = true;
};

} // namespace eng::ecs
