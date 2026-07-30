#pragma once
#include <eng/Handles.h>

#include <glm/glm.hpp>

#include <vector>

namespace eng { class Physics; }

namespace game {

struct GameContext;
struct ScenePlacement;

// Dynamic physics props authored as scene markers. Each is a
// Jolt rigid body whose render node is synced from the interpolated physics
// transform every frame. Bodies are removed before clearScene.
class PropSystem {
public:
    void spawnShowroom(GameContext& ctx,
                       const std::vector<ScenePlacement>& placements);
    // Body -> node reconciliation; call each frame after physics has stepped and
    // the interpolation alpha is set.
    void sync(GameContext& ctx);
    // Remove all prop bodies before a clearScene or exit.
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
};

} // namespace game
