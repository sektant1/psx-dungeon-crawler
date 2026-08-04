#include <eng/assets/AssetName.h>

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

using namespace eng::assets;

static void require(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "AssetNameTests: " << message << '\n';
        std::exit(1);
    }
}

// The name is accepted.
static void accepts(AssetNameKind kind, std::string_view name,
                    const std::string& why)
{
    const std::optional<AssetNameIssue> issue = validateAssetName(kind, name);
    if (issue) {
        std::cerr << "AssetNameTests: " << why << " -- but '" << name
                  << "' was reported as " << issue->code << ": "
                  << issue->message << '\n';
        std::exit(1);
    }
}

// The name is reported, AND for the stated reason.
//
// Naming the code rather than asserting "something was wrong" is what makes
// this a contract rather than a smoke test: a validator that rejects
// "Game/PropTerracotta" for having a lowercase segment would pass a bare
// has_value() check while saying something false about the name. The codes are
// what tools show the author, so they are part of the interface.
static void reports(AssetNameKind kind, std::string_view name,
                    const std::string& code, const std::string& why)
{
    const std::optional<AssetNameIssue> issue = validateAssetName(kind, name);
    if (!issue) {
        std::cerr << "AssetNameTests: " << why << " -- but '" << name
                  << "' was accepted\n";
        std::exit(1);
    }
    if (issue->code != code) {
        std::cerr << "AssetNameTests: " << why << " -- '" << name
                  << "' was reported as " << issue->code << ", expected "
                  << code << '\n';
        std::exit(1);
    }
}

int main()
{
    accepts(AssetNameKind::LocalId, "hollow_soldier",
            "local ids accept lower snake case");
    reports(AssetNameKind::LocalId, "Hollow Soldier", "id.local",
            "display text is not a stable id");
    accepts(AssetNameKind::QualifiedId, "game.particle.fireball_trail",
            "qualified ids carry registry ownership");
    reports(AssetNameKind::QualifiedId, "fireball_trail", "id.namespace",
            "qualified ids require a namespace");
    accepts(AssetNameKind::RuntimePath, "textures/vfx/flame_01.png",
            "runtime paths are relative lower snake case");
    reports(AssetNameKind::RuntimePath, "textures/vfx/Flame 01.PNG",
            "path.filename",
            "runtime paths reject platform-sensitive spelling");
    accepts(AssetNameKind::MaterialId, "Game/Kit/DungeonTwoSided",
            "materials use three PascalCase segments");
    reports(AssetNameKind::MaterialId, "Game/PropTerracotta", "material.shape",
            "flattened material ids are reported");
    accepts(AssetNameKind::ShaderPath, "shaders/particle_sprite.vert",
            "shader paths use stage extensions");
    reports(AssetNameKind::ShaderPath, "shaders/particle_sprite.txt",
            "shader.extension", "non-shader extensions are rejected");

    require(canonicalToken("AC Vixen-Part 02") == "ac_vixen_part_02",
            "producer canonicalization handles source spelling");
    require(canonicalToken("9 Slice") == "asset_9_slice",
            "tokens cannot begin with a digit");
    require(friendlyAssetLabel("game.particle.fireball_trail") ==
                "Fireball Trail",
            "qualified ids get concise tool labels");
    require(friendlyAssetLabel("Game/Kit/DungeonTwoSided") ==
                "Dungeon Two Sided",
            "material names split PascalCase for display");
    require(friendlyAssetLabel("textures/vfx/flame_01.png") == "Flame 01",
            "runtime paths hide directories and extensions in tool labels");

    std::cout << "AssetNameTests: ok\n";
    return 0;
}
