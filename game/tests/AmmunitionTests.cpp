#include "Ammunition.h"
#include "TestAssets.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "AmmunitionTests: " << message << '\n';
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

    // --- the shipped cartridges --------------------------------------------
    AmmoLibrary ammo;
    require(ammo.load(game::test::asset("config/ammo.toml")),
            "shipped ammunition did not load");
    require(ammo.all().size() >= 8, "the cartridge set shrank unexpectedly");

    const Cartridge* fmj = ammo.find("556_fmj");
    const Cartridge* ap = ammo.find("556_ap");
    const Cartridge* buck = ammo.find("12g_buck");
    const Cartridge* lapua = ammo.find("338_lm");
    require(fmj && ap && buck && lapua, "a required cartridge id is missing");

    // A calibre groups its rounds, which is what a loadout screen lists.
    const auto fiveFiveSix = ammo.forCalibre("5.56");
    require(fiveFiveSix.size() >= 3,
            "5.56 does not offer a choice of rounds");
    for (const Cartridge* round : fiveFiveSix)
        require(round->calibre == "5.56", "forCalibre returned another calibre");

    // The design invariants of the set, stated as tests so retuning cannot
    // quietly invert them.
    require(ap->penetration > fmj->penetration,
            "AP does not out-penetrate FMJ");
    require(ap->damage < fmj->damage,
            "AP is strictly better than FMJ, so there is no decision");
    require(fmj->windFactor > lapua->windFactor,
            "the light round is not moved more by wind than the heavy one");
    require(buck->drag > lapua->drag,
            "buckshot does not shed speed faster than a match round");
    require(lapua->muzzleVelocity > buck->muzzleVelocity,
            "the sniper round is not faster than buckshot");

    // --- drag ---------------------------------------------------------------
    // Exponential, so it approaches zero without reaching or crossing it. A
    // linear decay goes negative and the round flies backwards.
    require(near(applyDrag(300.0f, 0.0f, 1.0f), 300.0f),
            "zero drag changed the speed");
    const float slowed = applyDrag(300.0f, 0.5f, 1.0f);
    require(slowed < 300.0f && slowed > 0.0f, "drag did not slow the round");
    float speed = 300.0f;
    for (int step = 0; step < 2000; ++step)
        speed = applyDrag(speed, 2.0f, 0.05f);
    require(speed >= 0.0f, "drag drove the speed negative");

    // --- penetration --------------------------------------------------------
    // Unarmoured: the full damage, and no armour to degrade.
    {
        const PenetrationResult hit = resolvePenetration(*fmj, 0, 1.0f, 1.0f);
        require(hit.penetrated, "an unarmoured target stopped a round");
        require(near(hit.damage, fmj->damage),
                "an unarmoured hit did not do full damage");
        require(near(hit.armourDamage, 0.0f),
                "an unarmoured hit degraded armour");
    }

    // Fragmentation is rolled, and it only adds damage. `roll` below the chance
    // fragments; above it does not.
    {
        const PenetrationResult frag = resolvePenetration(*fmj, 0, 1.0f, 0.0f);
        const PenetrationResult clean = resolvePenetration(*fmj, 0, 1.0f, 1.0f);
        require(frag.fragmented && !clean.fragmented,
                "fragmentation did not follow the roll");
        require(frag.damage > clean.damage,
                "fragmenting did not increase damage");
    }

    // Armour it cannot defeat stops it -- and no number of hits changes that,
    // which is the property the whole system exists for.
    {
        const PenetrationResult stopped =
            resolvePenetration(*fmj, 6, 1.0f, 1.0f);
        require(!stopped.penetrated, "FMJ defeated class 6 armour");
        require(stopped.damage > 0.0f && stopped.damage < fmj->damage * 0.3f,
                "a stopped round did no blunt trauma, or too much");
        require(stopped.armourDamage > 0.0f,
                "a stopped round did not degrade the armour");
        require(near(stopped.speedRetained, 0.0f),
                "a stopped round kept travelling");
    }

    // ...but the same plate worn down eventually lets it through. That is what
    // keeps "cannot defeat" from being a permanent wall.
    {
        const PenetrationResult fresh = resolvePenetration(*ap, 5, 1.0f, 1.0f);
        const PenetrationResult worn = resolvePenetration(*ap, 5, 0.05f, 1.0f);
        require(!fresh.penetrated || worn.damage >= fresh.damage,
                "worn armour did not protect less than fresh armour");
        require(worn.penetrated, "AP never defeated a nearly destroyed plate");
    }

    // A round that outclasses the armour arrives with more of itself intact
    // than one that barely gets through.
    {
        const PenetrationResult marginal =
            resolvePenetration(*fmj, 3, 0.2f, 1.0f);
        const PenetrationResult decisive =
            resolvePenetration(*lapua, 3, 0.2f, 1.0f);
        require(marginal.penetrated && decisive.penetrated,
                "expected both rounds to penetrate this plate");
        require(decisive.speedRetained > marginal.speedRetained,
                "the decisive penetration did not retain more speed");
    }

    // --- wind ---------------------------------------------------------------
    // Off by default: a shipped level should not have a crosswind nobody asked
    // for, and 0 must mean exactly zero rather than a small drift.
    {
        WindState calm;
        calm.speed = 0.0f;
        calm.gustSpeed = 0.0f;
        require(glm::length(calm.at(0.0f)) < 0.000001f,
                "a calm wind still pushed rounds");

        WindState breeze;
        breeze.direction = {1.0f, 0.0f, 0.0f};
        breeze.speed = 4.0f;
        breeze.gustSpeed = 0.0f;
        require(near(glm::length(breeze.at(3.0f)), 4.0f),
                "wind speed did not match its authored magnitude");

        // The gust is deterministic: the same time gives the same wind, so a
        // long-range shot is reproducible.
        WindState gusty = breeze;
        gusty.gustSpeed = 2.0f;
        gusty.gustPeriod = 5.0f;
        require(near(glm::length(gusty.at(1.7f)),
                     glm::length(gusty.at(1.7f))),
                "the gust is not a function of time alone");
        require(!near(glm::length(gusty.at(1.25f)),
                      glm::length(gusty.at(3.75f))),
                "the gust did not vary over its period");
    }

    // --- rejection ----------------------------------------------------------
    // A malformed cartridge fails the load rather than arriving with a silently
    // corrected value: ammunition decides whether a shot kills.
    {
        AmmoLibrary bad;
        require(!bad.loadFromString(
                    "[[cartridge]]\nid=\"x\"\ncalibre=\"9mm\"\n"
                    "muzzle_velocity=-5.0\n"),
                "a negative muzzle velocity was accepted");
        require(!bad.loadFromString(
                    "[[cartridge]]\nid=\"x\"\ncalibre=\"9mm\"\n"
                    "penetration=99\n"),
                "an out-of-range penetration class was accepted");
        require(!bad.loadFromString(
                    "[[cartridge]]\nid=\"a\"\ncalibre=\"9mm\"\n"
                    "[[cartridge]]\nid=\"a\"\ncalibre=\"9mm\"\n"),
                "two cartridges sharing an id were accepted");
    }

    std::cout << "AmmunitionTests OK\n";
    return EXIT_SUCCESS;
}
