#include "EnemySystem.h"

#include "GameCollision.h"
#include "GameAssets.h"
#include "GameContext.h"
#include "combat/ActionStateSystem.h"
#include "combat/CombatDirector.h"

#include <eng/Log.h>
#include <eng/Primitive.h>
#include <eng/Renderer.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace game {

namespace {

// World queries are cast against level geometry only. That is not a shortcut:
// the enemy's own body is on the Prop layer, and a shape cast that starts
// inside its own collider reports itself at fraction 0 -- which reads as "there
// is a wall touching me" and freezes the enemy in place. Excluding Prop makes
// self-hits structurally impossible; enemy-vs-enemy spacing is the separation
// system below, and enemy-vs-prop is left to the props being pushable.
constexpr eng::CollisionMask kWorld = eng::layerMask(layer::Static);

glm::vec3 flatten(glm::vec3 v)
{
    v.y = 0.0f;
    return v;
}

glm::vec3 forwardOf(float yaw)
{
    return glm::vec3(std::sin(yaw), 0.0f, std::cos(yaw));
}

// Squared distance from `p` to the segment [a,b]. Both the melee arc and the
// bolt test need "how close did this get to the player's capsule axis", and
// that is the same question.
float distanceToSegment(glm::vec3 p, glm::vec3 a, glm::vec3 b)
{
    const glm::vec3 ab = b - a;
    const float len2 = glm::dot(ab, ab);
    const float t = len2 > 1e-6f
                        ? std::clamp(glm::dot(p - a, ab) / len2, 0.0f, 1.0f)
                        : 0.0f;
    return glm::length(p - (a + ab * t));
}

float segmentDistance(glm::vec3 p0, glm::vec3 p1, glm::vec3 q0, glm::vec3 q1,
                      float& pFraction)
{
    const glm::vec3 u = p1 - p0;
    const glm::vec3 v = q1 - q0;
    const glm::vec3 w = p0 - q0;
    const float a = glm::dot(u, u);
    const float b = glm::dot(u, v);
    const float c = glm::dot(v, v);
    const float d = glm::dot(u, w);
    const float e = glm::dot(v, w);
    const float denominator = a * c - b * b;
    float sNumerator = 0.0f;
    float sDenominator = denominator;
    float tNumerator = 0.0f;
    float tDenominator = denominator;

    if (denominator < 1e-6f) {
        sNumerator = 0.0f;
        sDenominator = 1.0f;
        tNumerator = e;
        tDenominator = c;
    } else {
        sNumerator = b * e - c * d;
        tNumerator = a * e - b * d;
        if (sNumerator < 0.0f) {
            sNumerator = 0.0f;
            tNumerator = e;
            tDenominator = c;
        } else if (sNumerator > sDenominator) {
            sNumerator = sDenominator;
            tNumerator = e + b;
            tDenominator = c;
        }
    }
    if (tNumerator < 0.0f) {
        tNumerator = 0.0f;
        if (-d < 0.0f) sNumerator = 0.0f;
        else if (-d > a) sNumerator = sDenominator;
        else {
            sNumerator = -d;
            sDenominator = a;
        }
    } else if (tNumerator > tDenominator) {
        tNumerator = tDenominator;
        if (-d + b < 0.0f) sNumerator = 0.0f;
        else if (-d + b > a) sNumerator = sDenominator;
        else {
            sNumerator = -d + b;
            sDenominator = a;
        }
    }
    const float sc = std::abs(sNumerator) < 1e-6f
                         ? 0.0f
                         : sNumerator / sDenominator;
    const float tc = std::abs(tNumerator) < 1e-6f
                         ? 0.0f
                         : tNumerator / tDenominator;
    pFraction = std::clamp(sc, 0.0f, 1.0f);
    return glm::length(w + sc * u - tc * v);
}

} // namespace

// --- systems over the enemy archetype ---------------------------------------

