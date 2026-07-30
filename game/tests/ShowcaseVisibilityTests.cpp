#include "ShowcaseVisibility.h"
#include "DungeonGen.h"
#include "ShowroomMotion.h"

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "ShowcaseVisibilityTests: " << message << '\n';
        std::exit(1);
    }
}

bool near(float a, float b)
{
    return std::abs(a - b) < 0.001f;
}
} // namespace

int main()
{
    constexpr float range = 16.0f;
    constexpr float hysteresis = 2.0f;

    require(!showcaseVisibleAtDistance(
                ShowcaseVisibilityState::Uninitialized, 17.0f, range,
                hysteresis),
            "a 16m exhibit must start hidden at 17m");
    require(!showcaseVisibleAtDistance(
                ShowcaseVisibilityState::Uninitialized, 21.0f, 20.0f,
                hysteresis),
            "a 20m exhibit must start hidden at 21m");
    require(showcaseVisibleAtDistance(
                ShowcaseVisibilityState::Hidden, 16.0f, range, hysteresis),
            "a hidden exhibit must enter at its authored range");
    require(!showcaseVisibleAtDistance(
                ShowcaseVisibilityState::Hidden, 16.01f, range, hysteresis),
            "a hidden exhibit must not enter outside its authored range");
    require(showcaseVisibleAtDistance(
                ShowcaseVisibilityState::Visible, 17.0f, range, hysteresis),
            "a previously visible exhibit must remain visible at 17m");
    require(showcaseVisibleAtDistance(
                ShowcaseVisibilityState::Visible, 18.0f, range, hysteresis),
            "a visible exhibit must remain through the hysteresis band");
    require(!showcaseVisibleAtDistance(
                ShowcaseVisibilityState::Visible, 18.01f, range, hysteresis),
            "a visible exhibit must leave beyond the hysteresis band");
    require(showcaseVisibleAtDistance(
                ShowcaseVisibilityState::Uninitialized, 1000.0f, 0.0f,
                hysteresis),
            "an exhibit without an authored range must remain available");

    const std::string path =
        std::string(APP_ASSET_DIR) + "/showroom_exhibits.toml";
    const toml::parse_result parsed = toml::parse_file(path);
    require(bool(parsed), "showroom exhibit TOML must parse");
    const toml::array* exhibits = parsed.table()["exhibit"].as_array();
    require(exhibits != nullptr, "showroom must contain exhibits");

    const toml::parse_result showroom = toml::parse_file(
        std::string(APP_ASSET_DIR) + "/showroom.toml");
    require(bool(showroom), "showroom TOML must parse");
    const toml::array* rowNodes = showroom.table()["dungeon"]["rows"].as_array();
    require(rowNodes != nullptr, "showroom must contain dungeon rows");
    std::vector<std::string> rows;
    for (const toml::node& row : *rowNodes)
        rows.push_back(row.value_or(std::string()));
    const gen::Layout layout = gen::Layout::fromRows(std::move(rows), true);
    require(layout.valid(), "showroom layout must validate");
    const gen::Cell anchor = layout.anchor();
    const float cell = float(showroom.table()["dungeon"]["cell_size"]
                                 .value_or(4.0));
    const auto walkableAtWorld = [&](float x, float z) {
        const int col = int(std::floor(x / cell + float(anchor.col) + 0.5f));
        const int row = int(std::floor(z / cell + float(anchor.row) + 0.5f));
        return layout.walkable(col, row);
    };

    int rangedRoots = 0;
    int uncullablePaths = 0;
    int visibleAtSpawn = 0;
    std::set<std::string> ids;
    for (const toml::node& node : *exhibits) {
        const toml::table* exhibit = node.as_table();
        require(exhibit != nullptr, "every exhibit entry must be a table");
        const std::string id =
            (*exhibit)["id"].value_or(std::string());
        require(!id.empty() && ids.insert(id).second,
                "showroom exhibit ids must be non-empty and unique");
        const toml::array* position = (*exhibit)["position"].as_array();
        require(position && position->size() == 3,
                "showroom exhibits must have a three-component position");
        const float x = float((*position)[0].value_or(0.0));
        const float y = float((*position)[1].value_or(0.0));
        const float z = float((*position)[2].value_or(0.0));
        require(walkableAtWorld(x, z),
                "showroom exhibit must stand on a walkable cell");
        const bool pathStrip =
            id == "arrival_path" || id == "reliquary_path" ||
            id == "proving_path";
        const auto authoredRange =
            (*exhibit)["visibility_range"].value<double>();
        if (pathStrip) {
            require(!authoredRange,
                    "navigation path strips must not be distance culled");
            ++uncullablePaths;
            ++visibleAtSpawn;
            continue;
        }

        require(authoredRange.has_value(),
                "every authored gallery exhibit must have a visibility range");
        const float visibilityRange = float(*authoredRange);
        const bool liquid =
            id == "water_pool" || id == "water_plinth" ||
            id == "lava_pool" || id == "lava_plinth";
        require(near(visibilityRange, liquid ? 20.0f : 16.0f),
                "gallery ranges must be 16m, or 20m for liquid studies");
        ++rangedRoots;

        const float spawnX = float(layout.spawn().col - anchor.col) * cell;
        constexpr float spawnY = 1.6f;
        const float spawnZ = float(layout.spawn().row - anchor.row) * cell;
        const float dx = x - spawnX;
        const float dy = y - spawnY;
        const float dz = z - spawnZ;
        const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (showcaseVisibleAtDistance(
                ShowcaseVisibilityState::Uninitialized, distance,
                visibilityRange, hysteresis))
            ++visibleAtSpawn;
    }

    require(rangedRoots == 14,
            "all fourteen authored gallery roots must be range culled");
    require(uncullablePaths == 3,
            "all three navigation path strips must remain uncullable");
    require(visibleAtSpawn == 7,
            "spawn visibility budget must keep only pools and path strips");

    const std::set<std::string> requiredIds = {
        "fire_particles", "smoke_particles", "poison_particles",
        "lava_ash_particles", "arcane_enchantment", "fire_enchantment",
        "rain_volume", "frost_enchantment", "water_pool", "water_plinth",
        "lava_pool", "lava_plinth", "carved_column", "iron_obelisk",
        "arrival_path", "reliquary_path", "proving_path"};
    require(ids == requiredIds, "showroom must retain every feature exhibit");

    const toml::parse_result demo = toml::parse_file(DEMO_SCENE_TOML);
    require(bool(demo), "shared crystal scene must parse");
    const toml::table* crystals = demo.table()["crystals"].as_table();
    require(crystals && (*crystals)["spire"].as_array() &&
                (*crystals)["spire"].as_array()->size() == 4 &&
                (*crystals)["instance"].as_array() &&
                (*crystals)["instance"].as_array()->size() == 5,
            "showroom crystal ring must retain four spires and five clusters");
    require(std::filesystem::exists(
                std::string(APP_ASSET_DIR) + "/meshes/props/prop_chest.obj"),
            "showroom spinning chest mesh must exist");
    const game::showroom::TreasureMotion rest =
        game::showroom::treasureMotion(0.0f);
    const game::showroom::TreasureMotion moved =
        game::showroom::treasureMotion(1.0f);
    require(rest.position != moved.position &&
                std::fabs(glm::dot(rest.orientation, moved.orientation)) < 0.999f,
            "showroom chest must hover and rotate over time");
    std::cout << "ShowcaseVisibilityTests: OK\n";
    return 0;
}
