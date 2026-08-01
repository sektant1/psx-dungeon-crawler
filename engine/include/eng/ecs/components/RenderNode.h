#pragma once

namespace eng::ecs {

// Tag: give this entity a renderer node even though it draws nothing itself.
//
// For an attachment parent whose children are the visible part, or an entity
// something else will hang off. Without it, an entity with only a Transform
// stays gameplay-side and costs the renderer nothing -- which is what most
// entities in a consolidated world are: a combatant's stats, a spawner, a
// trigger volume. MeshRenderer, LightRef and ParticleEmitter each imply a node
// on their own, so this is only for the case where none of them applies.
struct RenderNode {};

} // namespace eng::ecs
