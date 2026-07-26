#pragma once

#include <algorithm>

enum class ShowcaseVisibilityState {
    Uninitialized,
    Hidden,
    Visible,
};

// Hysteresis prevents an authored exhibit from flickering when the camera
// hovers around its visibility boundary. An uninitialized or hidden root
// enters at its range; a visible root leaves only after the additional
// hysteresis distance.
inline bool showcaseVisibleAtDistance(ShowcaseVisibilityState state,
                                      float distance,
                                      float visibilityRange,
                                      float hysteresis)
{
    if (visibilityRange <= 0.0f)
        return true;
    const float exitRange =
        visibilityRange + std::max(0.0f, hysteresis);
    return distance <=
           (state == ShowcaseVisibilityState::Visible
                ? exitRange
                : visibilityRange);
}
