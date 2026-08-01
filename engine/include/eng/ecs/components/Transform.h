#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace eng::ecs {

// Local transform, relative to the Parent (or to the world when there is none).
// This is the authored value: the one an editor writes and a system animates.
// Its composed form lives in WorldTransform and is derived, never set.
struct Transform {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; // identity (w,x,y,z)
    glm::vec3 scale{1.0f};
};

// The matrix this transform composes to. The one place T*R*S order is written
// down, so the resolve pass and anything that builds a matrix by hand agree.
inline glm::mat4 matrixOf(const Transform& t)
{
    glm::mat4 m = glm::mat4_cast(t.rotation);
    m[0] *= t.scale.x;
    m[1] *= t.scale.y;
    m[2] *= t.scale.z;
    m[3] = glm::vec4(t.position, 1.0f);
    return m;
}

// The inverse: a world matrix back into position/rotation/scale, for consumers
// that take the three separately -- the renderer's node setters, a physics body.
//
// Pushing the world *position* with the entity's *local* rotation is the
// plausible shortcut here, and it is wrong exactly when hierarchy starts being
// used for something: a child of a rotated parent then draws at the right place
// facing the wrong way, which reads as an art bug rather than a transform one.
//
// Uniform-scale assumption: a shear (non-uniform scale under rotation) cannot be
// expressed as position/rotation/scale at all, and the axis lengths below are
// the closest honest answer.
inline Transform decompose(const glm::mat4& m)
{
    Transform t;
    t.position = glm::vec3(m[3]);

    glm::vec3 x(m[0]), y(m[1]), z(m[2]);
    t.scale = {glm::length(x), glm::length(y), glm::length(z)};
    // A mirrored basis (negative determinant) has no rotation of its own; the
    // convention, as in glm's own decompose, is to fold the flip into X.
    if (glm::dot(glm::cross(x, y), z) < 0.0f) {
        t.scale.x = -t.scale.x;
        x = -x;
    }
    // A degenerate axis would make quat_cast produce NaN and take the node,
    // the body and the frame with it. Identity is the safe reading.
    if (t.scale.x == 0.0f || t.scale.y == 0.0f || t.scale.z == 0.0f)
        return t;

    glm::mat3 basis(x / glm::abs(t.scale.x), y / t.scale.y, z / t.scale.z);
    t.rotation = glm::normalize(glm::quat_cast(basis));
    return t;
}

} // namespace eng::ecs
