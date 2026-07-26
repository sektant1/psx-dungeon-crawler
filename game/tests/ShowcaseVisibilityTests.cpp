#include "ShowcaseVisibility.h"

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

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
        std::string(APP_ASSET_DIR) + "/lobby_showcase.toml";
    const toml::parse_result parsed = toml::parse_file(path);
    require(bool(parsed), "lobby showcase TOML must parse");
    const toml::array* exhibits = parsed.table()["exhibit"].as_array();
    require(exhibits != nullptr, "lobby showcase must contain exhibits");

    int rangedRoots = 0;
    int uncullablePaths = 0;
    int visibleAtSpawn = 0;
    for (const toml::node& node : *exhibits) {
        const toml::table* exhibit = node.as_table();
        require(exhibit != nullptr, "every exhibit entry must be a table");
        const std::string id =
            (*exhibit)["id"].value_or(std::string());
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

        const toml::array* position = (*exhibit)["position"].as_array();
        require(position && position->size() == 3,
                "ranged exhibits must have a three-component position");
        const float x = float((*position)[0].value_or(0.0));
        const float y = float((*position)[1].value_or(0.0));
        const float z = float((*position)[2].value_or(0.0));
        constexpr float spawnX = 0.0f;
        constexpr float spawnY = 1.6f;
        constexpr float spawnZ = 28.0f;
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
    std::cout << "ShowcaseVisibilityTests: OK\n";
    return 0;
}
