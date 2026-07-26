#pragma once

namespace eng::prototype {

// Engine-owned diagnostic render assets. They are generated at renderer startup,
// so every application gets reliable fallbacks without shipping app assets.
inline constexpr const char* kSurfaceTexture = "EnginePrototypeSurface.png";
inline constexpr const char* kSpriteTexture = "EnginePrototypeSprite.png";
inline constexpr const char* kParticleTexture = "EnginePrototypeParticle.png";
inline constexpr const char* kSurfaceMaterial = "Engine/PrototypeSurface";
inline constexpr const char* kParticleMaterial = "Engine/PrototypeParticle";

} // namespace eng::prototype
