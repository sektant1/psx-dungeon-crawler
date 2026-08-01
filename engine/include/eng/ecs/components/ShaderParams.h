#pragma once
#include <eng/ShaderUniforms.h>

namespace eng::ecs {

// Per-entity shader uniforms: the knobs this engine's shader family already
// exposes, driven by one entity rather than by the material every entity shares.
//
// The problem it solves. Ogre materials are shared by name -- `Game/Kit/Dungeon`
// is one object a hundred and sixty walls point at -- so
// `Renderer::setMaterialParam` tints *all* of them. Anything that wanted one
// pillar to glow therefore had to author a second material: a file, a name, an
// assetlint entry and a texture reference, for a value that changes at runtime
// anyway. That is why "make this one thing pulse" used to be a C++ job.
//
// With this component it is a component. SceneSync clones the entity's material
// on first push and sets the uniforms on that clone, so the entity gets its own
// and its neighbours keep theirs. The clone costs a material and breaks the
// entity out of its draw-call batch, which is why it happens only for entities
// that carry this -- the handful of hero objects it is for, never the hundred
// and sixty walls.
//
// Removing the component puts the shared material back. See eng::ShaderUniforms
// for what each field drives.
using ShaderParams = ShaderUniforms;

} // namespace eng::ecs
