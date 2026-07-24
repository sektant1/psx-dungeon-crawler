#pragma once
#include <eng/Handles.h>

#include <glm/glm.hpp>

#include <vector>

namespace eng { class Physics; }

namespace game {

struct GameContext;

// Dynamic physics props: the lobby entry-hall crates and barrels. Each is a
// Jolt rigid body whose render node is synced from the interpolated physics
// transform every frame. Lobby-only and spawned once; the bodies are removed
// before any clearScene (which owns the render nodes) and re-created never
// (known limitation preserved from the original inline code).
class PropSystem {
public:
    // Spawn the lobby staging (crates, barrels, and a guaranteed ground slab).
    void spawnLobby(GameContext& ctx);
    // Body -> node reconciliation; call each frame after physics has stepped and
    // the interpolation alpha is set.
    void sync(GameContext& ctx);
    // Remove all prop bodies (+ the ground slab) before a clearScene or exit.
    void teardown(eng::Physics& physics);
    bool alive() const { return mAlive; }

private:
    // renderOffset = body centre - mesh base (the mesh origin sits at the base,
    // so the node is placed halfHeight below the body centre).
    struct Prop {
        eng::NodeHandle node;
        eng::BodyHandle body;
        glm::vec3 renderOffset{0.0f};
    };
    std::vector<Prop> mProps;
    bool mAlive = false;
    eng::BodyHandle mGroundBody{};
};

} // namespace game
