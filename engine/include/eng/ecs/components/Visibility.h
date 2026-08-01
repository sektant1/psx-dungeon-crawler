#pragma once

namespace eng::ecs {

// Whether this entity draws. Pushed to its renderer node by SceneSync, and only
// when the value changes -- hiding is a state, not an event.
//
// Its own component rather than a flag on MeshRenderer because the thing being
// hidden is the *entity*, not one of the ways it draws: an entity with a mesh, a
// light and an emitter has one visibility, and an editor isolating a selection,
// a distance cull and a quest that reveals a door all want to write the same
// one. Absent means visible, so the component costs nothing until something
// actually hides.
//
// Hiding is not destroying: the node, the body and every component survive, so
// showing it again is a bool, and physics keeps colliding. An entity that must
// also stop colliding drops its Collider.
struct Visibility {
    bool visible = true;
};

} // namespace eng::ecs
