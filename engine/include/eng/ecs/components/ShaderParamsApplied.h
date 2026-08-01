#pragma once
#include <eng/ecs/components/ShaderParams.h>

namespace eng::ecs {

// What SceneSync last pushed for this entity, so it pushes only on a change.
//
// Setting a uniform means resolving a named constant on a cloned material's
// program parameters, which is a hash lookup per field per frame -- cheap once,
// wasteful sixty times a second for a value nobody touched. The comparison is
// on the whole struct because the fields are pushed together.
struct ShaderParamsApplied {
    ShaderParams value;
    bool valid = false; // nothing pushed yet: the first sync always pushes
};

} // namespace eng::ecs
