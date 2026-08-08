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

// One rig, out of whichever table holds it: `[hands]` in the schema-1 file, or
// an element of `[[rig]]` in the schema-2 one. The two forms differ only in
// where they sit, so they must not differ in how they are read.
bool parseRig(const toml::table& hands, HandsDefinition& out)
{
    HandsDefinition parsed = out;
    parsed.id = hands["id"].value_or(parsed.id);
    parsed.displayName = hands["name"].value_or(parsed.displayName);
    parsed.skeleton = hands["skeleton"].value_or(parsed.skeleton);
    parsed.model = hands["model"].value_or(parsed.model);
    parsed.material = hands["material"].value_or(parsed.material);
    parsed.idleAnimation =
        hands["idle_animation"].value_or(parsed.idleAnimation);
    parsed.bundledWeapon =
        hands["bundled_weapon"].value_or(parsed.bundledWeapon);
    // Framing is all-or-nothing: a rig that states one of the three states the
    // set, so a half-authored override cannot inherit a global that was tuned
    // for a different rig's proportions.
    if (hands["offset"] || hands["rotation"] || hands["scale"]) {
        parsed.hasFraming = true;
        parsed.framingOffset =
            vector3(*hands.as_table(), "offset", parsed.framingOffset);
        parsed.framingRotationDegrees = vector3(
            *hands.as_table(), "rotation", parsed.framingRotationDegrees);
        parsed.framingScale =
            number(*hands.as_table(), "scale", parsed.framingScale);
    }

    // An authored socket array replaces the defaults rather than merging with
    // them: half a vocabulary is the confusing case, where a weapon names a
    // socket the file appears not to define and the game finds one anyway.
    if (const toml::array* sockets = hands["socket"].as_array()) {
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
    if (const toml::array* layers = hands["sprite_layer"].as_array()) {
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

bool parseTable(const toml::table& root, HandsDefinition& out)
{
    const toml::table* hands = root["hands"].as_table();
    if (!hands)
        return true; // no section is not an error: the default is the ship
    return parseRig(*hands, out);
}

bool parseLibraryTable(const toml::table& root, HandsLibrary& out)
{
    HandsLibrary parsed;
    parsed.defaultRig = root["default_rig"].value_or(std::string{});

    if (const toml::array* rigs = root["rig"].as_array()) {
        for (const toml::node& node : *rigs) {
            const toml::table* table = node.as_table();
            if (!table)
                return false;
            // Each rig starts from the shipped default, so a file may state
            // only what differs -- and a rig that names no material still has
            // one rather than failing validation.
            HandsDefinition rig = defaultHandsDefinition();
            if (!parseRig(*table, rig))
                return false;
            if (rig.id.empty())
                return false;
            for (const HandsDefinition& existing : parsed.rigs)
                if (existing.id == rig.id)
                    return false; // two rigs answering to one id
            parsed.rigs.push_back(std::move(rig));
        }
    }

    // The single-rig form. Read only when there is no array, so a schema-2 file
    // that also happens to carry a legacy [hands] block does not end up with a
    // sixteenth rig nobody declared.
    if (parsed.rigs.empty()) {
        HandsDefinition single = defaultHandsDefinition();
        if (!parseTable(root, single))
            return false;
        if (single.id.empty())
            single.id = "hands";
        parsed.rigs.push_back(std::move(single));
    }

    if (!parsed.defaultRig.empty() && !parsed.find(parsed.defaultRig))
        return false; // a default naming a rig that is not in the file

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

const HandsDefinition* HandsLibrary::find(std::string_view id) const
{
    for (const HandsDefinition& rig : rigs)
        if (rig.id == id)
            return &rig;
    return nullptr;
}

const HandsDefinition& HandsLibrary::active() const
{
    if (const HandsDefinition* named = find(defaultRig))
        return *named;
    if (!rigs.empty())
        return rigs.front();
    // A library with no rigs at all can still hand back something loadable, so
    // an empty or unreadable file costs the player their choice of hands rather
    // than their hands.
    static const HandsDefinition fallback = defaultHandsDefinition();
    return fallback;
}

std::vector<std::string> HandsLibrary::ids() const
{
    std::vector<std::string> out;
    out.reserve(rigs.size());
    for (const HandsDefinition& rig : rigs)
        out.push_back(rig.id);
    return out;
}

bool parseHandsLibrary(const char* tomlSource, HandsLibrary& out)
{
    const toml::parse_result result = toml::parse(tomlSource);
    if (!result)
        return false;
    return parseLibraryTable(result.table(), out);
}

bool loadHandsLibrary(const std::string& tomlPath, HandsLibrary& out)
{
    const toml::parse_result result = toml::parse_file(tomlPath);
    if (!result) {
        eng::log::warn("Hands library: %s could not be parsed",
                       tomlPath.c_str());
        return false;
    }
    if (!parseLibraryTable(result.table(), out)) {
        eng::log::warn("Hands library: %s was rejected", tomlPath.c_str());
        return false;
    }
    return true;
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
