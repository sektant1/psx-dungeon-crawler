#pragma once

namespace eng {
class Renderer;

namespace particle_presets {
inline constexpr const char* Fire = "engine.fire";
inline constexpr const char* Smoke = "engine.smoke";
inline constexpr const char* Poison = "engine.poison";
inline constexpr const char* Rain = "engine.rain";
inline constexpr const char* LavaAsh = "engine.lava_ash";
inline constexpr const char* HitSparks = "engine.hit_sparks";
inline constexpr const char* FootstepDust = "engine.footstep_dust";
inline constexpr const char* PickupBurst = "engine.pickup_burst";

// Called by Renderer initialization. Public for custom renderer bootstraps and
// safe to call again: registration updates presets by stable name.
void registerDefaults(Renderer& renderer);
} // namespace particle_presets
} // namespace eng
