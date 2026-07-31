#include "PlayerWeapons.h"

#include <eng/Log.h>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <map>
#include <utility>

namespace game {
namespace {

float number(const toml::table& table, const char* key, float fallback)
{
    return float(table[key].value_or(double(fallback)));
}

glm::vec3 vector3(const toml::table& table, const char* key,
                  glm::vec3 fallback)
{
    const toml::array* values = table[key].as_array();
    if (!values || values->size() != 3)
        return fallback;
    return {float((*values)[0].value_or(double(fallback.x))),
            float((*values)[1].value_or(double(fallback.y))),
            float((*values)[2].value_or(double(fallback.z)))};
}

std::optional<WeaponPrimitive> primitive(const std::string& value)
{
    if (value == "box") return WeaponPrimitive::Box;
    if (value == "beveled_box") return WeaponPrimitive::BeveledBox;
    if (value == "sphere") return WeaponPrimitive::Sphere;
    if (value == "capsule") return WeaponPrimitive::Capsule;
    if (value == "cylinder") return WeaponPrimitive::Cylinder;
    if (value == "cone") return WeaponPrimitive::Cone;
    if (value == "disc") return WeaponPrimitive::Disc;
    return std::nullopt;
}

WeaponViewmodelPart part(WeaponPrimitive kind, glm::vec3 position,
                         glm::vec3 rotation, glm::vec3 scale,
                         const char* material, bool enchanted = false)
{
    WeaponViewmodelPart result;
    result.primitive = kind;
    result.position = position;
    result.rotationDegrees = rotation;
    result.scale = scale;
    result.material = material;
    result.enchanted = enchanted;
    return result;
}

std::vector<PlayerWeaponDef> defaults()
{
    PlayerWeaponDef spindle;
    spindle.id = "vesper_spindle";
    spindle.displayName = "VESPER SPINDLE";
    spindle.discipline = "NEEDLE / PRECISION";
    spindle.payloadId = "vesper_needle";
    spindle.trigger = WeaponTrigger::Automatic;
    spindle.fireInterval = 0.14f;
    spindle.arcCost = 2.0f;
    spindle.muzzleEffect = "vesper_muzzle";
    spindle.projectile.primitive = WeaponPrimitive::Cone;
    spindle.projectile.visualScale = {0.035f, 0.22f, 0.035f};
    spindle.projectile.material = "Game/ProjectileVesper";
    spindle.projectile.trailEffect = "vesper_trail";
    spindle.projectile.impactEffect = "vesper_impact";
    spindle.projectile.speed = 72.0f;
    spindle.projectile.lifetime = 1.1f;
    spindle.projectile.radius = 0.025f;
    spindle.viewmodel.parts = {
        part(WeaponPrimitive::Cylinder, {0, 0, 0}, {0, 0, 0},
             {0.035f, 0.30f, 0.035f}, "Game/ViewModelVesper"),
        part(WeaponPrimitive::Sphere, {0, 0.18f, 0}, {0, 0, 0},
             glm::vec3(0.075f), "Game/ViewModelVesperGlow", true),
    };

    PlayerWeaponDef arbalest;
    arbalest.id = "eidolon_arbalest";
    arbalest.displayName = "EIDOLON ARBALEST";
    arbalest.discipline = "TRIUNE / HEAVY";
    arbalest.payloadId = "eidolon_bolt";
    arbalest.fireInterval = 0.72f;
    arbalest.arcCost = 12.0f;
    arbalest.projectileCount = 3;
    arbalest.spreadDegrees = 8.0f;
    arbalest.muzzleEffect = "eidolon_muzzle";
    arbalest.projectile.primitive = WeaponPrimitive::Cone;
    arbalest.projectile.visualScale = {0.07f, 0.42f, 0.07f};
    arbalest.projectile.material = "Game/ProjectileEidolon";
    arbalest.projectile.trailEffect = "eidolon_trail";
    arbalest.projectile.impactEffect = "eidolon_impact";
    arbalest.projectile.speed = 48.0f;
    arbalest.projectile.lifetime = 1.5f;
    arbalest.projectile.radius = 0.055f;
    arbalest.viewmodel.position = {0.22f, -0.25f, -0.58f};
    arbalest.viewmodel.glowSchool = "sacred";
    arbalest.viewmodel.recoilDistance = 0.14f;
    arbalest.viewmodel.recoilPitchDegrees = 13.0f;
    arbalest.viewmodel.fireDuration = 0.38f;
    arbalest.viewmodel.parts = {
        part(WeaponPrimitive::BeveledBox, {0, 0, 0}, {90, 0, 0},
             {0.10f, 0.34f, 0.08f}, "Game/ViewModelEidolon"),
        part(WeaponPrimitive::Cylinder, {0, 0.02f, -0.04f}, {0, 0, 90},
             {0.025f, 0.30f, 0.025f}, "Game/ViewModelEidolonGlow", true),
    };

    PlayerWeaponDef talon;
    talon.id = "riven_talon";
    talon.displayName = "RIVEN TALON";
    talon.discipline = "SURGE / AGGRESSION";
    talon.payloadId = "riven_spark";
    talon.trigger = WeaponTrigger::Automatic;
    talon.fireInterval = 0.09f;
    talon.arcCost = 3.0f;
    talon.muzzleEffect = "talon_muzzle";
    talon.projectile.primitive = WeaponPrimitive::Sphere;
    talon.projectile.visualScale = glm::vec3(0.09f);
    talon.projectile.material = "Game/ProjectileTalon";
    talon.projectile.trailEffect = "talon_trail";
    talon.projectile.impactEffect = "talon_impact";
    talon.projectile.speed = 58.0f;
    talon.projectile.lifetime = 1.25f;
    talon.projectile.radius = 0.06f;
    talon.viewmodel.position = {0.27f, -0.25f, -0.52f};
    talon.viewmodel.glowSchool = "fire";
    talon.viewmodel.recoilDistance = 0.08f;
    talon.viewmodel.recoilPitchDegrees = 9.0f;
    talon.viewmodel.recoilYawDegrees = 4.0f;
    talon.viewmodel.recoilRecovery = 26.0f;
    talon.viewmodel.parts = {
        part(WeaponPrimitive::Sphere, {0, 0, 0}, {0, 0, 0},
             {0.10f, 0.08f, 0.14f}, "Game/ViewModelTalon"),
        part(WeaponPrimitive::Cone, {-0.07f, 0.06f, -0.05f}, {-70, 0, -12},
             {0.025f, 0.18f, 0.025f}, "Game/ViewModelTalonGlow", true),
        part(WeaponPrimitive::Cone, {0.0f, 0.08f, -0.06f}, {-76, 0, 0},
             {0.025f, 0.21f, 0.025f}, "Game/ViewModelTalonGlow", true),
        part(WeaponPrimitive::Cone, {0.07f, 0.06f, -0.05f}, {-70, 0, 12},
             {0.025f, 0.18f, 0.025f}, "Game/ViewModelTalonGlow", true),
    };

    return {std::move(spindle), std::move(arbalest), std::move(talon)};
}

bool valid(const PlayerWeaponDef& def)
{
    const auto finiteVec = [](glm::vec3 value) {
        return std::isfinite(value.x) && std::isfinite(value.y) &&
               std::isfinite(value.z);
    };
    const auto finite = [](std::initializer_list<float> values) {
        return std::all_of(values.begin(), values.end(),
                           [](float value) { return std::isfinite(value); });
    };
    if (def.id.empty() || def.displayName.empty() || def.discipline.empty() ||
        def.payloadId.empty() ||
        !finite({def.fireInterval, def.arcCost, def.spreadDegrees,
                 def.switchTime, def.projectile.speed,
                 def.projectile.lifetime, def.projectile.radius,
                 def.projectile.mass, def.projectile.gravityFactor,
                 def.projectile.aimRange, def.viewmodel.glowStrength,
                 def.viewmodel.fireDuration, def.viewmodel.recoilDistance,
                 def.viewmodel.recoilPitchDegrees,
                 def.viewmodel.recoilYawDegrees,
                 def.viewmodel.recoilRecovery, def.viewmodel.movementBob,
                 def.viewmodel.movementBobSpeed, def.viewmodel.idleSway,
                 def.viewmodel.lookSway}) ||
        !finiteVec(def.muzzleOffset) ||
        !finiteVec(def.projectile.visualScale) ||
        !finiteVec(def.viewmodel.position) ||
        !finiteVec(def.viewmodel.rotationDegrees) ||
        def.fireInterval <= 0.0f || def.arcCost < 0.0f ||
        def.projectileCount < 1 || def.projectileCount > 8 ||
        def.spreadDegrees < 0.0f || def.switchTime < 0.0f ||
        def.projectile.speed <= 0.0f || def.projectile.lifetime <= 0.0f ||
        def.projectile.radius <= 0.0f || def.projectile.mass <= 0.0f ||
        def.projectile.gravityFactor < 0.0f || def.projectile.aimRange <= 0.0f ||
        def.projectile.visualScale.x <= 0.0f ||
        def.projectile.visualScale.y <= 0.0f ||
        def.projectile.visualScale.z <= 0.0f ||
        def.viewmodel.glowStrength < 0.0f ||
        def.viewmodel.fireDuration <= 0.0f ||
        def.viewmodel.recoilDistance < 0.0f ||
        def.viewmodel.recoilRecovery < 0.0f ||
        def.viewmodel.movementBob < 0.0f ||
        def.viewmodel.movementBobSpeed < 0.0f ||
        def.viewmodel.idleSway < 0.0f || def.viewmodel.lookSway < 0.0f ||
        def.projectile.material.empty() || def.viewmodel.parts.empty())
        return false;
    return std::all_of(def.viewmodel.parts.begin(), def.viewmodel.parts.end(),
                       [](const WeaponViewmodelPart& value) {
                           const auto finite = [](glm::vec3 v) {
                               return std::isfinite(v.x) && std::isfinite(v.y) &&
                                      std::isfinite(v.z);
                           };
                           return !value.material.empty() && finite(value.position) &&
                                  finite(value.rotationDegrees) && finite(value.scale) &&
                                  value.scale.x > 0.0f &&
                                  value.scale.y > 0.0f && value.scale.z > 0.0f;
                       });
}

bool parseDefinitions(const toml::table& root,
                      std::vector<PlayerWeaponDef>& definitions)
{
    const toml::table* authored = root["player_weapon"].as_table();
    if (!authored)
        return false;

    std::map<int, PlayerWeaponDef> slots;
    for (auto&& [key, node] : *authored) {
        const toml::table* table = node.as_table();
        if (!table)
            return false;

        PlayerWeaponDef def;
        def.id = std::string(key.str());
        const int slot = int((*table)["slot"].value_or(int64_t{-1}));
        def.displayName = (*table)["name"].value_or(std::string{});
        def.discipline = (*table)["discipline"].value_or(std::string{});
        def.payloadId = (*table)["payload"].value_or(std::string{});
        const std::string trigger =
            (*table)["trigger"].value_or(std::string{"press"});
        if (trigger == "automatic") def.trigger = WeaponTrigger::Automatic;
        else if (trigger == "press") def.trigger = WeaponTrigger::Press;
        else return false;
        def.fireInterval = number(*table, "fire_interval", def.fireInterval);
        def.arcCost = number(*table, "arc_cost", def.arcCost);
        def.projectileCount =
            int((*table)["projectile_count"].value_or(int64_t{1}));
        def.spreadDegrees = number(*table, "spread_degrees", 0.0f);
        def.switchTime = number(*table, "switch_time", def.switchTime);
        def.muzzleOffset = vector3(*table, "muzzle_offset", def.muzzleOffset);
        def.muzzleEffect =
            (*table)["muzzle_effect"].value_or(std::string{});

        const toml::table* projectile = (*table)["projectile"].as_table();
        const toml::table* viewmodel = (*table)["viewmodel"].as_table();
        if (!projectile || !viewmodel || slot < 0 || slots.count(slot))
            return false;

        const auto projectilePrimitive = primitive(
            (*projectile)["shape"].value_or(std::string{"sphere"}));
        if (!projectilePrimitive)
            return false;
        def.projectile.primitive = *projectilePrimitive;
        def.projectile.visualScale =
            vector3(*projectile, "visual_scale", def.projectile.visualScale);
        def.projectile.material =
            (*projectile)["material"].value_or(std::string{});
        def.projectile.trailEffect =
            (*projectile)["trail_effect"].value_or(std::string{});
        def.projectile.impactEffect =
            (*projectile)["impact_effect"].value_or(std::string{});
        def.projectile.speed = number(*projectile, "speed", def.projectile.speed);
        def.projectile.lifetime =
            number(*projectile, "lifetime", def.projectile.lifetime);
        def.projectile.radius =
            number(*projectile, "radius", def.projectile.radius);
        def.projectile.mass = number(*projectile, "mass", def.projectile.mass);
        def.projectile.gravityFactor =
            number(*projectile, "gravity_factor", def.projectile.gravityFactor);
        def.projectile.aimRange =
            number(*projectile, "aim_range", def.projectile.aimRange);

        def.viewmodel.position =
            vector3(*viewmodel, "position", def.viewmodel.position);
        def.viewmodel.rotationDegrees =
            vector3(*viewmodel, "rotation", def.viewmodel.rotationDegrees);
        def.viewmodel.glowSchool =
            (*viewmodel)["glow_school"].value_or(std::string{"arcane"});
        def.viewmodel.glowStrength =
            number(*viewmodel, "glow_strength", def.viewmodel.glowStrength);
        def.viewmodel.fireDuration =
            number(*viewmodel, "fire_duration", def.viewmodel.fireDuration);
        def.viewmodel.recoilDistance =
            number(*viewmodel, "recoil_distance", def.viewmodel.recoilDistance);
        def.viewmodel.recoilPitchDegrees = number(
            *viewmodel, "recoil_pitch_degrees", def.viewmodel.recoilPitchDegrees);
        def.viewmodel.recoilYawDegrees = number(
            *viewmodel, "recoil_yaw_degrees", def.viewmodel.recoilYawDegrees);
        def.viewmodel.recoilRecovery =
            number(*viewmodel, "recoil_recovery", def.viewmodel.recoilRecovery);
        def.viewmodel.movementBob =
            number(*viewmodel, "movement_bob", def.viewmodel.movementBob);
        def.viewmodel.movementBobSpeed = number(
            *viewmodel, "movement_bob_speed", def.viewmodel.movementBobSpeed);
        def.viewmodel.idleSway =
            number(*viewmodel, "idle_sway", def.viewmodel.idleSway);
        def.viewmodel.lookSway =
            number(*viewmodel, "look_sway", def.viewmodel.lookSway);

        const toml::array* parts = (*viewmodel)["part"].as_array();
        if (!parts)
            return false;
        for (const toml::node& partNode : *parts) {
            const toml::table* partTable = partNode.as_table();
            if (!partTable)
                return false;
            const auto partPrimitive = primitive(
                (*partTable)["shape"].value_or(std::string{}));
            if (!partPrimitive)
                return false;
            WeaponViewmodelPart parsed;
            parsed.primitive = *partPrimitive;
            parsed.position = vector3(*partTable, "position", {});
            parsed.rotationDegrees = vector3(*partTable, "rotation", {});
            parsed.scale = vector3(*partTable, "scale", glm::vec3(1.0f));
            parsed.material =
                (*partTable)["material"].value_or(std::string{});
            parsed.enchanted =
                (*partTable)["enchanted"].value_or(false);
            def.viewmodel.parts.push_back(std::move(parsed));
        }

        if (!valid(def))
            return false;
        slots.emplace(slot, std::move(def));
    }

    if (slots.empty())
        return false;
    std::vector<PlayerWeaponDef> parsed;
    parsed.reserve(slots.size());
    int expectedSlot = 0;
    for (auto& [slot, def] : slots) {
        if (slot != expectedSlot++)
            return false;
        parsed.push_back(std::move(def));
    }
    definitions = std::move(parsed);
    return true;
}

} // namespace

bool validPlayerWeaponDefinition(const PlayerWeaponDef& definition)
{
    return valid(definition);
}

PlayerWeaponLibrary::PlayerWeaponLibrary() : mDefs(defaults()) {}

bool PlayerWeaponLibrary::load(const std::string& tomlPath)
{
    toml::parse_result parsed = toml::parse_file(tomlPath);
    if (!parsed || !parseDefinitions(parsed.table(), mDefs)) {
        eng::log::error("PlayerWeaponLibrary: invalid player weapons in '%s'",
                        tomlPath.c_str());
        return false;
    }
    return true;
}

bool PlayerWeaponLibrary::loadFromString(const char* tomlSource)
{
    toml::parse_result parsed = toml::parse(tomlSource);
    return parsed && parseDefinitions(parsed.table(), mDefs);
}

const PlayerWeaponDef* PlayerWeaponLibrary::find(const std::string& id) const
{
    const auto found = std::find_if(
        mDefs.begin(), mDefs.end(),
        [&](const PlayerWeaponDef& def) { return def.id == id; });
    return found == mDefs.end() ? nullptr : &*found;
}

void WeaponController::bind(const std::vector<PlayerWeaponDef>* definitions)
{
    mDefinitions = definitions;
    mSelected = 0;
    resetRuntime();
}

void WeaponController::sample(const WeaponCommand& command)
{
    if (!command.enabled) {
        mFireHeld = false;
        mFirePressed = false;
        mSwapPressed = false;
        mSelectSlot = -1;
        return;
    }
    mFireHeld = command.fireHeld;
    mFirePressed = mFirePressed || command.firePressed;
    mSwapPressed = mSwapPressed || command.swapPressed;
    if (command.selectSlot >= 0)
        mSelectSlot = command.selectSlot;
}

std::optional<std::size_t> WeaponController::fixedUpdate(float dt, Mana& arc,
                                                         bool canFire)
{
    if (!mDefinitions || mDefinitions->empty() || dt <= 0.0f)
        return std::nullopt;

    for (float& cooldown : mCooldowns)
        cooldown = std::max(0.0f, cooldown - dt);
    arc.current = std::min(arc.max, arc.current + arc.regenRate * dt);

    std::size_t next = mSelected;
    if (mSelectSlot >= 0 && mSelectSlot < int(mDefinitions->size()))
        next = std::size_t(mSelectSlot);
    else if (mSwapPressed)
        next = (mSelected + 1) % mDefinitions->size();
    mSelectSlot = -1;
    mSwapPressed = false;
    if (next != mSelected) {
        mSelected = next;
        mSwitchRemaining = (*mDefinitions)[mSelected].switchTime;
        mSelectionChanged = true;
        mFirePressed = false;
        return std::nullopt;
    }

    if (mSwitchRemaining > 0.0f) {
        mSwitchRemaining = std::max(0.0f, mSwitchRemaining - dt);
        if (mSwitchRemaining > 0.00001f)
            return std::nullopt;
    }

    if (!canFire) {
        mFirePressed = false;
        return std::nullopt;
    }

    const PlayerWeaponDef& def = (*mDefinitions)[mSelected];
    const bool wantsFire = def.trigger == WeaponTrigger::Automatic
                               ? mFireHeld
                               : mFirePressed;
    if (!wantsFire || mCooldowns[mSelected] > 0.0f)
        return std::nullopt;

    if (arc.current + 0.0001f < def.arcCost) {
        if (def.trigger == WeaponTrigger::Press)
            mFirePressed = false;
        return std::nullopt;
    }

    arc.current = std::max(0.0f, arc.current - def.arcCost);
    mCooldowns[mSelected] = def.fireInterval;
    mFirePressed = false;
    return mSelected;
}

const PlayerWeaponDef* WeaponController::selected() const
{
    if (!mDefinitions || mDefinitions->empty() || mSelected >= mDefinitions->size())
        return nullptr;
    return &(*mDefinitions)[mSelected];
}

bool WeaponController::consumeSelectionChanged()
{
    const bool changed = mSelectionChanged;
    mSelectionChanged = false;
    return changed;
}

void WeaponController::resetRuntime()
{
    mCooldowns.assign(mDefinitions ? mDefinitions->size() : 0, 0.0f);
    mSwitchRemaining = 0.0f;
    mFireHeld = false;
    mFirePressed = false;
    mSwapPressed = false;
    mSelectSlot = -1;
    mSelectionChanged = true;
}

std::vector<glm::vec3> projectileDirections(glm::vec3 direction, int count,
                                            float spreadDegrees)
{
    count = std::clamp(count, 1, 8);
    if (glm::dot(direction, direction) < 0.000001f)
        direction = {0.0f, 0.0f, -1.0f};
    direction = glm::normalize(direction);
    if (count == 1 || spreadDegrees <= 0.0f)
        return std::vector<glm::vec3>(std::size_t(count), direction);

    const glm::vec3 reference = std::abs(glm::dot(direction, glm::vec3(0, 1, 0)))
                                    < 0.98f
                                ? glm::vec3(0, 1, 0)
                                : glm::vec3(1, 0, 0);
    const glm::vec3 right = glm::normalize(glm::cross(direction, reference));
    const glm::vec3 localUp = glm::normalize(glm::cross(right, direction));
    std::vector<glm::vec3> result;
    result.reserve(std::size_t(count));
    for (int i = 0; i < count; ++i) {
        const float alpha = count == 1 ? 0.5f : float(i) / float(count - 1);
        const float angle = glm::radians((alpha - 0.5f) * spreadDegrees);
        result.push_back(glm::normalize(glm::angleAxis(angle, localUp) * direction));
    }
    return result;
}

} // namespace game
