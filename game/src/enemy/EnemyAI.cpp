#include "EnemyAI.h"

#include <algorithm>
#include <cmath>

namespace game {

const char* enemyStateName(EnemyState s)
{
    switch (s) {
        case EnemyState::Dormant:    return "Dormant";
        case EnemyState::Idle:       return "Idle";
        case EnemyState::Alert:      return "Alert";
        case EnemyState::Chase:      return "Chase";
        case EnemyState::Circle:     return "Circle";
        case EnemyState::Attack:     return "Attack";
        case EnemyState::Reposition: return "Reposition";
        case EnemyState::Search:     return "Search";
        case EnemyState::Flee:       return "Flee";
        case EnemyState::Stagger:    return "Stagger";
        case EnemyState::Dead:       return "Dead";
    }
    return "?";
}

} // namespace game

namespace game::enemyai {

namespace {

constexpr float kPi = 3.14159265358979323846f;

glm::vec3 flatten(glm::vec3 v)
{
    v.y = 0.0f;
    return v;
}

glm::vec3 safeNormalize(glm::vec3 v)
{
    const float len = glm::length(v);
    return len > 1e-4f ? v / len : glm::vec3(0.0f);
}

// Perpendicular (right-hand) of a horizontal direction, for circling.
glm::vec3 rightOf(glm::vec3 dir)
{
    return glm::vec3(dir.z, 0.0f, -dir.x);
}

float wrapAngle(float a)
{
    while (a > kPi)  a -= 2.0f * kPi;
    while (a < -kPi) a += 2.0f * kPi;
    return a;
}

void enter(EnemyBrain& b, EnemyState s)
{
    if (b.state == s)
        return;
    b.state = s;
    b.stateTime = 0.0f;
}

} // namespace

float yawToward(glm::vec3 from, glm::vec3 to)
{
    const glm::vec3 d = flatten(to - from);
    if (glm::length(d) < 1e-4f)
        return 0.0f;
    // Matches the player controller: forward = (sin yaw, 0, cos yaw).
    return std::atan2(d.x, d.z);
}

float turnToward(float current, float desired, float turnRateDeg, float dt)
{
    const float maxStep = glm::radians(turnRateDeg) * dt;
    const float delta = wrapAngle(desired - current);
    if (std::fabs(delta) <= maxStep)
        return wrapAngle(desired);
    return wrapAngle(current + (delta > 0.0f ? maxStep : -maxStep));
}

bool canPerceive(const EnemyDef& def, const Senses& s)
{
    if (!s.targetValid)
        return false;
    // Close enough to hear regardless of facing, and regardless of walls: a
    // dungeon enemy that ignores someone breathing on its back reads as broken,
    // not as stealthy.
    if (s.targetDistance <= def.perception.hearingRange)
        return true;
    if (s.targetDistance > def.perception.sightRange || !s.lineOfSight)
        return false;
    const glm::vec3 forward(std::sin(s.selfYaw), 0.0f, std::cos(s.selfYaw));
    const glm::vec3 toTarget = safeNormalize(flatten(s.targetPos - s.selfPos));
    if (toTarget == glm::vec3(0.0f))
        return true;
    const float cosHalf = std::cos(glm::radians(def.perception.sightFovDeg * 0.5f));
    return glm::dot(forward, toTarget) >= cosHalf;
}

int selectAttack(const EnemyDef& def, const EnemyBrain& brain,
                 const Senses& s, float roll)
{
    if (!s.targetValid)
        return -1;
    const glm::vec3 forward(std::sin(s.selfYaw), 0.0f, std::cos(s.selfYaw));
    const glm::vec3 toTarget = safeNormalize(flatten(s.targetPos - s.selfPos));

    float total = 0.0f;
    float weights[8] = {0.0f};
    const int count = std::min<int>(int(def.attacks.size()), 8);
    for (int i = 0; i < count; ++i) {
        const EnemyAttack& a = def.attacks[i];
        if (brain.cooldown[i] > 0.0f)
            continue;
        if (s.targetDistance < a.minRange || s.targetDistance > a.maxRange)
            continue;
        if (toTarget != glm::vec3(0.0f)) {
            const float cosCone = std::cos(glm::radians(a.aimConeDeg));
            if (glm::dot(forward, toTarget) < cosCone)
                continue;
        }
        // Ranged moves need to actually see the target; melee at contact range
        // does not (you are already touching).
        if (a.ranged && !s.lineOfSight)
            continue;
        const float w = std::max(0.0f, a.weight);
        weights[i] = w;
        total += w;
    }
    if (total <= 0.0f)
        return -1;

    float pick = std::clamp(roll, 0.0f, 0.999f) * total;
    for (int i = 0; i < count; ++i) {
        pick -= weights[i];
        if (weights[i] > 0.0f && pick <= 0.0f)
            return i;
    }
    // Floating-point tail: return the last eligible move rather than nothing.
    for (int i = count - 1; i >= 0; --i)
        if (weights[i] > 0.0f)
            return i;
    return -1;
}

void notifyAttackStarted(const EnemyDef& def, EnemyBrain& b, int attack)
{
    if (attack < 0 || attack >= int(def.attacks.size()) || attack >= 8)
        return;
    b.cooldown[attack] = def.attacks[size_t(attack)].cooldown;
}

void notifyAttackRefused(EnemyBrain& b)
{
    b.pendingAttack = -1;
    enter(b, EnemyState::Circle);
}

Intent think(const EnemyDef& def, EnemyBrain& b, const Senses& s, float dt)
{
    Intent intent;
    intent.desiredYaw = s.selfYaw;

    b.stateTime += dt;
    b.thinkTimer -= dt;
    b.aggroGrace = std::max(0.0f, b.aggroGrace - dt);
    const int count = std::min<int>(int(def.attacks.size()), 8);
    for (int i = 0; i < count; ++i)
        b.cooldown[i] = std::max(0.0f, b.cooldown[i] - dt);

    // --- states the brain does not get a vote on --------------------------
    if (s.dead) {
        enter(b, EnemyState::Dead);
        return intent;
    }
    if (s.staggered) {
        enter(b, EnemyState::Stagger);
        return intent; // locked: no move, no turn, no attack. That is the punish window.
    }
    if (b.state == EnemyState::Stagger)
        enter(b, EnemyState::Chase); // recovered angry, not idle

    // Perception is evaluated every tick (not on the think cadence): reacting
    // to being seen is the one thing that must never lag.
    const bool perceives = canPerceive(def, s);
    if (perceives) {
        b.lastKnownTarget = s.targetPos;
        b.hasLastKnown = true;
        b.lostSightFor = 0.0f;
    } else if (b.state != EnemyState::Idle && b.state != EnemyState::Dormant) {
        b.lostSightFor += dt;
    }

    if (b.state == EnemyState::Dormant) {
        if (!perceives)
            return intent;
        enter(b, EnemyState::Alert);
    }

    // Leash: too far from home, go back and forget. An unleashed enemy
    // (leashRange 0) never does this.
    const float fromHome = glm::length(flatten(s.selfPos - b.home));
    if (def.perception.leashRange > 0.0f &&
        fromHome > def.perception.leashRange &&
        b.state != EnemyState::Idle) {
        intent.moveDirection = safeNormalize(flatten(b.home - s.selfPos) +
                                             s.separation);
        intent.moveSpeed = def.locomotion.walkSpeed;
        intent.desiredYaw = yawToward(s.selfPos, b.home);
        if (fromHome < 1.0f) {
            b.hasLastKnown = false;
            enter(b, EnemyState::Idle);
        }
        return intent;
    }

    // Broken morale outranks everything else it might want to do.
    if (def.behaviour.fleeHealthPct > 0.0f &&
        s.healthPct < def.behaviour.fleeHealthPct && s.targetValid) {
        enter(b, EnemyState::Flee);
    }

    const glm::vec3 toTarget = flatten(s.targetPos - s.selfPos);
    const glm::vec3 toTargetDir = safeNormalize(toTarget);
    const float preferred = def.preferredRange();
    const float backoff = def.behaviour.backoffRange;

    switch (b.state) {
        case EnemyState::Idle:
            if (perceives) {
                enter(b, EnemyState::Alert);
                // Return here rather than breaking: the switch dispatched on
                // the *old* state, so breaking would fall through to the
                // engaged code below and the enemy would start closing on the
                // same tick it noticed you -- eating the whole alert beat.
                intent.desiredYaw = yawToward(s.selfPos, s.targetPos);
                return intent;
            }
            break;

        case EnemyState::Alert:
            // The tell. It turns to face you and does nothing else, which is
            // what gives the player the beat to react in.
            intent.desiredYaw = yawToward(s.selfPos, s.targetPos);
            if (b.stateTime >= def.perception.alertTime)
                enter(b, EnemyState::Chase);
            return intent;

        case EnemyState::Chase:
        case EnemyState::Circle:
        case EnemyState::Reposition:
            if (!perceives && b.lostSightFor >= def.perception.loseSightTime)
                enter(b, EnemyState::Search);
            break;

        case EnemyState::Search:
            if (perceives)
                enter(b, EnemyState::Chase);
            break;

        case EnemyState::Attack:
            // The swing owns the entity until ActionState returns to Idle.
            intent.desiredYaw = s.targetValid
                                    ? yawToward(s.selfPos, s.targetPos)
                                    : s.selfYaw;
            if (s.busy) {
                // Lunge: drift forward during the committed frames, which is
                // what makes a windup threatening instead of a free dodge cue.
                if (def.locomotion.lungeSpeed > 0.0f && toTargetDir != glm::vec3(0.0f)) {
                    intent.moveDirection = toTargetDir;
                    intent.moveSpeed = def.locomotion.lungeSpeed;
                }
                return intent;
            }
            // Swing finished. Decide whether to press or give space -- this is
            // the single dial that separates a brute from a skirmisher.
            if (randomUnit(b.rng) < def.behaviour.circleChance)
                enter(b, EnemyState::Circle);
            else if (randomUnit(b.rng) > def.behaviour.aggression)
                enter(b, EnemyState::Reposition);
            else
                enter(b, EnemyState::Chase);
            b.strafeSign = randomUnit(b.rng) < 0.5f ? -1.0f : 1.0f;
            b.thinkTimer = 0.0f;
            break;

        case EnemyState::Flee:
            if (s.targetValid) {
                intent.moveDirection =
                    safeNormalize(-toTargetDir + s.separation);
                intent.moveSpeed = def.locomotion.chaseSpeed;
                intent.desiredYaw = yawToward(s.targetPos, s.selfPos);
            }
            // It stops fleeing once it is out of the target's reach and has had
            // a moment; healing/regen is not modelled, so this is a lull, not a
            // reset.
            if (!perceives && b.stateTime > 3.0f)
                enter(b, EnemyState::Search);
            return intent;

        case EnemyState::Dormant:
        case EnemyState::Stagger:
        case EnemyState::Dead:
            return intent;
    }

    if (b.state == EnemyState::Search) {
        if (!b.hasLastKnown) {
            enter(b, EnemyState::Idle);
            return intent;
        }
        const glm::vec3 toLast = flatten(b.lastKnownTarget - s.selfPos);
        if (glm::length(toLast) < 0.8f) {
            b.hasLastKnown = false;
            enter(b, EnemyState::Idle);
            return intent;
        }
        intent.moveDirection = safeNormalize(safeNormalize(toLast) + s.separation);
        intent.moveSpeed = def.locomotion.walkSpeed;
        intent.desiredYaw = yawToward(s.selfPos, b.lastKnownTarget);
        return intent;
    }

    if (b.state == EnemyState::Idle) {
        // Sentries hold their ground and their facing; nothing to want.
        return intent;
    }

    // --- engaged: Chase / Circle / Reposition -----------------------------
    if (!s.targetValid)
        return intent;

    intent.desiredYaw = yawToward(s.selfPos, s.targetPos);

    // Committing to an attack is checked on every tick, not on the think
    // cadence: an opening that opens between decisions should still be taken.
    if (!s.busy && !def.behaviour.stationary) {
        // aggression gates *whether* it takes an opening. It used to be
        // OR'd with `state == Chase`, which made it dead for melee enemies:
        // Chase is the state they are in whenever they are closing, so a
        // 0.0-aggression enemy attacked exactly as eagerly as a 1.0 one.
        //
        // A hit recently taken overrides it -- being shot in the back should
        // never read as indifference.
        const bool willing = b.aggroGrace > 0.0f ||
                             randomUnit(b.rng) <= def.behaviour.aggression;
        if (willing) {
            const int pick = selectAttack(def, b, s, randomUnit(b.rng));
            if (pick >= 0) {
                b.pendingAttack = pick;
                intent.attack = pick;
                intent.wantsProjectile = def.attacks[size_t(pick)].ranged;
                enter(b, EnemyState::Attack);
                return intent;
            }
        }
    } else if (!s.busy && def.behaviour.stationary) {
        const int pick = selectAttack(def, b, s, randomUnit(b.rng));
        if (pick >= 0) {
            b.pendingAttack = pick;
            intent.attack = pick;
            intent.wantsProjectile = def.attacks[size_t(pick)].ranged;
            enter(b, EnemyState::Attack);
        }
        return intent; // never moves
    }

    if (def.behaviour.stationary)
        return intent;

    // Spacing. Everything below is "where do I stand while I wait", which is
    // the difference between an enemy that shuffles into your face and one that
    // feels like it is fighting you.
    if (b.state == EnemyState::Reposition) {
        intent.moveDirection = safeNormalize(-toTargetDir + s.separation);
        intent.moveSpeed = def.locomotion.strafeSpeed;
        if (b.stateTime >= def.behaviour.repositionTime)
            enter(b, EnemyState::Chase);
        return intent;
    }

    if (b.state == EnemyState::Circle) {
        const glm::vec3 tangent = rightOf(toTargetDir) * b.strafeSign;
        // Fold in a gentle radial correction so circling holds the ring rather
        // than spiralling out of it.
        const float radial = s.targetDistance - preferred;
        intent.moveDirection =
            safeNormalize(tangent + toTargetDir * std::clamp(radial, -1.0f, 1.0f) +
                          s.separation);
        intent.moveSpeed = def.locomotion.strafeSpeed;
        if (b.stateTime >= def.behaviour.repositionTime)
            enter(b, EnemyState::Chase);
        return intent;
    }

    // Chase.
    if (backoff > 0.0f && s.targetDistance < backoff) {
        intent.moveDirection = safeNormalize(-toTargetDir + s.separation);
        intent.moveSpeed = def.locomotion.strafeSpeed;
        return intent;
    }
    if (s.targetDistance > preferred) {
        intent.moveDirection = safeNormalize(toTargetDir + s.separation);
        intent.moveSpeed = def.locomotion.chaseSpeed;
        return intent;
    }
    // In the ring with nothing off cooldown: circle rather than stand still.
    if (b.thinkTimer <= 0.0f) {
        b.thinkTimer = def.behaviour.thinkInterval;
        b.strafeSign = randomUnit(b.rng) < 0.5f ? -1.0f : 1.0f;
        enter(b, EnemyState::Circle);
    }
    intent.moveDirection = safeNormalize(s.separation);
    intent.moveSpeed = intent.moveDirection != glm::vec3(0.0f)
                           ? def.locomotion.strafeSpeed
                           : 0.0f;
    return intent;
}

} // namespace game::enemyai
