#include "PlayerWeapons.h"
#include "TestAssets.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "PlayerWeaponTests: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

bool near(float a, float b, float epsilon = 0.001f)
{
    return std::abs(a - b) <= epsilon;
}

} // namespace

int main()
{
    game::test::mountGameAssets();
    using namespace game;

    PlayerWeaponLibrary library;
    require(library.load(game::test::asset("config/weapons.toml")),
            "shipped player weapons did not load");
    require(library.defs().size() == 2,
            "shipped loadout no longer matches the authored slots");
    // The Vesper Spindle was archived out of the loadout; its section lives in
    // assets/source/archive/weapons, which is not a runtime mount.
    require(library.find("vesper_spindle") == nullptr,
            "an archived weapon is still in the shipped loadout");

    const PlayerWeaponDef* arbalest = library.find("eidolon_arbalest");
    const PlayerWeaponDef* talon = library.find("riven_talon");
    require(arbalest && talon, "required weapon id is missing");
    // Slot order is what the player starts holding, and the library reports
    // definitions in it.
    require(library.defs().front().id == "riven_talon",
            "the talon is no longer the weapon in hand at slot 0");
    require(arbalest->projectileCount == 3 &&
                near(arbalest->spreadDegrees, 8.0f),
            "heavy weapon spread did not parse");
    require(talon->trigger == WeaponTrigger::Automatic &&
                near(talon->projectile.speed, 58.0f) &&
                talon->viewmodel.parts.size() == 4 &&
                talon->projectile.material == "Game/Prototype/ProjectileTalon",
            "talon cadence/projectile did not parse");
    // The finger-gun hand set, shared with the archived spindle: the talon
    // fires from the fingertip rather than stabbing with a knife.
    require(talon->viewmodel.handsIdleAnimation == "finger_gun_idle" &&
                talon->viewmodel.handsDrawAnimation == "finger_gun_fix" &&
                talon->viewmodel.handsFireAnimation == "finger_gun_fire" &&
                talon->viewmodel.handsMuzzleJoint == "f_index.03.R" &&
                near(talon->viewmodel.handsMuzzleOffset.y, 0.025f),
            "talon does not use the finger-gun hand animations");
    PlayerWeaponDef invalidDefinition = *talon;
    invalidDefinition.fireInterval =
        std::numeric_limits<float>::quiet_NaN();
    require(!validPlayerWeaponDefinition(invalidDefinition),
            "non-finite weapon timing was accepted");

    const auto directions = projectileDirections({0, 0, -1}, 3, 8.0f);
    require(directions.size() == 3, "fan emitted wrong shot count");
    require(directions[0].x * directions[2].x < 0.0f &&
                near(directions[1].x, 0.0f),
            "fan was not symmetric around aim");

    WeaponController controller;
    controller.bind(&library.defs());
    Mana arc;
    arc.current = arc.max = 100.0f;
    arc.regenRate = 0.0f;
    controller.sample({.enabled = true, .fireHeld = true,
                       .firePressed = true});
    const auto first = controller.fixedUpdate(1.0f / 60.0f, arc);
    require(first && *first == 0, "automatic weapon did not fire immediately");
    // Against slot 0's own cost rather than a literal: which weapon starts in
    // hand is a loadout decision, and this is testing that firing spends what
    // the definition says, not what that weapon happens to charge today.
    require(near(arc.current, 100.0f - library.defs().front().arcCost),
            "shot did not spend authored ARC");
    require(!controller.fixedUpdate(1.0f / 60.0f, arc),
            "cooldown allowed a duplicate fixed-step shot");

    controller.sample({.enabled = true, .selectSlot = 1});
    require(!controller.fixedUpdate(1.0f / 60.0f, arc),
            "weapon switch emitted a shot");
    require(controller.selectedIndex() == 1 &&
                controller.consumeSelectionChanged(),
            "direct slot selection did not update runtime state");
    for (int i = 0; i < 12; ++i)
        controller.fixedUpdate(1.0f / 60.0f, arc);
    controller.sample({.enabled = true, .firePressed = true});
    const auto heavy = controller.fixedUpdate(1.0f / 60.0f, arc);
    require(heavy && *heavy == 1, "press weapon did not fire after switch lock");
    require(near(arc.current, 100.0f - library.defs().front().arcCost -
                                  library.defs()[1].arcCost),
            "heavy shot spent wrong ARC amount");
    require(!controller.fixedUpdate(1.0f, arc),
            "press weapon repeated without another edge");

    const std::size_t previousCount = library.defs().size();
    require(!library.loadFromString("[player_weapon.bad]\nslot = 0\n"),
            "malformed definition was accepted");
    require(library.defs().size() == previousCount,
            "failed reload destroyed last valid loadout");

    // The shipped loadout carries one weapon of each presentation on purpose,
    // so switching slots in game shows both riding the same rig. If this ever
    // fails, the sprite path has been silently reverted to a model.
    require(talon->viewmodel.presentation == ViewmodelPresentation::Sprite,
            "the talon is no longer a sprite viewmodel");
    require(talon->viewmodel.spriteLayers.size() == 2,
            "the talon's sprite layers did not parse");
    require(arbalest->viewmodel.presentation == ViewmodelPresentation::Model,
            "the arbalest is no longer a model viewmodel");

    // --- fire modes ---------------------------------------------------------
    // The shipped loadout is projectile, and an omitted fire_mode must stay
    // projectile: the key was added after these weapons existed, and a default
    // that changed their delivery would be a content migration in disguise.
    require(arbalest->fireMode == WeaponFireMode::Projectile &&
                talon->fireMode == WeaponFireMode::Projectile,
            "shipped weapons are no longer projectile weapons");

    // The viewmodel/hands/motion half of a definition is identical whichever
    // delivery it selects, so these fixtures differ only in the delivery block.
    // That is the property under test: a melee weapon is a weapon.
    const char* kCommonTail = R"(
[player_weapon.probe.viewmodel]
socket = "right_hand"
hands_idle_animation = "relax"
hands_draw_animation = "relax"
hands_fire_animation = "grab.R"
hands_muzzle_joint = "f_index.03.R"
[[player_weapon.probe.viewmodel.part]]
shape = "box"
scale = [0.1, 0.1, 0.1]
material = "Game/ViewModelVesper"
)";

    const std::string meleeSource =
        std::string(R"(
[player_weapon.probe]
slot = 0
name = "PROBE"
discipline = "TEST"
payload = "riven_spark"
fire_mode = "melee"
[player_weapon.probe.melee]
reach = 2.4
radius = 0.6
windup = 0.05
active = 0.12
impulse = 7.0
max_targets = 3
impact_sound = "weapon.talon.impact"
)") + kCommonTail;

    PlayerWeaponLibrary melee;
    require(melee.loadFromString(meleeSource.c_str()),
            "a melee weapon with no projectile block was rejected");
    const PlayerWeaponDef* swing = melee.find("probe");
    require(swing && swing->fireMode == WeaponFireMode::Melee,
            "melee fire mode did not parse");
    require(near(swing->melee.reach, 2.4f) && near(swing->melee.active, 0.12f) &&
                swing->melee.maxTargets == 3,
            "melee delivery numbers did not parse");
    // The impact cue must come from the delivery that fired. Reading
    // .projectile here is what silently played nothing for melee weapons.
    require(weaponImpactSound(*swing) == "weapon.talon.impact",
            "melee weapon reported the wrong impact sound");

    const std::string hitscanSource =
        std::string(R"(
[player_weapon.probe]
slot = 0
name = "PROBE"
discipline = "TEST"
payload = "riven_spark"
fire_mode = "hitscan"
[player_weapon.probe.hitscan]
range = 55.0
impulse = 3.0
beam_material = "Game/Spells/BeamCore"
beam_width = 0.04
beam_seconds = 0.08
)") + kCommonTail;

    PlayerWeaponLibrary hitscan;
    require(hitscan.loadFromString(hitscanSource.c_str()),
            "a hitscan weapon with no projectile block was rejected");
    const PlayerWeaponDef* beam = hitscan.find("probe");
    require(beam && beam->fireMode == WeaponFireMode::Hitscan &&
                near(beam->hitscan.range, 55.0f) &&
                beam->hitscan.beamMaterial == "Game/Spells/BeamCore",
            "hitscan delivery did not parse");

    // A weapon must bring the block its own delivery needs...
    PlayerWeaponLibrary missing;
    require(!missing.loadFromString(
                (std::string(R"(
[player_weapon.probe]
slot = 0
name = "PROBE"
discipline = "TEST"
payload = "riven_spark"
fire_mode = "melee"
)") + kCommonTail)
                    .c_str()),
            "a melee weapon with no melee block was accepted");
    // ...and an unknown delivery is a typo, not a silent fallback to shooting.
    require(!missing.loadFromString(
                (std::string(R"(
[player_weapon.probe]
slot = 0
name = "PROBE"
discipline = "TEST"
payload = "riven_spark"
fire_mode = "conjuration"
)") + kCommonTail)
                    .c_str()),
            "an unknown fire_mode was accepted");

    // Only the selected delivery is validated: a melee weapon carrying a stale
    // projectile block it does not read must still load, or retuning a weapon's
    // kind becomes a rewrite of the parts that had nothing to do with it.
    PlayerWeaponDef stale = *swing;
    stale.projectile.speed = -1.0f;
    require(validPlayerWeaponDefinition(stale),
            "an unread projectile block invalidated a melee weapon");
    stale.melee.active = 0.0f;
    require(!validPlayerWeaponDefinition(stale),
            "a melee weapon with no active window was accepted");

    // --- magazines and reloading -------------------------------------------
    //
    // The shooter's pacing mechanism. Every assertion here is a rule a player
    // would notice being broken.
    {
        const std::string gunSource =
            std::string(R"(
[player_weapon.probe]
slot = 0
name = "PROBE"
discipline = "TEST"
payload = "riven_spark"
fire_mode = "hitscan"
trigger = "automatic"
fire_interval = 0.1
arc_cost = 0.0
[player_weapon.probe.hitscan]
range = 60.0
[player_weapon.probe.ammo]
magazine = 4
reserve = 6
reload_seconds = 1.0
reload_empty_seconds = 2.0
auto_reload = false
)") + kCommonTail;

        PlayerWeaponLibrary guns;
        require(guns.loadFromString(gunSource.c_str()),
                "a weapon with an ammo block failed to load");
        const PlayerWeaponDef* gun = guns.find("probe");
        require(gun && gun->usesMagazine(), "magazine was not parsed");
        require(gun->ammo.magazine == 4 && gun->ammo.reserve == 6,
                "magazine or reserve was not parsed");

        WeaponController controller;
        controller.bind(&guns.defs());
        Mana arc;
        arc.current = arc.max = 100.0f;
        arc.regenRate = 0.0f;

        // A fresh weapon starts loaded.
        require(controller.ammoState().magazine == 4,
                "a fresh magazine was not full");

        // Firing spends rounds, and the magazine bottoms out rather than
        // going negative.
        WeaponCommand command;
        command.enabled = true;
        command.fireHeld = true;
        int shots = 0;
        for (int step = 0; step < 40; ++step) {
            controller.sample(command);
            if (controller.fixedUpdate(0.1f, arc, true))
                ++shots;
        }
        require(shots == 4, "an empty magazine kept firing");
        require(controller.ammoState().magazine == 0,
                "the magazine did not empty");
        require(controller.consumeDryFire(),
                "firing an empty weapon reported no dry fire");

        // auto_reload is off in this definition, so nothing reloaded on its
        // own -- the shot count above proves it, and this proves a reload is
        // still available to ask for.
        require(controller.beginReload(), "an empty weapon refused to reload");
        require(controller.ammoState().reloading,
                "beginReload did not start a reload");
        // Empty reload is the slower of the two.
        require(near(controller.ammoState().reloadTotal, 2.0f),
                "an empty reload did not use reload_empty_seconds");

        // Mid-reload the weapon does not fire.
        controller.sample(command);
        require(!controller.fixedUpdate(0.5f, arc, true).has_value(),
                "a reloading weapon fired");

        // Finishing it draws from reserve, and only as much as fits.
        //
        // The trigger is RELEASED for this: holding it would fire the rounds
        // back off as fast as they arrive, and the total below would be
        // measuring the cadence rather than the reload.
        WeaponCommand idle;
        idle.enabled = true;
        for (int step = 0; step < 30; ++step) {
            controller.sample(idle);
            controller.fixedUpdate(0.1f, arc, true);
        }
        const WeaponAmmoState after = controller.ammoState();
        require(!after.reloading, "the reload never finished");
        require(after.magazine + after.reserve == 6,
                "reloading created or destroyed rounds");

        // Switching away cancels a reload rather than finishing it in the
        // holster.
        controller.beginReload();
        WeaponCommand swap;
        swap.enabled = true;
        swap.selectSlot = 0;
        controller.sample(swap);
        controller.fixedUpdate(0.1f, arc, true);
    }

    // A weapon with no [.ammo] block is not gated on ammunition at all, which
    // is what keeps every weapon authored before magazines existed working.
    {
        PlayerWeaponLibrary fantasy;
        const std::string source =
            std::string(R"(
[player_weapon.probe]
slot = 0
name = "PROBE"
discipline = "TEST"
payload = "riven_spark"
fire_mode = "hitscan"
trigger = "automatic"
fire_interval = 0.1
arc_cost = 0.0
[player_weapon.probe.hitscan]
range = 60.0
)") + kCommonTail;
        require(fantasy.loadFromString(source.c_str()), "no-ammo load failed");
        require(!fantasy.find("probe")->usesMagazine(),
                "a weapon with no ammo block claimed a magazine");

        WeaponController controller;
        controller.bind(&fantasy.defs());
        Mana arc;
        arc.current = arc.max = 100.0f;
        arc.regenRate = 0.0f;
        WeaponCommand command;
        command.enabled = true;
        command.fireHeld = true;
        int shots = 0;
        for (int step = 0; step < 20; ++step) {
            controller.sample(command);
            if (controller.fixedUpdate(0.1f, arc, true))
                ++shots;
        }
        require(shots > 10, "a weapon with no magazine ran out of ammunition");
    }

    std::cout << "PlayerWeaponTests OK\n";
    return EXIT_SUCCESS;
}
