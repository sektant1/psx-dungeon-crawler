#include "EnemyLibrary.h"

#include "combat/CombatVocabulary.h"

#include <eng/Log.h>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <algorithm>
#include <functional>
#include <unordered_set>

namespace game {

namespace {

float num(const toml::table& t, const char* key, float fallback)
{
    return float(t[key].value_or(double(fallback)));
}

glm::vec3 vec3Or(const toml::node_view<const toml::node>& node, glm::vec3 fb)
{
    const toml::array* a = node.as_array();
    if (!a || a->size() != 3)
        return fb;
    glm::vec3 out = fb;
    for (size_t i = 0; i < 3; ++i)
        out[int(i)] = float((*a)[i].value_or(double(fb[int(i)])));
    return out;
}

void readBody(const toml::table& t, EnemyBody& b)
{
    b.radius = num(t, "radius", b.radius);
    b.height = num(t, "height", b.height);
    b.mass = num(t, "mass", b.mass);
    b.eyeHeight = num(t, "eye_height", b.eyeHeight);
    b.separationRadius = num(t, "separation_radius", b.separationRadius);
}

void readVisual(const toml::table& t, EnemyVisual& v)
{
    v.material = t["material"].value_or(v.material);
    v.mesh = t["mesh"].value_or(v.mesh);
    v.scale = vec3Or(t["scale"], v.scale);
    v.hitFlash = num(t, "hit_flash", v.hitFlash);
    v.hitFlashTime = num(t, "hit_flash_time", v.hitFlashTime);
    v.castShadows = t["cast_shadows"].value_or(v.castShadows);
    v.blood = t["blood"].value_or(v.blood);
}

void readStats(const toml::table& t, EnemyStats& s)
{
    s.health = num(t, "health", s.health);
    s.poise = num(t, "poise", s.poise);
    s.poiseRegen = num(t, "poise_regen", s.poiseRegen);
    s.stamina = num(t, "stamina", s.stamina);
    s.staminaRegen = num(t, "stamina_regen", s.staminaRegen);
    s.deathImpulse = num(t, "death_impulse", s.deathImpulse);
    s.corpseTime = num(t, "corpse_time", s.corpseTime);
    // Resistances are authored as an inline table of channel = value. Stating
    // the key at all replaces the inherited row wholesale: a partial merge
    // would make "this variant has no resistances" unwritable.
    if (const toml::table* r = t["resistances"].as_table()) {
        s.resistances.clear();
        for (auto&& [key, node] : *r)
            s.resistances.push_back(
                {std::string(key.str()), float(node.value_or(0.0))});
    }
}

void readLocomotion(const toml::table& t, EnemyLocomotion& m)
{
    m.walkSpeed = num(t, "walk_speed", m.walkSpeed);
    m.chaseSpeed = num(t, "chase_speed", m.chaseSpeed);
    m.strafeSpeed = num(t, "strafe_speed", m.strafeSpeed);
    m.acceleration = num(t, "acceleration", m.acceleration);
    m.turnRateDeg = num(t, "turn_rate_deg", m.turnRateDeg);
    m.lungeSpeed = num(t, "lunge_speed", m.lungeSpeed);
    m.stepHeight = num(t, "step_height", m.stepHeight);
}

void readPerception(const toml::table& t, EnemyPerception& p)
{
    p.sightRange = num(t, "sight_range", p.sightRange);
    p.sightFovDeg = num(t, "sight_fov_deg", p.sightFovDeg);
    p.hearingRange = num(t, "hearing_range", p.hearingRange);
    p.loseSightTime = num(t, "lose_sight_time", p.loseSightTime);
    p.alertTime = num(t, "alert_time", p.alertTime);
    p.leashRange = num(t, "leash_range", p.leashRange);
}

void readBehaviour(const toml::table& t, EnemyBehaviour& b)
{
    b.aggression = num(t, "aggression", b.aggression);
    b.preferredRange = num(t, "preferred_range", b.preferredRange);
    b.backoffRange = num(t, "backoff_range", b.backoffRange);
    b.circleChance = num(t, "circle_chance", b.circleChance);
    b.repositionTime = num(t, "reposition_time", b.repositionTime);
    b.fleeHealthPct = num(t, "flee_health_pct", b.fleeHealthPct);
    b.dodgeChance = num(t, "dodge_chance", b.dodgeChance);
    b.thinkInterval = num(t, "think_interval", b.thinkInterval);
    b.startsDormant = t["starts_dormant"].value_or(b.startsDormant);
    b.stationary = t["stationary"].value_or(b.stationary);
}

std::optional<BulletPatternShape> readPatternShape(const std::string& value)
{
    if (value == "aimed") return BulletPatternShape::Aimed;
    if (value == "fan") return BulletPatternShape::Fan;
    if (value == "ring") return BulletPatternShape::Ring;
    return std::nullopt;
}

std::optional<BulletAimMode> readAimMode(const std::string& value)
{
    if (value == "facing") return BulletAimMode::Facing;
    if (value == "commit_target") return BulletAimMode::CommitTarget;
    if (value == "live_target") return BulletAimMode::LiveTarget;
    return std::nullopt;
}

std::optional<BulletPatternDef> readPattern(const toml::table& table,
                                            const EnemyAttack& attack)
{
    BulletPatternDef pattern;
    pattern.id = table["id"].value_or(attack.id + "_pattern");
    const toml::array* tracks = table["track"].as_array();
    if (!tracks)
        return std::nullopt;
    for (const toml::node& node : *tracks) {
        const toml::table* authored = node.as_table();
        if (!authored)
            return std::nullopt;
        const auto shape = readPatternShape(
            (*authored)["shape"].value_or(std::string{"aimed"}));
        const auto aim = readAimMode(
            (*authored)["aim"].value_or(std::string{"commit_target"}));
        if (!shape || !aim)
            return std::nullopt;

        BulletPatternTrack track;
        track.firstTick = bulletpattern::secondsToTicks(
            num(*authored, "at", 0.0f), 1.0f / 60.0f);
        track.repetitions = std::uint16_t(std::max(
            int64_t{0}, (*authored)["repeat"].value_or(int64_t{1})));
        track.intervalTicks = bulletpattern::secondsToTicks(
            num(*authored, "interval", 0.0f), 1.0f / 60.0f);
        track.rotationPerRepeatRadians =
            glm::radians(num(*authored, "rotate_degrees", 0.0f));
        track.volley.projectileId =
            (*authored)["projectile"].value_or(attack.weapon);
        track.volley.shape = *shape;
        track.volley.aim = *aim;
        track.volley.shotCount = std::uint16_t(std::max(
            int64_t{0}, (*authored)["shot_count"].value_or(int64_t{1})));
        track.volley.arcRadians =
            glm::radians(num(*authored, "arc_degrees", 0.0f));
        track.volley.angleOffsetRadians =
            glm::radians(num(*authored, "angle_offset_degrees", 0.0f));
        pattern.tracks.push_back(std::move(track));
    }
    const std::uint16_t activeTicks = bulletpattern::secondsToTicks(
        attack.timing.active, 1.0f / 60.0f);
    std::string error;
    if (!bulletpattern::validate(pattern, activeTicks, error)) {
        eng::log::error("Enemy attack '%s' rejected pattern: %s",
                        attack.id.c_str(), error.c_str());
        return std::nullopt;
    }
    return pattern;
}

void readAttacks(const toml::table& t, std::vector<EnemyAttack>& out)
{
    const toml::array* arr = t["attack"].as_array();
    if (!arr)
        return;
    // Same replace-not-merge rule as resistances: a row that states any attack
    // states its whole move list.
    out.clear();
    for (const toml::node& node : *arr) {
        const toml::table* a = node.as_table();
        if (!a)
            continue;
        EnemyAttack atk;
        atk.id = (*a)["id"].value_or(atk.id);
        atk.weapon = (*a)["weapon"].value_or(atk.weapon);
        atk.telegraphEffect =
            (*a)["telegraph_effect"].value_or(atk.telegraphEffect);
        atk.minRange = num(*a, "min_range", atk.minRange);
        atk.maxRange = num(*a, "max_range", atk.maxRange);
        atk.aimConeDeg = num(*a, "aim_cone_deg", atk.aimConeDeg);
        atk.cooldown = num(*a, "cooldown", atk.cooldown);
        atk.weight = num(*a, "weight", atk.weight);
        atk.ranged = (*a)["ranged"].value_or(atk.ranged);
        atk.projectileSpeed = num(*a, "projectile_speed", atk.projectileSpeed);
        atk.timing.windup = num(*a, "windup", atk.timing.windup);
        atk.timing.active = num(*a, "active", atk.timing.active);
        atk.timing.recovery = num(*a, "recovery", atk.timing.recovery);
        atk.timing.staminaCost = num(*a, "stamina_cost", atk.timing.staminaCost);
        atk.timing.poiseDamage = num(*a, "poise_damage", atk.timing.poiseDamage);
        atk.timing.isSweep = (*a)["is_sweep"].value_or(atk.timing.isSweep);
        atk.timing.arc = num(*a, "arc", atk.timing.arc);
        if (const toml::table* pattern = (*a)["pattern"].as_table()) {
            atk.pattern = readPattern(*pattern, atk);
            if (!atk.pattern) {
                eng::log::error("Enemy attack '%s' has an invalid pattern; "
                                "rejecting attack", atk.id.c_str());
                continue;
            }
        }
        out.push_back(std::move(atk));
    }
}

// Overlay one authored table onto an already-inherited def.
void readInto(const toml::table& t, EnemyDef& def)
{
    def.name = t["name"].value_or(def.name);
    def.category = t["category"].value_or(def.category);
    def.tier = t["tier"].value_or(def.tier);
    def.boss = t["boss"].value_or(def.boss);
    if (const toml::table* s = t["body"].as_table())        readBody(*s, def.body);
    if (const toml::table* s = t["visual"].as_table())      readVisual(*s, def.visual);
    if (const toml::table* s = t["stats"].as_table())       readStats(*s, def.stats);
    if (const toml::table* s = t["locomotion"].as_table())  readLocomotion(*s, def.locomotion);
    if (const toml::table* s = t["perception"].as_table())  readPerception(*s, def.perception);
    if (const toml::table* s = t["behaviour"].as_table())   readBehaviour(*s, def.behaviour);
    readAttacks(t, def.attacks);
}

struct StagedRow {
    const toml::table* table = nullptr;
    std::string inherits;
    bool spawnable = false;
};

} // namespace

bool EnemyLibrary::parse(const void* tomlTable)
{
    const toml::table& root = *static_cast<const toml::table*>(tomlTable);

    // Stage every row first: inheritance may point forward, and TOML tables are
    // unordered anyway, so resolving during the walk would depend on hash order.
    std::unordered_map<std::string, StagedRow> staged;
    auto stage = [&](const char* section, bool spawnable) {
        const toml::table* group = root[section].as_table();
        if (!group)
            return;
        for (auto&& [key, node] : *group) {
            const toml::table* t = node.as_table();
            if (!t)
                continue;
            const std::string id(key.str());
            if (staged.count(id)) {
                eng::log::error("EnemyLibrary: duplicate id '%s'", id.c_str());
                continue;
            }
            staged[id] = {t, (*t)["inherits"].value_or(std::string()), spawnable};
        }
    };
    stage("archetype", false);
    stage("enemy", true);

    if (staged.empty()) {
        eng::log::error("EnemyLibrary: document defines no enemies");
        return false;
    }

    // Flatten a chain by walking to its root and overlaying back down. Memoised
    // so a wide archetype tree is still linear.
    std::unordered_map<std::string, EnemyDef> flat;
    std::unordered_set<std::string> inProgress;
    std::function<const EnemyDef*(const std::string&)> flatten =
        [&](const std::string& id) -> const EnemyDef* {
        if (auto it = flat.find(id); it != flat.end())
            return &it->second;
        auto row = staged.find(id);
        if (row == staged.end())
            return nullptr;
        if (!inProgress.insert(id).second) {
            eng::log::error("EnemyLibrary: '%s' inherits itself (cycle)",
                            id.c_str());
            return nullptr;
        }
        EnemyDef def;
        if (!row->second.inherits.empty()) {
            const EnemyDef* base = flatten(row->second.inherits);
            if (!base) {
                eng::log::error(
                    "EnemyLibrary: '%s' inherits '%s', which does not exist; "
                    "row dropped",
                    id.c_str(), row->second.inherits.c_str());
                inProgress.erase(id);
                return nullptr;
            }
            def = *base;
        }
        def.id = id;
        def.inherits = row->second.inherits;
        readInto(*row->second.table, def);
        inProgress.erase(id);
        return &(flat[id] = std::move(def));
    };

    mEnemies.clear();
    for (const auto& [id, row] : staged) {
        const EnemyDef* def = flatten(id);
        if (!def || !row.spawnable)
            continue;
        if (def->attacks.empty()) {
            eng::log::error(
                "EnemyLibrary: enemy '%s' has no attacks; it would be a moving "
                "target dummy. Row dropped -- give it an [[enemy.%s.attack]] or "
                "make it an archetype.",
                id.c_str(), id.c_str());
            continue;
        }
        mEnemies[id] = std::make_shared<EnemyDef>(*def);
    }

    eng::log::info("EnemyLibrary: %d enemies from %d rows", int(mEnemies.size()),
                   int(staged.size()));
    return !mEnemies.empty();
}

bool EnemyLibrary::load(const std::string& tomlPath)
{
    toml::parse_result parsed = toml::parse_file(tomlPath);
    if (!parsed) {
        eng::log::error("EnemyLibrary: %s: %s", tomlPath.c_str(),
                        std::string(parsed.error().description()).c_str());
        mEnemies.clear();
        return false;
    }
    if (!parse(&parsed.table()))
        return false;
    mSourcePath = tomlPath;
    return true;
}

bool EnemyLibrary::loadFromString(const char* tomlSrc)
{
    toml::parse_result parsed = toml::parse(tomlSrc);
    if (!parsed) {
        eng::log::error("EnemyLibrary: %s",
                        std::string(parsed.error().description()).c_str());
        mEnemies.clear();
        return false;
    }
    mSourcePath.clear();
    return parse(&parsed.table());
}

void EnemyLibrary::resolve(const CombatVocabulary& vocabulary)
{
    for (auto& [id, defPtr] : mEnemies) {
        EnemyDef& def = *defPtr;
        for (float& v : def.resistanceById)
            v = 0.0f;
        for (const EnemyResistance& r : def.stats.resistances) {
            const DamageTypeId channel = vocabulary.damageType(r.channel);
            if (channel == kInvalidDamageType) {
                eng::log::error(
                    "EnemyLibrary: enemy '%s' resists '%s', which magic.toml "
                    "does not define; ignored",
                    id.c_str(), r.channel.c_str());
                continue;
            }
            def.resistanceById[channel] = r.value;
        }
        // Weapons an enemy names must exist too, but that check belongs to
        // whoever owns the weapon table; the library does not link one.
    }
}

EnemyLibrary::Ref EnemyLibrary::find(const std::string& id) const
{
    auto it = mEnemies.find(id);
    return it == mEnemies.end() ? Ref{} : it->second;
}

EnemyDef* EnemyLibrary::mutableDef(const std::string& id)
{
    auto it = mEnemies.find(id);
    return it == mEnemies.end() ? nullptr : it->second.get();
}

std::vector<std::string> EnemyLibrary::ids() const
{
    std::vector<std::string> out;
    out.reserve(mEnemies.size());
    for (const auto& [id, def] : mEnemies)
        out.push_back(id);
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace game
