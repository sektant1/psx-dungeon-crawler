// The half of enemy persistence that needs a live world.
//
// Split from EnemySave.cpp for the same reason the spawner is split: the codec
// is pure and exhaustively testable, and only these two functions require a
// registry, a renderer and a physics world to exist.
#include "EnemySave.h"

#include "EnemySpawner.h"
#include "EnemySystem.h"
#include "GameContext.h"
#include "combat/CombatComponents.h"
#include "combat/FeelComponents.h"

#include <eng/Log.h>

namespace game::enemysave {

EnemySaveData capture(const EnemySystem& enemies, const EnemySpawner& spawner)
{
    EnemySaveData out;
    const entt::registry& reg = enemies.registry();

    for (auto [e, tag, brain, motion, render, hp] :
         reg.view<const EnemyTag, const EnemyBrain, const EnemyMotion,
                  const EnemyRender, const Health>()
             .each()) {
        if (render.dying)
            continue; // corpses are decoration; see the header

        EnemySnapshot s;
        s.defId = tag.defId;
        s.feet = motion.feet;
        s.velocity = motion.velocity;
        s.yaw = motion.yaw;
        s.grounded = motion.grounded;

        s.health = hp.current;
        s.healthMax = hp.max;
        s.invulnTimer = hp.invulnTimer;
        if (const auto* poise = reg.try_get<Poise>(e)) {
            s.poise = poise->current;
            s.poiseMax = poise->max;
            s.poiseSinceHit = poise->sinceHit;
            s.staggerImmuneFor = poise->staggerImmuneFor;
        }
        if (const auto* stam = reg.try_get<Stamina>(e)) {
            s.stamina = stam->current;
            s.staminaMax = stam->max;
            s.staminaSinceSpend = stam->sinceSpend;
        }

        s.state = uint8_t(brain.state);
        s.stateTime = brain.stateTime;
        s.thinkTimer = brain.thinkTimer;
        s.lostSightFor = brain.lostSightFor;
        s.home = brain.home;
        s.lastKnownTarget = brain.lastKnownTarget;
        s.hasLastKnown = brain.hasLastKnown;
        s.strafeSign = brain.strafeSign;
        for (int i = 0; i < 8; ++i)
            s.cooldown[i] = brain.cooldown[i];
        s.rng = brain.rng;
        s.aggroGrace = brain.aggroGrace;

        // A mid-swing enemy is saved as though it had not started: the pending
        // attack goes with the ActionState the header explains is dropped.
        if (const auto* origin = reg.try_get<EnemyOrigin>(e)) {
            s.spawnerIndex = origin->spawnerIndex;
            if (origin->spawnerIndex >= 0 &&
                origin->spawnerIndex < spawner.size())
                s.spawnerId = spawner.point(origin->spawnerIndex).id;
        }

        if (const auto* fx = reg.try_get<StatusEffects>(e))
            for (const ActiveEffect& a : fx->active) {
                if (a.remaining <= 0.0f)
                    continue;
                s.effects.push_back({uint8_t(a.kind), a.magnitude, a.remaining,
                                     a.tickAccum});
            }

        out.enemies.push_back(std::move(s));
    }

    for (int i = 0; i < spawner.size(); ++i) {
        const EnemySpawnPoint& p = spawner.point(i);
        if (p.id.empty()) {
            // Nothing to key it by, so it cannot be restored. Say so once
            // rather than silently resetting an encounter on every load.
            eng::log::error(
                "enemysave: spawner %d ('%s') has no id; its wave state will "
                "not survive a save. Give it an `id`.",
                i, p.enemy.c_str());
            continue;
        }
        const EnemySpawnState& st = spawner.state(i);
        out.spawners.push_back({p.id, st.armed, st.exhausted, st.wavesSpawned,
                                st.timer, st.spawnedTotal});
    }
    return out;
}

int restore(GameContext& ctx, EnemySystem& enemies, EnemySpawner& spawner,
            const EnemySaveData& data)
{
    // Whatever is standing now is not what the save describes.
    enemies.clear(ctx);

    // Spawner pacing first: an enemy restored below carries a spawner index,
    // and the counts have to line up before anything ticks.
    for (const SpawnerSnapshot& s : data.spawners) {
        const int index = spawner.indexOf(s.id);
        if (index < 0) {
            eng::log::error(
                "enemysave: save names spawner '%s', which this level does "
                "not define; its state is dropped",
                s.id.c_str());
            continue;
        }
        EnemySpawnState& st = spawner.mutableState(index);
        st.armed = s.armed;
        st.exhausted = s.exhausted;
        st.wavesSpawned = s.wavesSpawned;
        st.timer = s.timer;
        st.spawnedTotal = s.spawnedTotal;
        st.aliveFromHere = 0; // recomputed by the next update()
    }

    entt::registry& reg = enemies.registry();
    int restored = 0;
    for (const EnemySnapshot& s : data.enemies) {
        // Re-resolve the spawner by id; the index in the save is only a hint,
        // and it is wrong the moment spawners.toml gains an entry above it.
        int spawnerIndex = spawner.indexOf(s.spawnerId);
        if (spawnerIndex < 0)
            spawnerIndex = -1;

        // Through the normal spawn path: a restored enemy must not be able to
        // end up with a body or a node built differently from a fresh one.
        const entt::entity e =
            enemies.spawn(ctx, s.defId, s.feet, s.yaw, spawnerIndex);
        if (e == entt::null) {
            eng::log::error(
                "enemysave: save contains '%s', which enemies.toml no longer "
                "defines; that enemy is dropped",
                s.defId.c_str());
            continue;
        }

        // ...then overwrite the defaults spawn() installed with what was saved.
        auto& motion = reg.get<EnemyMotion>(e);
        motion.velocity = s.velocity;
        motion.grounded = s.grounded;

        auto& hp = reg.get<Health>(e);
        hp.current = s.health;
        hp.max = s.healthMax;
        hp.invulnTimer = s.invulnTimer;
        if (auto* poise = reg.try_get<Poise>(e)) {
            poise->current = s.poise;
            poise->max = s.poiseMax;
            poise->sinceHit = s.poiseSinceHit;
            poise->staggerImmuneFor = s.staggerImmuneFor;
        }
        if (auto* stam = reg.try_get<Stamina>(e)) {
            stam->current = s.stamina;
            stam->max = s.staminaMax;
            stam->sinceSpend = s.staminaSinceSpend;
        }

        auto& brain = reg.get<EnemyBrain>(e);
        brain.state = EnemyState(s.state);
        brain.stateTime = s.stateTime;
        brain.thinkTimer = s.thinkTimer;
        brain.lostSightFor = s.lostSightFor;
        brain.home = s.home;
        brain.lastKnownTarget = s.lastKnownTarget;
        brain.hasLastKnown = s.hasLastKnown;
        brain.strafeSign = s.strafeSign;
        for (int i = 0; i < 8; ++i)
            brain.cooldown[i] = s.cooldown[i];
        brain.rng = s.rng;
        brain.aggroGrace = s.aggroGrace;
        // No pending attack: the swing was not saved, so nothing may deliver.
        brain.pendingAttack = -1;
        // A saved Attack or Stagger state would leave the brain waiting on an
        // ActionState that is now Idle -- Attack yields to it forever. Both
        // resolve to Chase, which is where recovering from either leads.
        if (brain.state == EnemyState::Attack ||
            brain.state == EnemyState::Stagger) {
            brain.state = EnemyState::Chase;
            brain.stateTime = 0.0f;
        }

        if (!s.effects.empty()) {
            auto& fx = reg.get_or_emplace<StatusEffects>(e);
            for (const EnemySnapshot::Effect& a : s.effects)
                fx.active.push_back({CrowdControl(a.kind), a.magnitude,
                                     a.remaining, a.tickAccum, entt::null});
        }
        ++restored;
    }
    return restored;
}

} // namespace game::enemysave
