#pragma once
#include "EnemyAI.h"
#include "EnemyComponents.h"
#include "EnemyLibrary.h"

#include <eng/Handles.h>
#include <eng/Physics.h>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace eng { class Renderer; }

namespace game {

struct GameContext;
class CombatDirector;

// Enemies live on the combat registry as an archetype of components:
//
//   EnemyTag + EnemyBrain + EnemyMotion + EnemyRender + EnemyOrigin
//   Health + Resistances + FactionTag + BodyLink   (CombatDirector's)
//   Poise + Stamina + ActionState                  (the feel layer's)
//
// Nothing in this system keeps a parallel list of enemies: every pass is a
// view over that archetype, which is what makes an enemy *be* a combatant with
// a brain rather than a separate object kept in sync with one. Adding a
// behaviour later means adding a component and a view, not a member here.
//
// Movement model. Enemies are *kinematic* cylinders that the brain steers, not
// dynamic bodies that get pushed around, and not Jolt character controllers.
// The reasons, in order:
//   - a kinematic body keeps a BodyHandle, and the combat model routes hits by
//     BodyHandle; a Jolt character has no body handle, so every enemy would
//     need a parallel hit-registration path;
//   - a crawler's enemies must hold spacing and telegraph on purpose. A dynamic
//     body the player can shove out of its own windup is not a fight;
//   - a kinematic body still blocks the player, still stops arrows, and is
//     still a solid Prop for every existing query.
// The cost is that walls are this system's problem: movement collide-and-slides
// with a shape cast and snaps to the floor with a ray, both in the .cpp. On
// death the body flips to dynamic and takes the killing blow's impulse, which
// is the corpse path the training dummy already proved.
class EnemySystem {
public:
    // An attack connected with the player -- a melee arc that covered them, or
    // a projectile that reached them. This system decides *whether* the attack
    // landed (it knows the reach, the arc and the bolt's flight); the game
    // decides what that costs, because the player is the game's object and is
    // not a physics body this system could route a hit to.
    //
    // `weapon` is a row in weapons.toml, so an enemy hit resolves through
    // exactly the pipeline a player hit does.
    using HitPlayerFn = std::function<void(entt::entity enemy,
                                           const std::string& weapon,
                                           float poiseDamage, glm::vec3 dir,
                                           glm::vec3 point)>;
    // An attack committed (windup started). Purely for feedback -- audio, a
    // telegraph flash, a camera cue. Nothing gameplay depends on it.
    using TelegraphFn = std::function<void(entt::entity enemy,
                                           const EnemyAttack&, glm::vec3 at)>;
    using DeathFn = std::function<void(entt::entity enemy, const EnemyDef&)>;
    // Something the enemy did that the game may want to make a noise about --
    // it spawned, it noticed you, it took a step, it landed.
    //
    // A callback rather than a call into the audio system, for the same reason
    // TelegraphFn is one: this system knows when an enemy does a thing, and the
    // game decides what a thing sounds like. It is also what keeps every action
    // in one vocabulary -- ActorAudio resolves a cue for a player, an NPC and
    // an enemy the same way, and this system does not learn a second one.
    using ActionFn =
        std::function<void(entt::entity enemy, ActorAction, glm::vec3 at)>;

    // `library` and `director` must outlive the system. Loading enemies.toml is
    // the caller's job, so a level can swap tables without this owning a file.
    void init(EnemyLibrary& library, CombatDirector& director);

    // Spawn `defId` standing on `feetPos`. Returns entt::null on an unknown id
    // (already logged). `spawnerIndex` is bookkeeping for EnemySpawner.
    //
    // `sounds` is the placement's own cue overrides, which sit above the
    // enemy type's own table. Empty -- which is every spawn that did not come
    // from an authored entity -- leaves the type's table in charge.
    entt::entity spawn(GameContext& ctx, const std::string& defId,
                       glm::vec3 feetPos, float yaw = 0.0f,
                       int spawnerIndex = -1,
                       const ActorSoundSet& sounds = {});

    // One fixed step: perception, brains, attack delivery, movement, corpses.
    // `targetFeet` is the player's feet position; `targetValid` false means
    // there is nothing to fight (cutscene, dead player) and every enemy idles.
    void fixedStep(GameContext& ctx, glm::vec3 targetFeet, bool targetValid,
                   float dt);

    // Copy interpolated physics transforms onto render nodes and advance the
    // presentation-only timers. Call from the render step.
    //
    // `dt` is the CHARACTERS channel's stepped delta, the same clock that
    // decides whether the transform is copied this frame -- so the pose
    // advances in the same quantised beats the body does. That is the shipped
    // stop-motion look, not an accident: a smoothly-animated skeleton on a
    // stepped transform reads as two different creatures fighting over one
    // body. `playerEye` is what engaged enemies turn their heads toward; null
    // means nobody is being watched, which is also what a level with no player
    // looks like (the editor's preview).
    void syncRender(GameContext& ctx, float dt,
                    const glm::vec3* playerEye = nullptr);
    void syncProjectileRender(GameContext& ctx);

