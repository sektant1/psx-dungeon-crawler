#pragma once
#include <string>

namespace eng::ecs {

// Draw this entity's mesh with a different material than its MeshRenderer names.
//
// Separate from MeshRenderer because the two answer to different authors and
// change at different rates. The mesh and its material come from the asset; an
// override is a *scene* decision -- this one pillar is the cracked variant,
// this door glows while the quest is live -- so making it its own component
// means it can be added, tweaked and dropped on any entity that draws, without
// touching the thing it overrides.
//
// An empty `material` is a no-op rather than a blank mesh: an override that has
// not been filled in yet must not erase what is already on screen.
//
// It carries only the *name*. Tint and rim used to live here too and never
// reached a shader -- fields that save, load and edit and then do nothing, which
// is worse than not having them, because the inspector says otherwise. Those
// values are eng::ecs::ShaderParams now, which owns a per-entity material clone
// and can actually push them. The split is also the right one: swapping which
// material an entity wears and tuning uniforms on the one it has are different
// operations with different costs.
struct MaterialOverride {
    std::string material;
};

} // namespace eng::ecs
