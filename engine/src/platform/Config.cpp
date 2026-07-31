#include <eng/Config.h>

#include <eng/Log.h>

#include <functional>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

namespace eng {

bool Config::load(const std::string& path)
{
    toml::parse_result result = toml::parse_file(path);
    if (!result) {
        log::error("Config: failed to parse %s: %s", path.c_str(),
                   std::string(result.error().description()).c_str());
        return false;
    }

    // Recursive, because TOML sub-tables nest arbitrarily and this used to
    // flatten exactly one level: `[combat.fireball]` arrived here as a *table*,
    // warned "unsupported value type", and every key under it -- speed, radius,
    // ttl -- was silently unreachable through this API. Four warnings per boot
    // for values that were fine, which is how a log teaches you to ignore it.
    //
    // Arrays stay unstored on purpose: there is no vector getter to read them
    // back with, so a caller that needs one (CombatConfig's light_colour) parses
    // the file itself. Warning about them would be noise, not signal.
    std::function<void(const std::string&, const toml::node&)> storeLeaf =
        [this, &storeLeaf](const std::string& key, const toml::node& n) {
            if (auto s = n.as_string())
                mStrings[key] = s->get();
            else if (auto f = n.as_floating_point())
                mNumbers[key] = f->get();
            else if (auto i = n.as_integer())
                mNumbers[key] = static_cast<double>(i->get());
            else if (auto b = n.as_boolean())
                mBools[key] = b->get();
            else if (const toml::table* nested = n.as_table()) {
                for (auto&& [nk, nv] : *nested)
                    storeLeaf(key + "." + std::string(nk.str()), nv);
            } else if (!n.is_array())
                log::warn("Config: unsupported value type for key '%s'",
                          key.c_str());
        };

    for (auto&& [k, v] : result.table()) {
        const std::string key(k.str());
        if (key == "bindings") {
            const toml::table* tbl = v.as_table();
            if (!tbl)
                continue;
            for (auto&& [bk, bv] : *tbl) {
                std::vector<std::string>& keys = mBindings[std::string(bk.str())];
                keys.clear(); // replace on re-load, don't append
                if (auto s = bv.as_string())
                    keys.push_back(s->get());
                else if (auto arr = bv.as_array())
                    for (auto&& e : *arr)
                        if (auto es = e.as_string())
                            keys.push_back(es->get());
            }
        } else if (const toml::table* tbl = v.as_table()) {
            for (auto&& [sk, sv] : *tbl)
                storeLeaf(key + "." + std::string(sk.str()), sv);
        } else {
            storeLeaf(key, v);
        }
    }
    return true;
}

std::string Config::getString(const std::string& key, const std::string& def) const
{
    auto it = mStrings.find(key);
    return it != mStrings.end() ? it->second : def;
}

double Config::getNumber(const std::string& key, double def) const
{
    auto it = mNumbers.find(key);
    return it != mNumbers.end() ? it->second : def;
}

bool Config::getBool(const std::string& key, bool def) const
{
    auto it = mBools.find(key);
    return it != mBools.end() ? it->second : def;
}

} // namespace eng