    // Tell an enemy it was hit, so it flashes and stops ignoring you. Damage
    // itself stays with CombatDirector; this is only the reaction.
    void notifyHit(entt::entity enemy);

    // Death entry point for the combat model: flips the corpse to dynamic and
    // starts the despawn timer. Safe on entities that are not enemies, so it
    // can be wired straight into CombatDirector::setDeathCallback.
    void onKilled(GameContext& ctx, entt::entity enemy, glm::vec3 impulseDir);

    // Remove one enemy's body, node and entity now, whatever state it is in.
    void despawn(GameContext& ctx, entt::entity enemy);
    // Despawn every enemy (level transition, shutdown).
    void clear(GameContext& ctx);

    void setHitPlayerCallback(HitPlayerFn fn) { mOnHitPlayer = std::move(fn); }
    void setTelegraphCallback(TelegraphFn fn) { mOnTelegraph = std::move(fn); }
    void setDeathCallback(DeathFn fn) { mOnDeath = std::move(fn); }
    void setActionCallback(ActionFn fn) { mOnAction = std::move(fn); }

    // Player capsule the attacks test against. Height is the standing height;
    // radius is generous on purpose -- an enemy that whiffs a swing that
    // visually connected reads worse than one that clips you slightly early.
    struct TargetShape {
        float height = 1.7f;
        float radius = 0.45f;
    };
    void setTargetShape(TargetShape shape) { mTarget = shape; }

    // The enemy behind a physics body, or entt::null. Hit routing uses this;
    // it returns null for non-enemy combatants (the player, a barrel).
    entt::entity entityForBody(eng::BodyHandle body) const;

    // The registry enemies live on -- the combat registry. Exposed so callers
    // can run their own views (the spawner counts its wave this way) instead of
    // this class growing a query method per question.
    entt::registry& registry();
    const entt::registry& registry() const;

    int aliveCount() const;  // excludes corpses
    int liveCount() const;   // includes corpses still on their despawn timer

    // Read-only projection for the look tooltip, the HUD and the debug panel.
    struct Snapshot {
        entt::entity entity = entt::null;
        std::string defId;
        std::string name;
        std::string category;
        EnemyState state = EnemyState::Idle;
        glm::vec3 position{0.0f};
        float health = 0.0f;
        float healthMax = 0.0f;
        float distance = 0.0f;
        bool boss = false;
    };
    std::vector<Snapshot> snapshot(glm::vec3 from) const;

private:
    // Move one enemy by `velocity * dt`, sliding along whatever it hits and
    // staying on the floor. Returns the feet position it ended at.
    glm::vec3 moveAndSlide(eng::Physics& physics, const EnemyDef& def,
                           glm::vec3 feetPos, glm::vec3 velocity, float dt,
                           bool& grounded) const;
    // Straight-line visibility from this enemy's eyes to `targetEye`.
    bool lineOfSight(eng::Physics& physics, glm::vec3 selfEye,
                     glm::vec3 targetEye) const;
    // Does this swing's wedge cover the player? Fires the hit callback if so.
    void resolveMelee(eng::Physics&, entt::entity e, const EnemyDef&,
                      const EnemyAttack&, const EnemyMotion&,
                      glm::vec3 targetFeet);
    // Create a bolt entity aimed at the player's torso.
    void spawnProjectile(GameContext& ctx, entt::entity owner,
                          const EnemyAttack&, glm::vec3 origin,
                          glm::vec3 targetFeet, bool targetValid,
                          glm::vec3 authoredDirection = glm::vec3(0.0f),
                          const std::string* payloadOverride = nullptr);
    // Advance every bolt: world sweep, player capsule test, lifetime.
    void stepProjectiles(GameContext& ctx, glm::vec3 targetFeet,
                         bool targetValid, float dt);

    EnemyLibrary* mLibrary = nullptr;
    CombatDirector* mDirector = nullptr;
    // Primitive meshes are cached per definition: a pack of twelve grunts is
    // one mesh and twelve nodes, not twelve identical cylinders.
    std::unordered_map<std::string, eng::MeshHandle> mMeshCache;
    // Reused between steps so the reap pass allocates nothing per frame.
    std::vector<entt::entity> mScratch;
    HitPlayerFn mOnHitPlayer;
    TelegraphFn mOnTelegraph;
    DeathFn mOnDeath;
    ActionFn mOnAction;
    TargetShape mTarget;
    eng::MeshHandle mBoltMesh;
    std::uint32_t mSpawnCounter = 0;
};

namespace enemy {

// Crowd spacing, as a system rather than a method: the push one enemy gets from
// every other live enemy within `radius`. Reads the cached EnemyMotion::feet, so
// it costs no physics queries. Pure -- tested without a world.
glm::vec3 separation(const entt::registry& reg, entt::entity self,
                     glm::vec3 selfPos, float radius);

// Refresh every enemy's cached feet position from physics. Runs before anything
// that compares two enemies' positions, so they are all from the same instant.
void syncPositions(entt::registry& reg, eng::Physics& physics);

} // namespace enemy

} // namespace game
