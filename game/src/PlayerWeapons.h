#pragma once

#include "combat/FeelComponents.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace game {

enum class WeaponTrigger { Press, Automatic };

enum class WeaponPrimitive {
    Box,
    BeveledBox,
    Sphere,
    Capsule,
    Cylinder,
    Cone,
    Disc,
};

struct PlayerProjectileDef {
    WeaponPrimitive primitive = WeaponPrimitive::Sphere;
    glm::vec3 visualScale{0.08f};
    std::string material = "Game/Prototype/ProjectileVesper";
    std::string trailEffect;
    std::string impactEffect;
    float speed = 50.0f;
    float lifetime = 1.5f;
    float radius = 0.05f;
    float mass = 0.1f;
    float gravityFactor = 0.0f;
    float aimRange = 80.0f;
};

struct WeaponViewmodelPart {
    WeaponPrimitive primitive = WeaponPrimitive::Box;
    glm::vec3 position{0.0f};
    glm::vec3 rotationDegrees{0.0f};
    glm::vec3 scale{1.0f};
    std::string material = "Game/ViewModelVesper";
    bool enchanted = false;
};

struct WeaponViewmodelDef {
    glm::vec3 position{0.24f, -0.24f, -0.55f};
    glm::vec3 rotationDegrees{0.0f};
    std::vector<WeaponViewmodelPart> parts;
    std::string glowSchool = "arcane";
    float glowStrength = 0.6f;

    float fireDuration = 0.16f;
    float recoilDistance = 0.06f;
    float recoilPitchDegrees = 6.0f;
    float recoilYawDegrees = 0.0f;
    float recoilRecovery = 20.0f;
    float movementBob = 0.012f;
    float movementBobSpeed = 7.5f;
    float idleSway = 0.004f;
    float lookSway = 0.0012f;
};

struct PlayerWeaponDef {
    std::string id;
    std::string displayName;
    std::string discipline;
    std::string payloadId;
    WeaponTrigger trigger = WeaponTrigger::Press;
    float fireInterval = 0.25f;
    float arcCost = 5.0f;
    int projectileCount = 1;
    float spreadDegrees = 0.0f;
    float switchTime = 0.18f;
    // Camera-local right/up/forward offset. Aim remains camera-derived.
    glm::vec3 muzzleOffset{0.18f, -0.16f, 0.40f};
    std::string muzzleEffect;
    PlayerProjectileDef projectile;
    WeaponViewmodelDef viewmodel;
};

bool validPlayerWeaponDefinition(const PlayerWeaponDef& definition);

class PlayerWeaponLibrary {
public:
    PlayerWeaponLibrary();

    bool load(const std::string& tomlPath);
    bool loadFromString(const char* tomlSource);

    const std::vector<PlayerWeaponDef>& defs() const { return mDefs; }
    std::vector<PlayerWeaponDef>& defs() { return mDefs; }
    const PlayerWeaponDef* find(const std::string& id) const;

private:
    std::vector<PlayerWeaponDef> mDefs;
};

struct WeaponCommand {
    bool enabled = false;
    bool fireHeld = false;
    bool firePressed = false;
    bool swapPressed = false;
    int selectSlot = -1;
};

// Fixed-step mutable state for one player loadout. Definitions remain immutable;
// cooldown, switch lock, buffered input, and shared ARC spending live here.
class WeaponController {
public:
    void bind(const std::vector<PlayerWeaponDef>* definitions);
    void sample(const WeaponCommand& command);
    std::optional<std::size_t> fixedUpdate(float dt, Mana& arc,
                                           bool canFire = true);

    std::size_t selectedIndex() const { return mSelected; }
    const PlayerWeaponDef* selected() const;
    bool consumeSelectionChanged();
    void resetRuntime();

private:
    const std::vector<PlayerWeaponDef>* mDefinitions = nullptr;
    std::vector<float> mCooldowns;
    std::size_t mSelected = 0;
    float mSwitchRemaining = 0.0f;
    bool mFireHeld = false;
    bool mFirePressed = false;
    bool mSwapPressed = false;
    int mSelectSlot = -1;
    bool mSelectionChanged = false;
};

// Deterministic horizontal fan around a camera-derived aim direction.
std::vector<glm::vec3> projectileDirections(glm::vec3 direction, int count,
                                            float spreadDegrees);

} // namespace game
