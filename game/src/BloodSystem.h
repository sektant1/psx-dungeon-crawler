#pragma once
#include <eng/Handles.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eng { class Renderer; }

namespace game {

// How much blood an event is worth. Severity scales spray amount and whether
// gibs appear at all, so one call site serves a scratch and a decapitation
// without the caller naming effects.
enum class BloodSeverity { Light, Normal, Heavy };

// One creature's blood: which effects it throws. Blood is particles and voxel
// chunks only -- sprays and gibs collide and settle where they land, which is
// what leaves the mark, so there is no decal here. Every name resolves against
// the particle library, so a new creature type is a TOML block and no C++.
struct BloodProfile {
    std::string sprayEffect;   // fine spatter, sprite
    std::string gibEffect;     // chunky voxel debris, Heavy only
    std::string mistEffect;    // optional lingering haze
    std::string dripEffect;    // continuous emitter while wounded

    // Below this fraction of maximum health the drip emitter runs.
    float dripHpFraction = 0.35f;
    // Multiplies spray amount, so a boss bleeds more than a rat without
    // duplicating every effect.
    float amountScale = 1.0f;
    // Spray cone bias: 0 follows the surface normal, 1 follows the incoming
    // damage direction. Most creatures want a blend of the two.
    float damageBias = 0.55f;
};

// Everything blood.toml says, as plain data.
struct BloodDefinitions {
    std::unordered_map<std::string, BloodProfile>  profiles;
};

// Reads blood.toml into plain data. This exists separately from load() because
// parsing has no business needing a renderer: keeping it pure is what makes the
// schema testable headlessly, and a schema nobody can test is a schema that
// breaks silently.
//
// Returns false only when the file cannot be read or parsed. Individual bad
// entries -- an id-less block, an unknown blend -- are warned about and skipped,
// because one typo should not cost the level all of its blood.
bool parseBloodDefinitions(const std::string& tomlPath, BloodDefinitions& out);

// The profile every creature falls back to, and the explicit opt-out for the
// things that do not bleed (a stone construct chips, it does not spray). Named
// constants because both blood.toml and enemies.toml spell them.
inline constexpr const char* kDefaultBlood = "default";
inline constexpr const char* kNoBlood = "none";

// How much a severity multiplies spray amount by. Exposed rather than buried
// in the .cpp so the mapping can be asserted without a renderer.
float bloodSeverityScale(BloodSeverity severity);

// What a damage event is worth, as a fraction of the victim's health rather
// than an absolute: the same fifteen points is a scratch on a boss and a
// maiming on a rat. A killing blow is always Heavy, which is what makes gibs
// read as "that died" rather than "that was hit hard". `maxHealth` <= 0 (an
// unknown victim) falls back to Normal instead of dividing by it.
//
// Pure, and separate from the call sites, because this is the one judgement
// call in the whole path from "took damage" to "threw blood" -- everything else
// is a name lookup, and a judgement nobody can test drifts.
BloodSeverity bloodSeverityFor(float damageDealt, float maxHealth, bool killed);

// Turns damage events into blood.
//
// This is the whole reason engine/ never learns what blood is: the decal system
// draws generic marks, the particle system throws generic particles, and the
// mapping from "something was hurt" to "which of those, how many, what colour"
// is a gameplay decision, so it lives here.
class BloodSystem
{
public:
    // Parses profiles. Returns false if the file is missing or unreadable, in
    // which case every spawn call becomes a no-op rather than a crash.
    bool load(eng::Renderer& renderer, const std::string& tomlPath);

    // point/normal come from the hit; damageDir is the direction the blow
    // travelled, used to bias the spray away from the attacker.
    void spawnHit(eng::Renderer& renderer, const std::string& profile,
                  glm::vec3 point, glm::vec3 normal, glm::vec3 damageDir,
                  BloodSeverity severity = BloodSeverity::Normal) const;

    // --- drip -----------------------------------------------------------
    // A wounded body drips, and unlike a hit that is a *state*: the emitter
    // runs for as long as the victim is below its profile's drip_hp_fraction,
    // and it has to travel with the body, so this owns a node per bleeder and
    // moves it. Call once a frame per living combatant and let it decide -- the
    // threshold is the profile's, not the caller's.
    //
    // `bleeder` is any stable integer id for the body. An integer and not an
    // entity on purpose: nothing else in this file knows a registry exists, and
    // a blood system that did could not be used by anything that has no ECS.
    void updateDrip(eng::Renderer& renderer, uint32_t bleeder,
                    const std::string& profile, glm::vec3 wound,
                    float healthFraction);
    // Stop one bleeder for good and release its node: death, despawn.
    void stopDrip(eng::Renderer& renderer, uint32_t bleeder);
    // Drop every bleeder WITHOUT touching the renderer, for a level transition:
    // clearScene has already destroyed the nodes, and handing it their handles
    // afterwards is how a use-after-free gets written.
    void forgetDrips() { mDrips.clear(); }

    const BloodProfile* profile(const std::string& id) const;
    bool loaded() const { return mLoaded; }
    std::vector<std::string> profileIds() const;

private:
    // One per body that is bleeding or has bled: the node the emitter rides on,
    // and the emitter itself while it is running. The node outlives a pause in
    // the bleeding (healed above the threshold, then hurt again) because
    // recreating it every time would churn the scene graph for nothing.
    struct Drip {
        eng::NodeHandle node;
        eng::ParticlesHandle fx; // invalid while not currently bleeding
    };

    std::unordered_map<std::string, BloodProfile> mProfiles;
    std::unordered_map<uint32_t, Drip> mDrips;
    bool mLoaded = false;
};

} // namespace game
