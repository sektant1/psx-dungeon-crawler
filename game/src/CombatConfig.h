#pragma once
#include <glm/glm.hpp>
#include <string>

namespace eng { class Input; }

// Data-oriented tunables for every attack, loaded from game.toml's [combat.*]
// tables and live-editable in the debug UI. Systems (SpellSystem,
// ProjectileSystem, MeleeSystem) hold a pointer to one instance and read it
// each time they fire, so UI edits take effect on the next cast/swing.
//
// Hotkeys are stored as the input ACTION name plus the current key name; the
// action->key mapping itself lives in [bindings]. The debug UI rebinds live
// via eng::Input::rebind and the change also persists if written back to toml.
struct CombatConfig {
    struct Fireball {
        float speed         = 18.0f;
        float radius        = 0.14f;
        float mass          = 0.30f;
        float ttl           = 6.0f;
        float impactImpulse = 3.0f;
        glm::vec3 lightColour{1.0f, 0.55f, 0.15f};
        float lightRange    = 4.0f;
        // Particle EFFECT names (registered from particles.toml), not materials.
        std::string trailParticle  = "fireball_trail";
        std::string muzzleParticle = "spell_muzzle";
        std::string impactParticle = "fireball_impact";
        std::string action = "cast_spell";
        std::string key    = "Q";
    } fireball;

    struct Beam {
        float range      = 40.0f;
        float width      = 0.06f;
        float impulse    = 4.0f;
        float segmentTtl = 0.10f;
        glm::vec3 lightColour{0.40f, 0.70f, 1.0f};
        float lightRange = 3.0f;
        std::string coreParticle   = "Game/BeamCore";   // beam segment MESH material
        std::string impactParticle = "beam_impact";      // particle EFFECT name
        std::string action = "cast_beam";
        std::string key    = "R";
    } beam;

    struct Arrow {
        float speed      = 60.0f;
        float radius     = 0.03f;
        float halfHeight = 0.22f;
        float mass       = 0.10f;
        float ttl        = 10.0f;
        std::string action = "fire_arrow";
        std::string key    = "F";
    } arrow;

    struct Melee {
        float reach   = 1.80f;
        float radius  = 0.50f;
        float impulse = 6.0f;
        float windup  = 0.12f;
        float active  = 0.13f;
        // Melee is bound to the mouse, not a rebindable key name.
    } melee;

    // Populate from the [combat.*] and [bindings] tables of the given toml file.
    // Missing keys keep their defaults. Returns false only if the file fails to
    // parse (values are always best-effort filled).
    bool load(const std::string& tomlPath);

    // Render the ImGui body of the "Attacks" debug window. `input` is used to
    // apply live hotkey rebinds.
    void drawDebugUi(eng::Input& input);
};
