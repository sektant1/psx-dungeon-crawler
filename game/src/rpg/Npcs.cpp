#include "Npcs.h"

#include <eng/Log.h>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <algorithm>

namespace game::rpg {

bool NpcLibrary::parse(const void* tomlTable)
{
    const toml::table& root = *static_cast<const toml::table*>(tomlTable);
    const toml::table* group = root["npc"].as_table();
    if (!group) {
        eng::log::error("NpcLibrary: no [npc.*] tables");
        mNpcs.clear();
        return false;
    }

    decltype(mNpcs) built;
    for (const auto& [key, node] : *group) {
        const toml::table* t = node.as_table();
        if (!t)
            continue;
        auto def = std::make_shared<NpcDef>();
        def->id = std::string(key.str());
        def->name = (*t)["name"].value_or(def->id);
        def->role = (*t)["role"].value_or(std::string());
        def->greeting = (*t)["greeting"].value_or(std::string());
        def->dialogue = (*t)["dialogue"].value_or(std::string());
        def->trader = (*t)["trader"].value_or(std::string());
        def->mesh = (*t)["mesh"].value_or(std::string());
        def->material = (*t)["material"].value_or(std::string());
        def->scale = float((*t)["scale"].value_or(1.0));
        def->height = float((*t)["height"].value_or(1.8));
        // A person of height zero is a look target with no volume: aiming at
        // them would be impossible and the stand-in would be a disc on the
        // floor. Clamped rather than rejected, because the row is otherwise
        // fine and refusing it would delete somebody over a typo'd number.
        if (!(def->height > 0.1f)) {
            eng::log::error("NpcLibrary: npc '%s' has height %.2f; using 1.8",
                            def->id.c_str(), double(def->height));
            def->height = 1.8f;
        }
        if (!(def->scale > 0.0f))
            def->scale = 1.0f;
        built[def->id] = std::move(def);
    }
    mNpcs = std::move(built);
    return true;
}

bool NpcLibrary::load(const std::string& tomlPath)
{
    toml::parse_result parsed = toml::parse_file(tomlPath);
    if (!parsed) {
        eng::log::error("NpcLibrary: %s: %s", tomlPath.c_str(),
                        std::string(parsed.error().description()).c_str());
        mNpcs.clear();
        return false;
    }
    if (!parse(&parsed.table()))
        return false;
    mSourcePath = tomlPath;
    return true;
}

bool NpcLibrary::loadFromString(const char* tomlSrc)
{
    toml::parse_result parsed = toml::parse(tomlSrc);
    if (!parsed) {
        eng::log::error("NpcLibrary: %s",
                        std::string(parsed.error().description()).c_str());
        mNpcs.clear();
        return false;
    }
    mSourcePath.clear();
    return parse(&parsed.table());
}

NpcLibrary::Ref NpcLibrary::find(const std::string& id) const
{
    const auto it = mNpcs.find(id);
    return it == mNpcs.end() ? Ref{} : it->second;
}

std::vector<std::string> NpcLibrary::ids() const
{
    std::vector<std::string> out;
    out.reserve(mNpcs.size());
    for (const auto& [id, def] : mNpcs)
        out.push_back(id);
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace game::rpg
