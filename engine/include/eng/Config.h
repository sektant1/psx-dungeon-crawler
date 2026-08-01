#pragma once
#include <map>
#include <string>
#include <vector>

namespace eng {

// TOML config, flattened to dotted "section.key" leaves, at any depth --
// `[combat.fireball] speed = 18` reads back as "combat.fireball.speed". The
// [bindings] table is kept separately: action -> list of SDL key names (a
// binding value may be a string or an array of strings).
//
// Arrays are not stored: there is no vector getter to read one back with, so a
// caller that needs one parses the file itself (game/src/CombatConfig.cpp).
class Config
{
public:
    bool load(const std::string& path);

    std::string getString(const std::string& key, const std::string& def = {}) const;
    double getNumber(const std::string& key, double def = 0.0) const;
    bool getBool(const std::string& key, bool def = false) const;

    const std::map<std::string, std::vector<std::string>>& bindings() const
    {
        return mBindings;
    }

private:
    std::map<std::string, std::string> mStrings;
    std::map<std::string, double> mNumbers;
    std::map<std::string, bool> mBools;
    std::map<std::string, std::vector<std::string>> mBindings;
};

} // namespace eng
