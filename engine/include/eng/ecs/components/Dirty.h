#pragma once

namespace eng::ecs {

// Tag: this entity's world transform, and its subtree's, needs recomputing.
//
// Set by every mutation that can move something (setLocalTransform, setParent,
// create) and cleared by the resolve pass. It is also what SceneSync uses to
// decide which transforms to push, so an unmoving scene costs nothing per
// frame -- the reason the tag exists rather than resolving everything.
struct Dirty {};

} // namespace eng::ecs
