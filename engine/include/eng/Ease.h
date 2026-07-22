#pragma once

namespace eng {

// Interpolation curves for Actions. t is normalized [0,1]; returns eased [0,1].
enum class Ease { Linear, QuadIn, QuadOut, QuadInOut, SinIn, SinOut, SinInOut };

float easeApply(float t, Ease ease);

} // namespace eng
