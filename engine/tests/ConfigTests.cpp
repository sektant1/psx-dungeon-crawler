// Config flattening: TOML in, dotted "section.key" leaves out.
//
// The depth is the point. This flattened exactly one level, so a nested table
// like `[combat.fireball]` arrived at the leaf store as a *table*, warned
// "unsupported value type for key 'combat.fireball'", and every value under it
// was unreachable through this API -- four warnings a boot for values that were
// perfectly fine, which is how a log teaches you to stop reading it.
#include <eng/Config.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "ConfigTests: " << message << '\n';
        std::exit(1);
    }
}

std::string writeTemp(const char* body)
{
    // Same convention as ObjGeometryTests/FileSystemTests: a named path under
    // /tmp, removed at the end.
    const std::string path = "/tmp/eng_config_test.toml";
    std::ofstream out(path);
    out << body;
    out.close();
    return path;
}

} // namespace

int main()
{
    const std::string path = writeTemp(R"(
[window]
title = "eng"
width = 960
scale = 1.5
fullscreen = false

[combat.fireball]
speed = 18.0
ttl = 6
light_colour = [1.0, 0.55, 0.15]

[combat.beam]
range = 40.0

[deep.a.b.c]
value = 7

[bindings]
quit = "Escape"
pause = ["Space", "Return"]
)");

    eng::Config cfg;
    require(cfg.load(path), "the fixture must parse");

    // --- one level, the case that always worked -------------------------
    require(cfg.getString("window.title") == "eng", "string leaf");
    require(cfg.getNumber("window.width") == 960.0, "integer reads as a number");
    require(cfg.getNumber("window.scale") == 1.5, "float leaf");
    require(cfg.getBool("window.fullscreen", true) == false, "bool leaf");

    // --- nested tables, the case that warned and dropped the values -----
    require(cfg.getNumber("combat.fireball.speed") == 18.0,
            "a two-level key must be reachable");
    require(cfg.getNumber("combat.fireball.ttl") == 6.0,
            "an integer under a nested table reads as a number");
    require(cfg.getNumber("combat.beam.range") == 40.0,
            "sibling sub-tables are both flattened");
    require(cfg.getNumber("deep.a.b.c.value") == 7.0,
            "flattening is recursive, not two levels of special case");

    // --- absent keys fall back rather than inventing a value -------------
    require(cfg.getNumber("combat.fireball.missing", -1.0) == -1.0,
            "an absent leaf returns the default");
    require(cfg.getNumber("combat.fireball", -1.0) == -1.0,
            "the table itself is not a value");
    require(cfg.getString("combat.fireball.light_colour", "none") == "none",
            "an array is not stored as a string");

    // --- bindings stay their own thing ----------------------------------
    const auto& bindings = cfg.bindings();
    require(bindings.count("quit") == 1 && bindings.at("quit").size() == 1,
            "a scalar binding is one key");
    require(bindings.count("pause") == 1 && bindings.at("pause").size() == 2,
            "an array binding keeps every key");
    require(cfg.getString("bindings.quit", "unset") == "unset",
            "bindings are not also flattened into the string map");

    std::remove(path.c_str());
    std::cout << "ConfigTests OK\n";
    return 0;
}
