#pragma once
#include <eng/Handles.h>

namespace eng::ecs {

// The renderer scene node backing this entity. Populated by SceneSync; callers
// never set it. Its presence is also how SceneSync knows an entity has already
// been materialised, so writing one by hand strands a node.
struct NodeRef {
    NodeHandle handle;
};

} // namespace eng::ecs
