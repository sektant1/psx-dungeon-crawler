#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "VfxShaderAssetTests: " << message << '\n';
        std::exit(1);
    }
}

std::string read(const std::string& relativePath)
{
    std::ifstream file(std::string(PROJECT_SOURCE_DIR) + "/" + relativePath);
    require(bool(file), "required VFX asset could not be opened");
    return {std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>()};
}

void requireText(const std::string& text, const char* expected,
                 const char* message)
{
    require(text.find(expected) != std::string::npos, message);
}

std::string material(const std::string& source, const std::string& name)
{
    const std::string marker = "material " + name;
    const std::size_t begin = source.find(marker);
    if (begin == std::string::npos)
        return {};
    const std::size_t end = source.find("\nmaterial ", begin + marker.size());
    return source.substr(begin, end - begin);
}

} // namespace

int main()
{
    const std::string program =
        read("engine/assets/programs/vfx.program");
    const std::string portalFragment =
        read("engine/assets/shaders/portal.frag");
    const std::string liquidFragment =
        read("engine/assets/shaders/liquid.frag");
    const std::string lavaFragment =
        read("engine/assets/shaders/lava.frag");
    // The portal and liquid profiles moved into one vfx.material when the
    // prototype content reset removed game.material/fantasy.material; the
    // shader-driven materials survived that reset deliberately.
    const std::string gameMaterials =
        read("game/assets/materials/vfx.material");
    const std::string& fantasyMaterials = gameMaterials;

    requireText(program, "vertex_program PixelVfx/PortalVS glsl",
                "dedicated portal vertex program is missing");
    requireText(program, "fragment_program PixelVfx/PortalFS glsl",
                "dedicated portal fragment program is missing");
    requireText(program, "vertex_program PixelVfx/LiquidVS glsl",
                "dedicated liquid vertex program is missing");
    requireText(program, "fragment_program PixelVfx/LiquidFS glsl",
                "dedicated liquid fragment program is missing");
    requireText(program, "fragment_program PixelVfx/LavaFS glsl",
                "dedicated lava fragment program is missing");

    requireText(portalFragment, "floor(time * portalStepFps)",
                "portal animation is not frame-stepped");
    requireText(portalFragment, "portalPalette",
                "portal output is not palette quantized");
    requireText(liquidFragment, "floor(time * liquidStepFps)",
                "liquid animation is not frame-stepped");
    requireText(liquidFragment, "liquidPalette",
                "liquid output is not palette quantized");
    requireText(lavaFragment, "lavaFbm",
                "lava shader lacks procedural multi-scale flow");
    requireText(lavaFragment, "domainWarp",
                "lava shader lacks animated domain warping");
    requireText(lavaFragment, "lavaPalette",
                "lava output is not palette quantized");

    for (const char* name : {"Game/PortalDown", "Game/PortalUp"}) {
        const std::string block = material(gameMaterials, name);
        require(!block.empty(), "portal material is missing");
        requireText(block, "vertex_program_ref PixelVfx/PortalVS",
                    "portal material uses the generic sprite vertex shader");
        requireText(block, "fragment_program_ref PixelVfx/PortalFS",
                    "portal material uses the generic sprite fragment shader");
        requireText(block, "depth_write on",
                    "portal material does not write depth");
        requireText(block, "cull_hardware none",
                    "portal membrane is unexpectedly culled");
        requireText(block, "filtering none",
                    "portal texture is not nearest filtered");
    }

    for (const char* name : {"Fantasy/Water", "Fantasy/ToxicSlime"}) {
        const std::string block = material(fantasyMaterials, name);
        require(!block.empty(), "liquid material is missing");
        requireText(block, "vertex_program_ref PixelVfx/LiquidVS",
                    "liquid material uses the generic sprite vertex shader");
        requireText(block, "fragment_program_ref PixelVfx/LiquidFS",
                    "liquid material uses the generic sprite fragment shader");
        requireText(block, "depth_write on",
                    "liquid material does not write depth");
        requireText(block, "filtering none",
                    "liquid texture is not nearest filtered");
    }
    const std::string lava = material(fantasyMaterials, "Fantasy/Lava");
    requireText(lava, "fragment_program_ref PixelVfx/LavaFS",
                "lava still uses the generic liquid fragment shader");
    requireText(lava, "depth_write on",
                "lava material does not write depth");
    requireText(lava, "filtering none",
                "lava texture is not nearest filtered");

    std::cout << "VfxShaderAssetTests OK\n";
}
