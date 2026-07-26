#include "ParticleEffects.h"

#include <eng/Renderer.h>

namespace particlefx {

void spawnFlame(eng::Renderer& renderer, eng::NodeHandle node)
{
    renderer.spawnParticles("torch_glow", node);
    renderer.spawnParticles("torch_fire", node);
    renderer.spawnParticles("torch_ash", node);
    renderer.spawnParticles("fire_smoke", node, glm::vec3(0.0f, 0.12f, 0.0f));
}

} // namespace particlefx
