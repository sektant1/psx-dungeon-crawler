#pragma once
#include <glm/glm.hpp>

namespace eng::ecs {

// Constant rotation about a local axis, applied to the entity's Transform by
// `spinSystem`.
//
// The smallest useful piece of authored motion: a pickup that turns, a rune
// ring, a slowly rotating hazard. Worth a component precisely because it is
// small -- without one, every spinning object in the game is a line of C++ in
// somebody's update loop that has to find the object again each frame.
//
// It writes the *local* Transform, so it composes: spin a socket and everything
// parented to it turns with it.
struct Spin {
    glm::vec3 axis{0.0f, 1.0f, 0.0f}; // normalised by the system; zero = no spin
    float degreesPerSecond = 90.0f;
};

} // namespace eng::ecs
