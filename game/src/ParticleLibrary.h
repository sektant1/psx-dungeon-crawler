#pragma once
#include <eng/Handles.h>
#include <eng/ParticleEffectDesc.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace eng { class Renderer; }

// Parses particles.toml into ParticleEffectDescs, registers them with the
// Renderer, and resolves names -> ParticleEffectId. Holds live descs for the
// debug UI (later task).
class ParticleLibrary {
public:
    bool load(eng::Renderer& r, const std::string& tomlPath);
    eng::ParticleEffectId id(const std::string& name) const;

    std::vector<eng::ParticleEffectDesc>& descs() { return mDescs; }
    void reregister(eng::Renderer& r, size_t index);

private:
    std::vector<eng::ParticleEffectDesc> mDescs;
    std::vector<eng::ParticleEffectId>   mIds;   // parallel to mDescs
    std::unordered_map<std::string, size_t> mByName;
};

// Shared flame recipe (wall torch, handheld torch, scene torch): spawns the
// glow + fire + ash + smoke effect set under `node` by effect name. Free
// function so every torch site shares one definition without holding the
// library instance (effects resolve through the Renderer's name map).
namespace particlefx {
void spawnFlame(eng::Renderer& r, eng::NodeHandle node);
}
