#include "PropSystem.h"

#include "GameContext.h"

#include <eng/Physics.h>
#include <eng/Renderer.h>

#include <glm/gtc/quaternion.hpp>

#include <string>

namespace game {

// Spawn dynamic crates and barrels in the lobby entry hall.
// Props sit a few metres in front of the spawn (toward the anchor room).
// Mesh origins are at the base; body centres are offset up by halfHeight.
// Crate: 0.8 m cube -> halfExtents {0.4, 0.4, 0.4}, body centre y = 0.4.
// Barrel: r=0.28, h=0.9 -> halfHeight 0.45, body centre y = 0.45.
void PropSystem::spawnLobby(GameContext& ctx)
{
    eng::Renderer& r = ctx.renderer;
    eng::Physics& physics = ctx.physics;
    const std::string props = ctx.assets + "/meshes/props/";
    eng::MeshHandle mCrate   = r.loadObj(props + "prop_crate.obj");
    eng::MeshHandle mBarrel0 = r.loadObj(props + "prop_barrel_p0.obj");
    eng::MeshHandle mBarrel1 = r.loadObj(props + "prop_barrel_p1.obj");

    const auto spawnCrate = [&](glm::vec3 bodyPos, float yawDeg) {
        constexpr float hh = 0.4f;
        eng::BodyDesc bd;
        bd.kind = eng::ShapeKind::Box;
        bd.halfExtents = {0.4f, hh, 0.4f};
        bd.position = bodyPos + glm::vec3(0.0f, 0.02f, 0.0f);
        bd.layer = eng::BodyLayer::Prop;
        bd.dynamic = true;
        bd.mass = 5.0f;
        bd.friction = 0.6f;
        eng::BodyHandle bh = physics.createBody(bd);
        // Node origin at mesh base = body centre lowered by halfHeight
        glm::vec3 nodePos = bodyPos - glm::vec3(0.0f, hh, 0.0f);
        eng::NodeHandle nh = r.createNode(eng::kRootNode, nodePos);
        if (yawDeg != 0.0f)
            r.setOrientation(nh, glm::angleAxis(glm::radians(yawDeg),
                                                glm::vec3(0.0f, 1.0f, 0.0f)));
        r.attachMesh(nh, mCrate, "Game/PropMarket");
        mProps.push_back({nh, bh, glm::vec3(0.0f, hh, 0.0f)});
    };

    const auto spawnBarrel = [&](glm::vec3 bodyPos, float yawDeg) {
        constexpr float halfH = 0.45f;
        constexpr float radius = 0.28f;
        eng::BodyDesc bd;
        bd.kind = eng::ShapeKind::Cylinder;
        bd.halfHeight = halfH;
        bd.radius = radius;
        bd.position = bodyPos + glm::vec3(0.0f, 0.02f, 0.0f);
        bd.layer = eng::BodyLayer::Prop;
        bd.dynamic = true;
        bd.mass = 8.0f;
        bd.friction = 0.6f;
        eng::BodyHandle bh = physics.createBody(bd);
        glm::vec3 nodePos = bodyPos - glm::vec3(0.0f, halfH, 0.0f);
        eng::NodeHandle nh = r.createNode(eng::kRootNode, nodePos);
        if (yawDeg != 0.0f)
            r.setOrientation(nh, glm::angleAxis(glm::radians(yawDeg),
                                                glm::vec3(0.0f, 1.0f, 0.0f)));
        r.attachMesh(nh, mBarrel0, "Game/PropPlanks");
        r.attachMesh(nh, mBarrel1, "Game/PropBauerhaus");
        mProps.push_back({nh, bh, glm::vec3(0.0f, halfH, 0.0f)});
    };

    // Guaranteed solid ground beneath the lobby prop staging. Per-cell
    // DungeonMap floor slabs do not reliably cover these hardcoded world
    // positions, so props were sinking / a barrel tipped half-in-floor.
    // Thin static box, top at y=0.
    {
        eng::BodyDesc gd;
        gd.kind = eng::ShapeKind::Box;
        gd.halfExtents = {4.0f, 0.10f, 3.0f};
        gd.position = {3.3f, -0.10f, 18.0f}; // top = -0.10 + 0.10 = 0.0
        gd.layer = eng::BodyLayer::Static;
        gd.dynamic = false;
        mGroundBody = physics.createBody(gd);
    }

    // Two crates stacked near the entry hall (spawn side of the anchor room)
    spawnCrate({3.0f, 0.4f, 18.0f},   10.0f);   // ground crate
    spawnCrate({3.0f, 1.2f, 18.0f},  -15.0f);   // stacked on top
    // A third crate to the side
    spawnCrate({1.5f, 0.4f, 17.0f},   30.0f);
    // Two barrels next to them
    spawnBarrel({4.5f, 0.45f, 17.5f},   0.0f);
    spawnBarrel({5.2f, 0.45f, 18.5f},  20.0f);
    // One more loose crate for variety
    spawnCrate({2.2f, 0.4f, 19.5f},  -20.0f);

    mAlive = true;
}

void PropSystem::sync(GameContext& ctx)
{
    if (!mAlive)
        return;
    eng::Renderer& r = ctx.renderer;
    for (auto& dp : mProps) {
        glm::vec3 p; glm::quat q;
        ctx.physics.getRenderTransform(dp.body, p, q);
        r.setPosition(dp.node, p - dp.renderOffset);
        r.setOrientation(dp.node, q);
    }
}

void PropSystem::teardown(eng::Physics& physics)
{
    if (!mAlive)
        return;
    for (auto& dp : mProps)
        physics.removeBody(dp.body);
    mProps.clear();
    mAlive = false;
    if (mGroundBody.valid()) {
        physics.removeBody(mGroundBody);
        mGroundBody = {};
    }
}

} // namespace game
