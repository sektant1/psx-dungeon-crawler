#include "Dummy.h"
#include "GameCollision.h"

#include <eng/Physics.h>
#include <eng/Renderer.h>
#include <eng/Handles.h>

#include <glm/gtc/quaternion.hpp>

// Capsule dimensions: radius 0.30, halfHeight 0.60 -> total height ~1.80 m
// (capsule = 2*halfHeight cylinder + 2*radius hemisphere caps).
// Body centre is at feetPos + {0, 0.9, 0} so the base sits at floor level.
// The render node (mesh origin at base) follows: nodePos = bodyCenter + q*(-offset).

static constexpr float kRadius     = 0.30f;
static constexpr float kHalfHeight = 0.60f;
static constexpr float kCentreY    = kHalfHeight + kRadius; // 0.90 m

void Dummy::init(eng::Physics& phys, eng::Renderer& r, glm::vec3 feetPos)
{
    // Physics capsule (kinematic while alive)
    eng::BodyDesc bd;
    bd.kind       = eng::ShapeKind::Capsule;
    bd.radius     = kRadius;
    bd.halfHeight = kHalfHeight;
    bd.position   = feetPos + glm::vec3(0.0f, kCentreY, 0.0f);
    bd.layer      = game::layer::Prop;
    bd.dynamic    = true;
    bd.mass       = 40.0f;
    bd.friction   = 0.6f;
    mBody = phys.createBody(bd);
    phys.setBodyKinematic(mBody, true); // stand rigid until killed

    // Render offset: body centre relative to mesh base
    mRenderOffset = glm::vec3(0.0f, kCentreY, 0.0f);

    // Mesh: prop_haybale.obj with the Game/PropHay material.
    //
    // Measured off the .obj: prop_haybale.obj is 1.353 x 0.794 x 1.800 m. It
    // is not "roughly cube-shaped ~0.65 m" as the previous comment claimed --
    // it is a flat, strongly oblong bale, twice as deep as it is tall. Scaling
    // it by (1.1, 2.77, 1.1) off that wrong assumption produced a 1.49 x 2.20
    // x 1.98 m object standing in for a 0.60 m wide, 1.80 m tall capsule: the
    // dummy was three times wider than what your swings actually had to hit,
    // and 40 cm of it stuck out above its own head.
    //
    // Scale is now derived from the measurement so the silhouette is the
    // hitbox: Y 1.800/0.794, X 0.600/1.353, Z 0.600/1.800.
    const std::string propsDir =
        std::string(APP_ASSET_DIR) + "/meshes/props/";
    eng::MeshHandle mesh = r.loadObj(propsDir + "prop_haybale.obj");

    mNode = r.createNode(eng::kRootNode, feetPos);
    constexpr float kMeshX = 1.353f, kMeshY = 0.794f, kMeshZ = 1.800f;
    constexpr float kTargetH = kCentreY * 2.0f;         // 1.80 m capsule height
    constexpr float kTargetW = kRadius * 2.0f;          // 0.60 m capsule width
    r.setScale(mNode, glm::vec3(kTargetW / kMeshX, kTargetH / kMeshY,
                                kTargetW / kMeshZ));
    r.attachMesh(mNode, mesh, "Game/PropHay", true);

    mAlive        = true;
    mInitialised  = true;
}

void Dummy::kill(eng::Physics& phys, glm::vec3 impulse, glm::vec3 atPoint)
{
    if (!mAlive || !mInitialised)
        return;
    mAlive = false;
    phys.setBodyKinematic(mBody, false); // become fully dynamic
    // Add a small upward bias so it tumbles rather than sliding
    impulse.y += 4.0f;
    phys.applyImpulse(mBody, impulse, atPoint);
}

void Dummy::syncRender(eng::Physics& phys, eng::Renderer& r)
{
    if (!mInitialised)
        return;
    glm::vec3 p;
    glm::quat q;
    phys.getRenderTransform(mBody, p, q);
    // Rotate the offset by the body orientation so it stays correct when toppled
    const glm::vec3 nodePos = p + q * (-mRenderOffset);
    r.setPosition(mNode, nodePos);
    r.setOrientation(mNode, q);
}

void Dummy::clear(eng::Physics& phys, eng::Renderer& r)
{
    if (!mInitialised)
        return;
    phys.removeBody(mBody);
    r.setNodeVisible(mNode, false);
    mBody         = {};
    mNode         = {};
    mAlive        = true;
    mInitialised  = false;
}
