// ViewModel.cpp — first-person sword viewmodel, procedural transform animation.
// No skeletal animation; everything is composed from glm::quat / glm::vec3
// offsets layered on top of a fixed rest pose each frame.

#include "ViewModel.h"

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

// Map x from [a,b] -> [0,1] clamped.
float remap01(float x, float a, float b)
{
    return smoothstep((x - a) / (b - a));
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

} // namespace

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------
void ViewModel::init(eng::Renderer& r, eng::NodeHandle headNode,
                     const std::string& propsDir,
                     const WeaponViewmodelPose& pose)
{
    WeaponViewmodelPose swordPose = pose;
    // Legacy sword exporter placed the origin above the grip centre. Keep
    // that asset correction here; new weapons use a grip-at-origin contract.
    if (glm::dot(swordPose.gripPivot, swordPose.gripPivot) < 0.000001f)
        swordPose.gripPivot = {0.0f, -0.65f, 0.0f};
    initWeapon(r, headNode, propsDir + "/prop_sword.obj",
               "Game/ViewModelWeapon", swordPose);
}

void ViewModel::initWeapon(eng::Renderer& r, eng::NodeHandle headNode,
                           const std::string& meshPath,
                           const std::string& materialName,
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
    const eng::MeshHandle weapon = r.loadObj(meshPath, &pivotBake);
    r.attachMesh(mNode, weapon, materialName, false, true);

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
    mParry      = 0.0f;
    mSwayPhase  = 0.0f;
}

// ---------------------------------------------------------------------------
// initStaff — procedural caster staff (shaft + crystal tip)
// ---------------------------------------------------------------------------
void ViewModel::initStaff(eng::Renderer& r, eng::NodeHandle headNode,
                          const std::string& crystalMeshPath,
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
    const eng::MeshHandle shaft = r.createBeveledBox(0.06f);
    eng::NodeHandle shaftNode = r.createNode(mNode, glm::vec3(0.0f));
    r.attachMesh(shaftNode, shaft, "Game/PropPlanks", false, true);
    r.setScale(shaftNode, glm::vec3(1.0f, 12.0f, 1.0f));

    // Crystal tip at the top of the shaft. Use the rim-lit mesh material made
    // for this spire — NOT a particle material (Game/BeamCore uses a billboard
    // vertex program + depth_write off, which mangles a solid mesh and reads as
    // faces wrongly culling).
    // Shaft top is at local y = +6. The crystal mesh's base sits at y=0.234 in
    // its own space, so at scale 2.0 the node must drop by 0.234*2 (=0.468) to
    // seat the base on the rod; a touch more embeds it for a seamless join.
    const eng::MeshHandle tip = r.loadObj(crystalMeshPath);
    eng::NodeHandle tipNode = r.createNode(mNode, glm::vec3(0.0f, 6.0f - 0.55f, 0.0f));
    r.attachMesh(tipNode, tip, "PSX/CrystalSpire", false, true);
    r.setScale(tipNode, glm::vec3(2.0f));

    r.setScale(mNode, glm::vec3(mPose.scale));
    r.setOrientation(mNode, poseOrientation(mPose));

    // Reset animation state on every re-init (level transition).
    mAttackTime = -1.0f;
    mParry      = 0.0f;
    mSwayPhase  = 0.0f;
}

void ViewModel::setVisible(eng::Renderer& r, bool show)
{
    if (mNode.valid())
        r.setNodeVisible(mNode, show);
}

