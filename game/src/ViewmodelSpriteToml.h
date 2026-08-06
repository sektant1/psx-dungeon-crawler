#pragma once

#include "SpriteViewmodel.h"

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <string>

namespace game {

// One `[[...sprite_layer]]` row, from either file that can author one:
// `weapons.toml` (a weapon's own layers) and `viewmodel_hands.toml` (the shared
// hand layers). It is the same row in both, which is exactly why this is one
// function rather than a copy in each parser -- a key added to one and missed
// in the other would be an authoring difference nothing reports.
//
// Missing keys keep the struct's default. Validation is the caller's, through
// validViewmodelSpriteLayer: the two files differ in what they do with a bad
// row (a weapon is rejected, a hand layer is dropped), and only they know that.
//
// Kept out of SpriteViewmodel.h so that header stays free of toml++ -- it is
// pulled in by PlayerWeapons.h, and therefore by the editor and four tests.
inline ViewmodelSpriteLayer parseViewmodelSpriteLayer(const toml::table& row)
{
    const auto number = [&row](const char* key, float fallback) {
        return float(row[key].value_or(double(fallback)));
    };
    const auto vector2 = [&row](const char* key, glm::vec2 fallback) {
        const toml::array* values = row[key].as_array();
        if (!values || values->size() != 2)
            return fallback;
        return glm::vec2(float((*values)[0].value_or(double(fallback.x))),
                         float((*values)[1].value_or(double(fallback.y))));
    };

    ViewmodelSpriteLayer layer;
    layer.id = row["id"].value_or(std::string{});
    layer.material = row["material"].value_or(std::string{});
    layer.offset = vector2("offset", layer.offset);
    layer.size = vector2("size", layer.size);
    layer.distance = number("distance", layer.distance);
    if (const toml::array* grid = row["grid"].as_array();
        grid && grid->size() == 2) {
        layer.grid.x = int((*grid)[0].value_or(int64_t{layer.grid.x}));
        layer.grid.y = int((*grid)[1].value_or(int64_t{layer.grid.y}));
    }
    layer.idleFrame = int(row["idle_frame"].value_or(int64_t{layer.idleFrame}));
    layer.fireFrame = int(row["fire_frame"].value_or(int64_t{layer.fireFrame}));
    layer.fireFrameCount =
        int(row["fire_frame_count"].value_or(int64_t{layer.fireFrameCount}));
    layer.fireFps = number("fire_fps", layer.fireFps);
    return layer;
}

} // namespace game