namespace enemy {

glm::vec3 separation(const entt::registry& reg, entt::entity self,
                     glm::vec3 selfPos, float radius)
{
    if (radius <= 0.0f)
        return glm::vec3(0.0f);
    glm::vec3 push(0.0f);
    for (auto [other, motion, render] :
         reg.view<const EnemyMotion, const EnemyRender>().each()) {
        if (other == self || render.dying)
            continue; // corpses do not shove
        const glm::vec3 delta = flatten(selfPos - motion.feet);
        const float d = glm::length(delta);
        if (d > radius || d < 1e-4f)
            continue;
        push += (delta / d) * (1.0f - d / radius);
    }
    return push;
}

void syncPositions(entt::registry& reg, eng::Physics& physics)
{
    for (auto [e, tag, motion, link] :
         reg.view<const EnemyTag, EnemyMotion, const BodyLink>().each()) {
        glm::vec3 pos;
        glm::quat rot;
        physics.getRenderTransform(link.body, pos, rot);
        motion.feet = pos - glm::vec3(0.0f, tag.def->body.height * 0.5f, 0.0f);
    }
}

} // namespace enemy

// --- lifetime ---------------------------------------------------------------

void EnemySystem::init(EnemyLibrary& library, CombatDirector& director)
{
    mLibrary = &library;
    mDirector = &director;
}

entt::registry& EnemySystem::registry() { return mDirector->registry(); }
const entt::registry& EnemySystem::registry() const
{
    return mDirector->registry();
}

entt::entity EnemySystem::spawn(GameContext& ctx, const std::string& defId,
                                glm::vec3 feetPos, float yaw, int spawnerIndex)
{
    if (!mLibrary || !mDirector)
        return entt::null;
    const EnemyLibrary::Ref def = mLibrary->find(defId);
    if (!def) {
        eng::log::error("EnemySystem: no enemy definition '%s'", defId.c_str());
        return entt::null;
    }

    const float halfHeight = def->body.height * 0.5f;
    const glm::vec3 centre = feetPos + glm::vec3(0.0f, halfHeight, 0.0f);
    const glm::quat orientation =
        glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));

    // --- body ------------------------------------------------------------
    // Capsule, matching the player's character controller: `halfHeight` is
    // half the *straight* section, and the caps add `radius` at each end.
    eng::BodyDesc bd;
    bd.kind = eng::ShapeKind::Capsule;
    bd.radius = def->body.cappedRadius();
    bd.halfHeight = def->body.straightHeight() * 0.5f;
    bd.position = centre;
    bd.orientation = orientation;
    bd.layer = layer::Prop; // hittable by melee, arrows and spells
    bd.dynamic = true;      // dynamic shape, driven kinematically until death
    bd.mass = def->body.mass;
    bd.friction = 0.6f;
    const eng::BodyHandle body = ctx.physics.createBody(bd);
    ctx.physics.setBodyKinematic(body, true);

    // --- visual ----------------------------------------------------------
    // Placeholder art is a tinted cylinder that is exactly the collision shape,
    // so what you can hit is what you can see. Cached per definition: a pack of
    // a dozen shares one mesh.
    eng::MeshHandle mesh;
    const std::string meshKey =
        defId + ":" + def->visual.mesh + ":" +
        std::to_string(def->body.cappedRadius()) + ":" +
        std::to_string(def->body.straightHeight());
    if (auto it = mMeshCache.find(meshKey); it != mMeshCache.end()) {
        mesh = it->second;
    } else if (!def->visual.mesh.empty()) {
        mesh = ctx.renderer.loadObj(assetPath(def->visual.mesh));
        mMeshCache[meshKey] = mesh;
    } else {
        // Built from the same two derived numbers as the collision capsule
        // above, so the drawn silhouette *is* the hitbox. `height` here is the
        // straight section, matching the generator's convention.
        eng::PrimitiveMeshDesc pm;
        pm.kind = eng::PrimitiveKind::Capsule;
        pm.radius = def->body.cappedRadius();
        pm.height = def->body.straightHeight();
        pm.segments = 12; // reads as PSX-era, still round enough to aim at
        pm.rings = 6;     // cap tessellation; low, to stay in the era
        mesh = ctx.renderer.createPrimitiveMesh(pm);
        mMeshCache[meshKey] = mesh;
    }

    const eng::NodeHandle node = ctx.renderer.createNode(eng::kRootNode, centre);
    ctx.renderer.setScale(node, def->visual.scale);
    ctx.renderer.setOrientation(node, orientation);
    ctx.renderer.attachMesh(node, mesh, def->visual.material,
                            "Game/Prototype/Floor", def->visual.castShadows);

    // --- components -------------------------------------------------------
    // The combat half first: the director creates the entity and owns the
    // body<->entity link, so an enemy is a combatant before it is an enemy.
    Health hp;
    hp.current = hp.max = def->stats.health;
    Resistances resist{};
    std::memcpy(resist.value, def->resistanceById, sizeof(resist.value));
    const entt::entity e =
        mDirector->addCombatant(body, hp, resist, Faction::Enemy);
    entt::registry& reg = mDirector->registry();

    // The feel half: same components the player has, so enemy swings run the
    // same state machine and answer to the same stagger rules.
    Poise& poise = reg.emplace<Poise>(e);
    poise.current = poise.max = def->stats.poise;
    poise.regenRate = def->stats.poiseRegen;
    Stamina& stam = reg.emplace<Stamina>(e);
    stam.current = stam.max = def->stats.stamina;
    stam.regenRate = def->stats.staminaRegen;
    reg.emplace<ActionState>(e);

    // The enemy half.
    reg.emplace<EnemyTag>(e, EnemyTag{def, defId});
    EnemyBrain& brain = reg.emplace<EnemyBrain>(e);
    brain.home = feetPos;
    brain.state = def->behaviour.startsDormant ? EnemyState::Dormant
                                               : EnemyState::Idle;
    // Per-spawn seed: without it every enemy in a pack rolls the same circle
    // direction on the same frame and the group moves like one puppet.
    brain.rng = 0x9E3779B9u ^ (++mSpawnCounter * 0x85EBCA6Bu);
    if (brain.rng == 0)
        brain.rng = 1;

    EnemyMotion& motion = reg.emplace<EnemyMotion>(e);
    motion.yaw = yaw;
    motion.feet = feetPos;
    reg.emplace<EnemyRender>(e, EnemyRender{node, mesh, 0.0f, 0.0f, false});
    reg.emplace<EnemyOrigin>(e, EnemyOrigin{spawnerIndex});
    reg.emplace<EnemyPatternExecution>(e);
    return e;
}

