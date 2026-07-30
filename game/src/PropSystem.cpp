#include "PropSystem.h"
#include "GameCollision.h"

#include "GameContext.h"
#include "scene/MapRuntime.h"

#include <eng/Physics.h>
#include <eng/Renderer.h>

#include <glm/gtc/quaternion.hpp>

#include <string>

namespace game {

// The scene owns placement; this system owns only dynamic physics behaviour.
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
void PropSystem::spawnShowroom(
    GameContext& ctx, const std::vector<ScenePlacement>& placements)
{
    eng::Renderer& r = ctx.renderer;
    eng::Physics& physics = ctx.physics;
    const std::string props = ctx.assets + "/meshes/props/";
    eng::MeshHandle mCrate   = r.loadObj(props + "prop_crate.obj");
    eng::MeshHandle mBarrel0 = r.loadObj(props + "prop_barrel_p0.obj");
    eng::MeshHandle mBarrel1 = r.loadObj(props + "prop_barrel_p1.obj");

    const auto spawnCrate = [&](glm::vec3 feetPos, glm::quat orientation) {
        constexpr float hh = 0.122f;
        eng::BodyDesc bd;
        bd.kind = eng::ShapeKind::Box;
        bd.halfExtents = {0.234f, hh, 0.368f};
        bd.position = feetPos + glm::vec3(0.0f, hh, 0.0f);
        bd.orientation = orientation;
        bd.layer = game::layer::Prop;
        bd.dynamic = true;
        bd.mass = 5.0f;
        bd.friction = 0.6f;
        eng::BodyHandle bh = physics.createBody(bd);
        eng::NodeHandle nh = r.createNode(eng::kRootNode, feetPos);
        r.setOrientation(nh, orientation);
        r.attachMesh(nh, mCrate, "Game/PropMarket");
        mProps.push_back({nh, bh, glm::vec3(0.0f, hh, 0.0f)});
    };

    const auto spawnBarrel = [&](glm::vec3 feetPos, glm::quat orientation) {
        constexpr float halfH = 0.694f;
        constexpr float radius = 0.667f;
        eng::BodyDesc bd;
        bd.kind = eng::ShapeKind::Cylinder;
        bd.halfHeight = halfH;
        bd.radius = radius;
        bd.position = feetPos + glm::vec3(0.0f, halfH, 0.0f);
        bd.orientation = orientation;
        bd.layer = game::layer::Prop;
        bd.dynamic = true;
        bd.mass = 8.0f;
        bd.friction = 0.6f;
        eng::BodyHandle bh = physics.createBody(bd);
        eng::NodeHandle nh = r.createNode(eng::kRootNode, feetPos);
        r.setOrientation(nh, orientation);
        r.attachMesh(nh, mBarrel0, "Game/PropPlanks");
        r.attachMesh(nh, mBarrel1, "Game/PropBauerhaus");
        mProps.push_back({nh, bh, glm::vec3(0.0f, halfH, 0.0f)});
    };

    for (const ScenePlacement& placement : placements) {
        if (placement.type == "physics.crate")
            spawnCrate(placement.position, placement.rotation);
        else if (placement.type == "physics.barrel")
            spawnBarrel(placement.position, placement.rotation);
    }
    mAlive = !mProps.empty();
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
}

} // namespace game
