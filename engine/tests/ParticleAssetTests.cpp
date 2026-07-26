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
} // namespace

int main()
{
    const std::string programs = read("engine/assets/programs/psx.program");
    const std::string materials = read("engine/assets/materials/psx.material");
    const std::string shader = read("engine/assets/shaders/particle.frag");

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
                           "poison_particles", "lava_ash_particles"}) {
        const std::string altar = exhibit(showcase, id);
        require(!altar.empty(), "particle altar exhibit exists");
        require(altar.find("particle_offset") != std::string::npos,
                "particle altar emits above its opaque bowl");
    }

    std::cout << "ParticleAssetTests OK\n";
}