// --- world queries ----------------------------------------------------------

bool EnemySystem::lineOfSight(eng::Physics& physics, glm::vec3 selfEye,
                              glm::vec3 targetEye) const
{
    const glm::vec3 delta = targetEye - selfEye;
    const float dist = glm::length(delta);
    if (dist < 1e-3f)
        return true;
    eng::RayHit hit;
    if (!physics.rayCast(selfEye, delta / dist, dist, hit, kWorld))
        return true;
    return hit.fraction * dist >= dist - 0.05f;
}

glm::vec3 EnemySystem::moveAndSlide(eng::Physics& physics, const EnemyDef& def,
                                    glm::vec3 feetPos, glm::vec3 velocity,
                                    float dt, bool& grounded) const
{
    const float halfHeight = def.body.height * 0.5f;
    glm::vec3 centre = feetPos + glm::vec3(0.0f, halfHeight, 0.0f);
    glm::vec3 motion = flatten(velocity) * dt;

    // The sweep shape is slightly slimmer than the body so a cylinder already
    // resting against a wall is not reported as penetrating it, which would
    // zero every move it ever tried.
    eng::BodyDesc probe;
    probe.kind = eng::ShapeKind::Capsule;
    probe.radius = std::max(0.05f, def.body.cappedRadius() - 0.02f);
    probe.halfHeight = std::max(0.01f, def.body.straightHeight() * 0.5f - 0.03f);
    probe.layer = layer::Prop;

    std::vector<eng::ShapeHit> hits;
    // Two passes: hit a wall, project the remaining motion along it, try again.
    // Two is enough for a corner; a third only matters in geometry an enemy
    // should not have been spawned into in the first place.
    for (int pass = 0; pass < 2 && glm::length(motion) > 1e-4f; ++pass) {
        hits.clear();
        physics.shapeCast(probe, centre, centre + motion, hits, kWorld);
        if (hits.empty()) {
            centre += motion;
            break;
        }
        glm::vec3 normal = flatten(hits.front().normal);
        const float nlen = glm::length(normal);
        if (nlen < 1e-4f)
            break;
        normal /= nlen;
        motion -= normal * std::min(0.0f, glm::dot(motion, normal));
        motion = flatten(motion) * 0.5f; // conservative: nudge, do not tunnel
    }

    glm::vec3 newFeet = centre - glm::vec3(0.0f, halfHeight, 0.0f);

    // Floor snap. The cast starts a step-height above the feet so the enemy
    // walks up stairs, and reaches a little below so a small drop is followed
    // rather than becoming a hover.
    const float up = std::max(0.1f, def.locomotion.stepHeight);
    eng::RayHit ground;
    if (physics.rayCast(newFeet + glm::vec3(0.0f, up, 0.0f),
                        glm::vec3(0.0f, -1.0f, 0.0f), up + 1.5f, ground,
                        kWorld)) {
        newFeet.y = ground.point.y;
        grounded = true;
    } else {
        // No floor within reach: fall, so an enemy shoved off a ledge does not
        // stand on air. Clamped per step; this is not a gravity model.
        newFeet.y -= std::min(9.0f * dt, 0.35f);
        grounded = false;
    }
    return newFeet;
}

