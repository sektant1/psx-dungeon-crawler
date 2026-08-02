#include <eng/render/Enchantment.h>

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "EnchantmentTests: " << message << '\n';
        std::exit(1);
    }
}

bool equal(glm::vec3 a, glm::vec3 b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

bool equal(glm::vec4 a, glm::vec4 b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}

std::string read(const std::string& relativePath)
{
    std::ifstream file(std::string(PROJECT_SOURCE_DIR) + "/" + relativePath);
    // Named, so a moved asset reports its own path instead of leaving the
    // reader to guess which of the four this was.
    require(bool(file),
            ("required enchantment asset could not be opened: " + relativePath)
                .c_str());
    return {std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>()};
}

void requireText(const std::string& text, const char* expected,
                 const char* message)
{
    require(text.find(expected) != std::string::npos, message);
}

void requireNoText(const std::string& text, const char* forbidden,
                   const char* message)
{
    require(text.find(forbidden) == std::string::npos, message);
}

} // namespace

int main()
{
    using namespace eng;

    {
        const EnchantmentDesc desc;
        require(equal(desc.palette.colour,
                      glm::vec4(0.62f, 0.32f, 1.0f, 0.68f)),
                "default palette colour changed");
        require(equal(desc.palette.scrollDirection, glm::vec3(1.0f)),
                "default palette scroll direction changed");
        require(desc.strength == 1.0f, "default strength changed");
        require(desc.runeScale == 3.5f, "default rune scale changed");
        require(equal(desc.scroll, glm::vec3(0.12f, 0.20f, 0.08f)),
                "default object-space scroll changed");
        require(desc.pulseSpeed == 3.1f, "default pulse speed changed");
        require(desc.pulseDepth == 0.18f, "default pulse depth changed");
        require(desc.edgeIntensity == 0.35f,
                "default edge intensity changed");
        require(desc.bandCount == 4.0f,
                "default enchant band count changed");
        require(desc.pixelScale == 18.0f,
                "default enchant pixel scale changed");
        require(desc.coreBoost == 1.35f,
                "default enchant core boost changed");
        require(desc.recursive, "enchantments no longer recurse by default");
    }

    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float infinity = std::numeric_limits<float>::infinity();
        EnchantmentDesc invalid;
        invalid.palette.colour = {nan, 9.0f, infinity, 0.5f};
        invalid.palette.scrollDirection = {-9.0f, nan, 1.0f};
        invalid.strength = nan;
        invalid.runeScale = -infinity;
        invalid.scroll = {nan, 2.0f, infinity};
        invalid.pulseSpeed = -4.0f;
        invalid.pulseDepth = 8.0f;
        invalid.edgeIntensity = -2.0f;
        invalid.bandCount = 99.0f;
        invalid.pixelScale = -4.0f;
        invalid.coreBoost = std::numeric_limits<float>::quiet_NaN();
        invalid.recursive = false;

        const EnchantmentDesc clean = sanitizeEnchantmentDesc(invalid);
        const EnchantmentPalette defaults;
        require(clean.palette.colour.r == defaults.colour.r,
                "non-finite palette channel did not use the default");
        require(clean.palette.colour.g == 4.0f,
                "out-of-range palette channel was not clamped");
        require(clean.palette.colour.a == 0.5f,
                "a valid palette channel was not preserved");
        require(clean.palette.scrollDirection.x == -4.0f,
                "out-of-range scroll axis was not clamped");
        require(clean.palette.scrollDirection.y == defaults.scrollDirection.y,
                "non-finite scroll axis did not use the default");
        require(clean.strength == 1.0f,
                "non-finite strength did not use the default");
        require(clean.runeScale == 3.5f,
                "non-finite rune scale did not use the default");
        require(equal(clean.scroll, glm::vec3(0.12f, 2.0f, 0.08f)),
                "scroll sanitization did not preserve finite components");
        require(clean.pulseSpeed == 0.0f,
                "negative pulse speed was not clamped");
        require(clean.pulseDepth == 1.0f,
                "pulse depth was not clamped to a modulation fraction");
        require(clean.edgeIntensity == 0.0f,
                "negative edge intensity was not clamped");
        require(clean.bandCount == 8.0f,
                "enchant band count was not clamped");
        require(clean.pixelScale == 4.0f,
                "enchant pixel scale was not clamped");
        require(clean.coreBoost == 1.35f,
                "non-finite core boost did not use the default");
        require(!clean.recursive, "sanitization changed recursion intent");

        EnchantmentDesc excessive;
        excessive.strength = 20.0f;
        excessive.runeScale = 1000.0f;
        excessive.pulseSpeed = infinity;
        excessive.edgeIntensity = 20.0f;
        const EnchantmentDesc bounded = sanitizeEnchantmentDesc(excessive);
        require(bounded.strength == 2.0f, "strength upper bound changed");
        require(bounded.runeScale == 64.0f, "rune scale upper bound changed");
        require(bounded.pulseSpeed == 3.1f,
                "non-finite pulse speed did not use the default");
        require(bounded.edgeIntensity == 4.0f,
                "edge intensity upper bound changed");
    }

    {
        // A palette travels through the desc untouched when it is already
        // valid: the engine has no table of its own to fall back to.
        EnchantmentDesc desc;
        desc.palette.colour = {0.1f, 0.2f, 0.3f, 0.4f};
        desc.palette.scrollDirection = {-1.0f, 0.5f, 2.0f};
        const EnchantmentDesc clean = sanitizeEnchantmentDesc(desc);
        require(equal(clean.palette.colour, desc.palette.colour),
                "a valid palette colour was modified");
        require(equal(clean.palette.scrollDirection, desc.palette.scrollDirection),
                "a valid scroll direction was modified");
    }

    {
        const std::vector<std::vector<int>> children{{1, 2}, {3}, {}, {}};
        const auto getChildren = [&](int node) { return children.at(node); };
        const std::vector<int> direct =
            collectEnchantmentTargets(0, false, getChildren);
        const std::vector<int> recursive =
            collectEnchantmentTargets(0, true, getChildren);
        require(direct == std::vector<int>{0},
                "nonrecursive enchantment escaped the target node");
        require(recursive == std::vector<int>({0, 1, 2, 3}),
                "recursive enchantment did not visit the complete subtree once");
    }

    {
        EnchantmentBookkeeping<int, int> states;
        require(states.size() == 0,
                "new enchantment bookkeeping is not empty");
        require(!states.replace(7, {"stone", "enchantment_1", 1}),
                "first enchantment unexpectedly replaced state");
        const auto* first = states.find(7);
        require(first && first->baseMaterial == "stone" &&
                    first->generatedMaterial == "enchantment_1" &&
                    first->owner == 1,
                "enchantment material state was not recorded");
        require(states.shouldClear(7, 9, 9),
                "direct attachment cannot be cleared");
        require(states.shouldClear(7, 4, 1),
                "recursive owner cannot clear its descendant");
        require(!states.shouldClear(7, 4, 9),
                "unrelated ancestor clears independently owned enchantment");

        const auto replaced =
            states.replace(7, {"marble", "enchantment_2", 2});
        require(replaced && replaced->baseMaterial == "stone" &&
                    replaced->generatedMaterial == "enchantment_1",
                "reapplication did not return the prior state for cleanup");
        require(states.size() == 1 &&
                    states.containsGeneratedMaterial("enchantment_2") &&
                    !states.containsGeneratedMaterial("enchantment_1"),
                "reapplication stacked duplicate enchantment state");

        const auto restored = states.take(7);
        require(restored && restored->baseMaterial == "marble" &&
                    restored->generatedMaterial == "enchantment_2" &&
                    states.size() == 0,
                "clearing did not return and remove restoration state");

        states.replace(3, {"wood", "enchantment_3", 3});
        states.replace(4, {"iron", "enchantment_4", 4});
        const auto all = states.takeAll();
        require(all.size() == 2 && states.size() == 0,
                "scene cleanup did not drain generated material state");
    }

    const std::string vertex =
        read("assets/shaders/enchantment.vert");
    const std::string fragment =
        read("assets/shaders/enchantment.frag");
    const std::string program =
        read("assets/programs/enchantment.program");
    const std::string legacyRenderer = read("engine/src/render/Renderer.cpp");
    const std::string rhiRenderer = read("engine/src/render/rhi/Renderer.cpp");

    requireText(vertex, "in vec3 normal",
                "vertex shader does not consume object-space normals");
    requireText(vertex, "out vec3 objectPosition",
                "vertex shader does not pass object-space position");
    requireText(vertex, "out vec3 objectNormal",
                "vertex shader does not pass object-space normal");
    requireNoText(vertex, "uv0",
                  "vertex shader still depends on model UVs");
    requireNoText(fragment, "uv0",
                  "fragment shader still depends on model UVs");
    requireText(fragment, "triplanarWeights",
                "fragment shader lacks normal-weighted triplanar projection");
    requireText(fragment, "floor(",
                "fragment shader lacks quantized rune cells");
    requireText(fragment, "quantizeBand",
                "fragment shader lacks intensity banding");
    requireText(fragment, "runeBody",
                "fragment shader lacks a readable rune body");
    requireText(fragment, "runeCore",
                "fragment shader lacks a separate bloom core");
    requireText(fragment, "cameraPositionObject",
                "fragment shader lacks object-space view direction");
    requireNoText(fragment, "fragNormalDepth",
                  "enchantment pass still writes normal/depth metadata");

    for (const char* uniform :
         {"enchantColour", "enchantStrength", "enchantRuneScale",
          "enchantScroll", "enchantPulseSpeed", "enchantPulseDepth",
          "enchantEdgeIntensity", "enchantBandCount",
          "enchantPixelScale", "enchantCoreBoost",
          "time", "cameraPositionObject"}) {
        requireText(fragment, uniform,
                    "fragment shader is missing a required uniform");
        requireText(program, uniform,
                    "program declaration is missing a shader uniform");
    }
    for (const char* uniform :
         {"enchantColour", "enchantStrength", "enchantRuneScale",
          "enchantScroll", "enchantPulseSpeed", "enchantPulseDepth",
          "enchantEdgeIntensity", "enchantBandCount",
          "enchantPixelScale", "enchantCoreBoost"}) {
        requireText(legacyRenderer, uniform,
                    "renderer does not bind a required enchantment uniform");
    }
    requireText(program, "param_named_auto time time",
                "program does not bind renderer time");
    requireText(program,
                "param_named_auto cameraPositionObject "
                "camera_position_object_space",
                "program does not bind object-space camera position");
    requireText(legacyRenderer, "setVertexProgram(\"Enchantment/VS\")",
                "renderer does not reference the enchantment vertex program");
    requireText(legacyRenderer, "setFragmentProgram(\"Enchantment/FS\")",
                "renderer does not reference the enchantment fragment program");
    // Backend-independent contract: the Vulkan cut preserves enchantment
    // state in draw constants instead of testing Ogre pass implementation.
    requireText(rhiRenderer, "constants.tintOpacity",
                "RHI renderer does not apply enchantment tint/opacity");
    requireText(rhiRenderer, "constants.rimColourStrength",
                "RHI renderer does not apply enchantment rim state");

    std::cout << "EnchantmentTests OK\n";
}
