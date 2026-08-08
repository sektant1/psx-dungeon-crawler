#pragma once

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace game {

// A cartridge: what a round is, independently of what fires it.
//
// The split between this and PlayerWeaponDef is the point. A weapon says which
// calibre it chambers; a cartridge says what a round of that calibre does. They
// vary independently -- two players with the same rifle and different boxes of
// ammunition are playing differently -- and that is only expressible if the
// round is its own thing rather than a set of fields on the gun.
//
// Authored in assets/config/ammo.toml, which documents every field and the
// reasoning behind the numbers.
struct Cartridge {
    std::string id;
    std::string displayName;
    // Which weapons can chamber it. Matches PlayerWeaponDef::ammo.ammoType.
    std::string calibre;

    // --- ballistics ---
    float muzzleVelocity = 300.0f; // m/s, scaled (see ammo.toml)
    float mass = 0.008f;           // kg
    float drag = 0.014f;           // fraction of speed shed per second
    float gravity = 1.0f;          // multiplier on world gravity
    float windFactor = 1.0f;       // how much wind pushes it

    // --- terminal ---
    float damage = 30.0f;
    // The armour class this round defeats. See resolvePenetration below.
    int penetration = 1;
    float armourDamage = 0.15f; // 0..1 degradation per hit
    float fragmentation = 0.0f; // chance of fragmenting AFTER penetrating
    float ricochet = 0.1f;      // chance of glancing off a shallow hard hit
    bool tracer = false;
};

// One vector for the level, scaled per round by its own windFactor.
//
// A per-zone wind field is a later problem; one vector is enough for the effect
// to be visible on a long shot and costs one multiply per projectile per tick.
struct WindState {
    glm::vec3 direction{1.0f, 0.0f, 0.0f};
    float speed = 0.0f;      // 0 disables it, which is the shipped default
    float gustSpeed = 0.0f;  // amplitude of a slow sinusoid on top
    float gustPeriod = 7.0f; // seconds

    // The wind right now. `time` is level time; the gust is deterministic, so
    // two clients or two replays of the same shot agree.
    glm::vec3 at(float time) const;
};

// What happened when a round met something.
struct PenetrationResult {
    bool penetrated = false;
    // Damage the target actually takes. On a stop this is blunt trauma through
    // the armour, which is a fraction rather than nothing -- being shot in a
    // plate still hurts, and a round that did literally nothing reads as the
    // hit not registering.
    float damage = 0.0f;
    // Degradation dealt to the armour, 0..1.
    float armourDamage = 0.0f;
    // Speed retained past the barrier, as a fraction. 0 when stopped.
    float speedRetained = 0.0f;
    bool fragmented = false;
};

// The rule, in one place so the projectile system, the melee sweep and any
// future turret all answer it identically.
//
//   penetration >= armourClass   the round goes through. Damage falls off with
//                                how marginal the penetration was, and the
//                                round keeps travelling more slowly.
//   penetration <  armourClass   it is stopped. The armour degrades, the target
//                                takes blunt trauma.
//
// The consequence worth having: armour is not a hit-point pool that a big
// enough number always beats. A round that cannot defeat a plate never will,
// however many you fire -- you change ammunition or you shoot somewhere else.
// Degradation is what keeps that from being a permanent wall.
//
// `armourDurability` is 0..1 remaining; worn armour protects less, which is
// what makes sustained fire on a hard target eventually work.
// `roll` is 0..1 from the caller's own RNG, so this stays a pure function and
// a test can pin the fragmentation branch.
PenetrationResult resolvePenetration(const Cartridge& round, int armourClass,
                                     float armourDurability, float roll);

// Speed after `dt` of drag. Exponential rather than linear: a linear decay
// reaches zero and then goes negative, and clamping that produces a round that
// stops dead in mid-air.
float applyDrag(float speed, float drag, float dt);

// ammo.toml, parsed. One reader, so "what is 5.56 AP" has one answer.
class AmmoLibrary {
public:
    bool load(const std::string& tomlPath);
    bool loadFromString(const char* tomlSource);

    const Cartridge* find(std::string_view id) const;
    // Every round a weapon of this calibre can chamber, in file order -- which
    // is what a loadout screen lists.
    std::vector<const Cartridge*> forCalibre(std::string_view calibre) const;
    const std::vector<Cartridge>& all() const { return mCartridges; }
    const WindState& wind() const { return mWind; }
    void setWind(const WindState& wind) { mWind = wind; }

private:
    std::vector<Cartridge> mCartridges;
    WindState mWind;
};

bool validCartridge(const Cartridge& round);

} // namespace game
