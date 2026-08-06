#include "HandsDefinition.h"

#include "ViewmodelSpriteToml.h"

#include <eng/Log.h>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <utility>

namespace game {
namespace {

float number(const toml::table& table, const char* key, float fallback)
{
    return float(table[key].value_or(double(fallback)));
}

glm::vec3 vector3(const toml::table& table, const char* key,
                  glm::vec3 fallback)
{
    const toml::array* values = table[key].as_array();
    if (!values || values->size() != 3)
        return fallback;
    return {float((*values)[0].value_or(double(fallback.x))),
            float((*values)[1].value_or(double(fallback.y))),
            float((*values)[2].value_or(double(fallback.z)))};
}

bool parseTable(const toml::table& root, HandsDefinition& out)
{
    const toml::table* hands = root["hands"].as_table();
    if (!hands)
        return true; // no section is not an error: the default is the ship

    HandsDefinition parsed = out;
    parsed.skeleton = (*hands)["skeleton"].value_or(parsed.skeleton);
    parsed.model = (*hands)["model"].value_or(parsed.model);
    parsed.material = (*hands)["material"].value_or(parsed.material);
    parsed.idleAnimation =
        (*hands)["idle_animation"].value_or(parsed.idleAnimation);

    // An authored socket array replaces the defaults rather than merging with
    // them: half a vocabulary is the confusing case, where a weapon names a
    // socket the file appears not to define and the game finds one anyway.
    if (const toml::array* sockets = (*hands)["socket"].as_array()) {
        parsed.sockets.clear();
        for (const toml::node& node : *sockets) {
            const toml::table* table = node.as_table();
            if (!table)
                return false;
            ViewmodelSocketDef socket;
            socket.name = (*table)["name"].value_or(std::string{});
            socket.joint = (*table)["joint"].value_or(std::string{});
            socket.offset = vector3(*table, "offset", socket.offset);
            socket.rotationDegrees =
                vector3(*table, "rotation", socket.rotationDegrees);
            socket.scale = number(*table, "scale", socket.scale);
            parsed.sockets.push_back(std::move(socket));
        }
    }

    // Same replace-don't-merge rule the sockets follow, for the same reason.
    if (const toml::array* layers = (*hands)["sprite_layer"].as_array()) {
        parsed.spriteLayers.clear();
        for (const toml::node& node : *layers) {
            const toml::table* table = node.as_table();
            if (!table)
                return false;
            parsed.spriteLayers.push_back(parseViewmodelSpriteLayer(*table));
        }
    }

    if (!validHandsDefinition(parsed))
        return false;
    out = std::move(parsed);
    return true;
}

} // namespace

HandsDefinition defaultHandsDefinition()
{
    HandsDefinition hands;
    // The two grips and the fingertips the finger-gun weapons already fire
    // from, so the shipped weapons keep working with no TOML at all.
    //
    // Every offset is zero on purpose: a socket names a place on the skeleton,
    // and the weapon's own hands_muzzle_offset / attach_offset is the nudge.
    // Splitting the same distance across both would double it the moment a
    // weapon moved from a raw joint name to the socket, which is exactly the
    // migration the shipped loadout is doing.
    hands.sockets = {
        {"right_hand", "hand.R", {}, {}, 1.0f},
        {"left_hand", "hand.L", {}, {}, 1.0f},
        {"right_index_tip", "f_index.03.R", {}, {}, 1.0f},
        {"right_middle_tip", "f_middle.03.R", {}, {}, 1.0f},
    };
    return hands;
}

bool validHandsDefinition(const HandsDefinition& hands)
{
    if (hands.skeleton.empty() || hands.model.empty() ||
        hands.material.empty() || hands.idleAnimation.empty())
        return false;
    for (std::size_t i = 0; i < hands.sockets.size(); ++i) {
        if (!validViewmodelSocket(hands.sockets[i]))
            return false;
        for (std::size_t j = i + 1; j < hands.sockets.size(); ++j)
            if (hands.sockets[i].name == hands.sockets[j].name)
                return false;
    }
    // Hand sprite layers are held to the same bar as a weapon's. A bad row here
    // would otherwise be dropped silently at build time and read as hands that
    // simply did not appear.
    for (const ViewmodelSpriteLayer& layer : hands.spriteLayers)
        if (!validViewmodelSpriteLayer(layer))
            return false;
    return true;
}

bool parseHandsDefinition(const char* tomlSource, HandsDefinition& out)
{
    const toml::parse_result result = toml::parse(tomlSource);
    if (!result)
        return false;
    return parseTable(result.table(), out);
}

bool loadHandsDefinition(const std::string& tomlPath, HandsDefinition& out)
{
    const toml::parse_result result = toml::parse_file(tomlPath);
    if (!result) {
        eng::log::warn("Hands definition: %s could not be parsed",
                       tomlPath.c_str());
        return false;
    }
    if (!parseTable(result.table(), out)) {
        eng::log::warn("Hands definition: %s was rejected", tomlPath.c_str());
        return false;
    }
    return true;
}

} // namespace game