// --- attack delivery --------------------------------------------------------

void EnemySystem::resolveMelee(eng::Physics& physics, entt::entity e,
                               const EnemyDef& def, const EnemyAttack& atk,
                               const EnemyMotion& motion, glm::vec3 targetFeet)
{
    if (!mOnHitPlayer)
        return;

    // The hitbox is a wedge: reach from the enemy's centre line, and an angle
    // around its facing. A sweep uses the attack's authored arc; a thrust uses
    // its aim cone. Testing against the player's capsule axis rather than a
    // point is what stops a swing at their feet from missing their head.
    const glm::vec3 selfCentre =
        motion.feet + glm::vec3(0.0f, def.body.height * 0.4f, 0.0f);
    const glm::vec3 legs = targetFeet + glm::vec3(0.0f, 0.15f, 0.0f);
    const glm::vec3 head =
        targetFeet + glm::vec3(0.0f, mTarget.height, 0.0f);
    const float reach = atk.maxRange + def.body.radius + mTarget.radius;
    if (distanceToSegment(selfCentre, legs, head) > reach)
        return;

    const glm::vec3 forward = forwardOf(motion.yaw);
    const glm::vec3 toTarget = flatten(targetFeet - motion.feet);
    const float len = glm::length(toTarget);
    if (len > 1e-4f) {
        const float halfAngle = atk.timing.isSweep && atk.timing.arc > 0.0f
                                    ? atk.timing.arc
                                    : glm::radians(atk.aimConeDeg);
        if (glm::dot(forward, toTarget / len) < std::cos(halfAngle))
            return;
    }

    // A swing does not pass through geometry. Range and arc alone let an enemy
    // pressed against one side of a doorway pillar hit you on the other, which
    // is within its 2 m reach and reads as being hit by nothing.
    const glm::vec3 chest =
        targetFeet + glm::vec3(0.0f, mTarget.height * 0.6f, 0.0f);
    if (!lineOfSight(physics,
                     motion.feet + glm::vec3(0.0f, def.body.eyeHeight, 0.0f),
                     chest))
        return;

    mOnHitPlayer(e, atk.weapon, atk.timing.poiseDamage, forward, chest);
}

void EnemySystem::spawnProjectile(GameContext& ctx, entt::entity owner,
                                   const EnemyAttack& atk, glm::vec3 origin,
                                   glm::vec3 targetFeet, bool targetValid,
                                   glm::vec3 authoredDirection,
                                   const std::string* payloadOverride)
{
    entt::registry& reg = registry();
    if (reg.storage<EnemyProjectile>().size() >= 512)
        return;

    // Aim at the torso, not the feet, and only lead as far as the authored
    // speed justifies. A bolt that tracks is not dodgeable and stops being a
    // projectile in any sense the player can read.
    glm::vec3 aim = authoredDirection;
    if (glm::dot(aim, aim) < 0.000001f)
        aim = forwardOf(reg.get<EnemyMotion>(owner).yaw);
    else
        aim = glm::normalize(aim);
    if (glm::dot(authoredDirection, authoredDirection) < 0.000001f &&
        targetValid) {
        const glm::vec3 to = targetFeet +
                             glm::vec3(0.0f, mTarget.height * 0.55f, 0.0f) -
                             origin;
        const float len = glm::length(to);
        if (len > 1e-3f)
            aim = to / len;
    }

    if (!mBoltMesh.valid()) {
        eng::PrimitiveMeshDesc pm;
        pm.kind = eng::PrimitiveKind::Sphere;
        pm.radius = 0.12f;
        pm.rings = 6;
        pm.segments = 8;
        mBoltMesh = ctx.renderer.createPrimitiveMesh(pm);
    }
    const eng::NodeHandle node = ctx.renderer.createNode(eng::kRootNode, origin);
    ctx.renderer.attachMesh(node, mBoltMesh, "Game/Enemy/RedElite",
                            "Game/Prototype/Floor", false);

    const entt::entity p = reg.create();
    EnemyProjectile& proj = reg.emplace<EnemyProjectile>(p);
    proj.position = origin;
    proj.velocity = aim * std::max(1.0f, atk.projectileSpeed);
    proj.weapon = payloadOverride ? *payloadOverride : atk.weapon;
    proj.owner = owner;
    proj.node = node;
    // Enough to cross a long hall and no more; a stray bolt should not be
    // waiting for you two rooms later.
    proj.ttl = 4.0f;
}

