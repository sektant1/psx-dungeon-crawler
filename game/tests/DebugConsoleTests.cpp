#include <eng/debug/Console.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

// The console's command layer, headless: everything here runs without an imgui
// context, which is the point of keeping parsing and dispatch out of draw().
static void require(bool c, const char* m)
{
    if (!c) { std::cerr << "DebugConsoleTests: " << m << '\n'; std::exit(1); }
}

int main()
{
    eng::DebugConsole console;

    std::vector<std::string> seen;
    console.registerCommand("spawn", "test", [&](const eng::DebugConsole::Args& a) {
        seen = a;
    });

    require(console.execute("spawn goblin 3"), "known command must run");
    require(seen.size() == 3, "argv includes the command name");
    require(seen[0] == "spawn" && seen[1] == "goblin" && seen[2] == "3",
            "arguments split on whitespace");

    require(console.execute("spawn \"two words\" x"), "quoted run");
    require(seen.size() == 3 && seen[1] == "two words",
            "double quotes keep an argument together");

    require(!console.execute("nope"), "unknown command reports failure");
    require(console.execute("   "), "a blank line is not an error");

    // Re-registering replaces rather than shadows.
    bool replaced = false;
    console.registerCommand("spawn", "test", [&](const eng::DebugConsole::Args&) {
        replaced = true;
    });
    console.execute("spawn");
    require(replaced, "re-registering a name replaces the handler");

    // Bound values assign and clamp.
    float fov = 70.0f;
    console.bindFloat("r.fov", &fov, 40.0f, 110.0f);
    console.execute("r.fov 95");
    require(fov == 95.0f, "bindFloat assigns");
    console.execute("r.fov 400");
    require(fov == 110.0f, "bindFloat clamps to its range");
    console.execute("r.fov");
    require(fov == 110.0f, "a bare read must not change the value");

    bool flag = false;
    console.bindBool("g.godmode", &flag);
    console.execute("g.godmode 1");
    require(flag, "bindBool assigns");
    console.execute("g.godmode off");
    require(!flag, "bindBool understands off");

    // Log capture survives the console outliving the sink registration.
    console.captureEngineLog();
    eng::log::info("console test line %d", 7);
    console.captureEngineLog(); // second call is a no-op, not a second sink

    std::cout << "DebugConsoleTests: ok\n";
    return 0;
}
