#include <eng/StringId.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <unordered_map>

using namespace eng;

static void require(bool c, const char* m)
{
    if (!c) {
        std::cerr << "StringIdTests: " << m << '\n';
        std::exit(1);
    }
}

// The property the whole design rests on: a literal id is a compile-time
// constant, so it can be a case label and costs nothing at runtime.
static_assert("fire_wand"_sid == StringId(hashString("fire_wand", 9)),
              "literal and function hash must agree");
static_assert("a"_sid != "b"_sid, "distinct strings, distinct ids");
static_assert(!StringId().valid(), "default id is unset");

static const char* dispatch(StringId id)
{
    switch (id.value()) {
    case "fire_wand"_sid.value(): return "wand";
    case "crossbow"_sid.value():  return "bow";
    default:                      return "?";
    }
}

int main()
{
    // Runtime hashing of a std::string agrees with the compile-time literal --
    // the point of the whole thing: an id read from TOML compares equal to one
    // written in C++.
    const std::string fromData = "fire_wand";
    require(intern(fromData) == "fire_wand"_sid, "runtime hash == literal hash");
    require(std::strcmp(dispatch("crossbow"_sid), "bow") == 0,
            "ids work as case labels");

    // Interning is idempotent and recovers the text.
    const std::size_t before = internedCount();
    const StringId id = intern("energy_claw");
    require(intern("energy_claw") == id, "interning twice gives one id");
    require(internedCount() == before + 1, "second intern adds no entry");
    require(std::strcmp(stringFromId(id), "energy_claw") == 0,
            "text recovered from id");
    require(std::strcmp(id.c_str(), "energy_claw") == 0, "c_str recovers text");

    // A never-interned id still prints something identifying rather than
    // nothing, which is what makes a profile bin or a log line readable.
    const StringId unknown = "never_interned_xyz"_sid;
    require(stringFromId(unknown) == nullptr, "unknown id has no text");
    require(unknown.c_str()[0] == '#', "unknown id prints its hex form");

    // Empty string must not collide with "unset": valid() gates on 0, and the
    // FNV basis is not 0.
    require(intern("").valid(), "empty string still hashes to a valid id");

    // Usable as a hash-map key without the caller writing a hasher.
    std::unordered_map<StringId, int> map;
    map["fire_wand"_sid] = 3;
    require(map.at(intern("fire_wand")) == 3, "usable as unordered_map key");

    require(stringIdCollisions() == 0, "no collisions among these strings");

    // 64-bit FNV-1a over a realistic set of engine names: everything distinct.
    // A regression here (a bad hash, a truncation to 32 bits) shows up as this
    // count dropping rather than as a mysterious lookup returning the wrong
    // object months later.
    const char* names[] = {"fire_wand",  "ethereal_crossbow", "energy_claw",
                           "minor_fire", "ethereal_bolt",     "energy_orb",
                           "player",     "enemy",             "projectile",
                           "frame",      "render",            "physics"};
    std::unordered_map<StringIdValue, std::string> seen;
    for (const char* n : names) {
        auto [it, inserted] = seen.try_emplace(hashString(n, std::strlen(n)), n);
        require(inserted, "no collision across engine names");
    }

    std::cout << "StringIdTests OK\n";
    return 0;
}
