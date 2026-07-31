// The brain, with no engine attached. Every one of these is a rule a designer
// tunes through enemies.toml, so each one is worth pinning.
#include "../src/enemy/EnemyAI.h"

#include <cmath>
#include <cstdio>

using namespace game;
using namespace game::enemyai;

static int failures = 0;
static void check(bool c, const char* m)
{
    if (!c) { std::fprintf(stderr, "FAIL: %s\n", m); ++failures; }
}
static bool nearly(float a, float b, float eps = 1e-3f)
{
    return std::fabs(a - b) < eps;
}

static EnemyDef melee()
{
    EnemyDef def;
    def.perception.sightRange = 20.0f;
    def.perception.sightFovDeg = 120.0f;
    def.perception.hearingRange = 3.0f;
    def.perception.alertTime = 0.4f;
    def.perception.loseSightTime = 2.0f;
    def.behaviour.aggression = 1.0f;
    def.behaviour.circleChance = 0.0f;
    def.behaviour.thinkInterval = 0.2f;
    EnemyAttack swing;
    swing.id = "swing";
    swing.maxRange = 2.0f;
    swing.aimConeDeg = 45.0f;
    swing.cooldown = 1.0f;
    def.attacks.push_back(swing);
    return def;
}

static Senses at(float distance, bool los = true)
{
    Senses s;
    s.targetValid = true;
    s.selfPos = glm::vec3(0.0f);
    s.selfYaw = 0.0f; // facing +Z
    s.targetPos = glm::vec3(0.0f, 0.0f, distance);
    s.targetDistance = distance;
    s.lineOfSight = los;
    return s;
}

