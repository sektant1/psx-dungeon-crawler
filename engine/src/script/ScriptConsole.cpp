#include <eng/script/ScriptHost.h>

#include <eng/debug/Console.h>

#include <string>

namespace eng::script {

void registerScriptCommands(eng::DebugConsole& console, ScriptHost& host)
{
    console.registerCommand(
        "lua", "evaluate a Lua expression or statement in the script state",
        [&host](const DebugConsole::Args& args) {
            if (args.size() < 2) return;
            // Rejoined: the console tokenises on spaces, and `lua x = 1 + 1` is
            // one expression, not four arguments.
            std::string line;
            for (std::size_t i = 1; i < args.size(); ++i) {
                if (i > 1) line += ' ';
                line += args[i];
            }
            std::string out;
            host.executeConsole(line, out);
        });

    console.registerCommand(
        "script.list", "every live script instance and whether it is quarantined",
        [&host](const DebugConsole::Args&) { host.listInstances(); });

    console.registerCommand(
        "script.reload", "reload one script by path, or all of them",
        [&host](const DebugConsole::Args& args) {
            host.reload(args.size() > 1 ? args[1] : std::string{});
        });

    console.registerCommand(
        "script.revive", "un-quarantine every instance that errored",
        [&host](const DebugConsole::Args&) { host.revive(); });
}

} // namespace eng::script
