#pragma once
#include <eng/particles/DecalSystem.h>

#include <glm/glm.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace eng { class Renderer; }

namespace game {

// How much blood an event is worth. Severity scales spray count, decal size and
// whether gibs appear at all, so one call site serves a scratch and a
// decapitation without the caller naming effects.
enum class BloodSeverity { Light, Normal, Heavy };

// One creature's blood: which effects it throws and which marks it leaves.
// Everything here is a name resolved against the particle library and the decal
// registry, so a new creature type is a TOML block and no C++ at all.
struct BloodProfile {
    std::string sprayEffect;   // fine spatter, sprite
    std::string gibEffect;     // chunky voxel debris, Heavy only
    std::string mistEffect;    // optional lingering haze
    std::string decalProfile;  // mark left where spray lands
    std::string poolProfile;   // growing pool under a bleeding body
    std::string dripEffect;    // continuous emitter while wounded

    // Below this fraction of maximum health the drip emitter runs.
    float dripHpFraction = 0.35f;
    // Multiplies spray amount and decal size, so a boss bleeds more than a rat
    // without duplicating every effect.
    float amountScale = 1.0f;
    // Spray cone bias: 0 follows the surface normal, 1 follows the incoming
    // damage direction. Most creatures want a blend of the two.
    float damageBias = 0.55f;
};

// One decal profile as the file states it, still paired with the id it will be
// registered under. Registration order matters -- a profile naming a decal must
// find it already registered -- so this is a vector rather than a map.
struct BloodDecalDef {
    std::string           id;
    eng::DecalProfileDesc desc;
};

// Everything blood.toml says, as plain data. Holding DecalProfileDesc here is
// deliberate: it is a POD of numbers, so carrying it costs nothing and keeps the
// parser from having to call into a renderer just to hand its results on.
struct BloodDefinitions {
    std::vector<BloodDecalDef>                     decals;
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

// How much a severity multiplies spray amount and decal size by. Exposed rather
// than buried in the .cpp so the mapping can be asserted without a renderer.
float bloodSeverityScale(BloodSeverity severity);

// Turns damage events into blood.
//
// This is the whole reason engine/ never learns what blood is: the decal system
// draws generic marks, the particle system throws generic particles, and the
// mapping from "something was hurt" to "which of those, how many, what colour"
// is a gameplay decision, so it lives here.
class BloodSystem
{
public:
    // Parses profiles and their decal definitions, registering the decals with
    // the renderer. Returns false if the file is missing or unreadable, in
    // which case every spawn call becomes a no-op rather than a crash.
    bool load(eng::Renderer& renderer, const std::string& tomlPath);

    // point/normal come from the hit; damageDir is the direction the blow
    // travelled, used to bias the spray away from the attacker.
    void spawnHit(eng::Renderer& renderer, const std::string& profile,
                  glm::vec3 point, glm::vec3 normal, glm::vec3 damageDir,
                  BloodSeverity severity = BloodSeverity::Normal) const;

    // A body that has come to rest and is still bleeding. Separate from
    // spawnHit because a pool grows from one persistent mark rather than
    // accumulating a fresh decal every frame.
    void spawnPool(eng::Renderer& renderer, const std::string& profile,
                   glm::vec3 groundPoint) const;

    const BloodProfile* profile(const std::string& id) const;
    bool loaded() const { return mLoaded; }
    std::vector<std::string> profileIds() const;

private:
    std::unordered_map<std::string, BloodProfile> mProfiles;
    bool mLoaded = false;
};

} // namespace game