void EnemySystem::stepProjectiles(GameContext& ctx, glm::vec3 targetFeet,
                                  bool targetValid, float dt)
{
    entt::registry& reg = registry();
    mScratch.clear();
    for (auto [p, proj] : reg.view<EnemyProjectile>().each()) {
        const glm::vec3 from = proj.position;
        const glm::vec3 to = from + proj.velocity * dt;
        bool consumed = false;

        // World first: a bolt that would have hit a pillar must not also hit
        // the player standing behind it.
        const glm::vec3 delta = to - from;
        const float dist = glm::length(delta);
        eng::RayHit hit;
        float wallAt = dist;
        if (dist > 1e-5f &&
            ctx.physics.rayCast(from, delta / dist, dist, hit, kWorld)) {
            wallAt = hit.fraction * dist;
            consumed = true;
        }

        if (targetValid) {
            const glm::vec3 legs = targetFeet + glm::vec3(0.0f, 0.15f, 0.0f);
            const glm::vec3 head =
                targetFeet + glm::vec3(0.0f, mTarget.height, 0.0f);
            float projectileFraction = 0.0f;
            const float d = segmentDistance(from, to, legs, head,
                                            projectileFraction);
            if (d <= proj.radius + mTarget.radius) {
                const float travelled = dist * projectileFraction;
                if (travelled <= wallAt + mTarget.radius) {
                    if (mOnHitPlayer) {
                        const glm::vec3 dir =
                            dist > 1e-5f ? delta / dist : glm::vec3(0, 0, 1);
                        mOnHitPlayer(proj.owner, proj.weapon, 0.0f, dir,
                                     targetFeet +
                                         glm::vec3(0.0f, mTarget.height * 0.55f,
                                                   0.0f));
                    }
                    consumed = true;
                }
            }
        }

        proj.position = consumed && dist > 1e-5f
                            ? from + (delta / dist) * wallAt
                            : to;
        proj.ttl -= dt;
        if (consumed || proj.ttl <= 0.0f)
            mScratch.push_back(p);
    }
    for (entt::entity p : mScratch) {
        if (const auto* proj = reg.try_get<EnemyProjectile>(p);
            proj && proj->node.valid())
            ctx.renderer.destroyNode(proj->node);
        reg.destroy(p);
    }
}

// --- the step ---------------------------------------------------------------

