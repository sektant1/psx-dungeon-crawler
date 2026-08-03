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
    controller.sample({true, true, true, false, -1});
    const auto first = controller.fixedUpdate(1.0f / 60.0f, arc);
    require(first && *first == 0, "automatic weapon did not fire immediately");
    // Against slot 0's own cost rather than a literal: which weapon starts in
    // hand is a loadout decision, and this is testing that firing spends what
    // the definition says, not what that weapon happens to charge today.
    require(near(arc.current, 100.0f - library.defs().front().arcCost),
            "shot did not spend authored ARC");
    require(!controller.fixedUpdate(1.0f / 60.0f, arc),
            "cooldown allowed a duplicate fixed-step shot");

    controller.sample({true, false, false, false, 1});
    require(!controller.fixedUpdate(1.0f / 60.0f, arc),
            "weapon switch emitted a shot");
    require(controller.selectedIndex() == 1 &&
                controller.consumeSelectionChanged(),
            "direct slot selection did not update runtime state");
    for (int i = 0; i < 12; ++i)
        controller.fixedUpdate(1.0f / 60.0f, arc);
    controller.sample({true, false, true, false, -1});
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

    std::cout << "PlayerWeaponTests OK\n";
    return EXIT_SUCCESS;
}
