#pragma once

#include <eng/assets/AssetRoot.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

// Shipped content, for the tests that assert against it.
//
// These tests deliberately read the real game pack rather than a fixture: if an
// artist renames a kit piece or drops an enemy, the build is where that should
// be reported. They used to reach it through compile-time macros baking an
// absolute source path (APP_ASSET_DIR, KIT_TOML, RITUAL_SCN, ASSET_ROOT,
// SCENE_SCHEMA) -- one macro per file, each a separate CMake site to update
// when the tree moves. They ask the resolver now, the same way the game does.
//
// Tests that read engine *source* text -- the shader and material content
// assertions in engine/tests -- correctly keep PROJECT_SOURCE_DIR. Those are
// assertions about the repository, not about what a running app can resolve.
namespace game::test {

// Discover the content root and mount the game set. Call once, first thing in
// main(). Aborts rather than letting every subsequent lookup fail one by one.
inline void mountGameAssets()
{
    if (!eng::assets::init() || !eng::assets::mount("game")) {
        std::cerr << "tests: no content root; cannot mount the game pack\n";
        std::exit(1);
    }
}

// A shipped file, by logical path. Aborts if the pack does not have it.
inline std::string asset(std::string_view logical)
{
    const std::string path = eng::assets::resolve(logical).string();
    if (path.empty()) {
        std::cerr << "tests: unresolved asset '" << logical << "'\n";
        std::exit(1);
    }
    return path;
}

// The game pack's own directory, for the checks that need a root to join
// pack-relative paths onto (SceneValidate's mesh-on-disk pass).
inline std::string gamePackDir()
{
    return eng::assets::packDir("content").string();
}

} // namespace game::test