void EnemySystem::fixedStep(GameContext& ctx, glm::vec3 targetFeet,
                            bool targetValid, float dt)
{
    if (!mDirector)
        return;
    entt::registry& reg = mDirector->registry();

    enemy::syncPositions(reg, ctx.physics);

    auto view = reg.view<EnemyTag, EnemyBrain, EnemyMotion, EnemyRender,
                         EnemyPatternExecution, ActionState, Stamina, Health,
                         BodyLink>();
    for (auto [e, tag, brain, motion, render, patternExecution, action, stamina,
               hp, link] :
         view.each()) {
        const EnemyDef& def = *tag.def;

        if (render.dying) {
            // Corpse: physics owns it now. Only the despawn clock ticks.
            if (render.corpseTimer > 0.0f)
                render.corpseTimer -= dt;
            continue;
        }
        if (hp.dead())
            continue; // onKilled has not run for it yet this step; it will

        // --- senses -------------------------------------------------------
        enemyai::Senses senses;
        senses.selfPos = motion.feet;
        senses.selfYaw = motion.yaw;
        senses.targetValid = targetValid;
        senses.targetPos = targetFeet;
        senses.healthPct = hp.max > 0.0f ? hp.current / hp.max : 0.0f;
        senses.staggered = action.phase == ActionPhase::Staggered;
        senses.busy = action.phase != ActionPhase::Idle;
        senses.dead = hp.dead();
        if (targetValid) {
            senses.targetDistance =
                glm::length(flatten(targetFeet - motion.feet));
            const glm::vec3 selfEye =
                motion.feet + glm::vec3(0.0f, def.body.eyeHeight, 0.0f);
            // The target's eyes, not its feet: sighting someone across a low
            // wall should work, and their feet are behind it.
            const glm::vec3 targetEye = targetFeet + glm::vec3(0.0f, 1.5f, 0.0f);
            senses.lineOfSight =
                senses.targetDistance <= def.perception.sightRange &&
                lineOfSight(ctx.physics, selfEye, targetEye);
        }
        senses.separation =
            enemy::separation(reg, e, motion.feet, def.body.separationRadius);

        // --- brain --------------------------------------------------------
        const enemyai::Intent intent = enemyai::think(def, brain, senses, dt);

        // --- attack delivery ----------------------------------------------
        // Starting an attack can still be refused (stamina), which is the point
        // of asking the feel layer rather than setting the phase directly.
        if (intent.attack >= 0 && intent.attack < int(def.attacks.size())) {
            const EnemyAttack& atk = def.attacks[size_t(intent.attack)];
            if (feel::actionstate::beginAttack(action, stamina, atk.timing)) {
                // The cooldown starts here, not where the brain chose the
                // move: a swing the feel layer refused was never made.
                enemyai::notifyAttackStarted(def, brain, intent.attack);
                if (mOnTelegraph)
                    mOnTelegraph(e, atk, motion.feet);
            } else {
                enemyai::notifyAttackRefused(brain); // no gas: back off
            }
        }
        // The active frame is one step wide and is set by actionstate::advance,
        // which CombatSystem::fixedStep runs before this. That ordering is what
        // makes "the swing connects exactly once" true.
        if (action.activeFiredThisStep && brain.pendingAttack >= 0 &&
            brain.pendingAttack < int(def.attacks.size())) {
            const EnemyAttack& atk = def.attacks[size_t(brain.pendingAttack)];
            const glm::vec3 origin =
                motion.feet + glm::vec3(0.0f, def.body.eyeHeight, 0.0f);
            if (atk.ranged && atk.pattern) {
                glm::vec3 commitAim = forwardOf(motion.yaw);
                if (targetValid) {
                    commitAim = targetFeet +
                                    glm::vec3(0.0f, mTarget.height * 0.55f, 0.0f) -
                                origin;
                }
                patternExecution.attackIndex = brain.pendingAttack;
                bulletpattern::begin(patternExecution.pattern, commitAim);
            } else if (atk.ranged) {
                spawnProjectile(ctx, e, atk, origin, targetFeet, targetValid);
            }
            else if (targetValid)
                resolveMelee(ctx.physics, e, def, atk, motion, targetFeet);
            brain.pendingAttack = -1;
        }

        if (patternExecution.attackIndex >= 0 &&
            patternExecution.attackIndex < int(def.attacks.size())) {
            const EnemyAttack& atk =
                def.attacks[std::size_t(patternExecution.attackIndex)];
            if (action.phase != ActionPhase::Active || !atk.pattern) {
                bulletpattern::cancel(patternExecution.pattern);
                patternExecution.attackIndex = -1;
            } else {
                const glm::vec3 origin =
                    motion.feet + glm::vec3(0.0f, def.body.eyeHeight, 0.0f);
                glm::vec3 liveAim = forwardOf(motion.yaw);
                if (targetValid)
                    liveAim = targetFeet +
                                  glm::vec3(0.0f, mTarget.height * 0.55f, 0.0f) -
                              origin;
                const BulletPatternStep patternStep = bulletpattern::advance(
                    *atk.pattern, patternExecution.pattern,
                    forwardOf(motion.yaw), liveAim);
                for (std::uint16_t i = 0; i < patternStep.commandCount; ++i) {
                    const BulletSpawnCommand& command = patternStep.commands[i];
                    spawnProjectile(ctx, e, atk, origin, targetFeet, targetValid,
                                    command.direction, command.projectileId);
                }
            }
        }

        // --- movement -----------------------------------------------------
        motion.yaw = enemyai::turnToward(motion.yaw, intent.desiredYaw,
                                         def.locomotion.turnRateDeg, dt);

        const glm::vec3 desired = intent.moveDirection * intent.moveSpeed;
        const float accel = std::max(0.1f, def.locomotion.acceleration);
        const glm::vec3 delta = desired - motion.velocity;
        const float deltaLen = glm::length(delta);
        const float maxDelta = accel * dt;
        motion.velocity += deltaLen > maxDelta && deltaLen > 1e-5f
                               ? delta * (maxDelta / deltaLen)
                               : delta;

        bool grounded = motion.grounded;
        motion.feet = moveAndSlide(ctx.physics, def, motion.feet,
                                   motion.velocity, dt, grounded);
        motion.grounded = grounded;

        const glm::vec3 centre =
            motion.feet + glm::vec3(0.0f, def.body.height * 0.5f, 0.0f);
        ctx.physics.setBodyTransform(
            link.body, centre,
            glm::angleAxis(motion.yaw, glm::vec3(0.0f, 1.0f, 0.0f)));

        if (render.hitFlash > 0.0f)
            render.hitFlash = std::max(0.0f, render.hitFlash - dt);
    }

    stepProjectiles(ctx, targetFeet, targetValid, dt);
    // Reap corpses whose timer ran out. Collected first: despawn destroys
    // entities, and a view must not be mutated while it is being iterated.
    mScratch.clear();
    for (auto [e, tag, render] :
         reg.view<const EnemyTag, const EnemyRender>().each())
        if (render.dying && tag.def->stats.corpseTime > 0.0f &&
            render.corpseTimer <= 0.0f)
            mScratch.push_back(e);
    for (entt::entity e : mScratch)
        despawn(ctx, e);
}