// ---------------------------------------------------------------------------
// update
// ---------------------------------------------------------------------------
void ViewModel::update(eng::Renderer& r, float dt,
                       bool triggerAttack, bool parryHeld)
{
    if (!mNode.valid())
        return;

    // ------------------------------------------------------------------
    // 1. Attack trigger
    // ------------------------------------------------------------------
    if (triggerAttack && mAttackTime < 0.0f)
        mAttackTime = 0.0f;

    // Advance attack timer.
    if (mAttackTime >= 0.0f) {
        mAttackTime += dt;
        if (mAttackTime >= kAttackDur)
            mAttackTime = -1.0f; // done
    }

    // ------------------------------------------------------------------
    // 2. Parry ease (attack takes priority — suppress parry while slashing)
    // ------------------------------------------------------------------
    const float parryTarget = (mAttackTime < 0.0f && parryHeld) ? 1.0f : 0.0f;
    mParry += (parryTarget - mParry) * std::min(1.0f, dt * 12.0f);

    // ------------------------------------------------------------------
    // 3. Idle breathing sway
    // ------------------------------------------------------------------
    mSwayPhase += dt;
    const float swayX = 0.005f * std::sin(mSwayPhase * 1.1f);
    const float swayY = 0.004f * std::sin(mSwayPhase * 1.8f);
    glm::vec3 idleOffset(swayX, swayY, 0.0f);

    // ------------------------------------------------------------------
    // 4. Compose final transform
    // ------------------------------------------------------------------
    // We build local position offset and a rotation delta on top of the
    // rest pose that was baked in init().

    glm::vec3 posOffset = idleOffset;
    glm::quat rotDelta  = glm::quat(1.0f, 0.0f, 0.0f, 0.0f); // identity

    // --- Attack animation ---
    // Three phases over kAttackDur (0.35 s):
    //   [0.00, 0.12) windup  : draw the edge back without yawing it broadside
    //   [0.12, 0.25) slash   : edge-leading diagonal forward cut
    //   [0.25, 0.35) recover : ease back to rest
    if (mAttackTime >= 0.0f) {
        const float t = mAttackTime;

        if (t < 0.12f) {
            // Pull the grip slightly out/up and cock the blade toward the
            // player. Rotation stays on camera X/Z so the cutting edge keeps
            // facing forward/back throughout the motion.
            const float p = remap01(t, 0.0f, 0.12f);
            posOffset += glm::vec3(0.04f * p, 0.03f * p, 0.05f * p);
            rotDelta =
                glm::angleAxis(glm::radians(-12.0f * p), glm::vec3(0, 0, 1)) *
                glm::angleAxis(glm::radians(28.0f * p), glm::vec3(1, 0, 0));

        } else if (t < 0.25f) {
            // --- Slash: fast diagonal downward-left.
            // Normalise within [0.12, 0.25].
            const float p = remap01(t, 0.12f, 0.25f);
            // Drive the grip inward/down/forward as the edge chops through.
            posOffset += glm::mix(
                glm::vec3( 0.04f,  0.03f,  0.05f),
                glm::vec3(-0.08f, -0.10f, -0.10f),
                p);
            const float pitchDeg = glm::mix(28.0f, -78.0f, p);
            const float rollDeg  = glm::mix(-12.0f, 18.0f, p);
            rotDelta =
                glm::angleAxis(glm::radians(rollDeg), glm::vec3(0, 0, 1)) *
                glm::angleAxis(glm::radians(pitchDeg), glm::vec3(1, 0, 0));

        } else {
            // --- Recover: ease back to rest.
            const float p = remap01(t, 0.25f, kAttackDur);
            posOffset += glm::mix(
                glm::vec3(-0.08f, -0.10f, -0.10f),
                glm::vec3(0.0f),
                p);
            const float pitchDeg = glm::mix(-78.0f, 0.0f, p);
            const float rollDeg  = glm::mix(18.0f, 0.0f, p);
            rotDelta =
                glm::angleAxis(glm::radians(rollDeg), glm::vec3(0, 0, 1)) *
                glm::angleAxis(glm::radians(pitchDeg), glm::vec3(1, 0, 0));
        }
    }

    // --- Parry animation (blended in when not attacking) ---
    if (mParry > 0.001f) {
        // Bring the edge inward and cant it across the upper body. X/Z-only
        // rotation retains the forward-facing edge established by the asset
        // twist instead of exposing the broad face during a block.
        const glm::vec3 guardPos(-0.14f * mParry, 0.10f * mParry,
                                 -0.04f * mParry);
        const glm::quat guardRot =
            glm::angleAxis(glm::radians(48.0f * mParry), glm::vec3(0, 0, 1)) *
            glm::angleAxis(glm::radians(-10.0f * mParry), glm::vec3(1, 0, 0));
        posOffset += guardPos;
        rotDelta   = glm::slerp(rotDelta, guardRot * rotDelta, mParry);
    }

    r.setPosition(mNode, mPose.position + posOffset);
    r.setOrientation(mNode, rotDelta * poseOrientation(mPose));
}
