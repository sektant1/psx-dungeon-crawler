#pragma once

#include <eng/Handles.h>

namespace eng { class Renderer; }

// Game-authored effect compositions. The engine owns individual data-driven
// effects; this layer gives dungeon concepts such as a flame their recipe.
namespace particlefx {
void spawnFlame(eng::Renderer& renderer, eng::NodeHandle node);
}