void EnemySystem::syncRender(GameContext& ctx)
{
    if (!mDirector)
        return;
    for (auto [e, tag, render, link] :
         registry().view<const EnemyTag, const EnemyRender, const BodyLink>()
             .each()) {
        if (!render.node.valid())
            continue;
        glm::vec3 pos;
        glm::quat rot;
        ctx.physics.getRenderTransform(link.body, pos, rot);
        ctx.renderer.setPosition(render.node, pos);
        ctx.renderer.setOrientation(render.node, rot);

        // Hit reaction: a brief squash. Cheap, silhouette-only, and it reads at
        // the resolution this renderer actually draws at -- a colour flash on a
        // 320-wide framebuffer does not.
        glm::vec3 scale = tag.def->visual.scale;
        if (render.hitFlash > 0.0f && tag.def->visual.hitFlashTime > 0.0f) {
            const float t = render.hitFlash / tag.def->visual.hitFlashTime;
            const float a = tag.def->visual.hitFlash * t;
            scale.x *= 1.0f + a;
            scale.z *= 1.0f + a;
            scale.y *= 1.0f - a;
        }
        ctx.renderer.setScale(render.node, scale);
    }

}

void EnemySystem::syncProjectileRender(GameContext& ctx)
{
    if (!mDirector)
        return;
    for (auto [p, proj] : registry().view<const EnemyProjectile>().each())
        if (proj.node.valid())
            ctx.renderer.setPosition(proj.node, proj.position);
}

// --- reactions --------------------------------------------------------------

void EnemySystem::notifyHit(entt::entity e)
{
    if (!mDirector || !registry().valid(e))
        return;
    entt::registry& reg = registry();
    if (auto* render = reg.try_get<EnemyRender>(e))
        if (const auto* tag = reg.try_get<EnemyTag>(e))
            render->hitFlash = tag->def->visual.hitFlashTime;
    if (auto* brain = reg.try_get<EnemyBrain>(e)) {
        // Being hit ends dormancy and buys a window of guaranteed aggression,
        // so shooting something in the back never reads as it not noticing.
        if (brain->state == EnemyState::Dormant ||
            brain->state == EnemyState::Idle)
            brain->state = EnemyState::Alert;
        brain->aggroGrace = 2.0f;
        brain->lostSightFor = 0.0f;
    }
}

