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

std::size_t countOf(const std::string& text, const std::string& needle)
{
    std::size_t n = 0;
    for (std::size_t at = text.find(needle); at != std::string::npos;
         at = text.find(needle, at + needle.size()))
        ++n;
    return n;
}

// One material's text out of a .mat file: from its own [material."Name"]
// header to the next top-level [material header. The params sub-table
// ([material."Name".params]) is inside that span, which is what these
// assertions read.
std::string material(const std::string& source, const std::string& name)
{
    const std::string marker = "[material.\"" + name + "\"]";
    const std::size_t begin = source.find(marker);
    if (begin == std::string::npos)
        return {};
    std::size_t end = begin + marker.size();
    while (true) {
        end = source.find("\n[material.", end);
        if (end == std::string::npos)
            return source.substr(begin);
        // Skip this material's own sub-tables.
        if (source.compare(end + 1, marker.size() - 1, marker, 0,
                           marker.size() - 1) != 0)
            return source.substr(begin, end - begin);
        end += 2;
    }
}

} // namespace

int main()
{
    const std::string vkSurface =
        read("assets/shaders/vulkan/surface.frag");
    const std::string portalFragment =
        read("assets/shaders/portal.frag");
    const std::string prototypePortalFragment =
        read("assets/shaders/prototype_portal.frag");
    // The look is split in two: a kernel every stylised surface shares, and the
    // pattern that makes this one a portal. The two fragment shaders are only
    // the field lookup that differs between them.
    const std::string surfaceCommon =
        read("assets/shaders/surface_common.glsl");
    const std::string portalPattern =
        read("assets/shaders/portal_pattern.glsl");
    const std::string portalVertex =
        read("assets/shaders/surface.vert");
    const std::string liquidFragment =
        read("assets/shaders/liquid.frag");
    const std::string prototypeLiquidFragment =
        read("assets/shaders/prototype_liquid.frag");
    const std::string lavaFragment =
        read("assets/shaders/lava.frag");
    // The portal and liquid profiles moved into one vfx.material when the
    // prototype content reset removed game.material/fantasy.material; the
    // shader-driven materials survived that reset deliberately.
    const std::string gameMaterials =
        read("assets/materials/vfx.mat");
    const std::string& fantasyMaterials = gameMaterials;
    const std::string editorMaterials =
        read("assets/materials/editor.mat");
    const std::string placementGhost =
        read("assets/shaders/placement_ghost.frag");

    // One shader, three profiles selected at runtime -- there are no per-look
    // programs to declare any more, so what must exist are the branches.
    requireText(vkSurface, "mode == 3", "portal profile is missing");
    requireText(vkSurface, "mode == 2", "lava profile is missing");
    requireText(vkSurface, "surfaceRimShade", "portal rims are missing");

    requireText(surfaceCommon, "floor(time * surfaceStepFps)",
                "portal animation is not frame-stepped");
    requireText(surfaceCommon, "surfacePalette",
                "portal output is not palette quantized");
    requireText(surfaceCommon, "surfaceBayer",
                "portal palette bands are not ordered-dithered");
    // The membrane is a slab: its pixel grid is authored in metres (read off
    // the mesh, see surface_common.glsl) and its four rims shade separately.
    // Both are what make the shader survive a resized or thickened mesh.
    requireText(surfaceCommon, "surfaceTexelSize",
                "portal pixel grid is not sized in metres");
    requireText(surfaceCommon, "fwidth(surfaceLocal.xz)",
                "portal does not derive the membrane's size from the mesh");
    requireText(surfaceCommon, "surfaceRimShade",
                "portal slab rims have no shading of their own");
    // Emission is separate from the palette on purpose: bloom has no colour of
    // its own, so without a glow term the palette has to be both the portal's
    // colour and its brightness, and a palette authored below 1.0 cannot glow
    // at all. The glow must be ADDED (that is what crosses 1.0 and blooms) and
    // must reach the rims too, or a thick slab's lit edge is the one dull part.
    // Arm width is a power curve on the value, not a palette change: the raw
    // sine is an even 50/50 arm/gap split and the palette thresholds are fixed,
    // so without it the only way to fatten an arm is to recolour the portal.
    requireText(portalPattern, "portalArmWidth",
                "portal arm thickness cannot be tuned apart from its colour");
    requireText(surfaceCommon, "surfaceGlowColour",
                "portal has no emission colour of its own");
    requireText(surfaceCommon, "surfaceGlowStrength",
                "portal emission has no strength control");
    requireText(surfaceCommon, "colour + surfaceGlowColour.rgb",
                "portal emission is not additive, so it cannot drive bloom");
    require(countOf(surfaceCommon, "surfaceGlow(") >= 3,
            "portal emission is not applied to both the face and the rims");
    // Both fragment shaders must stay a thin lookup over the shared kernel and
    // the shared pattern, or the authored and prototype portals drift apart.
    for (const std::string* frag : {&portalFragment, &prototypePortalFragment}) {
        requireText(*frag, "#include <surface_common.glsl>",
                    "portal shader does not build on the surface kernel");
        requireText(*frag, "#include <portal_pattern.glsl>",
                    "portal shader does not use the shared portal pattern");
        requireText(*frag, "float surfaceField(",
                    "portal shader does not supply the kernel's field");
    }
    requireText(portalFragment, "texture(surfaceTexture",
                "portal shader no longer samples its flow texture");
    requireText(prototypePortalFragment, "protoFbm",
                "prototype portal lacks a procedural stand-in field");
    require(surfaceCommon.find("void main()") == std::string::npos,
            "the surface kernel grew a main(); the pattern belongs to a "
            "profile, or no other surface type can reuse it");
    requireText(portalPattern, "void main()",
                "the portal profile has no entry point of its own");
    requireText(portalVertex, "cameraPositionObject",
                "portal vertex stage cannot feed the depth parallax");
    // The eye, in the same frame as the field it shears. Asserted against the
    // Vulkan stage rather than a .program declaration: the camera used to be an
    // Ogre auto-param bound by name, and is now a member of the scene uniform
    // block, so the binding IS the shader reading it. Both halves are named
    // because the parallax silently flattens if either goes: without the
    // inverse the eye stays in world space, and without the subtraction there
    // is no view vector at all.
    const std::string vkSurfaceVertex =
        read("assets/shaders/vulkan/surface.vert");
    requireText(vkSurfaceVertex, "scene.cameraPositionAndLightCount.xyz",
                "portal vertex stage does not read the camera for parallax");
    requireText(vkSurfaceVertex, "inverse(drawData.model)",
                "the eye is not brought into object space, so the parallax "
                "shear is expressed in the wrong frame");
    requireText(vkSurfaceVertex, "surfaceView",
                "portal vertex stage does not pass the view vector on");
    // The scrolling family. Deliberately NOT the surface kernel: this look
    // comes from sliding tiling art, not from a field evaluated per pixel.
    const std::string scrollCommon =
        read("assets/shaders/scroll_common.glsl");
    requireText(scrollCommon, "floor(time * liquidStepFps)",
                "liquid animation is not frame-stepped");
    requireText(scrollCommon, "liquidPalette",
                "liquid output is not palette quantized");
    // fract() on the OFFSET, never on the sampling coordinate: `time` grows
    // without bound and uv + speed*time grinds into judder after minutes, while
    // wrapping the coordinate itself would put a hard seam where it wraps.
    requireText(scrollCommon, "return fract(flow * steppedTime)",
                "liquid scroll offset is unbounded; it will lose precision");
    for (const std::string* frag : {&liquidFragment, &prototypeLiquidFragment}) {
        requireText(*frag, "#include <scroll_common.glsl>",
                    "liquid shader does not use the scrolling kernel");
        requireText(*frag, "liquidScroll(liquidFlowA",
                    "liquid shader scrolls without wrapping its offset");
    }
    require(scrollCommon.find("void main()") == std::string::npos,
            "the scrolling kernel grew a main(); that belongs to a profile");
    requireText(lavaFragment, "lavaFbm",
                "lava shader lacks procedural multi-scale flow");
    requireText(lavaFragment, "domainWarp",
                "lava shader lacks animated domain warping");
    requireText(lavaFragment, "lavaPalette",
                "lava output is not palette quantized");

    for (const char* name : {"Game/Vfx/PortalDown", "Game/Vfx/PortalUp"}) {
        const std::string block = material(gameMaterials, name);
        require(!block.empty(), "portal material is missing");
        requireText(block, "shader = \"surface.portal\"",
                    "portal material names the portal surface profile");
        requireText(block, "shader = \"surface.portal\"",
                    "portal material names the portal surface profile");
        requireText(block, "depth_write = true",
                    "portal material does not write depth");
        requireText(block, "cull = \"none\"",
                    "portal membrane is unexpectedly culled");
        requireText(block, "filter = \"nearest\"",
                    "portal texture is not nearest filtered");
        requireText(block, "surfaceCore = ",
                    "portal profile does not set its own core tone");
        requireText(block, "surfaceGlowColour = ",
                    "portal profile does not state what colour it blooms");
    }

    for (const char* name : {"Game/Vfx/Water", "Game/Vfx/ToxicSlime"}) {
        const std::string block = material(fantasyMaterials, name);
        require(!block.empty(), "liquid material is missing");
        requireText(block, "shader = \"surface.liquid\"",
                    "liquid material names the liquid surface profile");
        requireText(block, "shader = \"surface.liquid\"",
                    "liquid material names the liquid surface profile");
        requireText(block, "depth_write = true",
                    "liquid material does not write depth");
        requireText(block, "filter = \"nearest\"",
                    "liquid texture is not nearest filtered");
    }
    const std::string lava = material(fantasyMaterials, "Game/Vfx/Lava");
    requireText(lava, "shader = \"surface.lava\"",
                "lava still uses the generic liquid fragment shader");
    requireText(lava, "depth_write = true",
                "lava material does not write depth");
    requireText(lava, "filter = \"nearest\"",
                 "lava texture is not nearest filtered");

    const std::string ghost =
        material(editorMaterials, "Editor/PlacementGhost");
    require(!ghost.empty(), "placement ghost material is missing");
    requireText(ghost, "blend = \"alpha\"",
                "placement ghost is not transparent");
    requireText(ghost, "depth_write = false",
                "placement ghost incorrectly occludes committed geometry");
    requireText(ghost, "shader = \"editor.ghost\"",
                "placement ghost does not use its editor shader");
    requireText(placementGhost, "ghostColour",
                "placement ghost shader lacks tint and opacity control");

    std::cout << "VfxShaderAssetTests OK\n";
}
