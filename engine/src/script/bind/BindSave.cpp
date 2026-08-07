#include "script/bind/Bindings.h"

#include <eng/Log.h>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>

namespace eng::script {
namespace {

// One saved value. Three types, because that is what a save file is: a number,
// a flag, or a name. Anything structured a game wants to persist decomposes
// into these, and a format that could hold more would be a format nobody can
// read in a text editor when a player's save breaks.
struct Value {
    enum class Type : char { Number = 'n', Bool = 'b', String = 's' };
    Type type = Type::Number;
    double number = 0.0;
    bool boolean = false;
    std::string text;
};

// Loaded once on bind and held for the life of the state. A save file read on
// every `save.get` would turn a lookup in a hot loop into a file read, and the
// in-memory copy is the authority anyway: commit() writes it out.
struct Store {
    std::map<std::string, Value> values;
    std::string path;
    bool dirty = false;
};

// `key<TAB>type<TAB>value` per line. Plain text and hand-editable on purpose:
// the first thing anybody does with a save system is corrupt a save and need
// to look at it. Tab-separated because a key is authored and a value may
// contain spaces.
void load(Store& store)
{
    store.values.clear();
    std::ifstream in(store.path);
    if (!in)
        return; // a missing file is a new game, not an error

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty())
            continue;
        const std::size_t first = line.find('\t');
        if (first == std::string::npos)
            continue;
        const std::size_t second = line.find('\t', first + 1);
        if (second == std::string::npos)
            continue;
        const std::string key = line.substr(0, first);
        const char type = line[first + 1];
        const std::string raw = line.substr(second + 1);

        Value v;
        switch (type) {
        case 'b':
            v.type = Value::Type::Bool;
            v.boolean = raw == "1";
            break;
        case 's':
            v.type = Value::Type::String;
            v.text = raw;
            break;
        default:
            v.type = Value::Type::Number;
            v.number = std::strtod(raw.c_str(), nullptr);
            break;
        }
        store.values[key] = std::move(v);
    }
}

bool commit(Store& store)
{
    std::error_code ec;
    const std::filesystem::path file(store.path);
    if (file.has_parent_path())
        std::filesystem::create_directories(file.parent_path(), ec);

    // Through a temporary and then renamed, so a crash mid-write leaves the
    // previous save intact rather than a truncated one. Losing a save to a
    // power cut is forgivable; losing it to our own file handling is not.
    const std::filesystem::path temp = file.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::trunc);
        if (!out) {
            log::error("Script: save could not be written to %s",
                       store.path.c_str());
            return false;
        }
        for (const auto& [key, v] : store.values) {
            out << key << '\t' << char(v.type) << '\t';
            switch (v.type) {
            case Value::Type::Bool:   out << (v.boolean ? '1' : '0'); break;
            case Value::Type::String: out << v.text; break;
            case Value::Type::Number:
                // Enough digits to round-trip a double exactly. The default is
                // six significant figures, which turned save.set("score",
                // 1234567) into 1.23457e+06 and read it back as 1234570 --
                // silently wrong for scores, ids, timestamps and positions.
                out << std::setprecision(17) << v.number;
                break;
            }
            out << '\n';
        }
        if (!out) {
            log::error("Script: save failed while writing %s",
                       store.path.c_str());
            return false;
        }
    }
    std::filesystem::rename(temp, file, ec);
    if (ec) {
        log::error("Script: save could not replace %s: %s", store.path.c_str(),
                   ec.message().c_str());
        return false;
    }
    store.dirty = false;
    return true;
}

} // namespace

void bindSave(sol::state& lua, const std::string& path)
{
    // Shared by the closures below and destroyed with the state.
    auto store = std::make_shared<Store>();
    store->path = path;
    load(*store);

    sol::table s = lua.create_named_table("save");

    // The default is returned for a missing key AND for a key of a different
    // type. A save written by an older build that stored a number where this
    // one wants a string should read as "not set", not as garbage.
    s["get"] = [store](const std::string& key, sol::object fallback,
                       sol::this_state ts) -> sol::object {
        sol::state_view lv(ts);
        const auto it = store->values.find(key);
        if (it == store->values.end())
            return fallback;
        // A stored value of a different type than the caller expects reads as
        // "not set". A save written by an older build that put a number where
        // this one wants a string would otherwise hand Lua a number, and the
        // concat one line later would be a runtime error in somebody's game.
        // The fallback's type is the question being asked.
        const bool wants =
            (fallback.is<bool>() && it->second.type == Value::Type::Bool) ||
            (fallback.is<std::string>() &&
             it->second.type == Value::Type::String) ||
            (!fallback.is<bool>() && !fallback.is<std::string>() &&
             fallback.is<double>() && it->second.type == Value::Type::Number) ||
            !fallback.valid() || fallback == sol::lua_nil;
        if (!wants)
            return fallback;
        switch (it->second.type) {
        case Value::Type::Bool:
            return sol::make_object(lv, it->second.boolean);
        case Value::Type::String:
            return sol::make_object(lv, it->second.text);
        case Value::Type::Number:
            return sol::make_object(lv, it->second.number);
        }
        return fallback;
    };

    s["set"] = [store](const std::string& key, sol::object value) {
        Value v;
        if (value.is<bool>()) {
            v.type = Value::Type::Bool;
            v.boolean = value.as<bool>();
        } else if (value.is<double>()) {
            v.type = Value::Type::Number;
            v.number = value.as<double>();
        } else if (value.is<std::string>()) {
            v.type = Value::Type::String;
            v.text = value.as<std::string>();
        } else {
            log::warn("Script: save.set('%s') takes a number, a boolean or a "
                      "string", key.c_str());
            return;
        }
        store->values[key] = std::move(v);
        store->dirty = true;
    };

    s["has"] = [store](const std::string& key) {
        return store->values.find(key) != store->values.end();
    };

    // Explicit, not automatic. Writing on every set would put a file write in
    // whatever loop the script is in; a game decides for itself what a
    // checkpoint is.
    s["commit"] = [store]() { return commit(*store); };

    // Drops everything and writes the empty file, so "new game" is one call and
    // does not leave the old save on disk to be found by the next load.
    s["clear"] = [store]() {
        store->values.clear();
        store->dirty = true;
        return commit(*store);
    };
}

} // namespace eng::script
