// ViewModel.cpp — first-person sword viewmodel, procedural transform animation.
// No skeletal animation; everything is composed from glm::quat / glm::vec3
// offsets layered on top of a fixed rest pose each frame.

#include "ViewModel.h"
#include "ParticleEffects.h"

#include <eng/Primitive.h>
#include <eng/Renderer.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------
namespace {

// Smooth step t in [0,1].
float smoothstep(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

glm::quat poseOrientation(const WeaponViewmodelPose& pose)
{
    const glm::quat pitch = glm::angleAxis(
        glm::radians(pose.rotationDegrees.x), glm::vec3(1, 0, 0));
    const glm::quat yaw = glm::angleAxis(
        glm::radians(pose.rotationDegrees.y), glm::vec3(0, 1, 0));
    const glm::quat roll = glm::angleAxis(
        glm::radians(pose.rotationDegrees.z), glm::vec3(0, 0, 1));
    return yaw * roll * pitch;
}

glm::quat degreesOrientation(glm::vec3 degrees)
{
    const glm::quat pitch = glm::angleAxis(glm::radians(degrees.x),
                                            glm::vec3(1, 0, 0));
    const glm::quat yaw = glm::angleAxis(glm::radians(degrees.y),
                                          glm::vec3(0, 1, 0));
    const glm::quat roll = glm::angleAxis(glm::radians(degrees.z),
                                           glm::vec3(0, 0, 1));
    return yaw * roll * pitch;
}

eng::PrimitiveKind primitiveKind(game::WeaponPrimitive primitive)
{
    switch (primitive) {
        case game::WeaponPrimitive::Box: return eng::PrimitiveKind::Box;
        case game::WeaponPrimitive::BeveledBox:
            return eng::PrimitiveKind::BeveledBox;
        case game::WeaponPrimitive::Sphere: return eng::PrimitiveKind::Sphere;
        case game::WeaponPrimitive::Capsule: return eng::PrimitiveKind::Capsule;
        case game::WeaponPrimitive::Cylinder:
            return eng::PrimitiveKind::Cylinder;
        case game::WeaponPrimitive::Cone: return eng::PrimitiveKind::Cone;
        case game::WeaponPrimitive::Disc: return eng::PrimitiveKind::Disc;
    }
    return eng::PrimitiveKind::Box;
}

} // namespace

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------
void ViewModel::init(eng::Renderer& r, eng::NodeHandle headNode,
                     const std::string& propsDir, ViewmodelGlow glow,
                     const WeaponViewmodelPose& pose)
{
    WeaponViewmodelPose swordPose = pose;
    // Legacy sword exporter placed the origin above the grip centre. Keep
    // that asset correction here; new weapons use a grip-at-origin contract.
    if (glm::dot(swordPose.gripPivot, swordPose.gripPivot) < 0.000001f)
        swordPose.gripPivot = {0.0f, -0.65f, 0.0f};
    initWeapon(r, headNode, propsDir + "/prop_sword.obj",
               "Game/ViewModelWeapon", glow, swordPose);
}

void ViewModel::applyEnchant(eng::Renderer& r)
{
    if (mGlowNodes.empty() || mGlow.strength <= 0.0f)
        return; // this weapon was built without a glow
    for (eng::NodeHandle node : mGlowNodes) {
        if (mEnchantEnabled)
            r.setNodeEnchantment(node, mGlow.palette, mGlow.strength);
        else
            r.clearNodeEnchantment(node);
    }
}

void ViewModel::setEnchantEnabled(eng::Renderer& r, bool on)
{
    mEnchantEnabled = on;
    applyEnchant(r);
}

void ViewModel::initWeapon(eng::Renderer& r, eng::NodeHandle headNode,
                           const std::string& meshPath,
                           const std::string& materialName, ViewmodelGlow glow,
                           const WeaponViewmodelPose& pose)
{
    mPose = pose;
    mNode = r.createNode(headNode, mPose.position);
    // Normalize the imported model around its authored hand/grip socket.
    // Animation now rotates about the hand, not an arbitrary exporter origin.
    const glm::mat4 pivotBake =
        glm::rotate(glm::mat4(1.0f),
                    glm::radians(mPose.gripAxisTwistDegrees),
                    glm::vec3(0, 1, 0)) *
        glm::translate(glm::mat4(1.0f), -mPose.gripPivot);
    const eng::MeshHandle weapon = r.loadMesh(meshPath, &pivotBake);
    r.attachMesh(mNode, weapon, materialName, false, true);
    mGlow = glow;
    mGlowNodes = {mNode};
    applyEnchant(r);

    // The prop_sword.obj is authored at world scale (used in scene dressing at
    // 0.06x).  As a viewmodel it needs to be much smaller, but readable.
    // The shared scale is tuned for the current metre-ish prop convention.
    r.setScale(mNode, glm::vec3(mPose.scale));

    // Base orientation: the mesh's long axis runs along +Y (hilt at origin,
    // tip at +Y).  We want it to look like a sword held in the right hand:
    // Negative pitch maps the authored +Y blade axis into camera-forward -Z;
    // positive roll leans its tip toward screen centre from the right hand.
    r.setOrientation(mNode, poseOrientation(mPose));

    // Reset animation state on every re-init (level transition).
    mAttackTime = -1.0f;
    mRecoil = 0.0f;
    mSwayPhase = 0.0f;
}

// ---------------------------------------------------------------------------
// initStaff — procedural caster staff (shaft + crystal tip)
// ---------------------------------------------------------------------------
void ViewModel::initStaff(eng::Renderer& r, eng::NodeHandle headNode,
                          const std::string& crystalMeshPath,
                          ViewmodelGlow tipGlow,
                          const WeaponViewmodelPose& pose)
{
    // Staff-specific framing: held upright in the right hand, shaft already
    // authored along +Y so no grip-axis twist is needed.
    WeaponViewmodelPose staffPose = pose;
    staffPose.position          = {0.28f, -0.34f, -0.70f};
    staffPose.rotationDegrees   = {-6.0f, 10.0f, 2.0f};
    staffPose.scale             = 0.035f;
    staffPose.gripAxisTwistDegrees = 0.0f;
    mPose = staffPose;

    mNode = r.createNode(headNode, mPose.position);

    // Shaft: a unit beveled box stretched long+thin via a child node, so the
    // uniform viewmodel scale stays on mNode (mirrors the barrel multi-mesh).
    eng::PrimitiveMeshDesc shaftDesc;
    shaftDesc.kind = eng::PrimitiveKind::BeveledBox;
    shaftDesc.bevel = 0.06f;
    const eng::MeshHandle shaft = r.createPrimitiveMesh(shaftDesc);
    eng::NodeHandle shaftNode = r.createNode(mNode, glm::vec3(0.0f));
    r.attachMesh(shaftNode, shaft, "Game/PropPlanks", false, true);
    r.setScale(shaftNode, glm::vec3(1.0f, 12.0f, 1.0f));

    // Crystal tip at the top of the shaft. Use the rim-lit mesh material made
    // for this spire — NOT a particle material (Game/Spells/BeamCore uses a billboard
    // vertex program + depth_write off, which mangles a solid mesh and reads as
    // faces wrongly culling).
    // Shaft top is at local y = +6. The crystal mesh's base sits at y=0.234 in
    // its own space, so at scale 2.0 the node must drop by 0.234*2 (=0.468) to
    // seat the base on the rod; a touch more embeds it for a seamless join.
    eng::ModelImportOptions sourceImport;
    sourceImport.pivot = eng::PivotMode::Source;
    const eng::MeshHandle tip = r.loadMesh(crystalMeshPath, sourceImport);
    eng::NodeHandle tipNode = r.createNode(mNode, glm::vec3(0.0f, 6.0f - 0.55f, 0.0f));
    r.attachMesh(tipNode, tip, "Game/Demo/CrystalSpire", false, true);
    r.setScale(tipNode, glm::vec3(2.0f));

    r.setScale(mNode, glm::vec3(mPose.scale));
    r.setOrientation(mNode, poseOrientation(mPose));
    mGlow = tipGlow;
    mGlowNodes = {tipNode};
    applyEnchant(r);

    // Reset animation state on every re-init (level transition).
    mAttackTime = -1.0f;
    mRecoil = 0.0f;
    mSwayPhase = 0.0f;
}

// ---------------------------------------------------------------------------
// initTorch — handheld torch (wood handle + live flame + warm light)
// ---------------------------------------------------------------------------
void ViewModel::initTorch(eng::Renderer& r, eng::NodeHandle headNode,
                          ViewmodelGlow handleGlow,
                          const WeaponViewmodelPose& pose)
{
    WeaponViewmodelPose torchPose = pose;
    torchPose.position          = {0.30f, -0.32f, -0.68f};
    torchPose.rotationDegrees   = {-4.0f, 8.0f, 3.0f};
    torchPose.scale             = 0.045f;
    torchPose.gripAxisTwistDegrees = 0.0f;
    mPose = torchPose;

    mNode = r.createNode(headNode, mPose.position);

    // Handle: a short wood rod (unit box stretched on a child node).
    eng::PrimitiveMeshDesc handleDesc;
    handleDesc.kind = eng::PrimitiveKind::BeveledBox;
    handleDesc.bevel = 0.06f;
    const eng::MeshHandle handle = r.createPrimitiveMesh(handleDesc);
    eng::NodeHandle handleNode = r.createNode(mNode, glm::vec3(0.0f));
    r.attachMesh(handleNode, handle, "Game/PropPlanks", false, true);
    r.setScale(handleNode, glm::vec3(1.0f, 8.0f, 1.0f));

    // Flame seat at the top of the handle. Fire/glow/ash particles plus a warm
    // point light hang here so the torch actually illuminates while equipped.
    // (No wall bracket/mount — this is the handheld variant.)
    eng::NodeHandle flame = r.createNode(mNode, glm::vec3(0.0f, 4.4f, 0.0f));
    particlefx::spawnFlame(r, flame);

    const auto lin = [](float s) { return std::pow(s, 2.2f); };
    eng::LightDesc warm;
    warm.colour = glm::vec3(lin(1.0f), lin(0.60f), lin(0.30f)) * 3.5f;
    warm.range  = 5.5f;
    r.attachLight(flame, warm);

    r.setScale(mNode, glm::vec3(mPose.scale));
    r.setOrientation(mNode, poseOrientation(mPose));
    mGlow = handleGlow;
    mGlowNodes = {handleNode};
    applyEnchant(r);

    mAttackTime = -1.0f;
    mRecoil = 0.0f;
    mSwayPhase = 0.0f;
}

void ViewModel::initPlayerWeapon(
    eng::Renderer& r, eng::NodeHandle headNode,
    const game::WeaponViewmodelDef& definition, ViewmodelGlow glow)
{
    mPresentation = definition;
    mPose.position = definition.position;
    mPose.rotationDegrees = definition.rotationDegrees;
    mPose.scale = 1.0f;
    mPose.gripPivot = glm::vec3(0.0f);
    mPose.gripAxisTwistDegrees = 0.0f;
    mNode = r.createNode(headNode, mPose.position);
    mGlowNodes.clear();

    for (const game::WeaponViewmodelPart& part : definition.parts) {
        eng::PrimitiveMeshDesc meshDesc;
        meshDesc.kind = primitiveKind(part.primitive);
        meshDesc.bevel = 0.08f;
        meshDesc.rings = 8;
        meshDesc.segments = 10;
        const eng::MeshHandle mesh = r.createPrimitiveMesh(meshDesc);
        const eng::NodeHandle node = r.createNode(mNode, part.position);
        r.setOrientation(node, degreesOrientation(part.rotationDegrees));
        r.setScale(node, part.scale);
        r.attachMesh(node, mesh, part.material, false, true);
        if (part.enchanted)
            mGlowNodes.push_back(node);
    }
    if (mGlowNodes.empty())
        mGlowNodes.push_back(mNode);
    mGlow = glow;
    applyEnchant(r);
    r.setOrientation(mNode, poseOrientation(mPose));

    mAttackTime = -1.0f;
    mRecoil = 0.0f;
    mEquipTime = 0.0f;
    mLookOffset = glm::vec2(0.0f);
    mSwayPhase = 0.0f;
    mMovePhase = 0.0f;
}

void ViewModel::setVisible(eng::Renderer& r, bool show)
{
    if (mNode.valid())
        r.setNodeVisible(mNode, show);
}

// ---------------------------------------------------------------------------
// update
// ---------------------------------------------------------------------------
void ViewModel::beginEquip()
{
    mEquipTime = std::max(0.0f, mPresentation.fireDuration);
}

void ViewModel::configure(const game::WeaponViewmodelDef& definition)
{
    mPresentation = definition;
    mPose.position = definition.position;
    mPose.rotationDegrees = definition.rotationDegrees;
}

void ViewModel::update(eng::Renderer& r, float dt, bool triggerFire,
                       float moveSpeed, glm::vec2 lookDelta, bool grounded)
{
    if (!mNode.valid())
        return;

    if (triggerFire) {
        mAttackTime = 0.0f;
        mRecoil = std::min(1.5f, mRecoil + 1.0f);
    }

    if (mAttackTime >= 0.0f) {
        mAttackTime += dt;
        if (mAttackTime >= mPresentation.fireDuration)
            mAttackTime = -1.0f;
    }
    mRecoil *= std::exp(-mPresentation.recoilRecovery * std::max(0.0f, dt));
    mEquipTime = std::max(0.0f, mEquipTime - dt);
    mSwayPhase += dt;
    mMovePhase += dt * mPresentation.movementBobSpeed *
                  std::clamp(moveSpeed / 4.0f, 0.35f, 2.0f);

    const float lookBlend = std::min(1.0f, dt * 18.0f);
    const glm::vec2 lookTarget = glm::clamp(
        -lookDelta * mPresentation.lookSway, glm::vec2(-0.035f),
        glm::vec2(0.035f));
    mLookOffset = glm::mix(mLookOffset, lookTarget, lookBlend);

    const float idleX = mPresentation.idleSway *
                        std::sin(mSwayPhase * 1.15f);
    const float idleY = mPresentation.idleSway * 0.8f *
                        std::sin(mSwayPhase * 1.85f);
    const float moveAmount = grounded
                                 ? std::clamp(moveSpeed / 6.0f, 0.0f, 1.0f)
                                 : 0.0f;
    const glm::vec3 moveBob(
        std::sin(mMovePhase) * mPresentation.movementBob * moveAmount,
        -std::abs(std::cos(mMovePhase)) * mPresentation.movementBob * moveAmount,
        0.0f);
    glm::vec3 posOffset = moveBob + glm::vec3(
        idleX + mLookOffset.x, idleY + mLookOffset.y, 0.0f);

    float actionKick = mRecoil;
    if (mAttackTime >= 0.0f && mPresentation.fireDuration > 0.0f) {
        const float p = std::clamp(mAttackTime / mPresentation.fireDuration,
                                   0.0f, 1.0f);
        actionKick = std::max(actionKick, std::sin(p * glm::pi<float>()));
    }
    posOffset.z += mPresentation.recoilDistance * actionKick;
    if (mEquipTime > 0.0f && mPresentation.fireDuration > 0.0f)
        posOffset.y -= 0.22f * smoothstep(
            mEquipTime / mPresentation.fireDuration);

    const glm::quat rotDelta =
        glm::angleAxis(glm::radians(-mPresentation.recoilYawDegrees * actionKick),
                       glm::vec3(0, 1, 0)) *
        glm::angleAxis(
            glm::radians(-mPresentation.recoilPitchDegrees * actionKick),
            glm::vec3(1, 0, 0));

    r.setPosition(mNode, mPose.position + posOffset);
    r.setOrientation(mNode, rotDelta * poseOrientation(mPose));
}
