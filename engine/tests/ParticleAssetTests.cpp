#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "ParticleAssetTests: " << message << '\n';
        std::exit(1);
    }
}

std::string read(const std::string& relativePath)
{
    std::ifstream file(std::string(PROJECT_SOURCE_DIR) + "/" + relativePath);
    return {std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>()};
}

std::string exhibit(const std::string& toml, const std::string& id)
{
    const std::string marker = "id = \"" + id + "\"";
    const std::size_t begin = toml.find(marker);
    if (begin == std::string::npos) return {};
    const std::size_t end = toml.find("[[exhibit]]", begin);
    return toml.substr(begin, end - begin);
}

void requireText(const std::string& text, const char* expected,
                 const char* message)
{
    require(text.find(expected) != std::string::npos, message);
}
} // namespace

int main()
{
    const std::string programs = read("assets/programs/psx.program");
    const std::string materials = read("assets/materials/psx.material");
    const std::string shader = read("assets/shaders/particle.frag");
    const std::string runtime = read("engine/src/particles/Particles.cpp");
    const std::string presets =
        read("engine/src/particles/ParticlePresets.cpp");
    const std::string presetHeader =
        read("engine/include/eng/particles/ParticlePresets.h");

    const std::string sim = read("engine/src/particles/ParticleSim.cpp");

    // The offscreen simulation grace these assertions used to check was
    // Ogre's setNonVisibleUpdateTimeout, and it went away with the Ogre
    // backend. That is a deliberate trade, not an oversight: Ogre paused
    // culled systems because an unbounded number of them each updated
    // independently, whereas the CPU simulation walks one pool whose size is
    // fixed at startup. Cost is now bounded by capacity rather than by how
    // much of the level happens to be on screen, so pausing buys little and
    // would cost a visible catch-up pop when an emitter comes back into view.
    requireText(runtime, "kPoolCapacity",
                "the particle pool is no longer bounded at a fixed capacity");
    requireText(sim, "particleSystemLifetimeExpired(",
                "one-shot cleanup does not consume the pure deadline policy");
    require(runtime.find("getNumParticles") == std::string::npos,
            "one-shot cleanup still waits for a possibly dormant Ogre count");
    // Retirement must stay driven by the resolved emission window, never by a
    // live count: an effect that is culled or throttled can legitimately hold
    // zero particles for a frame without being finished.
    requireText(runtime, "mSim.instanceActive(",
                "handle retirement does not consult the simulation's own "
                "instance lifetime");

    require(programs.find("fragment_program RainParticle_FS") !=
                std::string::npos,
            "rain has a dedicated fragment program");
    require(programs.find("preprocessor_defines PROCEDURAL_RAIN=1") !=
                std::string::npos,
            "rain program selects the procedural rain shader");
    const std::size_t particleVertex =
        programs.find("vertex_program Particle_VS glsl");
    const std::size_t particleVertexEnd =
        programs.find("fragment_program", particleVertex);
    const std::string particleVertexBlock =
        programs.substr(particleVertex, particleVertexEnd - particleVertex);
    requireText(particleVertexBlock, "param_named_auto time time 1.0",
                "particle vertex program does not bind animation time");
    requireText(particleVertexBlock, "param_named atlasGrid float2",
                "particle vertex program does not bind atlas dimensions");
    require(materials.find(
                "texture_unit { texture retro_particle_atlas.png filtering") ==
                std::string::npos,
            "particle atlas texture unit uses parser-unsafe one-line syntax");

    const std::size_t rainMaterial =
        materials.find("material Engine/Psx/Rain");
    require(rainMaterial != std::string::npos, "rain material exists");
    require(materials.find("fragment_program_ref RainParticle_FS",
                           rainMaterial) != std::string::npos,
            "rain material uses its dedicated shader");
    require(shader.find("defined(PROCEDURAL_RAIN)") != std::string::npos,
            "particle shader implements procedural rain");

    for (const char* preset :
         {"engine.arcane_motes", "engine.frost_shards",
          "engine.toxic_bubbles", "engine.portal_wisps"}) {
        requireText(presetHeader, preset,
                    "modern pixel particle preset is missing");
    }
    for (const char* define :
         {"PROCEDURAL_MOTE", "PROCEDURAL_SHARD",
          "PROCEDURAL_BUBBLE", "PROCEDURAL_WISP"}) {
        requireText(shader, define,
                    "modern pixel particle mask is missing");
        requireText(programs, define,
                    "modern pixel particle program is missing");
    }
    for (const char* materialName :
         {"material Engine/Psx/ArcaneMote",
          "material Engine/Psx/FrostShard",
          "material Engine/Psx/ToxicBubble",
          "material Engine/Psx/PortalWisp"}) {
        requireText(materials, materialName,
                    "modern pixel particle material is missing");
    }
    requireText(presets,
                "base(Fire, \"Engine/Psx/Fire\", 0.32f, 0.38f, 96)",
                "fire particles are not substantially larger or more numerous");
    requireText(presets,
                "emitter({0,1,0}, 24, 42, 0.38f, 0.78f",
                "fire emission is not dense enough");
    requireText(presets,
                "base(Poison, \"Engine/Psx/Poison\", 0.20f, 0.26f, 72)",
                "poison particles lack the quality-first size/quota");
    requireText(presets, "{1.35f,0.12f,0.025f",
                "fire palette is not saturated HDR");
    requireText(presets, "{0.18f,1.30f,0.045f",
                "poison palette is not saturated HDR");
    requireText(presets, "e.boxSize = {3.45f, 2.55f, 0.16f}",
                "portal wisps do not cover the portal opening");
    requireText(read("game/src/SceneFactory.cpp"),
                "r.spawnParticles(style.particles, arch)",
                "portal wisps are not centered on the portal membrane");

    const std::string showcase = read("assets/config/showroom_exhibits.toml");
    for (const char* id : {"fire_particles", "smoke_particles",
                           "rain_volume"}) {
        const std::string altar = exhibit(showcase, id);
        require(!altar.empty(), "particle altar exhibit exists");
        require(altar.find("particle_offset") != std::string::npos,
                "particle altar emits above its opaque bowl");
    }

    const std::string poison = exhibit(showcase, "poison_particles");
    requireText(poison, "[exhibit.particle_options]",
                "poison altar has nested particle options");
    requireText(poison, "size_scale = 1.8",
                "poison altar enlarges its particles");
    requireText(poison, "amount_scale = 3.0",
                "poison altar increases particle density");
    requireText(poison, "lifetime_scale = 1.15",
                "poison altar extends particle lifetime");
    requireText(poison, "radius_scale = 1.25",
                "poison altar broadens its emitter");
    requireText(poison, "local_offset = [0.0, 0.72, 0.0]",
                "poison altar keeps its raised local offset in nested options");

    const std::string lavaAsh = exhibit(showcase, "lava_ash_particles");
    requireText(lavaAsh, "[exhibit.particle_options]",
                "lava ash altar has nested particle options");
    requireText(lavaAsh, "size_scale = 1.7",
                "lava ash altar enlarges its particles");
    requireText(lavaAsh, "amount_scale = 3.0",
                "lava ash altar increases particle density");
    requireText(lavaAsh, "lifetime_scale = 1.2",
                "lava ash altar extends particle lifetime");
    requireText(lavaAsh, "radius_scale = 1.35",
                "lava ash altar broadens its emitter");
    requireText(lavaAsh, "local_offset = [0.0, 0.72, 0.0]",
                "lava ash altar keeps its raised local offset in nested options");

    for (const char* id : {"fire_particles", "poison_particles",
                           "rain_volume"}) {
        const std::string altar = exhibit(showcase, id);
        requireText(altar, "label_offset = [1.65, 0.0, 0.0]",
                    "left-side particle altar label moves inward");
    }
    for (const char* id : {"smoke_particles", "lava_ash_particles"}) {
        const std::string altar = exhibit(showcase, id);
        requireText(altar, "label_offset = [-1.65, 0.0, 0.0]",
                    "right-side particle altar label moves inward");
    }

    std::cout << "ParticleAssetTests OK\n";
}
