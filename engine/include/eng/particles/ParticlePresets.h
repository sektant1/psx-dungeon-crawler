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
inline constexpr const char* ArcaneMotes = "engine.arcane_motes";
inline constexpr const char* FrostShards = "engine.frost_shards";
inline constexpr const char* ToxicBubbles = "engine.toxic_bubbles";
inline constexpr const char* PortalWisps = "engine.portal_wisps";

// Called by Renderer initialization. Public for custom renderer bootstraps and
// safe to call again: registration updates presets by stable name.
void registerDefaults(Renderer& renderer);
} // namespace particle_presets
} // namespace eng
