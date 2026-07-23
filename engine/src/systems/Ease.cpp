#include <eng/Ease.h>
#include <cmath>

namespace eng {

float easeApply(float t, Ease ease) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    constexpr float kPi = 3.14159265358979323846f;
    switch (ease) {
        case Ease::Linear:    return t;
        case Ease::QuadIn:    return t * t;
        case Ease::QuadOut:   return t * (2.0f - t);
        case Ease::QuadInOut: return t < 0.5f ? 2.0f*t*t : -1.0f + (4.0f - 2.0f*t)*t;
        case Ease::SinIn:     return 1.0f - std::cos(t * kPi * 0.5f);
        case Ease::SinOut:    return std::sin(t * kPi * 0.5f);
        case Ease::SinInOut:  return -0.5f * (std::cos(kPi * t) - 1.0f);
    }
    return t;
}

} // namespace eng
