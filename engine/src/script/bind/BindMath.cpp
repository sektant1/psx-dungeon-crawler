#include "script/bind/Bindings.h"

#include <glm/glm.hpp>

#include <cmath>
#include <string>

namespace eng::script {

void bindMath(sol::state& lua)
{
    // glm::vec3 directly rather than a wrapper type: every binding that takes
    // or returns a position already speaks it, and a wrapper would mean a
    // conversion at each boundary for no expressive gain.
    lua.new_usertype<glm::vec3>(
        "vec3",
        // Bound to call_constructor, not passed positionally: that makes the
        // usertype table itself callable, so a script writes vec3(1, 2, 3)
        // rather than vec3.new(1, 2, 3).
        sol::call_constructor,
        sol::constructors<glm::vec3(), glm::vec3(float),
                          glm::vec3(float, float, float)>(),
        "x", &glm::vec3::x,
        "y", &glm::vec3::y,
        "z", &glm::vec3::z,

        sol::meta_function::addition,
        [](const glm::vec3& a, const glm::vec3& b) { return a + b; },
        sol::meta_function::subtraction,
        [](const glm::vec3& a, const glm::vec3& b) { return a - b; },
        sol::meta_function::multiplication,
        sol::overload([](const glm::vec3& a, float s) { return a * s; },
                      [](float s, const glm::vec3& a) { return a * s; },
                      [](const glm::vec3& a, const glm::vec3& b) { return a * b; }),
        sol::meta_function::division,
        [](const glm::vec3& a, float s) {
            return s != 0.0f ? a / s : glm::vec3(0.0f);
        },
        sol::meta_function::unary_minus, [](const glm::vec3& a) { return -a; },
        sol::meta_function::equal_to,
        [](const glm::vec3& a, const glm::vec3& b) { return a == b; },
        sol::meta_function::to_string,
        [](const glm::vec3& a) {
            return "vec3(" + std::to_string(a.x) + ", " + std::to_string(a.y) +
                   ", " + std::to_string(a.z) + ")";
        },

        "length", [](const glm::vec3& a) { return glm::length(a); },
        // Guarded. Normalising a zero vector in glm is a division by zero and
        // yields NaN, which then propagates silently into a transform and puts
        // the entity nowhere visible. Zero is the answer a script author can
        // actually debug.
        "normalized",
        [](const glm::vec3& a) {
            const float len2 = glm::dot(a, a);
            return len2 > 1e-12f ? a / std::sqrt(len2) : glm::vec3(0.0f);
        },
        "dot",
        [](const glm::vec3& a, const glm::vec3& b) { return glm::dot(a, b); },
        "cross",
        [](const glm::vec3& a, const glm::vec3& b) { return glm::cross(a, b); });
}

} // namespace eng::script
