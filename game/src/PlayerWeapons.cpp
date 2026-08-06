#include "PlayerWeapons.h"

#include "ViewmodelSpriteToml.h"

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

// The loadout the game falls back to when weapons.toml is missing or malformed.
//
// It mirrors that file rather than extending it: a fallback that ships a weapon
// the authored table does not have is a loadout nobody can tune. The Vesper
// Spindle used to be here and is archived alongside its authored section in
// assets/source/archive/weapons/vesper_spindle.toml -- restoring it means
// pasting that section back and adding its definition here again.
//
// Order is the slot order: the first entry is what the player starts holding.
std::vector<PlayerWeaponDef> defaults()
{
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
    arbalest.fireSound = "weapon.eidolon.fire";
    arbalest.projectile.primitive = WeaponPrimitive::Cone;
    arbalest.projectile.visualScale = {0.07f, 0.42f, 0.07f};
    arbalest.projectile.material = "Game/Prototype/ProjectileEidolon";
    arbalest.projectile.trailEffect = "eidolon_trail";
    arbalest.projectile.impactEffect = "eidolon_impact";
    arbalest.projectile.impactSound = "weapon.eidolon.impact";
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
    arbalest.viewmodel.handsIdleAnimation = "guard_idle";
    arbalest.viewmodel.handsDrawAnimation = "guard_draw";
    arbalest.viewmodel.handsFireAnimation = "push.R";
    arbalest.viewmodel.handsMuzzleJoint = "f_middle.03.R";
    arbalest.viewmodel.handsMuzzleOffset = {0.0f, 0.03f, 0.0f};
    arbalest.viewmodel.socket = "right_hand";
    arbalest.viewmodel.attachOffset = {0.0f, 0.09f, 0.0f};
    arbalest.viewmodel.attachScale = 0.5f;
    arbalest.viewmodel.muzzleSocket = "right_middle_tip";

    PlayerWeaponDef talon;
    talon.id = "riven_talon";
    talon.displayName = "RIVEN TALON";
    talon.discipline = "SURGE / AGGRESSION";
    talon.payloadId = "riven_spark";
    talon.trigger = WeaponTrigger::Automatic;
    talon.fireInterval = 0.09f;
    talon.arcCost = 3.0f;
    talon.muzzleEffect = "talon_muzzle";
    talon.fireSound = "weapon.talon.fire";
    talon.projectile.primitive = WeaponPrimitive::Sphere;
    talon.projectile.visualScale = glm::vec3(0.09f);
    talon.projectile.material = "Game/Prototype/ProjectileTalon";
    talon.projectile.trailEffect = "talon_trail";
    talon.projectile.impactEffect = "talon_impact";
    talon.projectile.impactSound = "weapon.talon.impact";
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
    // The finger-gun set rather than the knife clips: the talon shoots from the
    // hand, and a stabbing animation reads as a melee weapon that happens to
    // fire. The muzzle is the index fingertip, where that pose points.
    talon.viewmodel.handsIdleAnimation = "finger_gun_idle";
    talon.viewmodel.handsDrawAnimation = "finger_gun_fix";
    talon.viewmodel.handsFireAnimation = "finger_gun_fire";
    talon.viewmodel.handsMuzzleJoint = "f_index.03.R";
    talon.viewmodel.handsMuzzleOffset = {0.0f, 0.025f, 0.0f};
    talon.viewmodel.socket = "right_hand";
    talon.viewmodel.attachOffset = {0.0f, 0.065f, 0.0f};
    talon.viewmodel.attachScale = 0.55f;
    talon.viewmodel.muzzleSocket = "right_index_tip";

    // Talon first: it is what the player starts with in hand.
    return {std::move(talon), std::move(arbalest)};
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
                 def.switchTime, def.viewmodel.glowStrength,
                 def.viewmodel.fireDuration, def.viewmodel.recoilDistance,
                 def.viewmodel.recoilPitchDegrees,
                 def.viewmodel.recoilYawDegrees,
                 def.viewmodel.recoilRecovery, def.viewmodel.movementBob,
                 def.viewmodel.movementBobSpeed, def.viewmodel.idleSway,
                 def.viewmodel.lookSway, def.viewmodel.handsScale}) ||
        !finiteVec(def.muzzleOffset) ||
        !finiteVec(def.viewmodel.handsMuzzleOffset) ||
        !finiteVec(def.viewmodel.handsOffset) ||
        !finiteVec(def.viewmodel.handsRotationDegrees) ||
        def.viewmodel.handsScale <= 0.0f ||
        !finiteVec(def.viewmodel.position) ||
        !finiteVec(def.viewmodel.rotationDegrees) ||
        def.fireInterval <= 0.0f || def.arcCost < 0.0f ||
        def.projectileCount < 1 || def.projectileCount > 8 ||
        def.spreadDegrees < 0.0f || def.switchTime < 0.0f ||
        def.viewmodel.glowStrength < 0.0f ||
        def.viewmodel.fireDuration <= 0.0f ||
        def.viewmodel.recoilDistance < 0.0f ||
        def.viewmodel.recoilRecovery < 0.0f ||
        def.viewmodel.movementBob < 0.0f ||
        def.viewmodel.movementBobSpeed < 0.0f ||
        def.viewmodel.idleSway < 0.0f || def.viewmodel.lookSway < 0.0f ||
        // A weapon must present *something*: sprite layers, a model, or the
        // placeholder primitives. None of those is an empty hand, which is what
        // shipped before this and what the requirement is here to stop
        // recurring. A sprite weapon satisfies it with its own layers -- the
        // hands' shared layers are not enough, or a weapon could be invisible
        // and still load.
        (def.viewmodel.presentation == ViewmodelPresentation::Sprite
             ? def.viewmodel.spriteLayers.empty()
             : (def.viewmodel.parts.empty() && def.viewmodel.model.empty())) ||
        !std::all_of(def.viewmodel.spriteLayers.begin(),
                     def.viewmodel.spriteLayers.end(),
                     validViewmodelSpriteLayer) ||
        (!def.viewmodel.model.empty() && def.viewmodel.modelMaterial.empty()) ||
        !finiteVec(def.viewmodel.spriteMuzzle) ||
        def.viewmodel.socket.empty() ||
        !finiteVec(def.viewmodel.attachOffset) ||
        !finiteVec(def.viewmodel.attachRotationDegrees) ||
        !std::isfinite(def.viewmodel.attachScale) ||
        def.viewmodel.attachScale <= 0.0f ||
        def.viewmodel.handsIdleAnimation.empty() ||
        def.viewmodel.handsDrawAnimation.empty() ||
        def.viewmodel.handsFireAnimation.empty() ||
        def.viewmodel.handsMuzzleJoint.empty())
        return false;

    // Only the selected delivery's block is checked. A weapon that once fired
    // bolts and is now a melee weapon keeps its projectile numbers in the file;
    // holding it to them would make retuning a weapon's kind a rewrite, and
    // rejecting a definition over a block nothing reads is a false failure.
    switch (def.fireMode) {
    case WeaponFireMode::Projectile:
        if (!finiteVec(def.projectile.visualScale) ||
            !finite({def.projectile.speed, def.projectile.lifetime,
                     def.projectile.radius, def.projectile.mass,
                     def.projectile.gravityFactor, def.projectile.aimRange}) ||
            def.projectile.speed <= 0.0f || def.projectile.lifetime <= 0.0f ||
            def.projectile.radius <= 0.0f || def.projectile.mass <= 0.0f ||
            def.projectile.gravityFactor < 0.0f ||
            def.projectile.aimRange <= 0.0f ||
            def.projectile.visualScale.x <= 0.0f ||
            def.projectile.visualScale.y <= 0.0f ||
            def.projectile.visualScale.z <= 0.0f ||
            def.projectile.material.empty())
            return false;
        break;
    case WeaponFireMode::Melee:
        if (!validWeaponMeleeDef(def.melee))
            return false;
        break;
    case WeaponFireMode::Hitscan:
        if (!validWeaponHitscanDef(def.hitscan))
            return false;
        break;
    }

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
        // Absent means projectile: every weapon that predates fire modes is a
        // projectile weapon, and a default that silently changed their delivery
        // would be a content migration disguised as a parser default.
        const std::optional<WeaponFireMode> fireMode = weaponFireModeFromName(
            (*table)["fire_mode"].value_or(std::string{"projectile"}));
        if (!fireMode)
            return false;
        def.fireMode = *fireMode;
        def.fireInterval = number(*table, "fire_interval", def.fireInterval);
        def.arcCost = number(*table, "arc_cost", def.arcCost);
        def.projectileCount =
            int((*table)["projectile_count"].value_or(int64_t{1}));
        def.spreadDegrees = number(*table, "spread_degrees", 0.0f);
        def.switchTime = number(*table, "switch_time", def.switchTime);
        def.muzzleOffset = vector3(*table, "muzzle_offset", def.muzzleOffset);
        def.muzzleEffect =
            (*table)["muzzle_effect"].value_or(std::string{});
        def.fireSound =
            (*table)["fire_sound"].value_or(def.fireSound);

        const toml::table* projectile = (*table)["projectile"].as_table();
        const toml::table* melee = (*table)["melee"].as_table();
        const toml::table* hitscan = (*table)["hitscan"].as_table();
        const toml::table* viewmodel = (*table)["viewmodel"].as_table();
        if (!viewmodel || slot < 0 || slots.count(slot))
            return false;
        // The selected delivery must have brought its numbers. The other two
        // blocks are optional and read when present, so flipping `fire_mode`
        // back to a delivery a weapon used before is a one-line edit.
        if ((def.fireMode == WeaponFireMode::Projectile && !projectile) ||
            (def.fireMode == WeaponFireMode::Melee && !melee) ||
            (def.fireMode == WeaponFireMode::Hitscan && !hitscan))
            return false;

        if (melee) {
            def.melee.reach = number(*melee, "reach", def.melee.reach);
            def.melee.radius = number(*melee, "radius", def.melee.radius);
            def.melee.windup = number(*melee, "windup", def.melee.windup);
            def.melee.active = number(*melee, "active", def.melee.active);
            def.melee.impulse = number(*melee, "impulse", def.melee.impulse);
            def.melee.maxTargets = int((*melee)["max_targets"].value_or(
                int64_t{def.melee.maxTargets}));
            def.melee.impactEffect =
                (*melee)["impact_effect"].value_or(std::string{});
            def.melee.impactSound =
                (*melee)["impact_sound"].value_or(std::string{});
        }
        if (hitscan) {
            def.hitscan.range = number(*hitscan, "range", def.hitscan.range);
            def.hitscan.impulse =
                number(*hitscan, "impulse", def.hitscan.impulse);
            def.hitscan.beamMaterial =
                (*hitscan)["beam_material"].value_or(std::string{});
            def.hitscan.beamWidth =
                number(*hitscan, "beam_width", def.hitscan.beamWidth);
            def.hitscan.beamSeconds =
                number(*hitscan, "beam_seconds", def.hitscan.beamSeconds);
            def.hitscan.impactEffect =
                (*hitscan)["impact_effect"].value_or(std::string{});
            def.hitscan.impactSound =
                (*hitscan)["impact_sound"].value_or(std::string{});
        }
        if (projectile) {
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
            def.projectile.impactSound =
                (*projectile)["impact_sound"].value_or(
                    def.projectile.impactSound);
            def.projectile.speed =
                number(*projectile, "speed", def.projectile.speed);
            def.projectile.lifetime =
                number(*projectile, "lifetime", def.projectile.lifetime);
            def.projectile.radius =
                number(*projectile, "radius", def.projectile.radius);
            def.projectile.mass =
                number(*projectile, "mass", def.projectile.mass);
            def.projectile.gravityFactor = number(*projectile, "gravity_factor",
                                                  def.projectile.gravityFactor);
            def.projectile.aimRange =
                number(*projectile, "aim_range", def.projectile.aimRange);
        }

        // Absent means model, so every weapon authored before sprite
        // viewmodels existed keeps its presentation.
        const std::optional<ViewmodelPresentation> presentation =
            viewmodelPresentationFromName(
                (*viewmodel)["presentation"].value_or(std::string{"model"}));
        if (!presentation)
            return false;
        def.viewmodel.presentation = *presentation;
        def.viewmodel.spriteMuzzle =
            vector3(*viewmodel, "sprite_muzzle", def.viewmodel.spriteMuzzle);
        if (const toml::array* spriteLayers =
                (*viewmodel)["sprite_layer"].as_array()) {
            for (const toml::node& node : *spriteLayers) {
                const toml::table* row = node.as_table();
                if (!row)
                    return false;
                def.viewmodel.spriteLayers.push_back(
                    parseViewmodelSpriteLayer(*row));
            }
        }

        def.viewmodel.position =
            vector3(*viewmodel, "position", def.viewmodel.position);
        def.viewmodel.rotationDegrees =
            vector3(*viewmodel, "rotation", def.viewmodel.rotationDegrees);
        def.viewmodel.glowSchool =
            (*viewmodel)["glow_school"].value_or(std::string{"arcane"});
        def.viewmodel.handsIdleAnimation =
            (*viewmodel)["hands_idle_animation"].value_or(std::string{});
        def.viewmodel.handsDrawAnimation =
            (*viewmodel)["hands_draw_animation"].value_or(std::string{});
        def.viewmodel.handsFireAnimation =
            (*viewmodel)["hands_fire_animation"].value_or(std::string{});
        def.viewmodel.handsMuzzleJoint =
            (*viewmodel)["hands_muzzle_joint"].value_or(std::string{});
        def.viewmodel.handsMuzzleOffset = vector3(
            *viewmodel, "hands_muzzle_offset", def.viewmodel.handsMuzzleOffset);
        def.viewmodel.handsOffset =
            vector3(*viewmodel, "hands_offset", def.viewmodel.handsOffset);
        def.viewmodel.handsRotationDegrees = vector3(
            *viewmodel, "hands_rotation", def.viewmodel.handsRotationDegrees);
        def.viewmodel.handsScale =
            number(*viewmodel, "hands_scale", def.viewmodel.handsScale);
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

        // Attachment: which socket on the hand rig holds this weapon, and what
        // hangs there. See game/src/WeaponViewmodel.h for why `model` empty
        // falls back to the primitives below rather than rendering nothing.
        def.viewmodel.socket =
            (*viewmodel)["socket"].value_or(def.viewmodel.socket);
        def.viewmodel.model = (*viewmodel)["model"].value_or(std::string{});
        def.viewmodel.modelMaterial =
            (*viewmodel)["material"].value_or(def.viewmodel.modelMaterial);
        def.viewmodel.attachOffset =
            vector3(*viewmodel, "attach_offset", def.viewmodel.attachOffset);
        def.viewmodel.attachRotationDegrees =
            vector3(*viewmodel, "attach_rotation",
                    def.viewmodel.attachRotationDegrees);
        def.viewmodel.attachScale =
            number(*viewmodel, "attach_scale", def.viewmodel.attachScale);
        def.viewmodel.muzzleSocket =
            (*viewmodel)["muzzle_socket"].value_or(std::string{});

        // A weapon with a model needs no primitives. One without still does --
        // valid() enforces that, because the alternative is an empty hand with
        // nothing in the console saying why.
        const toml::array* parts = (*viewmodel)["part"].as_array();
        if (!parts && def.viewmodel.model.empty())
            return false;
        static const toml::array kNoParts;
        for (const toml::node& partNode : parts ? *parts : kNoParts) {
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

const char* viewmodelPresentationName(ViewmodelPresentation presentation)
{
    switch (presentation) {
    case ViewmodelPresentation::Model: return "model";
    case ViewmodelPresentation::Sprite: return "sprite";
    }
    return "model";
}

std::optional<ViewmodelPresentation>
viewmodelPresentationFromName(const std::string& name)
{
    if (name == "model") return ViewmodelPresentation::Model;
    if (name == "sprite") return ViewmodelPresentation::Sprite;
    return std::nullopt;
}

const char* weaponFireModeName(WeaponFireMode mode)
{
    switch (mode) {
    case WeaponFireMode::Projectile: return "projectile";
    case WeaponFireMode::Melee: return "melee";
    case WeaponFireMode::Hitscan: return "hitscan";
    }
    return "projectile";
}

std::optional<WeaponFireMode> weaponFireModeFromName(const std::string& name)
{
    if (name == "projectile") return WeaponFireMode::Projectile;
    if (name == "melee") return WeaponFireMode::Melee;
    if (name == "hitscan") return WeaponFireMode::Hitscan;
    return std::nullopt;
}

bool validWeaponMeleeDef(const WeaponMeleeDef& m)
{
    const auto finite = [](std::initializer_list<float> values) {
        return std::all_of(values.begin(), values.end(),
                           [](float v) { return std::isfinite(v); });
    };
    return finite({m.reach, m.radius, m.windup, m.active, m.impulse}) &&
           m.reach > 0.0f && m.radius > 0.0f && m.windup >= 0.0f &&
           m.active > 0.0f && m.impulse >= 0.0f && m.maxTargets >= 1 &&
           m.maxTargets <= 32;
}

bool validWeaponHitscanDef(const WeaponHitscanDef& h)
{
    const auto finite = [](std::initializer_list<float> values) {
        return std::all_of(values.begin(), values.end(),
                           [](float v) { return std::isfinite(v); });
    };
    return finite({h.range, h.impulse, h.beamWidth, h.beamSeconds}) &&
           h.range > 0.0f && h.impulse >= 0.0f && h.beamWidth > 0.0f &&
           h.beamSeconds >= 0.0f;
}

const std::string& weaponImpactSound(const PlayerWeaponDef& weapon)
{
    switch (weapon.fireMode) {
    case WeaponFireMode::Projectile: return weapon.projectile.impactSound;
    case WeaponFireMode::Melee: return weapon.melee.impactSound;
    case WeaponFireMode::Hitscan: return weapon.hitscan.impactSound;
    }
    return weapon.projectile.impactSound;
}

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
