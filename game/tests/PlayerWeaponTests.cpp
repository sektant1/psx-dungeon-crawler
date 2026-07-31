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
    require(library.load(game::test::asset("weapons.toml")),
            "shipped player weapons did not load");
    require(library.defs().size() == 3,
            "shipped loadout must contain exactly three MVP weapons");

    const PlayerWeaponDef* spindle = library.find("vesper_spindle");
    const PlayerWeaponDef* arbalest = library.find("eidolon_arbalest");
    const PlayerWeaponDef* talon = library.find("riven_talon");
    require(spindle && arbalest && talon, "required weapon id is missing");
    require(spindle->trigger == WeaponTrigger::Automatic &&
                near(spindle->projectile.speed, 72.0f),
            "precision weapon cadence/projectile did not parse");
    require(arbalest->projectileCount == 3 &&
                near(arbalest->spreadDegrees, 8.0f),
            "heavy weapon spread did not parse");
    require(talon->viewmodel.parts.size() == 4 &&
                talon->projectile.material == "Game/Prototype/ProjectileTalon",
            "talon presentation did not parse");
    PlayerWeaponDef invalidDefinition = *spindle;
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
    require(near(arc.current, 98.0f), "shot did not spend authored ARC");
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
    require(near(arc.current, 86.0f), "heavy shot spent wrong ARC amount");
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
