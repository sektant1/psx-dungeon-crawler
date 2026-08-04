#pragma once
#include <string>

namespace eng::script {

// Everything the script host needs decided before it loads anything.
struct ScriptConfig {
    // Logical directory the watcher polls and script paths resolve against.
    // Scripts are read by explicit path through eng::assets::resolve, so this
    // is deliberately NOT a resource location in assets.toml -- that list is
    // the flat resource group, and the manifest's own rule is that anything
    // read by explicit path stays out of it.
    std::string root = "scripts";

    // Poll the root for changes and swap class tables in place. Development
    // only: a shipped build has nothing to reload from, and polling a
    // directory every frame for nothing is waste.
    bool hotReload = false;
};

} // namespace eng::script
