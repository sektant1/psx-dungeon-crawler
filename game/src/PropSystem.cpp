#include "PropSystem.h"
#include "GameCollision.h"

#include "GameContext.h"

#include <eng/Physics.h>
#include <eng/Renderer.h>

#include <glm/gtc/quaternion.hpp>

#include <string>

namespace game {

// Spawn a dedicated physics sandbox in the lobby's east arrival bay. Keeping
// it off the centre path lets players deliberately enter it to kick/shoot
// props instead of tripping over the showcase during every traversal.
// Mesh origins are at the base; body centres are offset up by halfHeight.
//
// The sizes below are measured off the .obj vertex bounds, not assumed. The
// numbers that used to be here were assumed, and both were wrong:
//
//   Crate: the comment said "0.8 m cube -> halfExtents {0.4, 0.4, 0.4}".
//   prop_crate.obj is 0.468 x 0.244 x 0.735 m. The old box was a 0.8 m cube
//   around a crate 24 cm tall -- more than three times too tall, so the player
//   was stopped by an invisible wall at waist height by a knee-high crate, and
//   the stacked pair sat 0.8 m apart with a visible gap between them.
//
//   Barrel: the comment said "r=0.28, h=0.9". prop_barrel_p0/p1 measure
//   1.334 m across and 1.388 m tall, so the cylinder was less than half the
//   barrel's width; you could walk into the staves up to the shoulder.
//
// Crate: 0.468 x 0.244 x 0.735 -> halfExtents {0.234, 0.122, 0.368}.
//   Kept oblong here (unlike the dungeon catalog, whose placement rotates
//   props by a grid hash) because these are placed at authored yaws and the
//   body rotates with the mesh once it is dynamic.
// Barrel: r = 0.667, full height 1.388 -> cylinder halfHeight 0.694.
void PropSystem::spawnLobby(GameContext& ctx)
{
    eng::Renderer& r = ctx.renderer;
    eng::Physics& physics = ctx.physics;
    const std::string props = ctx.assets + "/meshes/props/";
    eng::MeshHandle mCrate   = r.loadObj(props + "prop_crate.obj");
    eng::MeshHandle mBarrel0 = r.loadObj(props + "prop_barrel_p0.obj");
    eng::MeshHandle mBarrel1 = r.loadObj(props + "prop_barrel_p1.obj");

    const auto spawnCrate = [&](glm::vec3 bodyPos, float yawDeg) {
        constexpr float hh = 0.122f;
        eng::BodyDesc bd;
        bd.kind = eng::ShapeKind::Box;
        bd.halfExtents = {0.234f, hh, 0.368f};
        bd.position = bodyPos + glm::vec3(0.0f, 0.02f, 0.0f);
        bd.layer = game::layer::Prop;
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
        constexpr float halfH = 0.694f;
        constexpr float radius = 0.667f;
        eng::BodyDesc bd;
        bd.kind = eng::ShapeKind::Cylinder;
        bd.halfHeight = halfH;
        bd.radius = radius;
        bd.position = bodyPos + glm::vec3(0.0f, 0.02f, 0.0f);
        bd.layer = game::layer::Prop;
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
        gd.halfExtents = {3.5f, 0.10f, 3.0f};
        gd.position = {9.5f, -0.10f, 18.5f}; // top = 0
        gd.layer = game::layer::Static;
        gd.dynamic = false;
        mGroundBody = physics.createBody(gd);
    }

    // Spawn heights are body centres, so they follow the corrected half-
    // extents: a crate rests at y = 0.122 and a barrel at y = 0.694. With the
    // old oversized numbers these were 0.4 and 0.45, which now would have
    // dropped every prop in from mid-air on the first step.
    // Two crates stacked near the entry hall (spawn side of the anchor room)
    spawnCrate({9.0f, 0.122f, 18.0f},  10.0f);   // ground crate
    spawnCrate({9.0f, 0.366f, 18.0f}, -15.0f);   // stacked on top
    // A third crate to the side
    spawnCrate({7.5f, 0.122f, 17.0f},  30.0f);
    // Two barrels next to them. Spaced 1.72 m apart: the real barrel is 1.33 m
    // across, so the old 1.22 m spacing (tuned against a 0.56 m collider) now
    // starts them interpenetrating and they would shove each other on frame 1.
    spawnBarrel({10.5f, 0.694f, 17.5f},  0.0f);
    spawnBarrel({11.9f, 0.694f, 18.5f}, 20.0f);
    // One more loose crate for variety
    spawnCrate({8.2f, 0.122f, 19.5f}, -20.0f);

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
