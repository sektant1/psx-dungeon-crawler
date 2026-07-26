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
    const std::string programs = read("engine/assets/programs/psx.program");
    const std::string materials = read("engine/assets/materials/psx.material");
    const std::string shader = read("engine/assets/shaders/particle.frag");
    const std::string runtime = read("engine/src/particles/Particles.cpp");

    requireText(runtime, "kNonVisibleUpdateTimeout = 0.25f",
                "particles need a short offscreen simulation grace");
    requireText(runtime, "setNonVisibleUpdateTimeout("
                         "kNonVisibleUpdateTimeout)",
                "pooled particle systems do not consume the offscreen timeout");
    requireText(runtime, "particleSystemLifetimeExpired(",
                "one-shot cleanup does not consume the pure deadline policy");
    require(runtime.find("getNumParticles") == std::string::npos,
            "one-shot cleanup still waits for a possibly dormant Ogre count");

    require(programs.find("fragment_program RainParticle_FS") !=
                std::string::npos,
            "rain has a dedicated fragment program");
    require(programs.find("preprocessor_defines PROCEDURAL_RAIN=1") !=
                std::string::npos,
            "rain program selects the procedural rain shader");

    const std::size_t rainMaterial =
        materials.find("material Engine/Particles/Rain");
    require(rainMaterial != std::string::npos, "rain material exists");
    require(materials.find("fragment_program_ref RainParticle_FS",
                           rainMaterial) != std::string::npos,
            "rain material uses its dedicated shader");
    require(shader.find("defined(PROCEDURAL_RAIN)") != std::string::npos,
            "particle shader implements procedural rain");

    const std::string showcase = read("game/assets/lobby_showcase.toml");
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
    requireText(poison, "amount_scale = 2.0",
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
    requireText(lavaAsh, "amount_scale = 2.25",
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