int main()
{
    const EnemyDef def = melee();

    // --- perception -------------------------------------------------------
    {
        check(canPerceive(def, at(10.0f)), "sees a target in front, in range");
        check(!canPerceive(def, at(30.0f)), "does not see past sight range");
        check(!canPerceive(def, at(10.0f, /*los=*/false)),
              "does not see through a wall");

        Senses behind = at(10.0f);
        behind.targetPos = glm::vec3(0.0f, 0.0f, -10.0f); // directly behind
        check(!canPerceive(def, behind), "does not see behind itself");

        // ...but hears something close, whatever way it is facing.
        Senses close = at(2.0f);
        close.targetPos = glm::vec3(0.0f, 0.0f, -2.0f);
        close.lineOfSight = false;
        check(canPerceive(def, close),
              "hears a target inside the hearing radius regardless of facing");

        Senses none;
        check(!canPerceive(def, none), "no target means nothing to perceive");
    }

    // --- attack selection -------------------------------------------------
    {
        EnemyBrain b;
        check(selectAttack(def, b, at(1.5f), 0.0f) == 0, "picks the in-range move");
        check(selectAttack(def, b, at(9.0f), 0.0f) == -1, "out of range: no move");

        EnemyBrain cooling;
        cooling.cooldown[0] = 0.5f;
        check(selectAttack(def, cooling, at(1.5f), 0.0f) == -1,
              "a move on cooldown is not selectable");

        Senses side = at(1.5f);
        side.targetPos = glm::vec3(1.5f, 0.0f, 0.0f); // 90 deg off the 45 cone
        side.targetDistance = 1.5f;
        check(selectAttack(def, b, side, 0.0f) == -1,
              "a target outside the aim cone is not attacked");

        // Weighting: with weights 3 and 1, a roll in the first quarter of the
        // range picks the first move and one past 3/4 picks the second.
        EnemyDef two = def;
        two.attacks[0].weight = 3.0f;
        EnemyAttack second = two.attacks[0];
        second.id = "second";
        second.weight = 1.0f;
        two.attacks.push_back(second);
        check(selectAttack(two, b, at(1.5f), 0.1f) == 0, "low roll picks the heavy move");
        check(selectAttack(two, b, at(1.5f), 0.9f) == 1, "high roll picks the light move");

        // A ranged move needs line of sight even when a melee one would not.
        EnemyDef ranged = def;
        ranged.attacks[0].ranged = true;
        ranged.attacks[0].minRange = 1.0f;
        ranged.attacks[0].maxRange = 20.0f;
        check(selectAttack(ranged, b, at(10.0f, /*los=*/false), 0.0f) == -1,
              "no ranged attack without line of sight");
        check(selectAttack(ranged, b, at(10.0f), 0.0f) == 0,
              "ranged attack with line of sight");
    }

    // --- turning ----------------------------------------------------------
    {
        // Capped per step.
        const float y = turnToward(0.0f, 3.0f, 90.0f, 0.1f);
        check(nearly(y, glm::radians(9.0f)), "turn is capped by the turn rate");
        // Snaps when the remaining angle is within one step.
        check(nearly(turnToward(0.0f, 0.01f, 90.0f, 0.1f), 0.01f),
              "small remainder snaps to the target");
        // Takes the short way round the wrap.
        const float wrapped = turnToward(3.1f, -3.1f, 90.0f, 0.1f);
        check(wrapped > 3.1f || wrapped < -3.0f,
              "turns the short way across the +/-pi seam");
    }

    // --- yaw convention matches the player controller ---------------------
    {
        check(nearly(yawToward(glm::vec3(0.0f), glm::vec3(0, 0, 1)), 0.0f),
              "+Z is yaw 0");
        check(nearly(yawToward(glm::vec3(0.0f), glm::vec3(1, 0, 0)),
                     glm::radians(90.0f)),
              "+X is yaw 90 degrees");
    }

    // --- state machine ----------------------------------------------------
    {
        // Idle -> Alert on sight; Alert holds for alertTime and does not move.
        EnemyBrain b;
        Intent i = think(def, b, at(10.0f), 0.1f);
        check(b.state == EnemyState::Alert, "noticing the target enters Alert");
        check(nearly(glm::length(i.moveDirection), 0.0f),
              "Alert is a pause, not an approach");
        for (int n = 0; n < 10; ++n)
            i = think(def, b, at(10.0f), 0.1f);
        check(b.state != EnemyState::Alert, "Alert ends after alert_time");
    }
    {
        // Chase closes distance; at range it commits to the attack.
        EnemyBrain b;
        b.state = EnemyState::Chase;
        const Intent far = think(def, b, at(10.0f), 0.1f);
        check(far.moveSpeed > 0.0f, "chases when out of range");
        check(far.moveDirection.z > 0.9f, "chases toward the target");

        EnemyBrain c;
        c.state = EnemyState::Chase;
        const Intent close = think(def, c, at(1.5f), 0.1f);
        check(close.attack == 0, "commits to the attack when in range");
        check(c.state == EnemyState::Attack, "entering Attack");
        // The cooldown is *not* started here -- see notifyAttackStarted.
        check(nearly(c.cooldown[0], 0.0f), "committing alone costs no cooldown");
    }
    {
        // While the swing is running the brain yields to ActionState.
        EnemyBrain b;
        b.state = EnemyState::Attack;
        Senses s = at(1.5f);
        s.busy = true;
        const Intent i = think(def, b, s, 0.1f);
        check(i.attack == -1, "does not start a second attack mid-swing");
        check(b.state == EnemyState::Attack, "stays in Attack while busy");
    }
    {
        // Stagger locks everything. That is the punish window.
        EnemyBrain b;
        b.state = EnemyState::Chase;
        Senses s = at(1.5f);
        s.staggered = true;
        const Intent i = think(def, b, s, 0.1f);
        check(b.state == EnemyState::Stagger, "staggered overrides the state");
        check(nearly(i.moveSpeed, 0.0f), "no movement while staggered");
        check(i.attack == -1, "no attack while staggered");
    }
    {
        // Losing sight leads to Search, then back to Idle at the last known
        // position -- it does not know where you went.
        EnemyBrain b;
        b.state = EnemyState::Chase;
        b.hasLastKnown = true;
        b.lastKnownTarget = glm::vec3(0.0f, 0.0f, 8.0f);
        Senses blind = at(8.0f, /*los=*/false);
        blind.targetDistance = 25.0f; // also out of range: nothing to perceive
        for (int n = 0; n < 30; ++n)
            think(def, b, blind, 0.1f);
        check(b.state == EnemyState::Search, "gives up the chase and searches");
        const Intent i = think(def, b, blind, 0.1f);
        check(i.moveDirection.z > 0.9f, "searches toward the last known position");
    }
    {
        // Flee below the morale threshold.
        EnemyDef timid = def;
        timid.behaviour.fleeHealthPct = 0.5f;
        EnemyBrain b;
        b.state = EnemyState::Chase;
        Senses hurt = at(4.0f);
        hurt.healthPct = 0.2f;
        const Intent i = think(timid, b, hurt, 0.1f);
        check(b.state == EnemyState::Flee, "flees below the health threshold");
        check(i.moveDirection.z < -0.9f, "flees away from the target");
    }
    {
        // Dormant: does nothing until it perceives something.
        EnemyDef sleeper = def;
        sleeper.behaviour.startsDormant = true;
        EnemyBrain b;
        b.state = EnemyState::Dormant;
        Senses unseen = at(40.0f);
        think(sleeper, b, unseen, 0.1f);
        check(b.state == EnemyState::Dormant, "stays asleep when unseen");
        think(sleeper, b, at(5.0f), 0.1f);
        check(b.state == EnemyState::Alert, "wakes when it perceives the target");
    }
    {
        // Stationary enemies attack but never move.
        EnemyDef turret = def;
        turret.behaviour.stationary = true;
        turret.attacks[0].maxRange = 20.0f;
        EnemyBrain b;
        b.state = EnemyState::Chase;
        const Intent i = think(turret, b, at(10.0f), 0.1f);
        check(nearly(i.moveSpeed, 0.0f), "a stationary enemy never moves");
        check(i.attack == 0, "a stationary enemy still attacks");
    }
    {
        // Leash: too far from home, walk back and disengage.
        EnemyDef leashed = def;
        leashed.perception.leashRange = 5.0f;
        EnemyBrain b;
        b.state = EnemyState::Chase;
        b.home = glm::vec3(0.0f, 0.0f, -20.0f);
        Senses s = at(2.0f);
        const Intent i = think(leashed, b, s, 0.1f);
        check(i.moveDirection.z < -0.9f, "walks back toward home when leashed out");
        check(i.attack == -1, "does not attack while returning");
    }
    {
        // Backoff: a ranged enemy walks away rather than fighting in melee.
        EnemyDef archer = def;
        archer.behaviour.backoffRange = 6.0f;
        archer.behaviour.preferredRange = 12.0f;
        archer.attacks[0].minRange = 4.0f;
        archer.attacks[0].maxRange = 20.0f;
        EnemyBrain b;
        b.state = EnemyState::Chase;
        const Intent i = think(archer, b, at(2.0f), 0.1f);
        check(i.moveDirection.z < -0.9f, "backs off when the target is too close");
        check(i.attack == -1, "cannot use a min-range move point blank");
    }

    // --- aggression actually gates attacking ------------------------------
    // Regression: `willing` used to be OR'd with `state == Chase`, and Chase is
    // where a melee enemy spends most of its time, so aggression was ignored
    // exactly when it mattered. A 0-aggression enemy in range must mostly
    // decline; a 1.0 one must always take it.
    {
        EnemyDef timid = def;
        timid.behaviour.aggression = 0.0f;
        int attacks = 0;
        for (int seed = 1; seed <= 40; ++seed) {
            EnemyBrain b;
            b.rng = uint32_t(seed) * 2654435761u;
            b.state = EnemyState::Chase;
            if (think(timid, b, at(1.5f), 0.1f).attack >= 0)
                ++attacks;
        }
        check(attacks == 0, "aggression 0 never volunteers an attack");

        EnemyDef eager = def;
        eager.behaviour.aggression = 1.0f;
        int eagerAttacks = 0;
        for (int seed = 1; seed <= 40; ++seed) {
            EnemyBrain b;
            b.rng = uint32_t(seed) * 2654435761u;
            b.state = EnemyState::Chase;
            if (think(eager, b, at(1.5f), 0.1f).attack >= 0)
                ++eagerAttacks;
        }
        check(eagerAttacks == 40, "aggression 1 always takes the opening");
    }
    {
        // ...but a timid enemy that was just hit fights back regardless.
        EnemyDef timid = def;
        timid.behaviour.aggression = 0.0f;
        EnemyBrain b;
        b.state = EnemyState::Chase;
        b.aggroGrace = 2.0f;
        check(think(timid, b, at(1.5f), 0.1f).attack >= 0,
              "a recent hit overrides low aggression");
    }

    // --- the cooldown belongs to the swing, not to the intent --------------
    // Regression: think() started the cooldown when it *chose* a move, but the
    // feel layer can still refuse it for want of stamina. A starved enemy used
    // to burn its whole move list and then stand there with nothing available.
    {
        EnemyBrain b;
        b.state = EnemyState::Chase;
        const Intent i = think(def, b, at(1.5f), 0.1f);
        check(i.attack == 0, "committed");
        check(nearly(b.cooldown[0], 0.0f),
              "choosing a move does not put it on cooldown");

        notifyAttackStarted(def, b, 0);
        check(nearly(b.cooldown[0], def.attacks[0].cooldown),
              "starting the swing does");
    }
    {
        // Refusal frees the commitment and leaves the move usable.
        EnemyBrain b;
        b.state = EnemyState::Chase;
        think(def, b, at(1.5f), 0.1f);
        notifyAttackRefused(b);
        check(b.pendingAttack == -1, "refusal drops the pending attack");
        check(b.state == EnemyState::Circle, "and backs off to recover");
        check(nearly(b.cooldown[0], 0.0f), "the move is still available");
    }

    if (failures == 0) std::printf("EnemyAITests OK\n");
    return failures ? 1 : 0;
}
