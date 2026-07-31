#pragma once

#include <string>

namespace game {

// Presentation metadata for a placeable prop, straight out of
// dungeon_props.toml. It lives in its own header so the tooltip builder can be
// unit-tested without dragging in the whole dungeon builder.
struct PropInfo {
    std::string id;
    std::string displayName;
    std::string category;
    std::string description;
    std::string rarity;  // common | uncommon | rare | arcane
    std::string interact; // verb for the key prompt; empty when inert
};

} // namespace game
