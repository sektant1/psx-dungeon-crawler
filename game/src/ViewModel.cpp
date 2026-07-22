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
    initWeapon(r, headNode, propsDir + "/prop_sword.obj",
               "Game/ViewModelWeapon", pose);
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
    const glm::mat4 pivotBake = glm::translate(
        glm::mat4(1.0f), -mPose.gripPivot);
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
    //   [0.00, 0.12) windup  : pull back right and up, cock blade
    //   [0.12, 0.25) slash   : fast diagonal downward-left arc
    //   [0.25, 0.35) recover : ease back to rest
    if (mAttackTime >= 0.0f) {
        const float t = mAttackTime;

        if (t < 0.12f) {
            // --- Windup: pull sword back right (+X), up (+Y), rotate CCW yaw
            const float p = remap01(t, 0.0f, 0.12f);
            posOffset += glm::vec3(0.06f * p, 0.05f * p, 0.04f * p);
            // Cock the blade: yaw left (negative yaw = CCW from above) and
            // pitch back.
            const glm::quat yawBack =
                glm::angleAxis(glm::radians(-22.0f * p), glm::vec3(0, 1, 0));
            const glm::quat pitchBack =
                glm::angleAxis(glm::radians(-18.0f * p), glm::vec3(1, 0, 0));
            rotDelta = yawBack * pitchBack;

        } else if (t < 0.25f) {
            // --- Slash: fast diagonal downward-left.
            // Normalise within [0.12, 0.25].
            const float p = remap01(t, 0.12f, 0.25f);
            // Position sweeps from the windup offset to past-centre-left-down.
            posOffset += glm::mix(
                glm::vec3( 0.06f,  0.05f,  0.04f),
                glm::vec3(-0.10f, -0.08f, -0.02f),
                p);
            // Big yaw right (slash direction) + downward pitch.
            const float yawDeg   = glm::mix(-22.0f,  55.0f, p);
            const float pitchDeg = glm::mix(-18.0f, -30.0f, p);
            const float rollDeg  = glm::mix(  0.0f,  20.0f, p);
            rotDelta =
                glm::angleAxis(glm::radians(yawDeg),   glm::vec3(0, 1, 0)) *
                glm::angleAxis(glm::radians(pitchDeg),  glm::vec3(1, 0, 0)) *
                glm::angleAxis(glm::radians(rollDeg),   glm::vec3(0, 0, 1));

        } else {
            // --- Recover: ease back to rest.
            const float p = remap01(t, 0.25f, kAttackDur);
            posOffset += glm::mix(
                glm::vec3(-0.10f, -0.08f, -0.02f),
                glm::vec3(0.0f),
                p);
            const float yawDeg   = glm::mix( 55.0f, 0.0f, p);
            const float pitchDeg = glm::mix(-30.0f, 0.0f, p);
            const float rollDeg  = glm::mix( 20.0f, 0.0f, p);
            rotDelta =
                glm::angleAxis(glm::radians(yawDeg),   glm::vec3(0, 1, 0)) *
                glm::angleAxis(glm::radians(pitchDeg),  glm::vec3(1, 0, 0)) *
                glm::angleAxis(glm::radians(rollDeg),   glm::vec3(0, 0, 1));
        }
    }

    // --- Parry animation (blended in when not attacking) ---
    if (mParry > 0.001f) {
        // Raise the sword to a horizontal guard in front of the face:
        // shift left and up toward screen centre, rotate blade horizontal.
        const glm::vec3 guardPos(-0.12f * mParry, 0.08f * mParry, 0.02f * mParry);
        const glm::quat guardRot =
            glm::angleAxis(glm::radians(55.0f * mParry), glm::vec3(0, 1, 0)) *
            glm::angleAxis(glm::radians(35.0f * mParry), glm::vec3(1, 0, 0));
        posOffset += guardPos;
        rotDelta   = glm::slerp(rotDelta, guardRot * rotDelta, mParry);
    }

    r.setPosition(mNode, mPose.position + posOffset);
    r.setOrientation(mNode, rotDelta * poseOrientation(mPose));
}
