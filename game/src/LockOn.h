#pragma once

#include <eng/camera/CameraRig.h> // eng::CameraLockOn, what the rig consumes

#include <glm/glm.hpp>

#include <functional>
#include <vector>

namespace game {

// Something worth locking onto. Deliberately not an enemy: the same seam takes
// a boss, a breakable, a possessed chandelier. `id` is whatever the caller uses
// to recognise it again (an entt entity, today) and nothing here interprets it.
struct LockCandidate {
    int id = -1;
    // The point the camera frames -- the torso, not the feet. Framing feet
    // points the camera at the floor in front of the enemy.
    glm::vec3 point{0.0f};
    float radius = 0.6f;
};

// The rules a lock plays by. All of it is game policy: the camera rig knows
// only that there is a point to frame.
struct LockOnTuning {
    // How far a target may be to be acquired, and how far it may get before the
    // lock drops. The gap between them is the hysteresis that stops a lock
    // flickering off and on as an enemy backs away at exactly the limit.
    float acquireRange = 16.0f;
    float breakRange = 22.0f;
    // Half-angle of the cone a target must be inside to be acquired, degrees.
    // Wide, because a souls lock is a "that one, over there" gesture rather
    // than an aim -- but not so wide that it can grab something behind you.
    float acquireConeDegrees = 62.0f;
    // Pixels of horizontal mouse movement that switch to the next target while
    // locked. Souls uses a stick flick; a mouse needs a threshold or every
    // tremor changes target.
    float switchThresholdPixels = 190.0f;
    // Seconds a target may stay out of sight before the lock drops. Not zero:
    // an enemy crossing behind a pillar for three frames should not cost you
    // the lock in the middle of a swing.
    float occlusionGrace = 0.6f;
};

// The nearest-to-aim candidate inside the cone and the range, or null. Pure, so
// the acquisition rule is a test rather than something only visible by playing.
//
// "Nearest to aim" and not "nearest": the player is pointing at the thing they
// mean, and picking the closest body instead is the classic way a lock grabs
// the rat at your feet instead of the knight in front of you.
const LockCandidate* acquireLockTarget(const std::vector<LockCandidate>& all,
                                       glm::vec3 eye, glm::vec3 forward,
                                       const LockOnTuning& tuning);

// The next candidate to the left (-1) or right (+1) of the current one, by
// angle about the view's up axis, or null when there is nothing that side.
// Wrapping is deliberately NOT done: running out of targets on one side should
// stop, not teleport the camera across the arena.
const LockCandidate* switchLockTarget(const std::vector<LockCandidate>& all,
                                      int currentId, glm::vec3 eye,
                                      glm::vec3 forward, float direction,
                                      const LockOnTuning& tuning);

// Holds the lock across frames: acquire, keep, switch, drop.
//
// It owns the *decision* only. What the camera then does with the point is the
// rig's business (eng::ThirdPersonCameraRig), and what the character does with
// it -- turning to face the target so strafing reads as circling -- is the
// controller's. Three things, one fact between them.
class LockOnSystem {
public:
    void setTuning(const LockOnTuning& tuning) { mTuning = tuning; }
    LockOnTuning& tuning() { return mTuning; }
    const LockOnTuning& tuning() const { return mTuning; }

    // Is `point` visible from `eye`? Used only to drop a lock that has gone
    // behind geometry; an empty function means "always visible", which is what
    // a caller with no physics world wants.
    using VisibilityTest = std::function<bool(glm::vec3 eye, glm::vec3 point)>;

    struct Input {
        bool togglePressed = false; // the lock-on button, this frame
        float switchAxis = 0.0f;    // horizontal mouse delta, pixels
        bool enabled = true;        // false while a menu owns the input
    };

    // One frame. `candidates` is rebuilt by the caller every frame -- targets
    // die, spawn and walk out of range, and holding pointers to them across
    // frames is how a lock outlives its enemy.
    void update(const std::vector<LockCandidate>& candidates, glm::vec3 eye,
                glm::vec3 forward, const Input& input, float dt,
                const VisibilityTest& visible = {});

    void clear();

    bool locked() const { return mLocked; }
    int targetId() const { return mTargetId; }
    // Where the lock currently is, and how big it is. Empty when not locked.
    eng::CameraLockOn camera() const;
    // The same point, for the character's facing. Separate accessor because a
    // caller usually wants one or the other, and an optional at each site reads
    // better than a pair of out-parameters.
    bool targetPoint(glm::vec3& out) const;

private:
    LockOnTuning mTuning;
    bool mLocked = false;
    int mTargetId = -1;
    glm::vec3 mPoint{0.0f};
    float mRadius = 0.6f;
    float mUnseenFor = 0.0f;
    // Accumulated mouse travel since the last switch. Reset on a switch and
    // decayed towards zero, so a slow drift never adds up to one.
    float mSwitchTravel = 0.0f;
};

} // namespace game
