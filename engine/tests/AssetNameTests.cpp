#include <eng/assets/AssetName.h>

#include <cstdlib>
#include <iostream>
#include <string>

using namespace eng::assets;

static void require(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "AssetNameTests: " << message << '\n';
        std::exit(1);
    }
}

int main()
{
    require(!validateAssetName(AssetNameKind::LocalId, "hollow_soldier"),
            "local ids accept lower snake case");
    require(validateAssetName(AssetNameKind::LocalId, "Hollow Soldier"),
            "display text is not a stable id");
    require(!validateAssetName(AssetNameKind::QualifiedId,
                               "game.particle.fireball_trail"),
            "qualified ids carry registry ownership");
    require(validateAssetName(AssetNameKind::QualifiedId, "fireball_trail"),
            "qualified ids require a namespace");
    require(!validateAssetName(AssetNameKind::RuntimePath,
                               "textures/vfx/flame_01.png"),
            "runtime paths are relative lower snake case");
    require(validateAssetName(AssetNameKind::RuntimePath,
                              "textures/vfx/Flame 01.PNG"),
            "runtime paths reject platform-sensitive spelling");
    require(!validateAssetName(AssetNameKind::MaterialId,
                               "Game/Kit/DungeonTwoSided"),
            "materials use three PascalCase segments");
    require(validateAssetName(AssetNameKind::MaterialId, "Game/PropTerracotta"),
            "flattened material ids are reported");
    require(!validateAssetName(AssetNameKind::ShaderPath,
                               "shaders/particle_sprite.vert"),
            "shader paths use stage extensions");
    require(validateAssetName(AssetNameKind::ShaderPath,
                              "shaders/particle_sprite.txt"),
            "non-shader extensions are rejected");

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