void EnemySystem::onKilled(GameContext& ctx, entt::entity e,
                           glm::vec3 impulseDir)
{
    if (!mDirector || !registry().valid(e))
        return;
    entt::registry& reg = registry();
    // all_of is the gate that makes this safe to wire into the director's
    // global death callback: a dying barrel simply is not an enemy.
    if (!reg.all_of<EnemyTag, EnemyRender, BodyLink>(e))
        return;
    auto [tag, render, link] = reg.get<EnemyTag, EnemyRender, BodyLink>(e);
    if (render.dying)
        return;

    render.dying = true;
    render.corpseTimer = tag.def->stats.corpseTime;
    if (auto* brain = reg.try_get<EnemyBrain>(e))
        brain->state = EnemyState::Dead;

    // The corpse becomes a real rigid body and takes the killing blow. This is
    // the whole death animation: physics is the animator, which for a
    // placeholder cylinder reads better than any authored pose would.
    ctx.physics.setBodyKinematic(link.body, false);
    glm::vec3 impulse = impulseDir;
    const float len = glm::length(impulse);
    impulse = len > 1e-4f ? impulse / len : glm::vec3(0.0f, 0.0f, 1.0f);
    impulse *= tag.def->stats.deathImpulse * tag.def->body.mass * 0.05f;
    impulse.y += tag.def->stats.deathImpulse * 0.5f;
    glm::vec3 pos;
    glm::quat rot;
    ctx.physics.getRenderTransform(link.body, pos, rot);
    ctx.renderer.spawnParticles("enemy_death_burst", pos);
    ctx.physics.applyImpulse(link.body, impulse, pos);

    if (mOnDeath)
        mOnDeath(e, *tag.def);
}

void EnemySystem::despawn(GameContext& ctx, entt::entity e)
{
    if (!mDirector)
        return;
    entt::registry& reg = registry();
    if (!reg.valid(e) || !reg.all_of<EnemyTag>(e))
        return;
    if (auto* render = reg.try_get<EnemyRender>(e); render && render->node.valid())
        ctx.renderer.destroyNode(render->node);
    if (auto* link = reg.try_get<BodyLink>(e)) {
        const eng::BodyHandle body = link->body;
        ctx.physics.removeBody(body);
        mDirector->removeCombatant(body); // drops the link and destroys `e`
        return;
    }
    reg.destroy(e);
}

void EnemySystem::clear(GameContext& ctx)
{
    if (!mDirector)
        return;
    mScratch.clear();
    for (auto e : registry().view<EnemyTag>())
        mScratch.push_back(e);
    for (entt::entity e : mScratch)
        despawn(ctx, e);

    // Bolts in flight outlive their shooter by design, so clearing enemies has
    // to clear them explicitly or a level transition leaves nodes behind.
    mScratch.clear();
    for (auto [p, proj] : registry().view<const EnemyProjectile>().each()) {
        if (proj.node.valid())
            ctx.renderer.destroyNode(proj.node);
        mScratch.push_back(p);
    }
    for (entt::entity p : mScratch)
        registry().destroy(p);
    for (const auto& [id, mesh] : mMeshCache)
        ctx.renderer.releaseMesh(mesh);
    mMeshCache.clear();
    if (mBoltMesh.valid())
        ctx.renderer.releaseMesh(mBoltMesh);
    mBoltMesh = {};
}

// --- queries ----------------------------------------------------------------

entt::entity EnemySystem::entityForBody(eng::BodyHandle body) const
{
    if (!mDirector)
        return entt::null;
    const entt::entity e = mDirector->entityForBody(body);
    if (e == entt::null || !registry().all_of<EnemyTag>(e))
        return entt::null;
    return e;
}

int EnemySystem::aliveCount() const
{
    if (!mDirector)
        return 0;
    int n = 0;
    for (auto [e, render] : registry().view<const EnemyRender>().each())
        if (!render.dying)
            ++n;
    return n;
}

int EnemySystem::liveCount() const
{
    return mDirector ? int(registry().view<const EnemyTag>().size()) : 0;
}

std::vector<EnemySystem::Snapshot> EnemySystem::snapshot(glm::vec3 from) const
{
    std::vector<Snapshot> out;
    if (!mDirector)
        return out;
    for (auto [e, tag, brain, motion, hp] :
         registry()
             .view<const EnemyTag, const EnemyBrain, const EnemyMotion,
                   const Health>()
             .each()) {
        Snapshot s;
        s.entity = e;
        s.defId = tag.defId;
        s.name = tag.def->name;
        s.category = tag.def->category;
        s.state = brain.state;
        s.position = motion.feet;
        s.health = hp.current;
        s.healthMax = hp.max;
        s.distance = glm::length(flatten(motion.feet - from));
        s.boss = tag.def->boss;
        out.push_back(std::move(s));
    }
    return out;
}

} // namespace game
